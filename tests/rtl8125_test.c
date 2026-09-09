#include "kernel/net/rtl8125.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

size_t pci_device_count(void) { return 0; }
const rix_pci_device_t *pci_device(size_t index) { (void)index; return 0; }
int pci_bar_size(const rix_pci_device_t *device, unsigned bar, uint64_t *size,
                uint64_t *base, int *is_io) {
    (void)device; (void)bar; (void)size; (void)base; (void)is_io;
    return -1;
}
uint32_t pci_config_read32(uint8_t bus, uint8_t device, uint8_t function,
                           uint16_t offset) {
    (void)bus; (void)device; (void)function; (void)offset;
    return 0xffffffffu;
}
int pci_config_write32(uint8_t bus, uint8_t device, uint8_t function,
                       uint16_t offset, uint32_t value) {
    (void)bus; (void)device; (void)function; (void)offset; (void)value;
    return -1;
}
void serial_write(const char *s) { (void)s; }
void serial_write_dec(uint64_t v) { (void)v; }
void serial_write_hex(uint64_t v) { (void)v; }
uint64_t pmm_alloc_page_below(uint64_t max_physical_exclusive) {
    (void)max_physical_exclusive;
    return 0;
}
void pmm_free_page(uint64_t physical_address) { (void)physical_address; }
int vmm_map_page(uint64_t virtual_address, uint64_t physical_address,
                 uint64_t flags) {
    (void)virtual_address; (void)physical_address; (void)flags;
    return -1;
}

