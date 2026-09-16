/* USB Mass Storage Bulk-Only Transport driver (read/write data path).
 *
 * Provenance: USB Mass Storage BOT 1.0 (CBW/CSW phases, reset recovery)
 * and SCSI SPC/SBC command shapes, following the Linux
 * drivers/usb/storage/transport.c discipline: CBW → data → CSW strict
 * ordering, exact-length CSW, tag/signature validation, and reset
 * recovery (class BOT reset + Clear Feature on both bulk endpoints) on
 * failure or stall, with exactly one retry. Only what a single-user
 * desktop needs: SCSI subclass / BOT protocol, LUN 0 unless the device
 * reports more, 512-byte blocks.
 */

#include "storage.h"
#include "usb_ch9.h"
#include "xhci.h"
#include "xhci/trb.h"
#include "../serial.h"
#include "../storage/block.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"

static usb_storage_dev_t storages[USB_STORAGE_MAX_DEVS];
/* Block-layer bindings, parallel to storages[]: registered once,
 * activated per probe, deactivated on detach. */
typedef struct {
    rix_block_device_t bdev;
    uint8_t registered;
    uint8_t active;
    size_t controller;
    uint8_t slot;
    uint32_t blocks;
} usb_block_t;

static usb_block_t usb_blocks[USB_STORAGE_MAX_DEVS];

static void usb_block_register(size_t controller, uint8_t slot,
                               uint32_t blocks);
static int usb_storage_transaction(size_t controller, uint8_t slot,
                                   uint8_t interface_number, uint8_t bulk_in,
                                   uint8_t bulk_out, uint32_t *tag,
                                   const uint8_t *cdb, uint8_t cdb_len,
                                   uint8_t *xfer, uint32_t data_len,
                                   int data_in);
/* Single 512-byte staging sector (.bss image memory is physically
 * contiguous, satisfying the DMA linearity check). */
static uint8_t sector_stage[USB_STORAGE_SECTOR];

void usb_storage_put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* CBW/CSW header words (signature, tag, transfer length) are
 * little-endian on the wire (BOT 5.x __le32; Linux cpu_to_le32).
 * Only the CDB payload and SCSI data use big-endian. Mixing them up
 * puts "CBSU" on the wire and the device stalls every CBW. */
void usb_storage_put_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

uint32_t usb_storage_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void usb_storage_put_be16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

uint32_t usb_storage_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
        ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

void usb_storage_build_cbw(uint8_t cbw[BOT_CBW_SIZE], uint32_t tag,
                            uint32_t xfer_len, int data_in, uint8_t lun,
                            uint8_t cdb_len, const uint8_t *cdb) {
    unsigned i;
    for (i = 0; i < BOT_CBW_SIZE; ++i) cbw[i] = 0;
    usb_storage_put_le32(cbw, BOT_CBW_SIGNATURE);
    usb_storage_put_le32(cbw + 4u, tag);
    usb_storage_put_le32(cbw + 8u, xfer_len);
    cbw[12] = data_in ? 0x80u : 0x00u;
    cbw[13] = lun & 0x0fu;
    cbw[14] = cdb_len > 16u ? 16u : cdb_len;
    for (i = 0; i < cbw[14]; ++i) cbw[15 + i] = cdb[i];
}

/* 0 transport-good, -1 bad args, -2 short, -3 bad signature,
 * -4 tag mismatch, -10 command failed, -11 phase error. */
int usb_storage_parse_csw(const uint8_t *csw, size_t length, uint32_t tag) {
    uint32_t sig;
    uint32_t echo;
    if (!csw) return -1;
    if (length < BOT_CSW_SIZE) return -2;
    sig = usb_storage_le32(csw);
    if (sig != BOT_CSW_SIGNATURE) return -3;
    echo = usb_storage_le32(csw + 4u);
    if (echo != tag) return -4;
    if (csw[12] == BOT_CSW_GOOD) return 0;
    if (csw[12] == BOT_CSW_FAILED) return -10;
    if (csw[12] == BOT_CSW_PHASE_ERROR) return -11;
    return -10;
}

