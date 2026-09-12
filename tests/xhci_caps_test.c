#include "../kernel/usb/xhci_caps.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

/* Phase H4: SPC entry decoder. Layout: [ID=2][next][rev-lo][major]...
 * ports at [8]=offset, [9]=count. */

static void test_usb2_and_usb3(void) {
    /* USB2 range ports 1-10. */
    const uint8_t usb2[XHCI_SPC_ENTRY_SIZE] = {
        0x02, 0x10, 0x00, 0x02, 'U', 'S', 'B', ' ',
        1, 10, 0, 0, 0, 0, 0, 0
    };
    /* USB3 range ports 11-14. */
    const uint8_t usb3[XHCI_SPC_ENTRY_SIZE] = {
        0x02, 0x00, 0x10, 0x03, 'U', 'S', 'B', ' ',
        11, 4, 0, 0, 0, 0, 0, 0
    };
    xhci_proto_range_t r;
    assert(xhci_spc_decode_entry(usb2, &r) == 0);
    assert(r.major == 2 && r.start == 1 && r.count == 10);
    assert(xhci_spc_decode_entry(usb3, &r) == 0);
    assert(r.major == 3 && r.start == 11 && r.count == 4);
}

static void test_rejects(void) {
    xhci_proto_range_t r;
    const uint8_t legacy[XHCI_SPC_ENTRY_SIZE] = {
        0x01, 0x00, 0, 0, 0, 0, 0, 0, 1, 4, 0, 0, 0, 0, 0, 0
    };
    const uint8_t bad_major[XHCI_SPC_ENTRY_SIZE] = {
        0x02, 0x00, 0x00, 0x04, 'U', 'S', 'B', ' ',
        1, 4, 0, 0, 0, 0, 0, 0
    };
    const uint8_t zero_start[XHCI_SPC_ENTRY_SIZE] = {
        0x02, 0x00, 0x00, 0x02, 'U', 'S', 'B', ' ',
        0, 4, 0, 0, 0, 0, 0, 0
    };
    const uint8_t zero_count[XHCI_SPC_ENTRY_SIZE] = {
        0x02, 0x00, 0x00, 0x02, 'U', 'S', 'B', ' ',
        1, 0, 0, 0, 0, 0, 0, 0
    };
    const uint8_t overflow[XHCI_SPC_ENTRY_SIZE] = {
        0x02, 0x00, 0x00, 0x03, 'U', 'S', 'B', ' ',
        250, 10, 0, 0, 0, 0, 0, 0
    };
    assert(xhci_spc_decode_entry(legacy, &r) != 0);
    assert(xhci_spc_decode_entry(bad_major, &r) != 0);
    assert(xhci_spc_decode_entry(zero_start, &r) != 0);
    assert(xhci_spc_decode_entry(zero_count, &r) != 0);
    assert(xhci_spc_decode_entry(overflow, &r) != 0);
    assert(xhci_spc_decode_entry(0, &r) != 0);
    assert(xhci_spc_decode_entry(legacy, 0) != 0);
}

int main(void) {
    test_usb2_and_usb3();
    test_rejects();
    return 0;
}
