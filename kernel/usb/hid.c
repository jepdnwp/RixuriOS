#include "hid.h"
#include "hid_defs.h"
#include "../tty/tty.h"
#include <stddef.h>
#include <stdint.h>

#define HID_MOD_LCTRL 0x01u
#define HID_MOD_LSHIFT 0x02u
#define HID_MOD_RCTRL 0x10u
#define HID_MOD_RSHIFT 0x20u

static uint8_t modifiers;
static uint8_t previous[6];
static uint8_t caps_lock;

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
    if (code == 0x39u) {
        caps_lock ^= 1u;
        return 0;
    }
    if (code >= 0x4Fu && code <= 0x52u) {
        static const char *const seq[] = {"\x1b[C", "\x1b[D", "\x1b[B", "\x1b[A"};
        unsigned i = (unsigned)(code - 0x4Fu);
        for (const char *p = seq[i]; *p; ++p) if (tty_input(tty_id, (uint8_t)*p) != 0) return -2;
        return 0;
    }
    uint8_t c = key_ascii(code, shift, caps_lock != 0u);
    if (!c) return 0;
    if (ctrl && c >= 'a' && c <= 'z') c = (uint8_t)(c - 'a' + 1u);
    return tty_input(tty_id, c);
}

void hid_init(void) {
    modifiers = 0;
    caps_lock = 0;
    for (size_t i = 0; i < 6; ++i) previous[i] = 0;
}