void usb_storage_build_inquiry(uint8_t cdb[6], uint16_t alloc_len) {
    unsigned i;
    for (i = 0; i < 6u; ++i) cdb[i] = 0;
    cdb[0] = SCSI_INQUIRY;
    cdb[3] = (uint8_t)(alloc_len >> 8);
    cdb[4] = (uint8_t)alloc_len;
}

void usb_storage_build_read_capacity10(uint8_t cdb[10]) {
    unsigned i;
    for (i = 0; i < 10u; ++i) cdb[i] = 0;
    cdb[0] = SCSI_READ_CAPACITY10;
}

void usb_storage_build_read10(uint8_t cdb[10], uint32_t lba, uint16_t count) {
    unsigned i;
    for (i = 0; i < 10u; ++i) cdb[i] = 0;
    cdb[0] = SCSI_READ10;
    usb_storage_put_be32(cdb + 2u, lba);
    usb_storage_put_be16(cdb + 7u, count);
}

void usb_storage_build_write10(uint8_t cdb[10], uint32_t lba, uint16_t count) {
    unsigned i;
    for (i = 0; i < 10u; ++i) cdb[i] = 0;
    cdb[0] = SCSI_WRITE10;
    usb_storage_put_be32(cdb + 2u, lba);
    usb_storage_put_be16(cdb + 7u, count);
}

void usb_storage_build_sync_cache10(uint8_t cdb[10]) {
    unsigned i;
    for (i = 0; i < 10u; ++i) cdb[i] = 0;
    cdb[0] = SCSI_SYNC_CACHE10;
}

static int storage_control(size_t controller, uint8_t slot,
                           uint8_t request_type, uint8_t request,
                           uint16_t value, uint16_t index, void *data,
                           uint16_t length, uint16_t *actual) {
    rix_usb_setup_packet_t setup;
    setup.request_type = request_type;
    setup.request = request;
    setup.value = value;
    setup.index = index;
    setup.length = length;
    return xhci_control_transfer(controller, slot, &setup, data, actual);
}

/* GET_MAX_LUN (BOT 3.2): IN class interface request 0xFE. A stall means
 * "no LUN support, use LUN 0" — clear the halt and report 0, not an
 * error (Linux usb_stor_GetMaxLUN same rule). */
int usb_storage_get_max_lun(size_t controller, uint8_t slot,
                            uint8_t interface_number, uint8_t *max_lun) {
    uint8_t value = 0;
    uint16_t actual = 0;
    int rc;
    if (!max_lun || interface_number >= 32u) return -1;
    *max_lun = 0;
    rc = storage_control(controller, slot,
                         (uint8_t)(USB_DIR_IN | USB_TYPE_CLASS |
                                    USB_RECIP_INTERFACE),
                         BOT_GET_MAX_LUN, 0, interface_number, &value, 1,
                         &actual);
    if (rc == -(int)XHCI_COMP_STALL_ERROR) {
        /* Clear the expected stall so EP0 is usable again. */
        rix_usb_setup_packet_t clear;
        clear.request_type = (uint8_t)(USB_DIR_OUT | USB_TYPE_STANDARD |
                                       USB_RECIP_DEVICE);
        clear.request = USB_REQ_CLEAR_FEATURE;
        clear.value = USB_ENDPOINT_HALT;
        clear.index = 0;
        clear.length = 0;
        (void)xhci_control_transfer(controller, slot, &clear, 0, 0);
        return 0;
    }
    if (rc != 0) return rc;
    if (actual != 1u) return -2;
    *max_lun = value;
    return 0;
}

/* BOT reset recovery (BOT 5.3.4 + Linux usb_stor_reset_common): class
 * BOT reset, then Clear Feature HALT on both bulk endpoints (device
 * side) plus host Reset Endpoint on both (host side). Best-effort
 * clears; the reset itself must succeed. */
