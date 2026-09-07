#include "rtl8125.h"

int rix_rtl8125_is_supported(const rix_pci_device_t *device) {
    if (!device || device->vendor_id != RIX_RTL8125_VENDOR_ID) return 0;
    return device->device_id == RIX_RTL8125_DEVICE_ID || device->device_id == RIX_RTL8126_DEVICE_ID;
}

int rix_rtl8125_probe(rix_rtl8125_t *driver) {
    if (!driver) return -1;
    driver->present = 0;
    driver->link_up = 0;
    driver->pci = 0;
    driver->mmio_base = 0;
    driver->mmio_size = 0;
    driver->tx_head = driver->tx_tail = driver->rx_head = 0;
    for (size_t i = 0; i < pci_device_count(); ++i) {
        const rix_pci_device_t *device = pci_device(i);
        if (!rix_rtl8125_is_supported(device)) continue;
        uint64_t size = 0, base = 0;
        int is_io = 0;
        if (pci_bar_size(device, 2, &size, &base, &is_io) != 0 || is_io || !base || size < 0x100u) return -2;
        driver->pci = device;
        driver->mmio_base = base;
        driver->mmio_size = size;
        driver->present = 1;
        return 0;
    }
    return -3;
}

int rix_rtl8125_ring_indices_valid(const rix_rtl8125_t *driver) {
    if (!driver || !driver->present) return 0;
    return driver->tx_head < RIX_RTL8125_TX_RING_SIZE &&
           driver->tx_tail < RIX_RTL8125_TX_RING_SIZE &&
           driver->rx_head < RIX_RTL8125_RX_RING_SIZE;
}

int rix_rtl8125_validate_mmio(const rix_rtl8125_t *driver, uint32_t offset,
                              uint32_t width) {
    if (!driver || !driver->present || !width || offset > driver->mmio_size ||
        width > driver->mmio_size - offset) return -1;
    return 0;
}

int rix_rtl8125_prepare_descriptor(rix_rtl8125_descriptor_t *descriptor,
                                   uint64_t buffer_address, uint32_t length,
                                   uint32_t flags) {
    if (!descriptor || !buffer_address || !length || length > RIX_NET_MTU) return -1;
    descriptor->buffer_address = buffer_address;
    descriptor->length = length;
    descriptor->flags = flags & (RIX_RTL8125_DESC_OWN | RIX_RTL8125_DESC_EOR);
    return 0;
}

int rix_rtl8125_init_rings(rix_rtl8125_t *driver) {
    if (!driver || !driver->present) return -1;
    for (size_t i = 0; i < RIX_RTL8125_TX_RING_SIZE; ++i) {
        driver->tx_ring[i].buffer_address = 0;
        driver->tx_ring[i].length = 0;
        driver->tx_ring[i].flags = i + 1 == RIX_RTL8125_TX_RING_SIZE ? RIX_RTL8125_DESC_EOR : 0;
    }
    for (size_t i = 0; i < RIX_RTL8125_RX_RING_SIZE; ++i) {
        driver->rx_ring[i].buffer_address = 0;
        driver->rx_ring[i].length = 0;
        driver->rx_ring[i].flags = i + 1 == RIX_RTL8125_RX_RING_SIZE ? RIX_RTL8125_DESC_EOR : 0;
    }
    driver->tx_head = driver->tx_tail = driver->rx_head = 0;
    return 0;
}

static uint32_t mmio_read32(volatile uint8_t *mmio, uint32_t offset) {
    volatile uint32_t *value = (volatile uint32_t *)(mmio + offset);
    return *value;
}

static void mmio_write32(volatile uint8_t *mmio, uint32_t offset, uint32_t value) {
    volatile uint32_t *destination = (volatile uint32_t *)(mmio + offset);
    *destination = value;
}

int rix_rtl8125_hw_reset(volatile uint8_t *mmio, size_t mmio_size) {
    if (!mmio || mmio_size < RIX_RTL8125_REG_COMMAND + 4u) return -1;
    uint32_t command = mmio_read32(mmio, RIX_RTL8125_REG_COMMAND);
    mmio_write32(mmio, RIX_RTL8125_REG_COMMAND, command | RIX_RTL8125_CMD_RESET);
    for (unsigned attempt = 0; attempt < 100000u; ++attempt)
        if (!(mmio_read32(mmio, RIX_RTL8125_REG_COMMAND) & RIX_RTL8125_CMD_RESET)) return 0;
    return -2;
}

int rix_rtl8125_hw_enable(volatile uint8_t *mmio, size_t mmio_size,
                           uint32_t interrupt_mask) {
    if (!mmio || mmio_size < RIX_RTL8125_REG_IMR + 4u) return -1;
    mmio_write32(mmio, RIX_RTL8125_REG_IMR, interrupt_mask);
    mmio_write32(mmio, RIX_RTL8125_REG_COMMAND,
                 RIX_RTL8125_CMD_RX_ENABLE | RIX_RTL8125_CMD_TX_ENABLE);
    return 0;
}

int rix_rtl8125_read_mac(volatile uint8_t *mmio, size_t mmio_size, uint8_t mac[6]) {
    if (!mmio || !mac || mmio_size < RIX_RTL8125_REG_MAC0 + 6u) return -1;
    uint8_t zero = 1, broadcast = 1;
    for (size_t i = 0; i < 6; ++i) {
        mac[i] = mmio[RIX_RTL8125_REG_MAC0 + i];
        if (mac[i] != 0) zero = 0;
        if (mac[i] != 0xff) broadcast = 0;
    }
    if (zero || broadcast || (mac[0] & 1u)) return -2;
    return 0;
}

int rix_rtl8125_program_rings(rix_rtl8125_t *driver, volatile uint8_t *mmio,
                              size_t mmio_size) {
    if (!driver || !driver->present || !mmio ||
        mmio_size < RIX_RTL8125_REG_RX_DESC_LOW + 4u ||
        !driver->tx_dma.count || !driver->rx_dma.count) return -1;
    uint64_t tx = driver->tx_dma.pages[0];
    uint64_t rx = driver->rx_dma.pages[0];
    mmio_write32(mmio, RIX_RTL8125_REG_TX_DESC_LOW, (uint32_t)tx);
    mmio_write32(mmio, RIX_RTL8125_REG_TX_DESC_HIGH, (uint32_t)(tx >> 32));
    mmio_write32(mmio, RIX_RTL8125_REG_RX_DESC_LOW, (uint32_t)rx);
    mmio_write32(mmio, RIX_RTL8125_REG_RX_DESC_HIGH, (uint32_t)(rx >> 32));
    return 0;
}

int rix_rtl8125_read_link(volatile uint8_t *mmio, size_t mmio_size, int *link_up) {
    if (!mmio || !link_up || mmio_size < RIX_RTL8125_REG_PHY_STATUS + 4u) return -1;
    *link_up = (mmio_read32(mmio, RIX_RTL8125_REG_PHY_STATUS) & RIX_RTL8125_PHY_LINK_UP) != 0;
    return 0;
}

int rix_rtl8125_ack_interrupts(volatile uint8_t *mmio, size_t mmio_size,
                               uint32_t *pending) {
    if (!mmio || !pending || mmio_size < RIX_RTL8125_REG_ISR + 4u) return -1;
    uint32_t status = mmio_read32(mmio, RIX_RTL8125_REG_ISR);
    *pending = status;
    if (status) mmio_write32(mmio, RIX_RTL8125_REG_ISR, status);
    return 0;
}
