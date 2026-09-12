#include "ps2_keyboard.h"
#include "irq.h"
#include "../../tty/tty.h"
#include "../../sync/lock.h"
#include <stdint.h>
#include <stddef.h>

#define PS2_DATA_PORT 0x60u
#define PS2_STATUS_PORT 0x64u
#define PS2_DRAIN_MAX 16u

static uint8_t shift_held;
static uint8_t ctrl_held;
static uint8_t extended_scancode;
/* The 8042 output buffer is a single shared byte. IRQ1 and the poll worker
 * both drain it, so without mutual exclusion a byte can be consumed twice
 * (stale duplicate read) and one keypress appears as two. */
static rix_spinlock_t ps2_lock;
/* Phase E6 bottom half: IRQ1 only drains HW bytes into this ring (under
 * ps2_lock) and returns — it never calls tty_input anymore. The poll
 * worker moves HW bytes into the same ring, then processes the ring FIFO
 * in task context. Rationale: tty_input takes the input/output locks; an
 * IRQ handler spinning on those while the holder cannot resume until
 * iret is a keypress-timing deadlock (invisible without input HW, fatal
 * with it). After E6 no IRQ path takes tty/console locks. */
#define PS2_RING_SIZE 64u
static uint8_t ps2_ring[PS2_RING_SIZE];
static size_t ps2_ring_head, ps2_ring_tail, ps2_ring_count;
static uint64_t ps2_ring_dropped;
static void ps2_ring_push(uint8_t byte){
    if(ps2_ring_count<PS2_RING_SIZE){
        ps2_ring[ps2_ring_tail]=byte;
        ps2_ring_tail=(ps2_ring_tail+1u)%PS2_RING_SIZE;
        ps2_ring_count++;
    }else{
        /* Overflow drops newest (no IRQ-side logging, ever). */
        ps2_ring_dropped++;
    }
}

static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
static void ps2_irq_handler(unsigned irq, const struct interrupt_frame *frame);
static void ps2_handle_scancode(uint8_t scancode);

static const uint8_t scancode_to_ascii[128] = {
    0, 0, '1','2','3','4','5','6','7','8','9','0','-','=', 0, 0,
    'q','w','e','r','t','y','u','i','o','p','[',']', 0, 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0, '\\',
    'z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' ', 0,
    0,0,0,0,0,0,0,0,0,0,0,0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.',
    0,0,0,0,0
};

static const uint8_t scancode_shift[128] = {
    0, 0, '!','@','#','$','%','^','&','*','(',')','_','+', 0, 0,
    'Q','W','E','R','T','Y','U','I','O','P','{','}', 0, 0,
    'A','S','D','F','G','H','J','K','L',':','"','~', 0, '|',
    'Z','X','C','V','B','N','M','<','>','?', 0, '*', 0, ' ', 0,
    0,0,0,0,0,0,0,0,0,0,0,0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.',
    0,0,0,0,0
};

/* Drain pending 8042 bytes into the caller buffer. The caller must hold
 * ps2_lock so IRQ and poll paths never read the same byte twice. */
static size_t ps2_drain_locked(uint8_t *out, size_t capacity) {
    size_t count = 0;
    while (count < capacity && (inb(PS2_STATUS_PORT) & 1u))
        out[count++] = inb(PS2_DATA_PORT);
    return count;
}

static void ps2_irq_handler(unsigned irq, const struct interrupt_frame *frame) {
    (void)irq; (void)frame;
    uint8_t pending[PS2_DRAIN_MAX];
    size_t count;
    /* IRQ context must never block: if the poll worker holds the lock it is
     * already draining these bytes, so just return. Bytes go to the ring;
     * scancode processing (tty_input under the tty locks) happens only in
     * the poll worker's task context (Phase E6). */
    if (!rix_spin_trylock(&ps2_lock)) return;
    count = ps2_drain_locked(pending, sizeof(pending));
    for (size_t i = 0; i < count; ++i) ps2_ring_push(pending[i]);
    rix_spin_unlock(&ps2_lock);
}

static void ps2_handle_scancode(uint8_t scancode) {
    if (scancode == 0xe0u) {
        extended_scancode = 1u;
        return;
    }

    uint8_t released = scancode & 0x80u;
    uint8_t code = scancode & 0x7fu;
    uint8_t extended = extended_scancode;
    extended_scancode = 0;

    if (extended && !released) {
        uint8_t key = 0;
        if (code == 0x48u) key = RIX_TTY_KEY_UP;
        else if (code == 0x50u) key = RIX_TTY_KEY_DOWN;
        else if (code == 0x4bu) key = RIX_TTY_KEY_LEFT;
        else if (code == 0x4du) key = RIX_TTY_KEY_RIGHT;
        if (key) tty_input(0, key);
        return;
    }

    if (code == 0x2au || code == 0x36u) { shift_held = !released; return; }
    if (code == 0x1du) { ctrl_held = !released; return; }
    if (released) return;

    if (code == 0x1cu) { tty_input(0, '\r'); return; }
    if (code == 0x0eu) { tty_input(0, '\b'); return; }
    if (code == 0x0fu) { tty_input(0, '\t'); return; }
    if (code == 0x01u) { tty_input(0, 0x1bu); return; }

    uint8_t ch = 0;
    if (shift_held) ch = scancode_shift[code];
    else ch = scancode_to_ascii[code];

    if (ctrl_held && ch >= 'a' && ch <= 'z') ch = ch - 'a' + 1u;
    if (ctrl_held && ch >= 'A' && ch <= 'Z') ch = ch - 'A' + 1u;

    if (ch) tty_input(0, ch);
}

void ps2_keyboard_init(void) {
    rix_spin_init(&ps2_lock);
    shift_held = 0;
    ctrl_held = 0;
    extended_scancode = 0;
    ps2_ring_head = 0;
    ps2_ring_tail = 0;
    ps2_ring_count = 0;
    ps2_ring_dropped = 0;
    /* Do not probe or command the legacy 8042 here. On USB-only systems the
       controller may be absent or firmware-owned, and port I/O can stall the
       boot path. PS/2 input remains available when IRQ1 actually produces
       data; USB HID is handled by the xHCI worker. */
    irq_register(1u, ps2_irq_handler);
}

void ps2_keyboard_poll(void) {
    /* Firmware PS/2 routing is not consistent on modern boards; drain the
       controller even when IRQ1 is unavailable. The drain runs under the
       PS/2 lock with interrupts masked so an IRQ1 firing mid-drain cannot
       consume (and duplicate) the same byte. Phase E6: HW bytes join the
       ring first, then the whole ring is processed FIFO — single-context
       ordering for multi-byte sequences and shift state. */
    uint8_t pending[PS2_DRAIN_MAX];
    size_t count;
    uint64_t flags;
    rix_spin_lock_irqsave(&ps2_lock, &flags);
    count = ps2_drain_locked(pending, sizeof(pending));
    for (size_t i = 0; i < count; ++i) ps2_ring_push(pending[i]);
    while (ps2_ring_count) {
        uint8_t byte = ps2_ring[ps2_ring_head];
        ps2_ring_head = (ps2_ring_head + 1u) % PS2_RING_SIZE;
        ps2_ring_count--;
        ps2_handle_scancode(byte);
    }
    rix_spin_unlock_irqrestore(&ps2_lock, flags);
}