int usb_storage_reset_recovery(size_t controller, uint8_t slot,
                               uint8_t interface_number, uint8_t bulk_in,
                               uint8_t bulk_out) {
    int rc;
    if (interface_number >= 32u || bulk_in == 0u || bulk_out == 0u)
        return -1;
    rc = storage_control(controller, slot,
                         (uint8_t)(USB_DIR_OUT | USB_TYPE_CLASS |
                                    USB_RECIP_INTERFACE),
                         BOT_RESET, 0, interface_number, 0, 0, 0);
    if (rc != 0) return rc;
    {
        rix_usb_setup_packet_t clear;
        clear.request_type = (uint8_t)(USB_DIR_OUT | USB_TYPE_STANDARD |
                                       USB_RECIP_ENDPOINT);
        clear.request = USB_REQ_CLEAR_FEATURE;
        clear.value = USB_ENDPOINT_HALT;
        clear.index = bulk_in;
        clear.length = 0;
        (void)xhci_control_transfer(controller, slot, &clear, 0, 0);
        clear.index = bulk_out;
        (void)xhci_control_transfer(controller, slot, &clear, 0, 0);
    }
    (void)xhci_reset_endpoint(controller, slot, bulk_in);
    (void)xhci_reset_endpoint(controller, slot, bulk_out);
    return 0;
}

static int bulk_exact(size_t controller, uint8_t slot, uint8_t ep,
                      void *buffer, uint16_t length, int is_in) {
    uint16_t actual = 0;
    int rc;
    if (length == 0u) return 0;
    if (is_in)
        rc = xhci_bulk_transfer(controller, slot, ep, buffer, length,
                                &actual);
    else {
        /* Bulk-OUT has no residual accounting need; reuse the same path
         * with a scratch actual. */
        rc = xhci_bulk_transfer(controller, slot, ep, buffer, length,
                                &actual);
    }
    if (rc != 0) return rc;
    if (actual != length) return -2;
    return 0;
}

/* One BOT transaction with exactly one recovery + retry. data_len is
 * capped to 512 (one sector): the only caller sizes are INQUIRY(36),
 * CAPACITY(8) and single-sector READ/WRITE/SYNC (512/0). Larger sizes
 * fail closed.
 *
 * DMA staging: transfer buffers come from callers that may hand stack
 * slices straddling a physical page boundary (the block cache does), so
 * the payload always moves through a freshly allocated DMA page. The
 * page is per-call, so this stays correct under SMP concurrency with no
 * new locks; transfers never yield, so no other task can interleave. */
int usb_storage_command(size_t controller, uint8_t slot,
                        uint8_t interface_number, uint8_t bulk_in,
                        uint8_t bulk_out, uint32_t *tag, const uint8_t *cdb,
                        uint8_t cdb_len, void *data, uint32_t data_len,
                        int data_in) {
    uint64_t stage = 0;
    uint8_t *xfer = (uint8_t *)data;
    int rc;
    if (!tag || !cdb || cdb_len == 0u || cdb_len > 16u ||
        interface_number >= 32u || bulk_in == 0u || bulk_out == 0u)
        return -1;
    if (data_len > USB_STORAGE_SECTOR) return -1;
    if (data_len != 0u && !data) return -1;
    if (data_len != 0u) {
        /* Below 4 GiB: safe for xHCIs without 64-bit addressing. */
        stage = pmm_alloc_page_below(0x100000000ULL);
        if (!stage) return -20;
        xfer = (uint8_t *)vmm_phys_ptr(stage);
        if (!xfer) {
            pmm_free_page(stage);
            return -20;
        }
        if (!data_in) __builtin_memcpy(xfer, data, data_len);
    }
    rc = usb_storage_transaction(controller, slot, interface_number,
                                 bulk_in, bulk_out, tag, cdb, cdb_len,
                                 xfer, data_len, data_in);
    if (data_in && data_len != 0u && data)
        __builtin_memcpy(data, xfer, data_len);
    if (stage) pmm_free_page(stage);
    return rc;
}

