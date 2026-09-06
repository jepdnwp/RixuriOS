#include "kernel/usb/hid.h"
#include <stdio.h>
#include <stdint.h>

int tty_input(unsigned tty_id, uint8_t byte) {
    (void)tty_id;
    (void)byte;
    return 0;
}

static int expect(int condition, const char *name) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        return 1;
    }
    return 0;
}

int main(void) {
    static const uint8_t keyboard[] = {
        0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x05, 0x07,
        0x19, 0xe0, 0x29, 0xe7, 0x15, 0x00, 0x25, 0x01,
        0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01,
        0x75, 0x08, 0x81, 0x01, 0x95, 0x05, 0x75, 0x01,
        0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
        0x95, 0x01, 0x75, 0x03, 0x91, 0x01, 0x95, 0x06,
        0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07,
        0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xc0
    };
    rix_hid_report_info_t info;
    if (expect(hid_parse_report_descriptor(keyboard, sizeof(keyboard), &info) == 0,
                "keyboard descriptor parses")) return 1;
    if (expect(info.has_keyboard && !info.has_mouse && info.input_bytes >= 8u,
                "keyboard usage and report size")) return 1;
    static const uint8_t truncated[] = {0x05, 0x01, 0x09};
    if (expect(hid_parse_report_descriptor(truncated, sizeof(truncated), &info) != 0,
                "truncated descriptor rejected")) return 1;
    hid_init();
    static const uint8_t keyboard_report_id[] = {7, 0, 0, 0x04, 0, 0, 0, 0, 0};
    if (expect(hid_keyboard_report_protocol(0, keyboard_report_id,
                                            sizeof(keyboard_report_id), 7) == 0,
                "keyboard report ID accepted")) return 1;
    if (expect(hid_keyboard_report_protocol(0, keyboard_report_id,
                                            sizeof(keyboard_report_id), 8) == -2,
                "keyboard report ID mismatch rejected")) return 1;
    if (expect(hid_keyboard_report_protocol(0, keyboard_report_id,
                                            (uint16_t)UINT8_MAX + 1u, 0) == -3,
                "keyboard oversized report rejected")) return 1;
    static const uint8_t rollover[] = {0, 0, 1, 0, 0, 0, 0, 0};
    if (expect(hid_keyboard_report(0, rollover, sizeof(rollover)) == -2,
                "keyboard rollover rejected")) return 1;
    static const uint8_t mouse_report_id[] = {2, 1, 0xfe, 3, 4};
    rix_hid_mouse_report_t mouse;
    if (expect(hid_mouse_report_protocol(mouse_report_id, sizeof(mouse_report_id), 2,
                                         &mouse) == 0 && mouse.buttons == 1 &&
                mouse.dx == -2 && mouse.dy == 3 && mouse.wheel == 4,
                "mouse report ID parsed")) return 1;
    static const uint8_t mouse_no_id[] = {1, 2, 3, 4};
    if (expect(hid_mouse_report_protocol(mouse_no_id, sizeof(mouse_no_id), 0,
                                         &mouse) == 0 && mouse.buttons == 1,
                "mouse report without ID parsed")) return 1;
    puts("hid report tests: PASS");
    return 0;
}
