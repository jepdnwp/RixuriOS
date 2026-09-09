#include "rtl8125.h"
#include "../mm/vmm.h"
#include "../mm/pmm.h"
#include "../serial.h"

static rix_rtl8125_t controller;

static volatile uint8_t *map_regs(uint64_t base, uint64_t size) {
    if (!base || size < 0x100u) return 0;
    uint64_t mapped = (size + 0xfffu) & ~0xfffULL;
    for (uint64_t offset = 0; offset < mapped; offset += 0x1000u)
        if (vmm_map_page((base & ~0xfffULL) + offset, (base & ~0xfffULL) + offset,
                         RIXURI_PTE_PRESENT | RIXURI_PTE_WRITE | RIXURI_PTE_NX) != 0) return 0;
    return (volatile uint8_t *)(uintptr_t)(base & ~0xfffULL);
}

static uint64_t dma_page(void) {
    return pmm_alloc_page_below(0x100000000ULL);
}

static void zero_bytes(void *destination, size_t length) {
    uint8_t *bytes = destination;
    for (size_t i = 0; i < length; ++i) bytes[i] = 0;
}

static uint32_t mmio_read32(volatile uint8_t *mmio, uint32_t offset) {
    volatile uint32_t *value = (volatile uint32_t *)(mmio + offset);
    return *value;
}

static uint8_t mmio_read8(volatile uint8_t *mmio, uint32_t offset) {
    return *(volatile uint8_t *)(mmio + offset);
}

static void mmio_write16(volatile uint8_t *mmio, uint32_t offset, uint16_t value) {
    *(volatile uint16_t *)(mmio + offset) = value;
}

static void mmio_write32(volatile uint8_t *mmio, uint32_t offset, uint32_t value) {
    volatile uint32_t *destination = (volatile uint32_t *)(mmio + offset);
    *destination = value;
}

static void mmio_write8(volatile uint8_t *mmio, uint32_t offset, uint8_t value) {
    *(volatile uint8_t *)(mmio + offset) = value;
}

int rix_rtl8125_is_supported(const rix_pci_device_t *device) {
    if (!device || device->vendor_id != RIX_RTL8125_VENDOR_ID) return 0;
    return device->device_id == RIX_RTL8125_DEVICE_ID ||
           device->device_id == RIX_RTL8126_DEVICE_ID;
}

int rix_rtl8125_probe(rix_rtl8125_t *driver) {
    if (!driver) return -1;
    driver->present = 0;
    driver->link_up = 0;
    driver->pci = 0;
    driver->mmio_base = 0;
    driver->mmio_size = 0;
    driver->mmio = 0;
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
    descriptor->flags = (flags & (RIX_RTL8125_DESC_OWN | RIX_RTL8125_DESC_EOR)) |
                        RIX_RTL8125_DESC_FS | RIX_RTL8125_DESC_LS |
                        (length & RIX_RTL8125_DESC_LEN_MASK);
    descriptor->length = 0;
    return 0;
}