static int usb_storage_transaction(size_t controller, uint8_t slot,
                                   uint8_t interface_number, uint8_t bulk_in,
                                   uint8_t bulk_out, uint32_t *tag,
                                   const uint8_t *cdb, uint8_t cdb_len,
                                   uint8_t *xfer, uint32_t data_len,
                                   int data_in) {
    uint8_t cbw[BOT_CBW_SIZE];
    uint8_t csw[BOT_CSW_SIZE];
    uint32_t my_tag;
    int rc;
    unsigned attempt;
    for (attempt = 0; attempt < 2u; ++attempt) {
        uint16_t csw_actual = 0;
        uint16_t chunk;
        uint32_t done = 0;
        my_tag = *tag + 1u;
        if (my_tag == 0u) my_tag = 1u;
        *tag = my_tag;
        usb_storage_build_cbw(cbw, my_tag, data_len, data_in, 0,
                              cdb_len, cdb);
        rc = bulk_exact(controller, slot, bulk_out, cbw, BOT_CBW_SIZE, 0);
        if (rc != 0) {
            serial_write("xHCI: storage CBW failed=");
            serial_write_dec((uint64_t)(rc < 0 ? -rc : rc));
            serial_write("\r\n");
            goto recover;
        }
        while (done < data_len) {
            uint32_t left = data_len - done;
            chunk = left > USB_STORAGE_SECTOR ? USB_STORAGE_SECTOR
                                              : (uint16_t)left;
            if (data_in)
                rc = bulk_exact(controller, slot, bulk_in, xfer + done,
                                chunk, 1);
            else
                rc = bulk_exact(controller, slot, bulk_out, xfer + done,
                                chunk, 0);
            if (rc != 0) {
                serial_write("xHCI: storage DATA failed=");
                serial_write_dec((uint64_t)(rc < 0 ? -rc : rc));
                serial_write("\r\n");
                goto recover;
            }
            done += chunk;
        }
        for (unsigned i = 0; i < BOT_CSW_SIZE; ++i) csw[i] = 0;
        rc = xhci_bulk_transfer(controller, slot, bulk_in, csw,
                                BOT_CSW_SIZE, &csw_actual);
        if (rc != 0) {
            serial_write("xHCI: storage CSW failed=");
            serial_write_dec((uint64_t)(rc < 0 ? -rc : rc));
            serial_write("\r\n");
            goto recover;
        }
        rc = usb_storage_parse_csw(csw, csw_actual, my_tag);
        if (rc == 0) return 0;
    recover:
        if (attempt == 0u &&
            usb_storage_reset_recovery(controller, slot, interface_number,
                                       bulk_in, bulk_out) == 0)
            continue;
        return rc != 0 ? rc : -90;
    }
    return -90;
}

static usb_storage_dev_t *storage_mut(size_t controller, uint8_t slot) {
    for (unsigned i = 0; i < USB_STORAGE_MAX_DEVS; ++i) {
        if (storages[i].used && storages[i].controller == controller &&
            storages[i].slot == slot)
            return &storages[i];
    }
    return 0;
}

const usb_storage_dev_t *usb_storage_find(size_t controller, uint8_t slot) {
    for (unsigned i = 0; i < USB_STORAGE_MAX_DEVS; ++i) {
        if (storages[i].used && storages[i].controller == controller &&
            storages[i].slot == slot)
            return &storages[i];
    }
    return 0;
}

