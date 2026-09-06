#include "hid.h"
#include "../tty/tty.h"
#include <stddef.h>

#define HID_MOD_LCTRL 0x01u
#define HID_MOD_LSHIFT 0x02u
#define HID_MOD_LALT 0x04u
#define HID_MOD_RCTRL 0x10u
#define HID_MOD_RSHIFT 0x20u
#define HID_MOD_RALT 0x40u

static uint8_t modifiers;
static uint8_t previous[6];

static int contains(const uint8_t *keys, uint8_t code) {
    for (size_t i = 0; i < 6; ++i) if (keys[i] == code) return 1;
    return 0;
}

static uint8_t key_ascii(uint8_t code, int shift, int caps) {
    if (code >= 0x04u && code <= 0x1Du) {
        uint8_t c = (uint8_t)('a' + code - 0x04u);
        if (shift ^ caps) c = (uint8_t)(c - ('a' - 'A'));
        return c;
    }
    if (code >= 0x1Eu && code <= 0x27u) {
        static const char normal[] = "1234567890";
        static const char shifted[] = "!@#$%^&*()";
        return (uint8_t)(shift ? shifted[code - 0x1Eu] : normal[code - 0x1Eu]);
    }
    switch (code) {
        case 0x28u: return '\n';
        case 0x29u: return 27u;
        case 0x2Au: return 8u;
        case 0x2Bu: return '\t';
        case 0x2Cu: return ' ';
        case 0x2Du: return (uint8_t)(shift ? '_' : '-');
        case 0x2Eu: return (uint8_t)(shift ? '+' : '=');
        case 0x2Fu: return (uint8_t)(shift ? '{' : '[');
        case 0x30u: return (uint8_t)(shift ? '}' : ']');
        case 0x31u: return (uint8_t)(shift ? '|' : '\\');
        case 0x33u: return (uint8_t)(shift ? ':' : ';');
        case 0x34u: return (uint8_t)(shift ? '"' : '\'');
        case 0x35u: return (uint8_t)(shift ? '~' : '`');
        case 0x36u: return (uint8_t)(shift ? '<' : ',');
        case 0x37u: return (uint8_t)(shift ? '>' : '.');
        case 0x38u: return (uint8_t)(shift ? '?' : '/');
        default: return 0u;
    }
}

static int emit_key(unsigned tty_id, uint8_t code) {
    int shift = (modifiers & (HID_MOD_LSHIFT | HID_MOD_RSHIFT)) != 0u;
    int ctrl = (modifiers & (HID_MOD_LCTRL | HID_MOD_RCTRL)) != 0u;
    int caps = (modifiers & 0x40u) != 0u;
    if (code == 0x39u) return 0;
    if (code >= 0x4Fu && code <= 0x52u) {
        static const char *const seq[] = {"\x1b[C", "\x1b[D", "\x1b[B", "\x1b[A"};
        unsigned i = (unsigned)(code - 0x4Fu);
        for (const char *p = seq[i]; *p; ++p) if (tty_input(tty_id, (uint8_t)*p) != 0) return -2;
        return 0;
    }
    uint8_t c = key_ascii(code, shift, caps);
    if (!c) return 0;
    if (ctrl && c >= 'a' && c <= 'z') c = (uint8_t)(c - 'a' + 1u);
    return tty_input(tty_id, c);
}

void hid_init(void) {
    modifiers = 0;
    for (size_t i = 0; i < 6; ++i) previous[i] = 0;
}

int hid_keyboard_report(unsigned tty_id, const uint8_t *report, uint8_t length) {
    if (!report || length < RIX_HID_BOOT_KEYBOARD_REPORT) return -1;
    if (report[2] == 0x01u || report[3] == 0x01u || report[4] == 0x01u ||
        report[5] == 0x01u || report[6] == 0x01u || report[7] == 0x01u) return -2;
    modifiers = report[0];
    for (size_t i = 0; i < 6; ++i) {
        uint8_t code = report[2u + i];
        if (code != 0u && !contains(previous, code)) {
            int rc = emit_key(tty_id, code);
            if (rc != 0) return rc;
        }
    }
    for (size_t i = 0; i < 6; ++i) previous[i] = report[2u + i];
    return 0;
}

int hid_mouse_report(const uint8_t *report, uint8_t length, rix_hid_mouse_report_t *out) {
    if (!report || !out || length < RIX_HID_BOOT_MOUSE_REPORT) return -1;
    out->buttons = report[0];
    out->dx = (int8_t)report[1];
    out->dy = (int8_t)report[2];
    out->wheel = length >= 4u ? (int8_t)report[3] : 0;
    return 0;
}

uint8_t hid_keyboard_modifiers(void) { return modifiers; }
