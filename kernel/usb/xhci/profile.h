#pragma once

#include <stdint.h>

#define XHCI_PROFILE_BAD_PORT_MAX 8u

#define XHCI_PROFILE_QUIRK_NONE      0u
#define XHCI_PROFILE_QUIRK_USB2_ONLY (1u << 0)

#define XHCI_PROFILE_OK           0u
#define XHCI_PROFILE_VERIFY_PORTS (1u << 0)
#define XHCI_PROFILE_VERIFY_HCI   (1u << 1)
#define XHCI_PROFILE_VERIFY_USB3  (1u << 2)

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    const char *name;
    uint8_t expected_ports;
    uint16_t expected_hci;
    const uint8_t *bad_ports;
    uint8_t bad_port_count;
    uint32_t quirks;
} xhci_profile_t;

const xhci_profile_t *xhci_profile_lookup(uint16_t vendor_id, uint16_t device_id);
unsigned xhci_profile_verify(const xhci_profile_t *profile,
                             uint8_t actual_ports,
                             uint16_t actual_hci,
                             int has_usb3_range);
int xhci_profile_port_known_bad(const xhci_profile_t *profile, uint8_t port);
int xhci_profile_port_priority(int protocol_major, int known_bad);
