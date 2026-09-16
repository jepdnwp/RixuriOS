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

static usb_storage_dev_t storages[USB_STORAGE_MAX_DEVS];
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
 * CAPACITY(8) and single-sector READ/WRITE. larger sizes fail closed. */
int usb_storage_command(size_t controller, uint8_t slot,
                        uint8_t interface_number, uint8_t bulk_in,
                        uint8_t bulk_out, uint32_t *tag, const uint8_t *cdb,
                        uint8_t cdb_len, void *data, uint32_t data_len,
                        int data_in) {
    uint8_t cbw[BOT_CBW_SIZE];
    uint8_t csw[BOT_CSW_SIZE];
    uint32_t my_tag;
    int rc;
    unsigned attempt;
    if (!tag || !cdb || cdb_len == 0u || cdb_len > 16u ||
        interface_number >= 32u || bulk_in == 0u || bulk_out == 0u)
        return -1;
    if (data_len > USB_STORAGE_SECTOR) return -1;
    if (data_len != 0u && !data) return -1;
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
                rc = bulk_exact(controller, slot, bulk_in,
                                (uint8_t *)data + done, chunk, 1);
            else
                rc = bulk_exact(controller, slot, bulk_out,
                                (uint8_t *)data + done, chunk, 0);
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

const usb_storage_dev_t *usb_storage_find(size_t controller, uint8_t slot) {
    for (unsigned i = 0; i < USB_STORAGE_MAX_DEVS; ++i) {
        if (storages[i].used && storages[i].controller == controller &&
            storages[i].slot == slot)
            return &storages[i];
    }
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
    if (out_dev) *out_dev = dev;
    return 0;
}

static usb_storage_dev_t *storage_mut(size_t controller, uint8_t slot) {
    for (unsigned i = 0; i < USB_STORAGE_MAX_DEVS; ++i) {
        if (storages[i].used && storages[i].controller == controller &&
            storages[i].slot == slot)
            return &storages[i];
    }
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
    static uint8_t stage[USB_STORAGE_SECTOR];
    uint8_t cdb[16];
    uint32_t i;
    if (!dev || !buffer) return -1;
    if (lba >= dev->blocks) return -2;
    /* Stage through .bss so the DMA linearity check always holds, even
     * for caller buffers that cross a page boundary. */
    for (i = 0; i < USB_STORAGE_SECTOR; ++i)
        stage[i] = ((const uint8_t *)buffer)[i];
    usb_storage_build_write10(cdb, lba, 1u);
    return usb_storage_command(controller, slot, dev->interface_number,
                               dev->bulk_in, dev->bulk_out, &dev->tag, cdb,
                               10, stage, USB_STORAGE_SECTOR, 0);
}
