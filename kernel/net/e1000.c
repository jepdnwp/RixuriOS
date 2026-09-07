#include "e1000.h"
#include "../mm/vmm.h"
#include "../mm/pmm.h"
#include "../serial.h"

static rix_e1000_t controller;

static volatile uint32_t *map_regs(uint64_t base, uint64_t size) {
    if (!base || size < 0x4000) return 0;
    for (uint64_t offset = 0; offset < 0x4000; offset += 0x1000)
        if (vmm_map_page((base & ~0xfffULL) + offset, (base & ~0xfffULL) + offset,
                         RIXURI_PTE_PRESENT | RIXURI_PTE_WRITE | RIXURI_PTE_NX) != 0) return 0;
    return (volatile uint32_t *)(uintptr_t)(base & ~0xfffULL);
}

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

int rix_e1000_init(void) {
    for (size_t i = 0; i < pci_device_count(); ++i) {
        const rix_pci_device_t *device = pci_device(i);
        if (!rix_e1000_is_supported(device)) continue;
        uint64_t size = 0, base = 0; int is_io = 0;
        if (pci_bar_size(device, 0, &size, &base, &is_io) != 0 || is_io || !base) {
            serial_write("E1000: candidate rejected: invalid MMIO BAR\r\n");
            continue;
        }
        volatile uint32_t *regs = map_regs(base, size);
        if (!regs) { serial_write("E1000: candidate rejected: MMIO map failed\r\n"); continue; }
        for (size_t n = 0; n < sizeof(controller); ++n) ((uint8_t *)&controller)[n] = 0;
        controller.pci = device; controller.mmio_base = base; controller.mmio_size = size;
        controller.present = 1;
        if (rix_e1000_init_rings(&controller) != 0) return -1;
        if (rix_e1000_configure(&controller) != 0) {
            serial_write("E1000: ring configuration failed\r\n");
            controller.present = 0;
            continue;
        }
        serial_write("E1000: probe bus="); serial_write_dec(device->bus);
        serial_write(" dev="); serial_write_dec(device->device);
        serial_write(" bar="); serial_write_hex(base);
        serial_write(" size="); serial_write_dec(size);
        serial_write(" status="); serial_write_hex(regs[RIX_E1000_REG_STATUS / 4]);
        serial_write("\r\n");
        return 0;
    }
    serial_write("E1000: no supported controller\r\n");
    return -1;
}

int rix_e1000_configure(rix_e1000_t *driver) {
    if (!driver || !driver->present || !driver->mmio_base) return -1;
    uint64_t rx = pmm_alloc_page();
    uint64_t tx = pmm_alloc_page();
    if (!rx || !tx) {
        if (rx) pmm_free_page(rx);
        if (tx) pmm_free_page(tx);
        return -2;
    }
    driver->rx_ring_phys = rx;
    driver->tx_ring_phys = tx;
    for (size_t i = 0; i < 4096; ++i) {
        ((uint8_t *)(uintptr_t)rx)[i] = 0;
        ((uint8_t *)(uintptr_t)tx)[i] = 0;
    }
    rix_e1000_descriptor_t *rx_descriptors = (rix_e1000_descriptor_t *)(uintptr_t)rx;
    rix_e1000_descriptor_t *tx_descriptors = (rix_e1000_descriptor_t *)(uintptr_t)tx;
    for (size_t i = 0; i < RIX_E1000_RING_SIZE; ++i) {
        driver->rx_buffers[i] = pmm_alloc_page();
        driver->tx_buffers[i] = pmm_alloc_page();
        if (!driver->rx_buffers[i] || !driver->tx_buffers[i]) return -3;
        rx_descriptors[i].address = driver->rx_buffers[i];
        rx_descriptors[i].length = 0;
        rx_descriptors[i].status = 0;
        tx_descriptors[i].address = driver->tx_buffers[i];
        tx_descriptors[i].length = 0;
        tx_descriptors[i].status = 1;
        driver->rx_ring[i] = rx_descriptors[i];
        driver->tx_ring[i] = tx_descriptors[i];
    }
    volatile uint32_t *regs = (volatile uint32_t *)(uintptr_t)driver->mmio_base;
    regs[RIX_E1000_REG_RDBAL / 4] = (uint32_t)rx;
    regs[(RIX_E1000_REG_RDBAL + 4) / 4] = (uint32_t)(rx >> 32);
    regs[RIX_E1000_REG_RDH / 4] = 0;
    regs[RIX_E1000_REG_RDT / 4] = RIX_E1000_RING_SIZE - 1;
    regs[RIX_E1000_REG_TDBAL / 4] = (uint32_t)tx;
    regs[(RIX_E1000_REG_TDBAL + 4) / 4] = (uint32_t)(tx >> 32);
    regs[RIX_E1000_REG_TDH / 4] = 0;
    regs[RIX_E1000_REG_TDT / 4] = 0;
    regs[RIX_E1000_REG_RCTL / 4] = 0x00008002u;
    regs[RIX_E1000_REG_TCTL / 4] = 0x00000002u;
    return 0;
}

int rix_e1000_transmit(rix_e1000_t *driver, const void *data, size_t length) {
    if (!driver || !driver->present || !data || !length || length > RIX_NET_FRAME_CAPACITY)
        return -1;
    uint16_t slot = driver->tx_tail;
    if (driver->tx_ring[slot].status == 0) return -2;
    uint8_t *buffer = (uint8_t *)(uintptr_t)driver->tx_buffers[slot];
    for (size_t i = 0; i < length; ++i) buffer[i] = ((const uint8_t *)data)[i];
    driver->tx_ring[slot].length = (uint16_t)length;
    driver->tx_ring[slot].status = 0;
    volatile uint32_t *regs = (volatile uint32_t *)(uintptr_t)driver->mmio_base;
    driver->tx_tail = (uint16_t)((slot + 1u) % RIX_E1000_RING_SIZE);
    regs[RIX_E1000_REG_TDT / 4] = driver->tx_tail;
    return (int)length;
}

int rix_e1000_receive(rix_e1000_t *driver, void *data, size_t capacity, size_t *length) {
    if (!driver || !driver->present || !data || !length || !capacity) return -1;
    uint16_t slot = driver->rx_head;
    if (!(driver->rx_ring[slot].status & 1u)) return 0;
    size_t received = driver->rx_ring[slot].length;
    if (received > capacity) return -2;
    uint8_t *buffer = (uint8_t *)(uintptr_t)driver->rx_buffers[slot];
    for (size_t i = 0; i < received; ++i) ((uint8_t *)data)[i] = buffer[i];
    driver->rx_ring[slot].status = 0;
    driver->rx_ring[slot].length = 0;
    driver->rx_head = (uint16_t)((slot + 1u) % RIX_E1000_RING_SIZE);
    volatile uint32_t *regs = (volatile uint32_t *)(uintptr_t)driver->mmio_base;
    regs[RIX_E1000_REG_RDT / 4] = slot;
    *length = received;
    return 1;
}