int hid_keyboard_report(unsigned tty_id, const uint8_t *report, uint8_t length) {
    if (!report || length < RIX_HID_BOOT_KEYBOARD_REPORT) return -1;
    for (size_t i = 0; i < 6; ++i) if (report[2u + i] >= 0x01u && report[2u + i] <= 0x03u) return -2;
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

int hid_keyboard_report_protocol(unsigned tty_id, const uint8_t *report, uint16_t length,
                                 uint8_t report_id) {
    if (!report) return -1;
    if (report_id != 0u) {
        if (length < 1u || report[0] != report_id) return -2;
        report++;
        length--;
    }
    if (length > UINT8_MAX) return -3;
    return hid_keyboard_report(tty_id, report, (uint8_t)length);
}

int hid_mouse_report(const uint8_t *report, uint8_t length, rix_hid_mouse_report_t *out) {
    if (!report || !out || length < RIX_HID_BOOT_MOUSE_REPORT) return -1;
    out->buttons = report[0];
    out->dx = (int8_t)report[1];
    out->dy = (int8_t)report[2];
    out->wheel = length >= 4u ? (int8_t)report[3] : 0;
    return 0;
}

int hid_mouse_report_protocol(const uint8_t *report, uint16_t length, uint8_t report_id,
                              rix_hid_mouse_report_t *out) {
    if (!report || !out) return -1;
    if (report_id != 0u) {
        if (length < 1u || report[0] != report_id) return -2;
        report++;
        length--;
    }
    if (length > UINT8_MAX) return -3;
    return hid_mouse_report(report, (uint8_t)length, out);
}

uint8_t hid_keyboard_modifiers(void) { return modifiers; }

static uint32_t hid_item_value(const uint8_t *data, uint8_t size) {
    uint32_t value = 0;
    for (uint8_t i = 0; i < size; ++i) value |= (uint32_t)data[i] << (i * 8u);
    return value;
}

/* Sign-extend a raw item value (Linux item_sdata rules). */
static int32_t hid_item_signed(uint32_t raw, uint8_t size) {
    if (size == 1u) return (int8_t)raw;
    if (size == 2u) return (int16_t)raw;
    return (int32_t)raw;
}

/* Report-descriptor item walk after Linux drivers/hid/hid-core.c
 * (fetch_item short/long split, open/close_collection balancing, usage
 * minimum/maximum ranges, signed logical limits, report ID rules).
 * Deliberate subset: no HID_MAX_USAGES-scale tables, no Push/Pop stack,
 * no physical/unit accounting — only what boot keyboard/mouse detection
 * and input-size accounting need. New fail-closed code is -9 (malformed
 * item); -1..-8 keep their previous meanings. */
int hid_parse_report_descriptor(const uint8_t *data, size_t length,
                                rix_hid_report_info_t *out) {
    if (!data || !out) return -1;
    out->has_keyboard = 0;
    out->has_mouse = 0;
    out->has_report_id = 0;
    out->report_id = 0;
    out->input_bits = 0;
    out->input_bytes = 0;
    uint32_t usage_page = 0;
    uint32_t usage = 0;
    uint32_t usage_min = 0;
    uint32_t usage_max = 0;
    int have_range = 0;
    int32_t logical_min = 0;
    int32_t logical_max = 0;
    uint32_t report_size = 0;
    uint32_t report_count = 0;
    unsigned collections = 0;
    int delimiter_depth = 0;
    size_t offset = 0;
    while (offset < length) {
        uint8_t prefix = data[offset++];
        if (prefix == 0xfeu) {
            /* Long items are parse-fatal in Linux hid-core. */
            return -9;
        }
        uint8_t size_code = prefix & 0x03u;
        uint8_t item_size = size_code == 3u ? 4u : size_code;
        uint8_t tag = (prefix >> 4) & 0x0fu;
        uint8_t type = (prefix >> 2) & 0x03u;
        if ((size_t)item_size > length - offset) return -4;
        uint32_t value = hid_item_value(&data[offset], item_size);
        offset += item_size;
        if (type == HID_ITEM_TYPE_GLOBAL) {
            switch (tag) {
            case HID_GLOBAL_ITEM_TAG_USAGE_PAGE:
                usage_page = value;
                break;
            case HID_GLOBAL_ITEM_TAG_LOGICAL_MINIMUM:
                logical_min = hid_item_signed(value, item_size);
                break;
            case HID_GLOBAL_ITEM_TAG_LOGICAL_MAXIMUM:
                /* Linux: maximum decodes signed when minimum is negative. */
                logical_max = (logical_min < 0)
                    ? hid_item_signed(value, item_size)
                    : (int32_t)value;
                break;
            case HID_GLOBAL_ITEM_TAG_REPORT_SIZE:
                report_size = value;
                break;
            case HID_GLOBAL_ITEM_TAG_REPORT_ID:
                /* Linux rejects explicit Report ID 0 and IDs above 255. */
                if (value == 0u) return -9;
                if (value > 0xffu) return -5;
                out->has_report_id = 1;
                out->report_id = (uint8_t)value;
                break;
            case HID_GLOBAL_ITEM_TAG_REPORT_COUNT:
                report_count = value;
                break;
            default:
                /* Physical/unit/push/pop and unknown globals: accepted and
                 * ignored for boot input purposes (Linux aborts on truly
                 * unknown global tags, but none of those appear in
                 * keyboard/mouse descriptors and failing on them would
                 * only brick input on odd-but-harmless firmware). */
                break;
            }
        } else if (type == HID_ITEM_TYPE_LOCAL) {
            if (tag == HID_LOCAL_ITEM_TAG_USAGE) {
                usage = value;
            } else if (tag == HID_LOCAL_ITEM_TAG_USAGE_MINIMUM) {
                usage_min = value;
                usage_max = value;
                have_range = 1;
            } else if (tag == HID_LOCAL_ITEM_TAG_USAGE_MAXIMUM) {
                /* Linux expands minimum..maximum inclusive; a maximum
                 * below the minimum is malformed. */
                if (!have_range || value < usage_min) return -9;
                usage_max = value;
            } else if (tag == HID_LOCAL_ITEM_TAG_DELIMITER) {
                if (value != 0u) {
                    if (delimiter_depth != 0) return -9;
                    delimiter_depth = 1;
                } else {
                    if (delimiter_depth != 1) return -9;
                    delimiter_depth = 0;
                }
            }
            /* Designators/strings: ignored like Linux. */
        } else if (type == HID_ITEM_TYPE_MAIN) {
            if (tag == HID_MAIN_ITEM_TAG_BEGIN_COLLECTION) {
                if (collections >= 32u) return -9;
                collections++;
            } else if (tag == HID_MAIN_ITEM_TAG_END_COLLECTION) {
                if (collections == 0u) return -9;
                collections--;
            } else if (tag == HID_MAIN_ITEM_TAG_INPUT) {
                /* Linux validates the signed logical range per field. */
                if (logical_max < logical_min) return -9;
                if (report_size > 0xffffu || report_count > 0xffffu ||
                    (report_size != 0u && report_count > 0xffffu / report_size)) return -6;
                uint32_t bits = report_size * report_count;
                if (bits > 0xffffu - out->input_bits) return -7;
                out->input_bits = (uint16_t)(out->input_bits + bits);
                out->input_bytes = (uint16_t)((out->input_bits + 7u) / 8u);
                if (usage_page == HID_UP_KEYBOARD &&
                    (usage == HID_GD_KEYBOARD || usage == 0x07u ||
                     (have_range && usage_min <= HID_GD_KEYBOARD &&
                      usage_max >= HID_GD_KEYBOARD)))
                    out->has_keyboard = 1;
                if (usage_page == HID_UP_GENDESK &&
                    (usage == HID_GD_MOUSE ||
                     (have_range && usage_min <= HID_GD_MOUSE &&
                      usage_max >= HID_GD_MOUSE)))
                    out->has_mouse = 1;
            }
            /* Output/Feature items contribute no input bits; every main
             * item resets the local usage state like Linux. */
            usage = 0;
            usage_min = 0;
            usage_max = 0;
            have_range = 0;
        }
        /* Reserved type 3: ignored like Linux. */
    }
    if (collections != 0u || delimiter_depth != 0) return -9;
    return out->input_bits != 0u ? 0 : -8;
}

#ifndef HID_PARSER_HOST_TEST
int hid_xhci_keyboard_poll(size_t controller, uint8_t slot, uint8_t endpoint_address,
                           unsigned tty_id, uint8_t *report, uint16_t report_capacity,
                           uint16_t *actual_length) {
    if (!report || report_capacity < RIX_HID_BOOT_KEYBOARD_REPORT) return -1;
    uint16_t received = 0;
    int rc = xhci_interrupt_transfer(controller, slot, endpoint_address, report,
                                      report_capacity, &received);
    if (actual_length) *actual_length = received;
    if (rc != 0) return rc;
    if (received > UINT8_MAX) return -3;
    if (received < RIX_HID_BOOT_KEYBOARD_REPORT) return -2;
    return hid_keyboard_report(tty_id, report, (uint8_t)received);
}

int hid_xhci_mouse_poll(size_t controller, uint8_t slot, uint8_t endpoint_address,
                        uint8_t *report, uint16_t report_capacity,
                        rix_hid_mouse_report_t *out, uint16_t *actual_length) {
    if (!report || !out || report_capacity < RIX_HID_BOOT_MOUSE_REPORT) return -1;
    uint16_t received = 0;
    int rc = xhci_interrupt_transfer(controller, slot, endpoint_address, report,
                                      report_capacity, &received);
    if (actual_length) *actual_length = received;
    if (rc != 0) return rc;
    if (received > UINT8_MAX) return -2;
    return hid_mouse_report(report, (uint8_t)received, out);
}

int hid_xhci_keyboard_poll_protocol(size_t controller, uint8_t slot,
                                    uint8_t endpoint_address, unsigned tty_id,
                                    uint8_t report_id, uint8_t *report,
                                    uint16_t report_capacity, uint16_t *actual_length) {
    if (!report || report_capacity == 0u) return -1;
    uint16_t received = 0;
    int rc = xhci_interrupt_transfer(controller, slot, endpoint_address, report,
                                      report_capacity, &received);
    if (actual_length) *actual_length = received;
    if (rc != 0) return rc;
    return hid_keyboard_report_protocol(tty_id, report, received, report_id);
}

int hid_xhci_mouse_poll_protocol(size_t controller, uint8_t slot,
                                 uint8_t endpoint_address, uint8_t report_id,
                                 uint8_t *report, uint16_t report_capacity,
                                 rix_hid_mouse_report_t *out,
                                 uint16_t *actual_length) {
    if (!report || !out || report_capacity == 0u) return -1;
    uint16_t received = 0;
    int rc = xhci_interrupt_transfer(controller, slot, endpoint_address, report,
                                      report_capacity, &received);
    if (actual_length) *actual_length = received;
    if (rc != 0) return rc;
    return hid_mouse_report_protocol(report, received, report_id, out);
}
#endif
