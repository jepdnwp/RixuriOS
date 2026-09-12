#include "xhci_profile.h"

#define AMD_VENDOR_ID 0x1022u

static const uint8_t bad_43f7[] = { 1u };
static const uint8_t bad_15b6[] = { 1u };
static const uint8_t bad_15b7[] = { 9u, 10u, 12u, 13u, 15u, 16u };
static const uint8_t bad_15b8[] = { 1u };

static const xhci_profile_t profiles[] = {
    { AMD_VENDOR_ID, 0x43F7u, "AMD chipset xHCI", 4u, 0x0110u,
      bad_43f7, (uint8_t)(sizeof(bad_43f7) / sizeof(bad_43f7[0])), XHCI_PROFILE_QUIRK_NONE },
    { AMD_VENDOR_ID, 0x15B6u, "AMD Raphael xHCI", 4u, 0x0120u,
      bad_15b6, (uint8_t)(sizeof(bad_15b6) / sizeof(bad_15b6[0])), XHCI_PROFILE_QUIRK_NONE },
    { AMD_VENDOR_ID, 0x15B7u, "AMD Raphael xHCI", 18u, 0x0120u,
      bad_15b7, (uint8_t)(sizeof(bad_15b7) / sizeof(bad_15b7[0])), XHCI_PROFILE_QUIRK_NONE },
    { AMD_VENDOR_ID, 0x15B8u, "AMD Raphael USB2 xHCI", 1u, 0x0120u,
      bad_15b8, (uint8_t)(sizeof(bad_15b8) / sizeof(bad_15b8[0])), XHCI_PROFILE_QUIRK_USB2_ONLY },
};

const xhci_profile_t *xhci_profile_lookup(uint16_t vendor_id, uint16_t device_id) {
    for (unsigned i = 0u; i < sizeof(profiles) / sizeof(profiles[0]); ++i) {
        if (profiles[i].vendor_id == vendor_id && profiles[i].device_id == device_id)
            return &profiles[i];
    }
    return 0;
}

unsigned xhci_profile_verify(const xhci_profile_t *profile,
                             uint8_t actual_ports,
                             uint16_t actual_hci,
                             int has_usb3_range) {
    unsigned result = XHCI_PROFILE_OK;
    if (!profile) return XHCI_PROFILE_OK;
    if (actual_ports != profile->expected_ports) result |= XHCI_PROFILE_VERIFY_PORTS;
    if (actual_hci != profile->expected_hci) result |= XHCI_PROFILE_VERIFY_HCI;
    if ((profile->quirks & XHCI_PROFILE_QUIRK_USB2_ONLY) && has_usb3_range)
        result |= XHCI_PROFILE_VERIFY_USB3;
    return result;
}

int xhci_profile_port_known_bad(const xhci_profile_t *profile, uint8_t port) {
    if (!profile || port == 0u) return 0;
    for (unsigned i = 0u; i < profile->bad_port_count && i < XHCI_PROFILE_BAD_PORT_MAX; ++i)
        if (profile->bad_ports[i] == port) return 1;
    return 0;
}

int xhci_profile_port_priority(int protocol_major, int known_bad) {
    int score;
    if (protocol_major == 2) score = 0;
    else if (protocol_major == 3) score = 1;
    else score = 2;
    return score + (known_bad ? 4 : 0);
}
