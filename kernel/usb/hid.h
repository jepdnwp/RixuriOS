#pragma once
#include <stddef.h>
#include <stdint.h>
#include "xhci.h"

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

typedef struct {
    uint8_t has_keyboard;
    uint8_t has_mouse;
    uint8_t has_report_id;
    uint8_t report_id;
    uint16_t input_bits;
    uint16_t input_bytes;
} rix_hid_report_info_t;

void hid_init(void);
int hid_keyboard_report(unsigned tty_id, const uint8_t *report, uint8_t length);
int hid_mouse_report(const uint8_t *report, uint8_t length, rix_hid_mouse_report_t *out);
uint8_t hid_keyboard_modifiers(void);
int hid_parse_report_descriptor(const uint8_t *data, size_t length,
                                rix_hid_report_info_t *out);

/* Submit one interrupt-IN report and deliver it to the existing HID parser. */
int hid_xhci_keyboard_poll(size_t controller, uint8_t slot, uint8_t endpoint_address,
                           unsigned tty_id, uint8_t *report, uint16_t report_capacity,
                           uint16_t *actual_length);
int hid_xhci_mouse_poll(size_t controller, uint8_t slot, uint8_t endpoint_address,
                        uint8_t *report, uint16_t report_capacity,
                        rix_hid_mouse_report_t *out, uint16_t *actual_length);