int usb_storage_verify_recovery(size_t controller, uint8_t slot) {
    usb_storage_dev_t *dev = storage_mut(controller, slot);
    uint8_t cdb[16];
    uint8_t sector[USB_STORAGE_SECTOR];
    unsigned i;
    int rc;
    if (!dev) return -1;
    for (i = 0; i < 6u; ++i) cdb[i] = 0;
    cdb[0] = 0xFFu;
    /* A compliant device must reject the illegal opcode (CSW failed or
     * stall). Acceptance would mean the device executed the unknown. */
    rc = usb_storage_command(controller, slot, dev->interface_number,
                             dev->bulk_in, dev->bulk_out, &dev->tag, cdb,
                             6, 0, 0, 1);
    if (rc == 0) {
        serial_write("xHCI: storage illegal opcode accepted\r\n");
        return -13;
    }
    /* The command above already ran reset recovery internally; a plain
     * read must now succeed, proving the device is healthy again. */
    for (i = 0; i < USB_STORAGE_SECTOR; ++i) sector[i] = 0;
    usb_storage_build_read10(cdb, 0u, 1u);
    rc = usb_storage_command(controller, slot, dev->interface_number,
                             dev->bulk_in, dev->bulk_out, &dev->tag, cdb,
                             10, sector, USB_STORAGE_SECTOR, 1);
    if (rc != 0) {
        serial_write("xHCI: storage post-recovery read failed=");
        serial_write_dec((uint64_t)(rc < 0 ? -rc : rc));
        serial_write("\r\n");
        return -14;
    }
    serial_write("xHCI: storage recovery=PASS\r\n");
    return 0;
}

/* Probe: GET_MAX_LUN, INQUIRY (log vendor/product), READ CAPACITY(10)
 * (512-only, fail closed otherwise), then read LBA 0 and log its first
 * 8 bytes. Strictly read-only: unlike a scratch self-test, this runs on
 * unknown real sticks, so it must never write user data. Returns 0 with
 * *out_dev set. */
int usb_storage_probe(size_t controller, uint8_t slot,
                      uint8_t interface_number, uint8_t bulk_in,
                      uint8_t bulk_out, usb_storage_dev_t **out_dev) {
    usb_storage_dev_t *dev = 0;
    uint8_t inquiry[36];
    uint8_t capacity[8];
    uint8_t cdb[16];
    uint32_t tag = 0;
    uint32_t blocks;
    uint32_t i;
    int crc;
    if (interface_number >= 32u || bulk_in == 0u || bulk_out == 0u)
        return -1;
    for (i = 0; i < USB_STORAGE_MAX_DEVS; ++i) {
        if (!storages[i].used) {
            dev = &storages[i];
            break;
        }
    }
    if (!dev) return -2;
    crc = usb_storage_get_max_lun(controller, slot, interface_number,
                                  &dev->max_lun);
    if (crc != 0) {
        serial_write("xHCI: storage max-lun failed=");
        serial_write_dec((uint64_t)(crc < 0 ? -crc : crc));
        serial_write("\r\n");
        return -12;
    }
    for (i = 0; i < sizeof(inquiry); ++i) inquiry[i] = 0;
    usb_storage_build_inquiry(cdb, sizeof(inquiry));
    crc = usb_storage_command(controller, slot, interface_number, bulk_in,
                              bulk_out, &tag, cdb, 6, inquiry,
                              sizeof(inquiry), 1);
    if (crc != 0) {
        serial_write("xHCI: storage INQUIRY failed=");
        serial_write_dec((uint64_t)(crc < 0 ? -crc : crc));
        serial_write("\r\n");
        return -3;
    }
    serial_write("xHCI: storage INQUIRY vendor=");
    for (i = 8u; i < 16u; ++i) serial_write_hex(inquiry[i]);
    serial_write(" product=");
    for (i = 16u; i < 32u; ++i) serial_write_hex(inquiry[i]);
    serial_write("\r\n");
    for (i = 0; i < sizeof(capacity); ++i) capacity[i] = 0;
    usb_storage_build_read_capacity10(cdb);
    if (usb_storage_command(controller, slot, interface_number, bulk_in,
                            bulk_out, &tag, cdb, 10, capacity,
                            sizeof(capacity), 1) != 0)
        return -4;
    if (usb_storage_be32(capacity + 4u) != USB_STORAGE_SECTOR) {
        serial_write("xHCI: storage non-512 block unsupported\r\n");
        return -5;
    }
    blocks = usb_storage_be32(capacity) + 1u;
    if (blocks == 0u) return -6;
    for (i = 0; i < USB_STORAGE_SECTOR; ++i) sector_stage[i] = 0;
    usb_storage_build_read10(cdb, 0u, 1u);
    if (usb_storage_command(controller, slot, interface_number, bulk_in,
                            bulk_out, &tag, cdb, 10, sector_stage,
                            USB_STORAGE_SECTOR, 1) != 0)
        return -7;
    serial_write("xHCI: storage LBA0=");
    for (i = 0; i < 8u; ++i) serial_write_hex(sector_stage[i]);
    serial_write("\r\n");
    dev->used = 1;
    dev->controller = controller;
    dev->slot = slot;
    dev->interface_number = interface_number;
    dev->bulk_in = bulk_in;
    dev->bulk_out = bulk_out;
    dev->tag = tag;
    dev->blocks = blocks;
    serial_write("xHCI: storage ready blocks=");
    serial_write_dec(blocks);
    serial_write("\r\n");
    if (usb_storage_verify_recovery(controller, slot) != 0) {
        dev->used = 0;
        return -15;
    }
    usb_block_register(controller, slot, blocks);
    if (out_dev) *out_dev = dev;
    return 0;
}

