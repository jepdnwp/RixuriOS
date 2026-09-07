#include "kernel/net/e1000.h"
#include <assert.h>

size_t pci_device_count(void) { return 0; }
const rix_pci_device_t *pci_device(size_t index) { (void)index; return 0; }
int pci_bar_size(const rix_pci_device_t *device, unsigned bar, uint64_t *size,
                uint64_t *base, int *is_io) {
    (void)device; (void)bar; (void)size; (void)base; (void)is_io; return -1;
}
int vmm_map_page(uint64_t virtual_address, uint64_t physical_address, uint64_t flags) {
    (void)virtual_address; (void)physical_address; (void)flags; return 0;
}
void serial_write(const char *text) { (void)text; }
void serial_write_dec(uint64_t value) { (void)value; }
void serial_write_hex(uint64_t value) { (void)value; }
uint64_t pmm_alloc_page(void) { return 0; }
void pmm_free_page(uint64_t physical_address) { (void)physical_address; }

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
