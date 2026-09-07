#include "kernel/net/e1000.h"
#include <assert.h>

int main(void) {
    rix_pci_device_t device = {0};
    device.vendor_id = RIX_E1000_VENDOR_ID;
    device.device_id = RIX_E1000_DEVICE_82540EM;
    assert(rix_e1000_is_supported(&device));
    device.device_id = 0x1234;
    assert(!rix_e1000_is_supported(&device));
    rix_e1000_t driver = {0};
    assert(rix_e1000_init_rings(&driver) != 0);
    driver.present = 1;
    driver.mmio_size = 0x4000;
    assert(rix_e1000_validate_mmio(&driver, RIX_E1000_REG_RCTL, 4) == 0);
    assert(rix_e1000_validate_mmio(&driver, 0x3fff, 2) != 0);
    assert(rix_e1000_init_rings(&driver) == 0);
    assert(driver.rx_head == 0 && driver.tx_tail == 0);
    return 0;
}
