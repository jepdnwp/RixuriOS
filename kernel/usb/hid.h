#pragma once
#include <stdint.h>

#define RIX_HID_BOOT_KEYBOARD_REPORT 8u
#define RIX_HID_BOOT_MOUSE_REPORT 4u

typedef struct {
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keys[6];
} rix_hid_keyboard_report_t;

typedef struct {
    int8_t dx;
    int8_t dy;
    int8_t wheel;
    uint8_t buttons;
} rix_hid_mouse_report_t;

void hid_init(void);
int hid_keyboard_report(unsigned tty_id, const uint8_t *report, uint8_t length);
int hid_mouse_report(const uint8_t *report, uint8_t length, rix_hid_mouse_report_t *out);
uint8_t hid_keyboard_modifiers(void);
