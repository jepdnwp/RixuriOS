#include "xhci_caps.h"

int xhci_spc_decode_entry(const uint8_t entry[XHCI_SPC_ENTRY_SIZE],
                          xhci_proto_range_t *out) {
    if (!entry || !out) return -1;
    if (entry[0] != XHCI_EXT_CAP_ID_PROTOCOL) return -2;
    uint8_t major = entry[3];
    uint8_t start = entry[8];
    uint8_t count = entry[9];
    /* USB major must be 2 or 3; ports are 1-based; empty ranges and a
     * range running past port 255 are firmware garbage, not data. */
    if ((major != 2u && major != 3u) || start == 0u || count == 0u ||
        (uint16_t)start + (uint16_t)count > 256u) return -2;
    out->major = major;
    out->start = start;
    out->count = count;
    return 0;
}
