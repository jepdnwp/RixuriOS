#include "ps2_keyboard.h"
#include "irq.h"
#include "../../tty/tty.h"
#include <stdint.h>

#define PS2_DATA_PORT 0x60u
#define PS2_STATUS_PORT 0x64u
#define PS2_COMMAND_PORT 0x64u

static uint8_t shift_held;
static uint8_t ctrl_held;
static uint8_t extended_scancode;

static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static int ps2_wait_input_clear(void) {
    for (unsigned i = 0; i < 100000u; ++i)
        if ((inb(PS2_STATUS_PORT) & 2u) == 0u) return 0;
    return -1;
}
static int ps2_wait_output_full(void) {
    for (unsigned i = 0; i < 100000u; ++i)
        if (inb(PS2_STATUS_PORT) & 1u) return 0;
    return -1;
}
static void ps2_write_device(uint8_t value) {
    if (ps2_wait_input_clear() == 0) outb(PS2_DATA_PORT, value);
}

static void ps2_irq_handler(unsigned irq, const struct interrupt_frame *frame);

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

static void ps2_irq_handler(unsigned irq, const struct interrupt_frame *frame) {
    (void)irq; (void)frame;
    uint8_t scancode = inb(PS2_DATA_PORT);

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
    shift_held = 0;
    ctrl_held = 0;
    extended_scancode = 0;
    /* Some firmware/QEMU configurations leave the 8042 keyboard interface
       disabled until it is explicitly enabled and scanning is started. */
    if (ps2_wait_input_clear() == 0) outb(PS2_COMMAND_PORT, 0xAEu);
    while (inb(PS2_STATUS_PORT) & 1u) (void)inb(PS2_DATA_PORT);
    ps2_write_device(0xF4u);
    if (ps2_wait_output_full() == 0) (void)inb(PS2_DATA_PORT);
    irq_register(1u, ps2_irq_handler);
}

void ps2_keyboard_poll(void) {
    /* Firmware PS/2 routing is not consistent on modern boards; drain the
       controller even when IRQ1 is unavailable. */
    while (inb(PS2_STATUS_PORT) & 1u) ps2_irq_handler(1u, NULL);
}