int usb_storage_read(size_t controller, uint8_t slot, uint32_t lba,
                     void *buffer) {
    usb_storage_dev_t *dev = storage_mut(controller, slot);
    uint8_t cdb[16];
    if (!dev || !buffer) return -1;
    if (lba >= dev->blocks) return -2;
    usb_storage_build_read10(cdb, lba, 1u);
    return usb_storage_command(controller, slot, dev->interface_number,
                               dev->bulk_in, dev->bulk_out, &dev->tag, cdb,
                               10, buffer, USB_STORAGE_SECTOR, 1);
}

int usb_storage_write(size_t controller, uint8_t slot, uint32_t lba,
                      const void *buffer) {
    usb_storage_dev_t *dev = storage_mut(controller, slot);
    uint8_t cdb[16];
    if (!dev || !buffer) return -1;
    if (lba >= dev->blocks) return -2;
    /* DMA staging happens inside usb_storage_command. */
    usb_storage_build_write10(cdb, lba, 1u);
    return usb_storage_command(controller, slot, dev->interface_number,
                               dev->bulk_in, dev->bulk_out, &dev->tag, cdb,
                               10, (void *)buffer, USB_STORAGE_SECTOR, 0);
}

void usb_storage_detach(size_t controller, uint8_t slot) {
    for (unsigned i = 0; i < USB_STORAGE_MAX_DEVS; ++i) {
        if (storages[i].used && storages[i].controller == controller &&
            storages[i].slot == slot)
            storages[i].used = 0;
    }
    for (unsigned i = 0; i < USB_STORAGE_MAX_DEVS; ++i) {
        if (usb_blocks[i].active && usb_blocks[i].controller == controller &&
            usb_blocks[i].slot == slot)
            usb_blocks[i].active = 0;
    }
}

/* Block-layer backend: one rix_block_device ("usbN") per probed stick.
 * submit() maps BIO sectors 1:1 to single-sector BOT transfers (the BIO
 * contract caps count at RIX_BIO_MAX_SECTORS) and SYNCHRONIZE CACHE to
 * flush. Dead slots fail closed via the active flag plus a live slot
 * check, so unplugged sticks can never complete ghost I/O. */
