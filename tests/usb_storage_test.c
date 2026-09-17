/* Host test: USB BOT/SCSI builders and CSW validation (pure logic). */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../kernel/usb/storage.h"
#include "../kernel/usb/xhci.h"

/* ---- Stubs (fail closed, never succeed) ---- */
int xhci_control_transfer(size_t c, uint8_t s, const rix_usb_setup_packet_t *q,
                          void *d, uint16_t *a) {
    (void)c; (void)s; (void)q; (void)d; (void)a; return -1;
}
int xhci_bulk_transfer(size_t c, uint8_t s, uint8_t e, void *b, uint16_t l,
                       uint16_t *a) {
    (void)c; (void)s; (void)e; (void)b; (void)l; (void)a; return -1;
}
int xhci_reset_endpoint(size_t c, uint8_t s, uint8_t e) {
    (void)c; (void)s; (void)e; return -1;
}
void serial_write(const char *s) { (void)s; }
void serial_write_hex(uint64_t v) { (void)v; }
void serial_write_dec(uint64_t v) { (void)v; }
uint64_t pmm_alloc_page_below(uint64_t m) { (void)m; return 0x1000u; }
void pmm_free_page(uint64_t p) { (void)p; }
static uint8_t stub_page[4096];
void *vmm_phys_ptr(uint64_t p) { return p == 0x1000u ? stub_page : 0; }
int xhci_slot_active(size_t c, uint8_t s) { (void)c; (void)s; return 0; }
int block_register(void *d) { (void)d; return -1; }

int main(void) {
    uint8_t cbw[BOT_CBW_SIZE];
    uint8_t cdb[16] = {0x28, 0, 0, 0, 0, 1, 0, 0, 1, 0};
    /* BE helpers round-trip. */
    uint8_t be[4];
    usb_storage_put_be32(be, 0x12345678u);
    assert(be[0] == 0x12u && be[1] == 0x34u && be[2] == 0x56u && be[3] == 0x78u);
    assert(usb_storage_be32(be) == 0x12345678u);
    usb_storage_put_be16(be, 0xabcdu);
    assert(be[0] == 0xabu && be[1] == 0xcdu);
    /* CBW layout: signature, tag, length, flags, LUN, CDB len, CDB.
     * Header words are little-endian on the wire ("USBC" bytes); assert
     * raw bytes, not a round-trip through the same helper. */
    usb_storage_build_cbw(cbw, 0x11223344u, 512u, 1, 0, 10, cdb);
    assert(cbw[0] == 0x55u && cbw[1] == 0x53u && cbw[2] == 0x42u &&
           cbw[3] == 0x43u);
    assert(cbw[4] == 0x44u && cbw[5] == 0x33u && cbw[6] == 0x22u &&
           cbw[7] == 0x11u);
    assert(cbw[8] == 0x00u && cbw[9] == 0x02u && cbw[10] == 0x00u &&
           cbw[11] == 0x00u);
    assert(cbw[12] == 0x80u && cbw[13] == 0u && cbw[14] == 10u);
    assert(memcmp(cbw + 15u, cdb, 10) == 0);
    assert(usb_storage_le32(cbw) == BOT_CBW_SIGNATURE);
    assert(usb_storage_le32(cbw + 4u) == 0x11223344u);
    usb_storage_build_cbw(cbw, 1u, 0u, 0, 3u, 6, cdb);
    assert(cbw[12] == 0x00u && (cbw[13] & 0x0fu) == 3u && cbw[14] == 6u);
    /* CSW validation (little-endian header on the wire). */
    {
        uint8_t csw[BOT_CSW_SIZE] = {0};
        usb_storage_put_le32(csw, BOT_CSW_SIGNATURE);
        usb_storage_put_le32(csw + 4u, 0x11223344u);
        csw[12] = 0;
        assert(usb_storage_parse_csw(csw, sizeof(csw), 0x11223344u) == 0);
        csw[12] = 1;
        assert(usb_storage_parse_csw(csw, sizeof(csw), 0x11223344u) == -10);
        csw[12] = 2;
        assert(usb_storage_parse_csw(csw, sizeof(csw), 0x11223344u) == -11);
        csw[12] = 0;
        assert(usb_storage_parse_csw(csw, sizeof(csw), 0xdeadbeefu) == -4);
        csw[0] ^= 0xffu;
        assert(usb_storage_parse_csw(csw, sizeof(csw), 0x11223344u) == -3);
        assert(usb_storage_parse_csw(csw, 12u, 0x11223344u) == -2);
        assert(usb_storage_parse_csw(0, sizeof(csw), 0x11223344u) == -1);
    }
    /* CDB builders. */
    {
        uint8_t q[6];
        uint8_t r10[10];
        uint8_t w10[10];
        uint8_t cap[10];
        usb_storage_build_inquiry(q, 36u);
        assert(q[0] == 0x12u && q[3] == 0u && q[4] == 36u && q[5] == 0u);
        usb_storage_build_read_capacity10(cap);
        assert(cap[0] == 0x25u && cap[1] == 0u && cap[9] == 0u);
        usb_storage_build_read10(r10, 0x12345678u, 0x0100u);
        assert(r10[0] == 0x28u && r10[2] == 0x12u && r10[5] == 0x78u);
        assert(r10[7] == 0x01u && r10[8] == 0x00u && r10[9] == 0u);
        usb_storage_build_write10(w10, 100u, 1u);
        assert(w10[0] == 0x2Au && w10[5] == 100u && w10[8] == 1u);
    }
    /* Wire paths fail closed through stubs. */
    {
        uint8_t lun = 0xffu;
        uint8_t data[36];
        uint32_t tag = 0;
        assert(usb_storage_get_max_lun(0, 1, 0, &lun) != 0);
        assert(usb_storage_get_max_lun(0, 1, 0, 0) != 0);
        assert(usb_storage_get_max_lun(0, 1, 32u, &lun) != 0);
        assert(usb_storage_reset_recovery(0, 1, 0, 0x81u, 0x02u) != 0);
        assert(usb_storage_reset_recovery(0, 1, 0, 0, 0x02u) != 0);
        assert(usb_storage_command(0, 1, 0, 0x81u, 0x02u, &tag, cdb, 10,
                                   data, sizeof(data), 1) != 0);
        assert(usb_storage_command(0, 1, 0, 0x81u, 0x02u, 0, cdb, 10, data,
                                   sizeof(data), 1) != 0);
        assert(usb_storage_command(0, 1, 0, 0x81u, 0x02u, &tag, cdb, 0, data,
                                   sizeof(data), 1) != 0);
        assert(usb_storage_command(0, 1, 0, 0x81u, 0x02u, &tag, cdb, 10, 0,
                                   sizeof(data), 1) != 0);
        assert(usb_storage_command(0, 1, 0, 0x81u, 0x02u, &tag, cdb, 10, data,
                                   513u, 1) != 0);
        assert(usb_storage_probe(0, 1, 0, 0x81u, 0x02u, 0) != 0);
        assert(usb_storage_probe(0, 1, 0, 0, 0x02u, 0) != 0);
        assert(usb_storage_read(0, 9, 0, data) != 0);
        assert(usb_storage_read(0, 9, 0, 0) != 0);
        assert(usb_storage_write(0, 9, 0, data) != 0);
        assert(usb_storage_write(0, 9, 0, 0) != 0);
        assert(usb_storage_find(0, 9) == 0);
    }
    printf("usb-storage: OK\n");
    return 0;
}
