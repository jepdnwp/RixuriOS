#include "e1000.h"
#include "../mm/vmm.h"
#include "../mm/pmm.h"
#include "../serial.h"

static rix_e1000_t controller;

static volatile uint32_t *map_regs(uint64_t base, uint64_t size) {
    if (!base || size < 0x6000u) return 0;
    uint64_t mapped = (size + 0xfffu) & ~0xfffULL;
    for (uint64_t offset = 0; offset < mapped; offset += 0x1000u)
        if (vmm_map_page((base & ~0xfffULL) + offset, (base & ~0xfffULL) + offset,
                         RIXURI_PTE_PRESENT | RIXURI_PTE_WRITE | RIXURI_PTE_NX) != 0) return 0;
    return (volatile uint32_t *)(uintptr_t)(base & ~0xfffULL);
}

static uint64_t dma_page(void) {
    return pmm_alloc_page_below(0x100000000ULL);
}

static void zero_bytes(void *destination, size_t length) {
    uint8_t *bytes = destination;
    for (size_t i = 0; i < length; ++i) bytes[i] = 0;
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
        driver->rx_ring[i] = (rix_e1000_descriptor_t){0};
        driver->tx_ring[i] = (rix_e1000_descriptor_t){.status = RIX_E1000_TX_STATUS_DD};
    }
    driver->rx_head = driver->rx_tail = driver->tx_head = driver->tx_tail = 0;
    return 0;
}

