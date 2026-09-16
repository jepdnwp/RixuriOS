/* Host test: BOT retry state machine against a scripted peer.
 *
 * The always-fail stubs in usb_storage_test prove fail-closed behavior.
 * Here the peer fails the first CBW with a stall, then behaves: this
 * proves usb_storage_command runs reset recovery and the retry succeeds
 * (returns 0 with the tag advanced). No hardware involved; the seam is
 * the driver's own transfer-function boundary.
 */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../kernel/usb/storage.h"
#include "../kernel/usb/xhci.h"

static int bulk_calls;
static uint32_t seen_tag;
static int controls_ok;

int xhci_control_transfer(size_t c, uint8_t s, const rix_usb_setup_packet_t *q,
                          void *d, uint16_t *a) {
    (void)c; (void)s; (void)q; (void)d; (void)a;
    controls_ok++;
    return 0;
}

int xhci_bulk_transfer(size_t c, uint8_t s, uint8_t e, void *b, uint16_t l,
                       uint16_t *a) {
    (void)c; (void)s; (void)e;
    if (bulk_calls == 0) {
        bulk_calls++;
        return -6;
    }
    if (l == BOT_CBW_SIZE) {
        seen_tag = usb_storage_le32((const uint8_t *)b + 4u);
        if (a) *a = l;
        bulk_calls++;
        return 0;
    }
    if (l == BOT_CSW_SIZE) {
        uint8_t *csw = (uint8_t *)b;
        usb_storage_put_le32(csw, BOT_CSW_SIGNATURE);
        usb_storage_put_le32(csw + 4u, seen_tag);
        csw[12] = 0;
        if (a) *a = l;
        bulk_calls++;
        return 0;
    }
    return -1;
}

int xhci_reset_endpoint(size_t c, uint8_t s, uint8_t e) {
    (void)c; (void)s; (void)e;
    return 0;
}

void serial_write(const char *s) { (void)s; }
void serial_write_hex(uint64_t v) { (void)v; }
void serial_write_dec(uint64_t v) { (void)v; }

int main(void) {
    uint8_t cdb[6] = {0};
    uint32_t tag = 0;
    int rc = usb_storage_command(0, 1, 0, 0x81u, 0x02u, &tag, cdb, 6, 0,
                                 0, 1);
    assert(rc == 0);
    assert(tag == 2u);
    assert(seen_tag == 2u);
    assert(bulk_calls == 3);
    assert(controls_ok > 0);
    printf("usb-storage-retry: OK\n");
    return 0;
}
