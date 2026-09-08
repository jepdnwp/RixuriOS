#pragma once
#include "net.h"
#include "../pci/pci.h"
#include <stddef.h>
#include <stdint.h>

#define RIX_E1000_VENDOR_ID 0x8086u
#define RIX_E1000_DEVICE_82540EM 0x100eu
#define RIX_E1000_DEVICE_82545EM 0x100fu
#define RIX_E1000_DEVICE_82574L 0x10d3u
#define RIX_E1000_REG_CTRL 0x0000u
#define RIX_E1000_REG_STATUS 0x0008u
#define RIX_E1000_REG_EERD 0x0014u
#define RIX_E1000_REG_RCTL 0x0100u
#define RIX_E1000_REG_TCTL 0x0400u
#define RIX_E1000_REG_RDBAL 0x2800u
#define RIX_E1000_REG_RDLEN 0x2808u
#define RIX_E1000_REG_RDH 0x2810u
#define RIX_E1000_REG_RDT 0x2818u
#define RIX_E1000_REG_TDBAL 0x3800u
#define RIX_E1000_REG_TDLEN 0x3808u
#define RIX_E1000_REG_TDH 0x3810u
#define RIX_E1000_REG_TDT 0x3818u
#define RIX_E1000_REG_RAL 0x5400u
#define RIX_E1000_REG_RAH 0x5404u
#define RIX_E1000_RING_SIZE 64u

#define RIX_E1000_RX_STATUS_DD 0x01u
#define RIX_E1000_RX_STATUS_EOP 0x02u
#define RIX_E1000_TX_STATUS_DD 0x01u
#define RIX_E1000_TX_CMD_EOP 0x01u
#define RIX_E1000_TX_CMD_IFCS 0x02u
#define RIX_E1000_TX_CMD_RS 0x08u

/* Intel legacy 8254x descriptor: exactly 16 bytes on the DMA ring. */
typedef struct {
    uint64_t address;
    uint16_t length;
    uint8_t checksum_offset;
    uint8_t command;
    uint8_t status;
    uint8_t checksum_start;
    uint16_t special;
} rix_e1000_descriptor_t;

_Static_assert(sizeof(rix_e1000_descriptor_t) == 16, "E1000 descriptor ABI");

typedef struct {
    const rix_pci_device_t *pci;
    uint64_t mmio_base;
    uint64_t mmio_size;
    uint8_t mac[6];
    uint8_t present;
    uint8_t link_up;
    uint16_t rx_head;
    uint16_t rx_tail;
    uint16_t tx_head;
    uint16_t tx_tail;
    uint64_t rx_ring_phys;
    uint64_t tx_ring_phys;
    uint64_t rx_buffers[RIX_E1000_RING_SIZE];
    uint64_t tx_buffers[RIX_E1000_RING_SIZE];
    rix_e1000_descriptor_t rx_ring[RIX_E1000_RING_SIZE];
    rix_e1000_descriptor_t tx_ring[RIX_E1000_RING_SIZE];
} rix_e1000_t;

int rix_e1000_is_supported(const rix_pci_device_t *device);
int rix_e1000_validate_mmio(const rix_e1000_t *driver, uint32_t offset, uint32_t width);
int rix_e1000_init_rings(rix_e1000_t *driver);
int rix_e1000_init(void);
int rix_e1000_configure(rix_e1000_t *driver);
int rix_e1000_transmit(rix_e1000_t *driver, const void *data, size_t length);
int rix_e1000_poll_tx(rix_e1000_t *driver);
int rix_e1000_receive(rix_e1000_t *driver, void *data, size_t capacity, size_t *length);
const rix_e1000_t *rix_e1000_default(void);
int rix_e1000_link_up(const rix_e1000_t *driver);