int rix_e1000_configure(rix_e1000_t *driver) {
    if (!driver || !driver->present || !driver->mmio_base) return -1;
    volatile uint32_t *regs = (volatile uint32_t *)(uintptr_t)driver->mmio_base;
    regs[RIX_E1000_REG_CTRL / 4] = 0x04000000u;
    for (volatile unsigned wait = 0; wait < 100000u; ++wait) {
        if (!(regs[RIX_E1000_REG_CTRL / 4] & 0x04000000u)) break;
    }
    uint32_t control = regs[RIX_E1000_REG_CTRL / 4];
    regs[RIX_E1000_REG_CTRL / 4] = control | 0x00000060u;
    uint64_t rx = dma_page();
    uint64_t tx = dma_page();
    if (!rx || !tx) {
        if (rx) pmm_free_page(rx);
        if (tx) pmm_free_page(tx);
        return -2;
    }
    driver->rx_ring_phys = rx;
    driver->tx_ring_phys = tx;
    zero_bytes((void *)(uintptr_t)rx, RIXURI_PAGE_SIZE);
    zero_bytes((void *)(uintptr_t)tx, RIXURI_PAGE_SIZE);
    volatile rix_e1000_descriptor_t *rx_descriptors =
        (volatile rix_e1000_descriptor_t *)(uintptr_t)rx;
    volatile rix_e1000_descriptor_t *tx_descriptors =
        (volatile rix_e1000_descriptor_t *)(uintptr_t)tx;
    for (size_t i = 0; i < RIX_E1000_RING_SIZE; ++i) {
        driver->rx_buffers[i] = dma_page();
        driver->tx_buffers[i] = dma_page();
        if (!driver->rx_buffers[i] || !driver->tx_buffers[i]) return -3;
        zero_bytes((void *)(uintptr_t)driver->rx_buffers[i], RIXURI_PAGE_SIZE);
        zero_bytes((void *)(uintptr_t)driver->tx_buffers[i], RIXURI_PAGE_SIZE);
        rix_e1000_descriptor_t rx_descriptor = {
            .address = driver->rx_buffers[i], .length = RIX_E1000_RX_BUFFER_SIZE,
            .status = 0
        };
        rix_e1000_descriptor_t tx_descriptor = {
            .address = driver->tx_buffers[i], .status = RIX_E1000_TX_STATUS_DD
        };
        rx_descriptors[i] = rx_descriptor;
        tx_descriptors[i] = tx_descriptor;
        driver->rx_ring[i] = rx_descriptor;
        driver->tx_ring[i] = tx_descriptor;
    }
    regs[RIX_E1000_REG_RDBAL / 4] = (uint32_t)rx;
    regs[(RIX_E1000_REG_RDBAL + 4) / 4] = (uint32_t)(rx >> 32);
    regs[RIX_E1000_REG_RDLEN / 4] = RIX_E1000_RING_SIZE * sizeof(rix_e1000_descriptor_t);
    regs[RIX_E1000_REG_RDH / 4] = 0;
    regs[RIX_E1000_REG_RDT / 4] = RIX_E1000_RING_SIZE - 1;
    regs[RIX_E1000_REG_TDBAL / 4] = (uint32_t)tx;
    regs[(RIX_E1000_REG_TDBAL + 4) / 4] = (uint32_t)(tx >> 32);
    regs[RIX_E1000_REG_TDLEN / 4] = RIX_E1000_RING_SIZE * sizeof(rix_e1000_descriptor_t);
    regs[RIX_E1000_REG_TDH / 4] = 0;
    regs[RIX_E1000_REG_TDT / 4] = 0;
    regs[RIX_E1000_REG_TCTL / 4] = 0x0103f0fau;
    uint32_t ral = regs[RIX_E1000_REG_RAL / 4];
    uint32_t rah = regs[RIX_E1000_REG_RAH / 4];
    if (!ral && !(rah & 0xffffu)) {
        ral = 0x12005452u;
        rah = 0x00005634u;
    }
    driver->mac[0] = (uint8_t)ral;
    driver->mac[1] = (uint8_t)(ral >> 8);
    driver->mac[2] = (uint8_t)(ral >> 16);
    driver->mac[3] = (uint8_t)(ral >> 24);
    driver->mac[4] = (uint8_t)rah;
    driver->mac[5] = (uint8_t)(rah >> 8);
    regs[RIX_E1000_REG_RAL / 4] = ral;
    regs[RIX_E1000_REG_RAH / 4] = rah | 0x80000000u;
    regs[RIX_E1000_REG_RCTL / 4] = 0x0400801eu;
    driver->link_up = (regs[RIX_E1000_REG_STATUS / 4] & 2u) != 0;
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
        uint32_t command = pci_config_read32(device->bus, device->device,
                                             device->function, 0x04u);
        if (pci_config_write32(device->bus, device->device, device->function,
                               0x04u, command | 0x00000006u) != 0) {
            serial_write("E1000: candidate rejected: PCI bus-master enable failed\r\n");
            continue;
        }
        volatile uint32_t *regs = map_regs(base, size);
        if (!regs) { serial_write("E1000: candidate rejected: MMIO map failed\r\n"); continue; }
        zero_bytes(&controller, sizeof(controller));
        controller.pci = device;
        controller.mmio_base = base;
        controller.mmio_size = size;
        controller.present = 1;
        if (rix_e1000_init_rings(&controller) != 0 ||
            rix_e1000_configure(&controller) != 0) {
            serial_write("E1000: ring configuration failed\r\n");
            controller.present = 0;
            continue;
        }
        serial_write("E1000: probe bus="); serial_write_dec(device->bus);
        serial_write(" dev="); serial_write_dec(device->device);
        serial_write(" bar="); serial_write_hex(base);
        serial_write(" size="); serial_write_dec(size);
        serial_write(" link="); serial_write_dec(controller.link_up);
        serial_write(" mac="); serial_write_hex((uint64_t)controller.mac[0] << 40 |
                                                   (uint64_t)controller.mac[1] << 32 |
                                                   (uint64_t)controller.mac[2] << 24 |
                                                   (uint64_t)controller.mac[3] << 16 |
                                                   (uint64_t)controller.mac[4] << 8 |
                                                   controller.mac[5]);
        serial_write(" status="); serial_write_hex(regs[RIX_E1000_REG_STATUS / 4]);
        serial_write("\r\n");
        return 0;
    }
    serial_write("E1000: no supported controller\r\n");
    return -1;
}

int rix_e1000_poll_tx(rix_e1000_t *driver) {
    if (!driver || !driver->present || !driver->tx_ring_phys) return -1;
    volatile rix_e1000_descriptor_t *descriptors =
        (volatile rix_e1000_descriptor_t *)(uintptr_t)driver->tx_ring_phys;
    volatile uint32_t *regs = (volatile uint32_t *)(uintptr_t)driver->mmio_base;
    uint16_t hardware_head = (uint16_t)regs[RIX_E1000_REG_TDH / 4];
    int completed = 0;
    while (driver->tx_head != hardware_head) {
        driver->tx_ring[driver->tx_head].status = descriptors[driver->tx_head].status;
        driver->tx_ring[driver->tx_head].length = descriptors[driver->tx_head].length;
        driver->tx_head = (uint16_t)((driver->tx_head + 1u) % RIX_E1000_RING_SIZE);
        ++completed;
    }
    return completed;
}

