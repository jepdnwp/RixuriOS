#include "kernel/net/rtl8125.h"
#include <assert.h>

size_t pci_device_count(void) { return 0; }
const rix_pci_device_t *pci_device(size_t index) { (void)index; return 0; }
int pci_bar_size(const rix_pci_device_t *device, unsigned bar, uint64_t *size,
                uint64_t *base, int *is_io) {
    (void)device; (void)bar; (void)size; (void)base; (void)is_io;
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
    assert(descriptor.flags == RIX_RTL8125_DESC_OWN);
    assert(rix_rtl8125_prepare_descriptor(&descriptor, 0, 128, 0) != 0);
    rix_rtl8125_t driver = {0};
    assert(rix_rtl8125_validate_mmio(&driver, 0, 1) != 0);
    driver.present = 1;
    driver.mmio_size = 0x1000;
    assert(rix_rtl8125_init_rings(&driver) == 0);
    assert(driver.tx_ring[RIX_RTL8125_TX_RING_SIZE - 1].flags == RIX_RTL8125_DESC_EOR);
    assert(driver.rx_ring[RIX_RTL8125_RX_RING_SIZE - 1].flags == RIX_RTL8125_DESC_EOR);
    assert(rix_rtl8125_ring_indices_valid(&driver));
    assert(rix_rtl8125_validate_mmio(&driver, 0x3c, 4) == 0);
    assert(rix_rtl8125_validate_mmio(&driver, 0xfff, 2) != 0);
    uint8_t mmio[0x100] = {0};
    assert(rix_rtl8125_hw_enable(mmio, sizeof(mmio), 0x55aa) == 0);
    assert(*(uint32_t *)(mmio + RIX_RTL8125_REG_IMR) == 0x55aa);
    assert(*(uint32_t *)(mmio + RIX_RTL8125_REG_COMMAND) ==
           (RIX_RTL8125_CMD_RX_ENABLE | RIX_RTL8125_CMD_TX_ENABLE));
    assert(rix_rtl8125_hw_reset(mmio, RIX_RTL8125_REG_COMMAND) != 0);
    mmio[0] = 0x02; mmio[1] = 0x11; mmio[2] = 0x22;
    mmio[3] = 0x33; mmio[4] = 0x44; mmio[5] = 0x55;
    uint8_t mac[6];
    assert(rix_rtl8125_read_mac(mmio, sizeof(mmio), mac) == 0);
    assert(mac[0] == 0x02 && mac[5] == 0x55);
    driver.tx_dma.count = 1; driver.tx_dma.pages[0] = 0x1234567887654321ULL;
    driver.rx_dma.count = 1; driver.rx_dma.pages[0] = 0x0fedcba987654321ULL;
    assert(rix_rtl8125_program_rings(&driver, mmio, sizeof(mmio)) == 0);
    assert(*(uint32_t *)(mmio + RIX_RTL8125_REG_TX_DESC_LOW) == 0x87654321u);
    assert(*(uint32_t *)(mmio + RIX_RTL8125_REG_RX_DESC_HIGH) == 0x0fedcba9u);
    int link = 1;
    *(uint32_t *)(mmio + RIX_RTL8125_REG_PHY_STATUS) = RIX_RTL8125_PHY_LINK_UP;
    assert(rix_rtl8125_read_link(mmio, sizeof(mmio), &link) == 0 && link == 1);
    *(uint32_t *)(mmio + RIX_RTL8125_REG_PHY_STATUS) = 0;
    assert(rix_rtl8125_read_link(mmio, sizeof(mmio), &link) == 0 && link == 0);
    uint32_t pending = 0;
    *(uint32_t *)(mmio + RIX_RTL8125_REG_ISR) = 0xa5u;
    assert(rix_rtl8125_ack_interrupts(mmio, sizeof(mmio), &pending) == 0);
    assert(pending == 0xa5u);
    return 0;
}
