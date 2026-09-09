#pragma once
#include "net.h"
#include "../pci/pci.h"
#include "../pci/dma.h"
#include <stddef.h>
#include <stdint.h>

#define RIX_RTL8125_VENDOR_ID 0x10ecu
#define RIX_RTL8125_DEVICE_ID 0x8125u
#define RIX_RTL8126_DEVICE_ID 0x8126u
#define RIX_RTL8125_TX_RING_SIZE 64u
#define RIX_RTL8125_RX_RING_SIZE 64u
#define RIX_RTL8125_RX_BUFFER_SIZE 2048u
#define RIX_RTL8125_REG_MAC0 0x00u
#define RIX_RTL8125_REG_TX_DESC_LOW 0x20u
#define RIX_RTL8125_REG_TX_DESC_HIGH 0x24u
#define RIX_RTL8125_REG_RX_DESC_HIGH 0xe0u
#define RIX_RTL8125_REG_RX_DESC_LOW 0xe4u
#define RIX_RTL8125_REG_COMMAND 0x37u
#define RIX_RTL8125_REG_TPPOLL 0x38u
#define RIX_RTL8125_TPPOLL_NPQ 0x40u
#define RIX_RTL8125_REG_RCR 0x44u
#define RIX_RTL8125_RCR_APM 0x02u
#define RIX_RTL8125_RCR_AM 0x04u
#define RIX_RTL8125_RCR_AB 0x08u
#define RIX_RTL8125_RCR_ACCEPT (RIX_RTL8125_RCR_APM | RIX_RTL8125_RCR_AM | RIX_RTL8125_RCR_AB)
#define RIX_RTL8125_REG_IMR 0x3cu
#define RIX_RTL8125_REG_ISR 0x3eu
#define RIX_RTL8125_REG_PHY_STATUS 0x6cu
#define RIX_RTL8125_PHY_LINK_UP 0x02u
#define RIX_RTL8125_CMD_RESET 0x10u
#define RIX_RTL8125_CMD_RX_ENABLE 0x08u
#define RIX_RTL8125_CMD_TX_ENABLE 0x04u
#define RIX_RTL8125_DESC_OWN 0x80000000u
#define RIX_RTL8125_DESC_EOR 0x40000000u

typedef struct {
    uint64_t buffer_address;
    uint32_t length;
    uint32_t flags;
} rix_rtl8125_descriptor_t;

typedef struct {
    const rix_pci_device_t *pci;
    uint64_t mmio_base;
    uint64_t mmio_size;
    volatile uint8_t *mmio;
    uint8_t mac[6];
    rix_dma_buffer_t tx_dma;
    rix_dma_buffer_t rx_dma;
    uint64_t tx_ring_phys;
    uint64_t rx_ring_phys;
    uint64_t tx_buffers[RIX_RTL8125_TX_RING_SIZE];
    uint64_t rx_buffers[RIX_RTL8125_RX_RING_SIZE];
    rix_rtl8125_descriptor_t tx_ring[RIX_RTL8125_TX_RING_SIZE];
    rix_rtl8125_descriptor_t rx_ring[RIX_RTL8125_RX_RING_SIZE];
    uint16_t tx_head;
    uint16_t tx_tail;
    uint16_t rx_head;
    uint8_t present;
    int link_up;
} rix_rtl8125_t;

int rix_rtl8125_is_supported(const rix_pci_device_t *device);
int rix_rtl8125_probe(rix_rtl8125_t *driver);
int rix_rtl8125_ring_indices_valid(const rix_rtl8125_t *driver);
int rix_rtl8125_validate_mmio(const rix_rtl8125_t *driver, uint32_t offset,
                              uint32_t width);
int rix_rtl8125_prepare_descriptor(rix_rtl8125_descriptor_t *descriptor,
                                    uint64_t buffer_address, uint32_t length,
                                    uint32_t flags);
int rix_rtl8125_init_rings(rix_rtl8125_t *driver);
int rix_rtl8125_hw_reset(volatile uint8_t *mmio, size_t mmio_size);
int rix_rtl8125_hw_enable(volatile uint8_t *mmio, size_t mmio_size,
                           uint32_t interrupt_mask);
int rix_rtl8125_read_mac(volatile uint8_t *mmio, size_t mmio_size, uint8_t mac[6]);
int rix_rtl8125_program_rings(rix_rtl8125_t *driver, volatile uint8_t *mmio,
                               size_t mmio_size);
int rix_rtl8125_program_rx_filter(volatile uint8_t *mmio, size_t mmio_size);
int rix_rtl8125_read_link(volatile uint8_t *mmio, size_t mmio_size, int *link_up);
int rix_rtl8125_ack_interrupts(volatile uint8_t *mmio, size_t mmio_size,
                               uint32_t *pending);
int rix_rtl8125_init(void);
int rix_rtl8125_configure(rix_rtl8125_t *driver);
int rix_rtl8125_transmit(rix_rtl8125_t *driver, const void *data, size_t length);
int rix_rtl8125_poll_tx(rix_rtl8125_t *driver);
int rix_rtl8125_receive(rix_rtl8125_t *driver, void *data, size_t capacity, size_t *length);
rix_rtl8125_t *rix_rtl8125_default(void);
int rix_rtl8125_link_up(const rix_rtl8125_t *driver);