int rix_e1000_transmit(rix_e1000_t *driver, const void *data, size_t length) {
    if (!driver || !driver->present || !data || !length || length > RIX_NET_FRAME_CAPACITY ||
        !driver->tx_ring_phys) return -1;
    (void)rix_e1000_poll_tx(driver);
    uint16_t slot = driver->tx_tail;
    volatile rix_e1000_descriptor_t *descriptors =
        (volatile rix_e1000_descriptor_t *)(uintptr_t)driver->tx_ring_phys;
    if (!(descriptors[slot].status & RIX_E1000_TX_STATUS_DD)) return -2;
    uint8_t *buffer = (uint8_t *)(uintptr_t)driver->tx_buffers[slot];
    for (size_t i = 0; i < length; ++i) buffer[i] = ((const uint8_t *)data)[i];
    descriptors[slot].length = (uint16_t)length;
    descriptors[slot].checksum_offset = 0;
    descriptors[slot].checksum_start = 0;
    descriptors[slot].special = 0;
    descriptors[slot].status = 0;
    descriptors[slot].command = RIX_E1000_TX_CMD_EOP | RIX_E1000_TX_CMD_IFCS | RIX_E1000_TX_CMD_RS;
    driver->tx_ring[slot] = (rix_e1000_descriptor_t){
        .address = driver->tx_buffers[slot], .length = (uint16_t)length,
        .command = descriptors[slot].command, .status = 0
    };
    volatile uint32_t *regs = (volatile uint32_t *)(uintptr_t)driver->mmio_base;
    driver->tx_tail = (uint16_t)((slot + 1u) % RIX_E1000_RING_SIZE);
    regs[RIX_E1000_REG_TDT / 4] = driver->tx_tail;
    return (int)length;
}

int rix_e1000_receive(rix_e1000_t *driver, void *data, size_t capacity, size_t *length) {
    if (!driver || !driver->present || !data || !length || !capacity || !driver->rx_ring_phys)
        return -1;
    volatile rix_e1000_descriptor_t *descriptors =
        (volatile rix_e1000_descriptor_t *)(uintptr_t)driver->rx_ring_phys;
    uint16_t slot = driver->rx_head;
    uint8_t status = descriptors[slot].status;
    if (status & RIX_E1000_RX_STATUS_DD) {
        static unsigned dd_seq = 0;
        if (dd_seq < 8) {
            serial_write("E1000 RX DD #"); serial_write_dec(dd_seq);
            serial_write(" status="); serial_write_hex(status);
            serial_write(" len="); serial_write_dec(descriptors[slot].length);
            serial_write(" slot="); serial_write_dec(slot);
            serial_write("\r\n");
        }
        ++dd_seq;
    }
    if (!(status & RIX_E1000_RX_STATUS_DD)) return 0;
    if (!(status & RIX_E1000_RX_STATUS_EOP)) return -2;
    size_t received = descriptors[slot].length;
    if (received > capacity || received > RIX_NET_FRAME_CAPACITY) return -3;
    uint8_t *buffer = (uint8_t *)(uintptr_t)driver->rx_buffers[slot];
    for (size_t i = 0; i < received; ++i) ((uint8_t *)data)[i] = buffer[i];
    descriptors[slot].status = 0;
    descriptors[slot].length = RIX_E1000_RX_BUFFER_SIZE;
    driver->rx_ring[slot].status = 0;
    driver->rx_ring[slot].length = RIX_E1000_RX_BUFFER_SIZE;
    driver->rx_head = (uint16_t)((slot + 1u) % RIX_E1000_RING_SIZE);
    volatile uint32_t *regs = (volatile uint32_t *)(uintptr_t)driver->mmio_base;
    regs[RIX_E1000_REG_RDT / 4] = slot;
    *length = received;
    return 1;
}

rix_e1000_t *rix_e1000_default(void) {
    return controller.present ? &controller : 0;
}

int rix_e1000_link_up(const rix_e1000_t *driver) {
    return driver && driver->present && driver->link_up;
}