static int usb_block_submit(rix_block_device_t *d, rix_bio_t *bio) {
    usb_block_t *w = (usb_block_t *)d->driver_data;
    uint64_t end;
    uint64_t i;
    /* FLUSH carries no buffer by contract (block.c only requires
     * count==0); READ/WRITE must have one. Rejecting a NULL flush
     * breaks every journal commit, which is exactly how an earlier
     * revision failed file creation on USB roots. */
    if (!w || !bio || (bio->op != RIX_BIO_FLUSH && !bio->buffer)) {
        if (bio) {
            bio->state = RIX_BIO_ERROR;
            bio->error = -1;
        }
        return -1;
    }
    if (!w->active || !xhci_slot_active(w->controller, w->slot)) {
        bio->state = RIX_BIO_ERROR;
        bio->error = -1;
        return -1;
    }
    if (bio->op == RIX_BIO_FLUSH) {
        usb_storage_dev_t *dev = storage_mut(w->controller, w->slot);
        uint8_t cdb[16];
        int rc;
        if (!dev) {
            bio->state = RIX_BIO_ERROR;
            bio->error = -1;
            return -1;
        }
        usb_storage_build_sync_cache10(cdb);
        rc = usb_storage_command(w->controller, w->slot,
                                 dev->interface_number, dev->bulk_in,
                                 dev->bulk_out, &dev->tag, cdb, 10, 0, 0,
                                 1);
        bio->state = rc == 0 ? RIX_BIO_COMPLETE : RIX_BIO_ERROR;
        bio->error = rc;
        return rc;
    }
    if (bio->op != RIX_BIO_READ && bio->op != RIX_BIO_WRITE) {
        bio->state = RIX_BIO_ERROR;
        bio->error = -2;
        return -2;
    }
    if (bio->count == 0u || bio->count > RIX_BIO_MAX_SECTORS) {
        bio->state = RIX_BIO_ERROR;
        bio->error = -2;
        return -2;
    }
    end = bio->sector + (uint64_t)bio->count;
    if (end < bio->sector || end > w->blocks) {
        bio->state = RIX_BIO_ERROR;
        bio->error = -2;
        return -2;
    }
    if (bio->buffer_size < (uint64_t)bio->count * USB_STORAGE_SECTOR) {
        bio->state = RIX_BIO_ERROR;
        bio->error = -2;
        return -2;
    }
    for (i = 0; i < bio->count; ++i) {
        uint32_t lba = (uint32_t)(bio->sector + i);
        uint8_t *buf = (uint8_t *)bio->buffer + i * USB_STORAGE_SECTOR;
        int rc = (bio->op == RIX_BIO_READ)
            ? usb_storage_read(w->controller, w->slot, lba, buf)
            : usb_storage_write(w->controller, w->slot, lba, buf);
        if (rc != 0) {
            serial_write("xHCI: storage BIO op=");
            serial_write_dec(bio->op);
            serial_write(" sector=");
            serial_write_dec(bio->sector + i);
            serial_write(" rc=");
            serial_write_dec((uint64_t)(rc < 0 ? -rc : rc));
            serial_write("\r\n");
            bio->state = RIX_BIO_ERROR;
            bio->error = rc;
            return rc;
        }
    }
    bio->state = RIX_BIO_COMPLETE;
    bio->error = 0;
    return 0;
}

static void usb_block_register(size_t controller, uint8_t slot,
                               uint32_t blocks) {
    for (unsigned i = 0; i < USB_STORAGE_MAX_DEVS; ++i) {
        usb_block_t *w = &usb_blocks[i];
        if (w->active) continue;
        w->bdev.name[0] = 'u';
        w->bdev.name[1] = 's';
        w->bdev.name[2] = 'b';
        w->bdev.name[3] = (char)('0' + i);
        w->bdev.name[4] = 0;
        w->bdev.sector_size = USB_STORAGE_SECTOR;
        w->bdev.sector_count = blocks;
        w->bdev.max_sectors = RIX_BIO_MAX_SECTORS;
        w->bdev.flags = 0;
        w->bdev.submit = usb_block_submit;
        w->bdev.driver_data = w;
        if (!w->registered) {
            if (block_register(&w->bdev) != 0) return;
            w->registered = 1;
        }
        w->active = 1;
        w->controller = controller;
        w->slot = slot;
        w->blocks = blocks;
        serial_write("xHCI: storage block usb");
        serial_write_dec(i);
        serial_write(" blocks=");
        serial_write_dec(blocks);
        serial_write("\r\n");
        return;
    }
    serial_write("xHCI: storage block registry full\r\n");
}
