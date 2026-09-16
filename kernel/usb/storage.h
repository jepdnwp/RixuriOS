#pragma once
#include <stddef.h>
#include <stdint.h>

/* USB Mass Storage Bulk-Only Transport (BOT) + SCSI transparent command
 * set (read/write path). Values from USB BOT 1.0 (CBW/CSW, GET_MAX_LUN,
 * BOT reset) and SPC/SBC (INQUIRY, READ CAPACITY(10), READ(10),
 * WRITE(10), TEST UNIT READY). Single-user desktop subset: one LUN per
 * device is exercised (GET_MAX_LUN stall means LUN 0), 512-byte blocks
 * only, no ATA pass-through, no UAS. */

#define USB_MASS_CLASS 8u
#define USB_MASS_SUBCLASS_SCSI 6u
#define USB_MASS_PROTO_BOT 80u

#define BOT_CBW_SIGNATURE 0x43425355u
#define BOT_CSW_SIGNATURE 0x53425355u
#define BOT_CBW_SIZE 31u
#define BOT_CSW_SIZE 13u
#define BOT_GET_MAX_LUN 0xFEu
#define BOT_RESET 0xFFu
#define BOT_CSW_GOOD 0u
#define BOT_CSW_FAILED 1u
#define BOT_CSW_PHASE_ERROR 2u

#define SCSI_TEST_UNIT_READY 0x00u
#define SCSI_INQUIRY 0x12u
#define SCSI_READ_CAPACITY10 0x25u
#define SCSI_READ10 0x28u
#define SCSI_WRITE10 0x2Au

#define USB_STORAGE_MAX_DEVS 4u
#define USB_STORAGE_SECTOR 512u

typedef struct {
    uint8_t used;
    size_t controller;
    uint8_t slot;
    uint8_t interface_number;
    uint8_t bulk_in;
    uint8_t bulk_out;
    uint32_t tag;
    uint8_t max_lun;
    uint32_t blocks;
} usb_storage_dev_t;

/* Pure builders/parsers (host-tested, no hardware). _be32 is for SCSI
 * CDB/data (big-endian); _le32 is for CBW/CSW headers (little-endian
 * per BOT — Linux cpu_to_le32). */
void usb_storage_put_be32(uint8_t *p, uint32_t v);
void usb_storage_put_be16(uint8_t *p, uint16_t v);
uint32_t usb_storage_be32(const uint8_t *p);
void usb_storage_put_le32(uint8_t *p, uint32_t v);
uint32_t usb_storage_le32(const uint8_t *p);
void usb_storage_build_cbw(uint8_t cbw[BOT_CBW_SIZE], uint32_t tag,
                            uint32_t xfer_len, int data_in, uint8_t lun,
                            uint8_t cdb_len, const uint8_t *cdb);
int usb_storage_parse_csw(const uint8_t *csw, size_t length, uint32_t tag);
void usb_storage_build_inquiry(uint8_t cdb[6], uint16_t alloc_len);
void usb_storage_build_read_capacity10(uint8_t cdb[10]);
void usb_storage_build_read10(uint8_t cdb[10], uint32_t lba, uint16_t count);
void usb_storage_build_write10(uint8_t cdb[10], uint32_t lba, uint16_t count);

/* Wire path (control + bulk transfers, bounded, fail-closed). */
int usb_storage_get_max_lun(size_t controller, uint8_t slot,
                            uint8_t interface_number, uint8_t *max_lun);
int usb_storage_reset_recovery(size_t controller, uint8_t slot,
                               uint8_t interface_number, uint8_t bulk_in,
                               uint8_t bulk_out);
int usb_storage_command(size_t controller, uint8_t slot,
                        uint8_t interface_number, uint8_t bulk_in,
                        uint8_t bulk_out, uint32_t *tag, const uint8_t *cdb,
                        uint8_t cdb_len, void *data, uint32_t data_len,
                        int data_in);
int usb_storage_probe(size_t controller, uint8_t slot,
                      uint8_t interface_number, uint8_t bulk_in,
                      uint8_t bulk_out, usb_storage_dev_t **out_dev);
int usb_storage_read(size_t controller, uint8_t slot, uint32_t lba,
                     void *buffer);
int usb_storage_write(size_t controller, uint8_t slot, uint32_t lba,
                      const void *buffer);
const usb_storage_dev_t *usb_storage_find(size_t controller, uint8_t slot);
