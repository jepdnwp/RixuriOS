/* Host test: Linux-derived xHCI register/TRB definitions match the
 * verified hardware values.
 *
 * Compares kernel/usb/xhci/regs.h + trb.h (ported from Linux
 * drivers/usb/host/xhci.h) against the numeric values used on real AMD
 * hardware. Any mismatch fails closed. The driver itself uses native
 * RixuriOS MMIO/DMA/sync primitives directly (no compat layer).
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "../kernel/usb/xhci/regs.h"
#include "../kernel/usb/xhci/trb.h"

int main(void) {
    /* Operational offsets used by setup_runtime/reset_controller. */
    assert(XHCI_USBCMD == 0x00u);
    assert(XHCI_USBSTS == 0x04u);
    assert(XHCI_PAGESIZE == 0x08u);
    assert(XHCI_DNCTRL == 0x14u);
    assert(XHCI_CRCR == 0x18u);
    assert(XHCI_DCBAAP == 0x30u);
    assert(XHCI_CONFIG == 0x38u);
    assert(XHCI_PORTSC_BASE == 0x400u);
    assert(XHCI_PORT_STRIDE == 0x10u);
    /* Command/status bits. */
    assert(XHCI_CMD_RUN == (1u << 0));
    assert(XHCI_CMD_HCRST == (1u << 1));
    assert(XHCI_CMD_INTE == (1u << 2));
    assert(XHCI_STS_HCH == (1u << 0));
    assert(XHCI_STS_CNR == (1u << 11));
    assert(XHCI_STS_HCE == (1u << 12));
    /* Capability offsets. */
    assert(XHCI_HCSPARAMS1 == 0x04u);
    assert(XHCI_HCCPARAMS1 == 0x10u);
    assert(XHCI_DBOFF == 0x14u);
    assert(XHCI_RTSOFF == 0x18u);
    assert(XHCI_HCC_AC64 == (1u << 0));
    /* TRB types mirrored in xhci.c. */
    assert(XHCI_TRB_NORMAL == 1u);
    assert(XHCI_TRB_SETUP_STAGE == 2u);
    assert(XHCI_TRB_DATA_STAGE == 3u);
    assert(XHCI_TRB_STATUS_STAGE == 4u);
    assert(XHCI_TRB_LINK == 6u);
    assert(XHCI_TRB_ENABLE_SLOT == 9u);
    assert(XHCI_TRB_DISABLE_SLOT == 10u);
    assert(XHCI_TRB_ADDRESS_DEVICE == 11u);
    assert(XHCI_TRB_TRANSFER_EVENT == 32u);
    assert(XHCI_TRB_COMMAND_COMPLETION == 33u);
    assert(XHCI_TRB_PORT_STATUS_CHANGE == 34u);
    assert(XHCI_TRB_TYPE_SHIFT == 10u);
    assert(XHCI_TRB_SLOT_SHIFT == 24u);
    assert(XHCI_TRB_EP_SHIFT == 16u);
    /* Completion codes. */
    assert(XHCI_COMP_SUCCESS == 1u);
    assert(XHCI_COMP_USB_TRANSACTION_ERROR == 4u);
    assert(XHCI_COMP_SHORT_PACKET == 13u);
    assert(XHCI_GET_COMP_CODE(0x01000000u) == 1u);
    assert(XHCI_EVENT_TRB_LEN(0x00ffffffu) == 0xffffffu);
    /* Context flags. */
    assert(XHCI_INPUT_ADD_SLOT == (1u << 0));
    assert(XHCI_INPUT_ADD_EP0 == (1u << 1));
    assert(XHCI_EP_CONTROL == 4u);
    /* Struct sizes: TRB is always 16 bytes; ERST entry is 16 bytes. */
    assert(sizeof(rix_xhci_trb_t) == 16u);
    assert(sizeof(rix_xhci_erst_entry_t) == 16u);
    assert(sizeof(rix_xhci_cap_regs_t) == 32u);
    printf("xhci-linux-derived: OK\n");
    return 0;
}
