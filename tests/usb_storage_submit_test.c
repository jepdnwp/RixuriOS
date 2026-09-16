/* Host test: USB storage block submit path against a scripted peer.
 *
 * Scripts xhci_bulk_transfer as a minimal BOT device (INQUIRY, READ
 * CAPACITY, READ10, WRITE10, SYNC CACHE, reject-all for opcode 0xFF)
 * so probe, read, write and flush run the real driver code, including
 * the DMA staging page. Fail-closed submit validation is covered too.
 */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdio.h>
#include <string.h>

#include "../kernel/usb/storage.h"
#include "../kernel/usb/xhci.h"
#include "../kernel/storage/block.h"

/* ---- Scripted peer state ---- */
static int g_slot_active = 1;
static uint8_t fake_page[4096];
static uint8_t cur_opcode;
static uint32_t cur_lba;
static uint16_t cur_count;
static int cur_data_in;
static uint32_t cur_tag;
static int cur_failed;
static uint8_t out_captured[512];
static uint16_t out_captured_len;
static int saw_sync;

uint64_t pmm_alloc_page_below(uint64_t m) { (void)m; return 0x1000u; }
void pmm_free_page(uint64_t p) { (void)p; }
void *vmm_phys_ptr(uint64_t p) { return p == 0x1000u ? fake_page : 0; }
int xhci_slot_active(size_t c, uint8_t s) {
    (void)c; (void)s; return g_slot_active;
}
void serial_write(const char *s) { (void)s; }
void serial_write_hex(uint64_t v) { (void)v; }
void serial_write_dec(uint64_t v) { (void)v; }

int xhci_control_transfer(size_t c, uint8_t s, const rix_usb_setup_packet_t *q,
                          void *d, uint16_t *a) {
    (void)c; (void)s; (void)q;
    if (a) *a = q->length;
    if (d && q->length) memset(d, 0, q->length);
    return 0;
}

int xhci_reset_endpoint(size_t c, uint8_t s, uint8_t e) {
    (void)c; (void)s; (void)e; return 0;
}

int xhci_bulk_transfer(size_t c, uint8_t s, uint8_t e, void *b, uint16_t l,
                       uint16_t *a) {
    uint8_t *buf = (uint8_t *)b;
    (void)c; (void)s;
    if (e == 0x02u && l == BOT_CBW_SIZE) {
        if (usb_storage_le32(buf) != BOT_CBW_SIGNATURE) return -5;
        cur_tag = usb_storage_le32(buf + 4u);
        cur_data_in = (buf[12] & 0x80u) != 0u;
        cur_opcode = buf[15];
        cur_lba = ((uint32_t)buf[17] << 24) | ((uint32_t)buf[18] << 16) |
            ((uint32_t)buf[19] << 8) | (uint32_t)buf[20];
        cur_count = (uint16_t)(((uint16_t)buf[22] << 8) | buf[23]);
        cur_failed = (cur_opcode == 0xFFu);
        if (a) *a = l;
        return 0;
    }
    if (e == 0x81u && l == BOT_CSW_SIZE) {
        usb_storage_put_le32(buf, BOT_CSW_SIGNATURE);
        usb_storage_put_le32(buf + 4u, cur_tag);
        memset(buf + 8u, 0, 4u);
        buf[12] = cur_failed ? 1u : 0u;
        if (cur_opcode == 0x35u) saw_sync = 1;
        if (a) *a = l;
        return 0;
    }
    if (!cur_data_in && l <= sizeof(out_captured)) {
        memcpy(out_captured, buf, l);
        out_captured_len = l;
        if (a) *a = l;
        return 0;
    }
    if (cur_data_in) {
        if (cur_opcode == 0x12u) {
            memset(buf, 0, l);
            memcpy(buf, "FAKEUSB ", 8);
        } else if (cur_opcode == 0x25u) {
            memset(buf, 0, l);
            buf[3] = 15;
            buf[6] = 2;
        } else {
            for (uint16_t i = 0; i < l; ++i)
                buf[i] = (uint8_t)((cur_lba + i) & 0xFFu);
        }
        if (a) *a = l;
        return 0;
    }
    return -1;
}

