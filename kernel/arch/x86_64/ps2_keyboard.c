#include "ps2_keyboard.h"
#include "irq.h"
#include "../../tty/tty.h"
#include <stdint.h>

#define PS2_DATA_PORT 0x60u

static uint8_t shift_held;
static uint8_t ctrl_held;

static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

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

    if (scancode == 0xe0u) return;

    uint8_t released = scancode & 0x80u;
    uint8_t code = scancode & 0x7fu;

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
    irq_register(1u, ps2_irq_handler);
}
