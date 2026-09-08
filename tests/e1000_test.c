#include "kernel/net/e1000.h"
#include <assert.h>
#include <stdint.h>

static uint8_t dma_pages[130][4096] __attribute__((aligned(4096)));
static size_t dma_page_index;

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
uint64_t pmm_alloc_page_below(uint64_t max_physical_exclusive) {
    (void)max_physical_exclusive;
    if (dma_page_index >= 130) return 0;
    return (uint64_t)(uintptr_t)dma_pages[dma_page_index++];
}
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
    driver.mmio_size = 0x6000;
    assert(rix_e1000_validate_mmio(&driver, RIX_E1000_REG_RCTL, 4) == 0);
    assert(rix_e1000_validate_mmio(&driver, 0x5fff, 2) != 0);
    assert(rix_e1000_init_rings(&driver) == 0);
    assert(driver.rx_head == 0 && driver.tx_tail == 0);
    assert(sizeof(rix_e1000_descriptor_t) == 16);

    volatile uint32_t mmio[0x6000 / 4] = {0};
    driver.mmio_base = (uint64_t)(uintptr_t)mmio;
    mmio[RIX_E1000_REG_STATUS / 4] = 2u;
    dma_page_index = 0;
    assert(rix_e1000_configure(&driver) == 0);
    assert(rix_e1000_link_up(&driver));
    assert(mmio[RIX_E1000_REG_RDLEN / 4] == RIX_E1000_RING_SIZE * 16u);
    assert(mmio[RIX_E1000_REG_TDLEN / 4] == RIX_E1000_RING_SIZE * 16u);

    const uint8_t frame[] = {0xde, 0xad, 0xbe, 0xef};
    assert(rix_e1000_transmit(&driver, frame, sizeof(frame)) == (int)sizeof(frame));
    volatile rix_e1000_descriptor_t *tx =
        (volatile rix_e1000_descriptor_t *)(uintptr_t)driver.tx_ring_phys;
    assert(tx[0].length == sizeof(frame));
    assert((tx[0].command & (RIX_E1000_TX_CMD_EOP | RIX_E1000_TX_CMD_RS)) ==
           (RIX_E1000_TX_CMD_EOP | RIX_E1000_TX_CMD_RS));
    tx[0].status = RIX_E1000_TX_STATUS_DD;
    mmio[RIX_E1000_REG_TDH / 4] = 1;
    assert(rix_e1000_poll_tx(&driver) == 1);
    assert(driver.tx_head == 1);

    volatile rix_e1000_descriptor_t *rx =
        (volatile rix_e1000_descriptor_t *)(uintptr_t)driver.rx_ring_phys;
    uint8_t *rx_buffer = (uint8_t *)(uintptr_t)driver.rx_buffers[0];
    rx_buffer[0] = 0x11; rx_buffer[1] = 0x22; rx_buffer[2] = 0x33;
    rx[0].length = 3;
    rx[0].status = RIX_E1000_RX_STATUS_DD | RIX_E1000_RX_STATUS_EOP;
    uint8_t received[8] = {0};
    size_t received_length = 0;
    assert(rix_e1000_receive(&driver, received, sizeof(received), &received_length) == 1);
    assert(received_length == 3 && received[0] == 0x11 && received[2] == 0x33);
    assert(driver.rx_head == 1 && mmio[RIX_E1000_REG_RDT / 4] == 0);
    assert(rix_e1000_receive(&driver, received, sizeof(received), &received_length) == 0);
    return 0;
}