int main(void) {
    usb_storage_dev_t *dev = 0;
    rix_block_device_t *bdev;
    uint8_t buf[1024];
    rix_bio_t bio;
    /* Probe runs INQUIRY, CAPACITY, LBA0 read and the recovery drill
     * (illegal opcode rejected, device healthy after). */
    assert(block_init() == 0);
    assert(usb_storage_probe(0, 1, 0, 0x81u, 0x02u, &dev) == 0);
    assert(dev != 0 && dev->blocks == 16u);
    bdev = block_find("usb0");
    assert(bdev != 0);
    /* Multi-sector read: LBAs must ascend across commands. */
    memset(buf, 0xAA, sizeof(buf));
    bio.op = RIX_BIO_READ;
    bio.sector = 4;
    bio.count = 2;
    bio.buffer = buf;
    bio.buffer_size = sizeof(buf);
    bio.state = RIX_BIO_PENDING;
    bio.error = 0;
    assert(block_submit(bdev, &bio) == 0);
    assert(bio.state == RIX_BIO_COMPLETE);
    for (uint16_t i = 0; i < 512u; ++i) assert(buf[i] == (uint8_t)((4u + i) & 0xFFu));
    for (uint16_t i = 0; i < 512u; ++i)
        assert(buf[512u + i] == (uint8_t)((5u + i) & 0xFFu));
    /* Write captures the exact payload bytes. */
    for (uint16_t i = 0; i < 512u; ++i) buf[i] = (uint8_t)(i ^ 0x5Au);
    bio.op = RIX_BIO_WRITE;
    bio.sector = 7;
    bio.count = 1;
    bio.buffer = buf;
    bio.buffer_size = 512u;
    bio.state = RIX_BIO_PENDING;
    bio.error = 0;
    assert(block_submit(bdev, &bio) == 0);
    assert(bio.state == RIX_BIO_COMPLETE);
    assert(out_captured_len == 512u);
    for (uint16_t i = 0; i < 512u; ++i)
        assert(out_captured[i] == (uint8_t)(i ^ 0x5Au));
    /* Flush issues SYNCHRONIZE CACHE. */
    saw_sync = 0;
    bio.op = RIX_BIO_FLUSH;
    bio.sector = 0;
    bio.count = 0;
    bio.buffer = buf;
    bio.buffer_size = sizeof(buf);
    bio.state = RIX_BIO_PENDING;
    bio.error = 0;
    assert(block_submit(bdev, &bio) == 0);
    assert(bio.state == RIX_BIO_COMPLETE && saw_sync);
    /* Flush carries no buffer by contract; rejecting NULL fails every
     * journal commit (observed: file creation impossible on USB roots). */
    saw_sync = 0;
    bio.op = RIX_BIO_FLUSH;
    bio.buffer = 0;
    bio.buffer_size = 0;
    bio.state = RIX_BIO_PENDING;
    bio.error = 0;
    assert(block_submit(bdev, &bio) == 0);
    assert(bio.state == RIX_BIO_COMPLETE && saw_sync);
    /* Fail-closed validation. block_submit pre-validates some shapes
     * without touching bio state, so assert its rc plus the submit
     * layer's own verdict via direct calls. */
    bio.op = RIX_BIO_READ;
    bio.sector = 15;
    bio.count = 2;
    bio.buffer = buf;
    bio.buffer_size = sizeof(buf);
    bio.state = RIX_BIO_PENDING;
    assert(block_submit(bdev, &bio) != 0);
    bio.state = RIX_BIO_PENDING;
    bio.error = 0;
    assert(bdev->submit(bdev, &bio) != 0 && bio.state == RIX_BIO_ERROR);
    bio.sector = 0;
    bio.count = 0;
    bio.state = RIX_BIO_PENDING;
    bio.error = 0;
    assert(block_submit(bdev, &bio) != 0);
    assert(bdev->submit(bdev, &bio) != 0 && bio.state == RIX_BIO_ERROR);
    bio.count = 1;
    bio.buffer_size = 511u;
    bio.state = RIX_BIO_PENDING;
    bio.error = 0;
    assert(block_submit(bdev, &bio) != 0);
    assert(bdev->submit(bdev, &bio) != 0 && bio.state == RIX_BIO_ERROR);
    bio.buffer_size = 512u;
    bio.op = (rix_bio_op_t)9;
    bio.state = RIX_BIO_PENDING;
    bio.error = 0;
    assert(block_submit(bdev, &bio) != 0 && bio.state == RIX_BIO_ERROR);
    /* Dead slot fails closed. */
    g_slot_active = 0;
    bio.op = RIX_BIO_READ;
    bio.count = 1;
    bio.state = RIX_BIO_PENDING;
    bio.error = 0;
    assert(block_submit(bdev, &bio) != 0 && bio.state == RIX_BIO_ERROR);
    g_slot_active = 1;
    printf("usb-storage-submit: OK\n");
    return 0;
}