int main(void) {
    rix_pci_device_t device = {0};
    device.vendor_id = RIX_RTL8125_VENDOR_ID;
    device.device_id = RIX_RTL8125_DEVICE_ID;
    assert(rix_rtl8125_is_supported(&device));
    device.device_id = 0x1234;
    assert(!rix_rtl8125_is_supported(&device));
    rix_rtl8125_descriptor_t descriptor;
    assert(rix_rtl8125_prepare_descriptor(&descriptor, 0x1000, 128,
                                           RIX_RTL8125_DESC_OWN | 0x80) == 0);
    assert(descriptor.flags == (RIX_RTL8125_DESC_OWN | RIX_RTL8125_DESC_FS |
                                RIX_RTL8125_DESC_LS | 128u));
    assert(rix_rtl8125_prepare_descriptor(&descriptor, 0, 128, 0) != 0);
    rix_rtl8125_t driver = {0};
    assert(rix_rtl8125_validate_mmio(&driver, 0, 1) != 0);
    driver.present = 1;
    driver.mmio_size = 0x1000;
    assert(rix_rtl8125_init_rings(&driver) == 0);
    assert(driver.tx_ring[RIX_RTL8125_TX_RING_SIZE - 1].flags == RIX_RTL8125_DESC_EOR);
    assert(driver.rx_ring[RIX_RTL8125_RX_RING_SIZE - 1].flags == RIX_RTL8125_DESC_EOR);
    for (size_t i = 0; i < RIX_RTL8125_TX_RING_SIZE; ++i)
        assert((driver.tx_ring[i].flags & RIX_RTL8125_DESC_OWN) == 0);
    assert(rix_rtl8125_ring_indices_valid(&driver));
    assert(rix_rtl8125_validate_mmio(&driver, 0x3c, 4) == 0);
    assert(rix_rtl8125_validate_mmio(&driver, 0xfff, 2) != 0);
    uint8_t mmio[0x100] = {0};
    assert(rix_rtl8125_hw_enable(mmio, sizeof(mmio), 0x55aa) == 0);
    assert(*(uint32_t *)(mmio + RIX_RTL8125_REG_IMR) == 0x55aa);
    assert(mmio[RIX_RTL8125_REG_COMMAND] ==
           (RIX_RTL8125_CMD_RX_ENABLE | RIX_RTL8125_CMD_TX_ENABLE));
    assert(rix_rtl8125_hw_reset(mmio, RIX_RTL8125_REG_COMMAND) != 0);
    mmio[0] = 0x02; mmio[1] = 0x11; mmio[2] = 0x22;
    mmio[3] = 0x33; mmio[4] = 0x44; mmio[5] = 0x55;
    uint8_t mac[6];
    assert(rix_rtl8125_read_mac(mmio, sizeof(mmio), mac) == 0);
    assert(mac[0] == 0x02 && mac[5] == 0x55);
    driver.tx_ring_phys = 0x1234567887654000ULL;
    driver.rx_ring_phys = 0x0fedcba987654000ULL;
    assert(rix_rtl8125_program_rings(&driver, mmio, sizeof(mmio)) == 0);
    assert(*(uint32_t *)(mmio + RIX_RTL8125_REG_TX_DESC_LOW) == 0x87654000u);
    assert(*(uint32_t *)(mmio + RIX_RTL8125_REG_TX_DESC_HIGH) == 0x12345678u);
    assert(*(uint32_t *)(mmio + RIX_RTL8125_REG_RX_DESC_LOW) == 0x87654000u);
    assert(*(uint32_t *)(mmio + RIX_RTL8125_REG_RX_DESC_HIGH) == 0x0fedcba9u);
    driver.tx_ring_phys = 0;
    assert(rix_rtl8125_program_rings(&driver, mmio, sizeof(mmio)) != 0);
    assert(rix_rtl8125_program_rx_filter(mmio, sizeof(mmio)) == 0);
    assert(*(uint32_t *)(mmio + RIX_RTL8125_REG_RCR) ==
           (RIX_RTL8125_RCR_APM | RIX_RTL8125_RCR_AM | RIX_RTL8125_RCR_AB));
    assert(rix_rtl8125_program_rx_filter(mmio, RIX_RTL8125_REG_RCR) != 0);
    int link = 1;
    *(uint32_t *)(mmio + RIX_RTL8125_REG_PHY_STATUS) = RIX_RTL8125_PHY_LINK_UP;
    assert(rix_rtl8125_read_link(mmio, sizeof(mmio), &link) == 0 && link == 1);
    *(uint32_t *)(mmio + RIX_RTL8125_REG_PHY_STATUS) = 0;
    assert(rix_rtl8125_read_link(mmio, sizeof(mmio), &link) == 0 && link == 0);
    uint32_t pending = 0;
    *(uint32_t *)(mmio + RIX_RTL8125_REG_ISR) = 0xa5u;
    assert(rix_rtl8125_ack_interrupts(mmio, sizeof(mmio), &pending) == 0);
    assert(pending == 0xa5u);
    {
        /* Transmit programs the descriptor, hands it off (OWN) and rings
         * the TPPOLL doorbell; receive consumes a NIC-completed descriptor
         * and re-arms it. Backed by real heap pages, no hardware. */
        static uint8_t txmmio[0x100] = {0};
        rix_rtl8125_descriptor_t *txring;
        rix_rtl8125_descriptor_t *rxring;
        uint8_t *txbuf, *rxbuf;
        uint8_t payload[64];
        uint8_t sink[128];
        size_t got = 0;
        for (size_t i = 0; i < sizeof(payload); ++i) payload[i] = (uint8_t)(0xa0 + i);
        txring = calloc(64, sizeof(*txring));
        rxring = calloc(64, sizeof(*rxring));
        txbuf = calloc(2048, 1);
        rxbuf = calloc(2048, 1);
        assert(txring && rxring && txbuf && rxbuf);
        driver.tx_ring_phys = (uint64_t)(uintptr_t)txring;
        driver.rx_ring_phys = (uint64_t)(uintptr_t)rxring;
        driver.tx_buffers[0] = (uint64_t)(uintptr_t)txbuf;
        driver.rx_buffers[0] = (uint64_t)(uintptr_t)rxbuf;
        driver.mmio = txmmio;
        driver.tx_head = driver.tx_tail = driver.rx_head = 0;
        assert(rix_rtl8125_transmit(&driver, payload, sizeof(payload)) ==
               (int)sizeof(payload));
        assert(txring[0].length == 0);
        assert((txring[0].flags & (RIX_RTL8125_DESC_OWN | RIX_RTL8125_DESC_FS |
                                   RIX_RTL8125_DESC_LS | RIX_RTL8125_DESC_LEN_MASK)) ==
               (RIX_RTL8125_DESC_OWN | RIX_RTL8125_DESC_FS |
                RIX_RTL8125_DESC_LS | sizeof(payload)));
        assert(memcmp(txbuf, payload, sizeof(payload)) == 0);
        assert(txmmio[RIX_RTL8125_REG_TPPOLL] == RIX_RTL8125_TPPOLL_NPQ);
        memcpy(rxbuf, payload, sizeof(payload));
        rxring[0].length = 0;
        rxring[0].flags = (uint32_t)(sizeof(payload) + 4u);
        assert(rix_rtl8125_receive(&driver, sink, sizeof(sink), &got) == 1);
        assert(got == sizeof(payload));
        assert(memcmp(sink, payload, sizeof(payload)) == 0);
        assert(rxring[0].length == 0);
        assert((rxring[0].flags & (RIX_RTL8125_DESC_OWN | RIX_RTL8125_DESC_LEN_MASK)) ==
               (RIX_RTL8125_DESC_OWN | RIX_RTL8125_RX_BUFFER_SIZE));
        assert(driver.rx_head == 1);
        free(txring);
        free(rxring);
        free(txbuf);
        free(rxbuf);
    }
    return 0;
}
