#include "e1000.h"

int rix_e1000_is_supported(const rix_pci_device_t *device) {
    if (!device || device->vendor_id != RIX_E1000_VENDOR_ID) return 0;
    return device->device_id == RIX_E1000_DEVICE_82540EM ||
           device->device_id == RIX_E1000_DEVICE_82545EM ||
           device->device_id == RIX_E1000_DEVICE_82574L;
}

int rix_e1000_validate_mmio(const rix_e1000_t *driver, uint32_t offset, uint32_t width) {
    if (!driver || !driver->present || !width || offset > driver->mmio_size ||
        width > driver->mmio_size - offset) return -1;
    return 0;
}

int rix_e1000_init_rings(rix_e1000_t *driver) {
    if (!driver || !driver->present) return -1;
    for (size_t i = 0; i < RIX_E1000_RING_SIZE; ++i) {
        driver->rx_ring[i].address = 0;
        driver->rx_ring[i].length = 0;
        driver->rx_ring[i].status = 0;
        driver->rx_ring[i].errors = 0;
        driver->tx_ring[i].address = 0;
        driver->tx_ring[i].length = 0;
        driver->tx_ring[i].status = 0;
        driver->tx_ring[i].errors = 0;
    }
    driver->rx_head = driver->rx_tail = driver->tx_head = driver->tx_tail = 0;
    return 0;
}
