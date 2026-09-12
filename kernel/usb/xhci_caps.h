#pragma once
#include <stddef.h>
#include <stdint.h>

/* Phase H4: pure Supported Protocol Capability (ext-cap ID 2) decoder.
 * No MMIO in this TU by design: the caller snapshots 16 bytes per
 * entry (volatile or test buffer) and decodes here, so this is fully
 * host-testable. Layout per xHCI 7.2 (all little-endian):
 *   +0: Cap ID (2) | Next pointer
 *   +2: protocol revision minor.major (2=USB2, 3=USB3)
 *   +4: "USB " name (ignored)
 *   +8: compatible port offset (1-based)
 *   +9: port count
 *   +10..15: protocol-defined (ignored)
 * Returns 0 with major/start/count filled, -1 on bad input, -2 when
 * the entry is not a protocol capability or is malformed. */
#define XHCI_EXT_CAP_ID_PROTOCOL 0x02u
#define XHCI_SPC_ENTRY_SIZE 16u
#define XHCI_SPC_MAX_RANGES 4u
typedef struct {
    uint8_t major;
    uint8_t start;
    uint8_t count;
} xhci_proto_range_t;
int xhci_spc_decode_entry(const uint8_t entry[XHCI_SPC_ENTRY_SIZE],
                          xhci_proto_range_t *out);