int rix_rtl8125_init_rings(rix_rtl8125_t *driver) {
    if (!driver || !driver->present) return -1;
    for (size_t i = 0; i < RIX_RTL8125_TX_RING_SIZE; ++i) {
        driver->tx_ring[i].buffer_address = 0;
        driver->tx_ring[i].length = 0;
        /* Empty TX descriptors belong to the driver (OWN clear); OWN is set
         * only when handing a filled descriptor to the NIC. RX descriptors
         * below stay NIC-owned. */
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

int rix_rtl8125_hw_reset(volatile uint8_t *mmio, size_t mmio_size) {
    if (!mmio || mmio_size < RIX_RTL8125_REG_COMMAND + 4u) return -1;
    /* ChipCmd is an 8-bit register at 0x37. Reading/writing it as a dword
     * corrupts adjacent RTL8125 registers and leaves the real NIC in reset. */
    uint8_t command = mmio_read8(mmio, RIX_RTL8125_REG_COMMAND);
    mmio_write8(mmio, RIX_RTL8125_REG_COMMAND, command | RIX_RTL8125_CMD_RESET);
    for (unsigned attempt = 0; attempt < 100000u; ++attempt)
        if (!(mmio_read8(mmio, RIX_RTL8125_REG_COMMAND) & RIX_RTL8125_CMD_RESET)) return 0;
    return -2;
}

int rix_rtl8125_hw_enable(volatile uint8_t *mmio, size_t mmio_size,
                           uint32_t interrupt_mask) {
    if (!mmio || mmio_size < RIX_RTL8125_REG_IMR + 4u) return -1;
    mmio_write32(mmio, RIX_RTL8125_REG_IMR, interrupt_mask);
    mmio_write8(mmio, RIX_RTL8125_REG_COMMAND,
                mmio_read8(mmio, RIX_RTL8125_REG_COMMAND) |
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
        !driver->tx_ring_phys || !driver->rx_ring_phys) return -1;
    uint64_t tx = driver->tx_ring_phys;
    uint64_t rx = driver->rx_ring_phys;
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

/* Accept unicast-to-us, multicast and broadcast. Without this the receiver
 * configuration after reset drops everything on the wire. */
int rix_rtl8125_program_rx_filter(volatile uint8_t *mmio, size_t mmio_size) {
    if (!mmio || mmio_size < RIX_RTL8125_REG_RCR + 4u) return -1;
    mmio_write32(mmio, RIX_RTL8125_REG_RCR, RIX_RTL8125_RCR_ACCEPT);
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

int rix_rtl8125_init(void) {
    for (size_t i = 0; i < pci_device_count(); ++i) {
        const rix_pci_device_t *device = pci_device(i);
        if (!rix_rtl8125_is_supported(device)) continue;
        uint64_t size = 0, base = 0; int is_io = 0;
        if (pci_bar_size(device, 2, &size, &base, &is_io) != 0 || is_io || !base) {
            serial_write("RTL8125: candidate rejected: invalid MMIO BAR\r\n");
            continue;
        }
        uint32_t command = pci_config_read32(device->bus, device->device,
                                             device->function, 0x04u);
        if (pci_config_write32(device->bus, device->device, device->function,
                               0x04u, command | 0x00000006u) != 0) {
            serial_write("RTL8125: candidate rejected: PCI bus-master enable failed\r\n");
            continue;
        }
        volatile uint8_t *mmio = map_regs(base, size);
        if (!mmio) {
            serial_write("RTL8125: candidate rejected: MMIO map failed\r\n");
            continue;
        }
        zero_bytes(&controller, sizeof(controller));
        controller.pci = device;
        controller.mmio_base = base;
        controller.mmio_size = size;
        controller.mmio = mmio;
        controller.present = 1;
        if (rix_rtl8125_init_rings(&controller) != 0 ||
            rix_rtl8125_configure(&controller) != 0) {
            serial_write("RTL8125: ring configuration failed\r\n");
            controller.present = 0;
            continue;
        }
        serial_write("RTL8125: probe bus="); serial_write_dec(device->bus);
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
        serial_write(" status="); serial_write_hex(mmio_read32(mmio, RIX_RTL8125_REG_PHY_STATUS));
        serial_write("\r\n");
        return 0;
    }
    serial_write("RTL8125: no supported controller\r\n");
    return -1;
}

int rix_rtl8125_configure(rix_rtl8125_t *driver) {
    if (!driver || !driver->present || !driver->mmio) return -1;
    if (rix_rtl8125_hw_reset(driver->mmio, driver->mmio_size) != 0) return -2;

    uint64_t tx_ring_page = dma_page();
    uint64_t rx_ring_page = dma_page();
    if (!tx_ring_page || !rx_ring_page) {
        if (tx_ring_page) pmm_free_page(tx_ring_page);
        if (rx_ring_page) pmm_free_page(rx_ring_page);
        return -3;
    }
    driver->tx_ring_phys = tx_ring_page;
    driver->rx_ring_phys = rx_ring_page;
    zero_bytes((void *)(uintptr_t)tx_ring_page, RIXURI_PAGE_SIZE);
    zero_bytes((void *)(uintptr_t)rx_ring_page, RIXURI_PAGE_SIZE);

    volatile rix_rtl8125_descriptor_t *tx_descriptors =
        (volatile rix_rtl8125_descriptor_t *)(uintptr_t)tx_ring_page;
    volatile rix_rtl8125_descriptor_t *rx_descriptors =
        (volatile rix_rtl8125_descriptor_t *)(uintptr_t)rx_ring_page;

    for (size_t i = 0; i < RIX_RTL8125_TX_RING_SIZE; ++i) {
        driver->tx_buffers[i] = dma_page();
        if (!driver->tx_buffers[i]) return -4;
        zero_bytes((void *)(uintptr_t)driver->tx_buffers[i], RIXURI_PAGE_SIZE);
        rix_rtl8125_descriptor_t descriptor = {
            .buffer_address = driver->tx_buffers[i],
            .flags = i + 1 == RIX_RTL8125_TX_RING_SIZE ? RIX_RTL8125_DESC_EOR : 0,
            .length = 0
        };
        tx_descriptors[i] = descriptor;
        driver->tx_ring[i] = descriptor;
    }
    for (size_t i = 0; i < RIX_RTL8125_RX_RING_SIZE; ++i) {
        driver->rx_buffers[i] = dma_page();
        if (!driver->rx_buffers[i]) return -5;
        zero_bytes((void *)(uintptr_t)driver->rx_buffers[i], RIXURI_PAGE_SIZE);
        rix_rtl8125_descriptor_t descriptor = {
            .buffer_address = driver->rx_buffers[i],
            .flags = RIX_RTL8125_DESC_OWN | RIX_RTL8125_RX_BUFFER_SIZE |
                     (i + 1 == RIX_RTL8125_RX_RING_SIZE ? RIX_RTL8125_DESC_EOR : 0),
            .length = 0
        };
        rx_descriptors[i] = descriptor;
        driver->rx_ring[i] = descriptor;
    }

    if (rix_rtl8125_program_rings(driver, driver->mmio, driver->mmio_size) != 0) return -6;

    uint8_t mac[6];
    if (rix_rtl8125_read_mac(driver->mmio, driver->mmio_size, mac) == 0) {
        for (size_t i = 0; i < 6; ++i) driver->mac[i] = mac[i];
    }

    rix_rtl8125_hw_enable(driver->mmio, driver->mmio_size, 0u);
    /* RTL8125 RxMaxSize is a 16-bit byte-count register at 0xda. */
    if (rix_rtl8125_validate_mmio(driver, RIX_RTL8125_REG_RX_MAX_SIZE, 2u) != 0)
        return -8;
    mmio_write16(driver->mmio, RIX_RTL8125_REG_RX_MAX_SIZE,
                 (uint16_t)RIX_RTL8125_RX_BUFFER_SIZE);
    if (rix_rtl8125_program_rx_filter(driver->mmio, driver->mmio_size) != 0) return -7;
    rix_rtl8125_read_link(driver->mmio, driver->mmio_size, &driver->link_up);
    return 0;
}

int rix_rtl8125_poll_tx(rix_rtl8125_t *driver) {
    if (!driver || !driver->present || !driver->tx_ring_phys) return -1;
    volatile rix_rtl8125_descriptor_t *descriptors =
        (volatile rix_rtl8125_descriptor_t *)(uintptr_t)driver->tx_ring_phys;
    int completed = 0;
    while (driver->tx_head != driver->tx_tail) {
        if (descriptors[driver->tx_head].flags & RIX_RTL8125_DESC_OWN) break;
        driver->tx_ring[driver->tx_head].flags = descriptors[driver->tx_head].flags;
        driver->tx_ring[driver->tx_head].length = descriptors[driver->tx_head].length;
        driver->tx_head = (uint16_t)((driver->tx_head + 1u) % RIX_RTL8125_TX_RING_SIZE);
        ++completed;
    }
    return completed;
}

int rix_rtl8125_transmit(rix_rtl8125_t *driver, const void *data, size_t length) {
    if (!driver || !driver->present || !data || !length || length > RIX_NET_FRAME_CAPACITY ||
        !driver->tx_ring_phys) return -1;
    (void)rix_rtl8125_poll_tx(driver);
    uint16_t slot = driver->tx_tail;
    volatile rix_rtl8125_descriptor_t *descriptors =
        (volatile rix_rtl8125_descriptor_t *)(uintptr_t)driver->tx_ring_phys;
    if (descriptors[slot].flags & RIX_RTL8125_DESC_OWN) return -2;
    uint8_t *buffer = (uint8_t *)(uintptr_t)driver->tx_buffers[slot];
    for (size_t i = 0; i < length; ++i) buffer[i] = ((const uint8_t *)data)[i];
    descriptors[slot].buffer_address = driver->tx_buffers[slot];
    descriptors[slot].length = 0;
    descriptors[slot].flags = RIX_RTL8125_DESC_OWN | RIX_RTL8125_DESC_FS |
        RIX_RTL8125_DESC_LS | (uint32_t)length |
        (slot + 1 == RIX_RTL8125_TX_RING_SIZE ? RIX_RTL8125_DESC_EOR : 0);
    driver->tx_ring[slot].buffer_address = driver->tx_buffers[slot];
    driver->tx_ring[slot].length = (uint32_t)length;
    driver->tx_ring[slot].flags = descriptors[slot].flags;
    driver->tx_tail = (uint16_t)((slot + 1u) % RIX_RTL8125_TX_RING_SIZE);
    /* Ring the TX doorbell: without TPPOLL/NPQ the NIC never fetches the
     * handed-off descriptor. */
    if (rix_rtl8125_validate_mmio(driver, RIX_RTL8125_REG_TPPOLL, 1u) != 0) return -3;
    driver->mmio[RIX_RTL8125_REG_TPPOLL] = RIX_RTL8125_TPPOLL_NPQ;
    return (int)length;
}

int rix_rtl8125_receive(rix_rtl8125_t *driver, void *data, size_t capacity, size_t *length) {
    if (!driver || !driver->present || !data || !length || !capacity || !driver->rx_ring_phys)
        return -1;
    volatile rix_rtl8125_descriptor_t *descriptors =
        (volatile rix_rtl8125_descriptor_t *)(uintptr_t)driver->rx_ring_phys;
    uint16_t slot = driver->rx_head;
    uint32_t flags = descriptors[slot].flags;
    if (flags & RIX_RTL8125_DESC_OWN) return 0;
    size_t received = flags & RIX_RTL8125_DESC_LEN_MASK;
    /* The NIC reports the Ethernet FCS in the descriptor length; software
     * receives an L2 frame without the four-byte FCS. */
    if (received < 4u) return -2;
    received -= 4u;
    if (received > capacity || received > RIX_NET_FRAME_CAPACITY) return -3;
    uint8_t *buffer = (uint8_t *)(uintptr_t)driver->rx_buffers[slot];
    for (size_t i = 0; i < received; ++i) ((uint8_t *)data)[i] = buffer[i];
    descriptors[slot].length = 0;
    descriptors[slot].flags = RIX_RTL8125_DESC_OWN | RIX_RTL8125_RX_BUFFER_SIZE |
        (slot + 1 == RIX_RTL8125_RX_RING_SIZE ? RIX_RTL8125_DESC_EOR : 0);
    driver->rx_ring[slot].length = RIX_RTL8125_RX_BUFFER_SIZE;
    driver->rx_ring[slot].flags = descriptors[slot].flags;
    driver->rx_head = (uint16_t)((slot + 1u) % RIX_RTL8125_RX_RING_SIZE);
    *length = received;
    return 1;
}

rix_rtl8125_t *rix_rtl8125_default(void) {
    return controller.present ? &controller : 0;
}

int rix_rtl8125_link_up(const rix_rtl8125_t *driver) {
    return driver && driver->present && driver->link_up;
}
