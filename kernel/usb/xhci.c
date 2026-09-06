#include "xhci.h"
#include "../pci/pci.h"
#include "../mm/vmm.h"
#include "../mm/pmm.h"
#include <stddef.h>

#define XHCI_MAX 4
#define XHCI_MAX_SLOTS 256u
#define PCI_CLASS_SERIAL 0x0C
#define PCI_SUBCLASS_USB 0x03
#define PCI_PROGIF_XHCI 0x30
#define PCI_COMMAND 0x04
#define PCI_COMMAND_MEMORY (1u << 1)
#define PCI_COMMAND_BUS_MASTER (1u << 2)
#define XHCI_CAPLENGTH 0x00
#define XHCI_HCIVERSION 0x02
#define XHCI_HCSPARAMS1 0x04
#define XHCI_HCCPARAMS1 0x10
#define XHCI_DBOFF 0x14
#define XHCI_RTSOFF 0x18
#define XHCI_USBCMD 0x80
#define XHCI_USBSTS 0x84
#define XHCI_CRCR 0x18
#define XHCI_DCBAAP 0x30
#define XHCI_CONFIG 0x38
#define XHCI_PORTSC_BASE 0x400
#define XHCI_PORT_STRIDE 0x10
#define XHCI_PORT_CCS (1u << 0)
#define XHCI_PORT_PED (1u << 1)
#define XHCI_PORT_PR (1u << 4)
#define XHCI_PORT_SPEED_SHIFT 10
#define XHCI_PORT_SPEED_MASK (0xFu << XHCI_PORT_SPEED_SHIFT)
#define XHCI_PORT_CSC (1u << 17)
#define XHCI_PORT_PRC (1u << 21)
#define XHCI_CMD_RS (1u << 0)
#define XHCI_CMD_HCRST (1u << 1)
#define XHCI_STS_HCH (1u << 0)
#define XHCI_STS_HSE (1u << 2)
#define XHCI_TRB_LINK 6u
#define XHCI_TRB_NORMAL 1u
#define XHCI_TRB_SETUP_STAGE 2u
#define XHCI_TRB_DATA_STAGE 3u
#define XHCI_TRB_STATUS_STAGE 4u
#define XHCI_TRB_TRANSFER_EVENT 32u
#define XHCI_TRB_ENABLE_SLOT 9u
#define XHCI_TRB_DISABLE_SLOT 10u
#define XHCI_TRB_ADDRESS_DEVICE 11u
#define XHCI_TRB_COMMAND_COMPLETION 33u
#define XHCI_TRB_TYPE_SHIFT 10u
#define XHCI_TRB_SLOT_SHIFT 24u
#define XHCI_TRB_TC (1u << 1)
#define XHCI_TRB_ENT (1u << 1)
#define XHCI_TRB_CH (1u << 4)
#define XHCI_TRB_IOC (1u << 5)
#define XHCI_TRB_IDT (1u << 6)
#define XHCI_TRB_DIR (1u << 16)
#define XHCI_TRB_CYCLE (1u << 0)
#define XHCI_COMPLETION_SUCCESS 1u
#define XHCI_POLL_LIMIT 1000000u
#define XHCI_CMD_RING_TRBS 64u
#define XHCI_EVENT_RING_TRBS 64u
#define XHCI_ERDP_EHB (1ULL << 3)
#define XHCI_HCC_AC64 (1u << 0)
#define XHCI_HCC_CSZ (1u << 2)
#define XHCI_INPUT_ADD_SLOT (1u << 1)
#define XHCI_INPUT_ADD_EP0 (1u << 2)
#define XHCI_SLOT_CONTEXT_ENTRIES (1u << 27)
#define XHCI_EP0_TYPE_CONTROL 4u
#define XHCI_EP0_CERR 3u

/* A TRB is always 16-byte aligned and is written in little-endian fields. */
typedef struct {
    uint32_t parameter_lo;
    uint32_t parameter_hi;
    uint32_t status;
    uint32_t control;
} xhci_trb_t;

typedef struct {
    uint64_t ring_segment_base;
    uint32_t ring_segment_size;
    uint32_t reserved;
} xhci_erst_entry_t;

typedef struct {
    uint8_t allocated;
    uint8_t addressed;
    uint8_t port;
    uint8_t speed;
    uint64_t device_context_phys;
    uint64_t input_context_phys;
    uint64_t ep0_ring_phys;
    uint16_t ep0_enqueue;
    uint8_t ep0_cycle;
} xhci_slot_runtime_t;

typedef struct {
    uint16_t command_enqueue;
    uint16_t event_dequeue;
    uint8_t command_cycle;
    uint8_t event_cycle;
    xhci_slot_runtime_t slots[XHCI_MAX_SLOTS];
} xhci_runtime_t;

static rix_xhci_controller_t controllers[XHCI_MAX];
static xhci_runtime_t runtimes[XHCI_MAX];
static size_t count;

static int map_range(uint64_t base, uint64_t length) {
    if (length == 0 || base > UINT64_MAX - (length - 1u)) return -1;
    uint64_t first = base & ~0xFFFULL;
    uint64_t last = (base + length - 1u) & ~0xFFFULL;
    for (uint64_t page = first;; page += 0x1000ULL) {
        if (vmm_map_page(page, page, RIXURI_PTE_PRESENT | RIXURI_PTE_WRITE | RIXURI_PTE_NX) != 0) return -1;
        if (page == last) break;
        if (page > UINT64_MAX - 0x1000ULL) return -1;
    }
    return 0;
}

static void zero_page(uint64_t phys) {
    volatile uint8_t *p = (volatile uint8_t *)(uintptr_t)phys;
    for (size_t i = 0; i < 4096; ++i) p[i] = 0;
}

static void zero_runtime(xhci_runtime_t *rt) {
    volatile uint8_t *p = (volatile uint8_t *)rt;
    for (size_t i = 0; i < sizeof(*rt); ++i) p[i] = 0;
    rt->command_cycle = 1;
    rt->event_cycle = 1;
}

static uint64_t dma_page(const rix_xhci_controller_t *c) {
    /* xHCI without AC64 can only address the first 4 GiB. */
    uint64_t limit = (c->hcc_params1 & XHCI_HCC_AC64) != 0u ? UINT64_MAX : 0x100000000ULL;
    return pmm_alloc_page_below(limit);
}

static int wait_halted(volatile uint8_t *op, int halted) {
    volatile uint32_t *sts = (volatile uint32_t *)(op + XHCI_USBSTS);
    for (uint32_t i = 0; i < XHCI_POLL_LIMIT; ++i) {
        uint32_t s = *sts;
        int is_halted = (s & XHCI_STS_HCH) != 0u;
        if (is_halted == halted) return (s & XHCI_STS_HSE) ? -2 : 0;
    }
    return -1;
}

static int reset_controller(volatile uint8_t *op) {
    volatile uint32_t *cmd = (volatile uint32_t *)(op + XHCI_USBCMD);
    volatile uint32_t *sts = (volatile uint32_t *)(op + XHCI_USBSTS);
    *cmd &= ~XHCI_CMD_RS;
    if (wait_halted(op, 1) != 0) return -1;
    *cmd |= XHCI_CMD_HCRST;
    for (uint32_t i = 0; i < XHCI_POLL_LIMIT; ++i) {
        uint32_t v = *cmd;
        uint32_t s = *sts;
        if ((s & XHCI_STS_HSE) != 0u) return -2;
        if ((v & XHCI_CMD_HCRST) == 0u) return 0;
    }
    return -3;
}

static void release_runtime_pages(rix_xhci_controller_t *c) {
    if (c->dcbaa_phys) pmm_free_page(c->dcbaa_phys);
    if (c->cmd_ring_phys) pmm_free_page(c->cmd_ring_phys);
    if (c->event_ring_phys) pmm_free_page(c->event_ring_phys);
    if (c->erst_phys) pmm_free_page(c->erst_phys);
    c->dcbaa_phys = 0;
    c->cmd_ring_phys = 0;
    c->event_ring_phys = 0;
    c->erst_phys = 0;
}

static int setup_runtime(rix_xhci_controller_t *c, volatile uint8_t *base, xhci_runtime_t *rt) {
    uint64_t dcbaa = dma_page(c);
    uint64_t cmd_ring = dma_page(c);
    uint64_t event_ring = dma_page(c);
    uint64_t erst = dma_page(c);
    if (!dcbaa || !cmd_ring || !event_ring || !erst) {
        if (dcbaa) pmm_free_page(dcbaa);
        if (cmd_ring) pmm_free_page(cmd_ring);
        if (event_ring) pmm_free_page(event_ring);
        if (erst) pmm_free_page(erst);
        return -1;
    }
    zero_page(dcbaa);
    zero_page(cmd_ring);
    zero_page(event_ring);
    zero_page(erst);
    zero_runtime(rt);

    volatile xhci_trb_t *ring = (volatile xhci_trb_t *)(uintptr_t)cmd_ring;
    ring[XHCI_CMD_RING_TRBS - 1u].parameter_lo = (uint32_t)cmd_ring;
    ring[XHCI_CMD_RING_TRBS - 1u].parameter_hi = (uint32_t)(cmd_ring >> 32);
    ring[XHCI_CMD_RING_TRBS - 1u].control =
        (XHCI_TRB_LINK << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_TC | XHCI_TRB_CYCLE;

    volatile xhci_erst_entry_t *entry = (volatile xhci_erst_entry_t *)(uintptr_t)erst;
    entry[0].ring_segment_base = event_ring;
    entry[0].ring_segment_size = XHCI_EVENT_RING_TRBS;

    volatile uint64_t *dcbaa_ptr = (volatile uint64_t *)(uintptr_t)dcbaa;
    dcbaa_ptr[0] = 0;

    volatile uint64_t *dcbaap = (volatile uint64_t *)(base + XHCI_DCBAAP);
    volatile uint64_t *crcr = (volatile uint64_t *)(base + XHCI_CRCR);
    *dcbaap = dcbaa;
    *crcr = cmd_ring | XHCI_TRB_CYCLE;

    uint32_t db_off = *(volatile uint32_t *)(base + XHCI_DBOFF) & ~0x3u;
    uint32_t rt_off = *(volatile uint32_t *)(base + XHCI_RTSOFF) & ~0x1Fu;
    if (rt_off < c->cap_length) {
        pmm_free_page(dcbaa);
        pmm_free_page(cmd_ring);
        pmm_free_page(event_ring);
        pmm_free_page(erst);
        return -2;
    }
    volatile uint8_t *runtime = base + rt_off;
    volatile uint32_t *iman = (volatile uint32_t *)(runtime + 0x20);
    volatile uint32_t *erstsz = (volatile uint32_t *)(runtime + 0x28);
    volatile uint64_t *erstba = (volatile uint64_t *)(runtime + 0x30);
    volatile uint64_t *erdp = (volatile uint64_t *)(runtime + 0x38);
    *iman &= ~1u;
    *erstsz = 1u;
    *erstba = erst;
    *erdp = event_ring | XHCI_ERDP_EHB;
    *iman |= 1u;

    volatile uint32_t *config = (volatile uint32_t *)(base + XHCI_CONFIG);
    *config = c->max_slots;
    volatile uint32_t *db0 = (volatile uint32_t *)(base + db_off);
    *db0 = 0;

    c->dcbaa_phys = dcbaa;
    c->cmd_ring_phys = cmd_ring;
    c->event_ring_phys = event_ring;
    c->erst_phys = erst;
    c->running = 0;
    return 0;
}

int xhci_init(void) {
    count = 0;
    for (size_t i = 0; i < pci_device_count() && count < XHCI_MAX; ++i) {
        const rix_pci_device_t *d = pci_device(i);
        if (!d || d->class_code != PCI_CLASS_SERIAL || d->subclass != PCI_SUBCLASS_USB || d->prog_if != PCI_PROGIF_XHCI) continue;
        uint32_t lo = d->bars[0];
        if (lo & 1u) continue;
        uint64_t bar = (uint64_t)(lo & 0xfffffff0u);
        if (((lo >> 1) & 3u) == 2u) bar |= (uint64_t)d->bars[1] << 32;
        if (!bar || map_range(bar, 0x10000u) != 0) continue;

        uint32_t command = pci_config_read32(d->bus, d->device, d->function, PCI_COMMAND);
        command |= PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER;
        if (pci_config_write32(d->bus, d->device, d->function, PCI_COMMAND, command) != 0) continue;
        command = pci_config_read32(d->bus, d->device, d->function, PCI_COMMAND);
        if ((command & (PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER)) !=
            (PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER)) continue;

        volatile uint8_t *base = (volatile uint8_t *)(uintptr_t)bar;
        rix_xhci_controller_t *c = &controllers[count];
        c->bus = d->bus;
        c->device = d->device;
        c->function = d->function;
        c->bar0 = bar;
        c->cap_length = base[XHCI_CAPLENGTH];
        c->hci_version = *(volatile uint16_t *)(base + XHCI_HCIVERSION);
        uint32_t hcs = *(volatile uint32_t *)(base + XHCI_HCSPARAMS1);
        c->max_slots = (uint8_t)(hcs & 0xffu);
        c->max_intrs = (uint8_t)((hcs >> 8) & 0x7ffu);
        c->max_ports = (uint8_t)((hcs >> 24) & 0xffu);
        c->hcc_params1 = *(volatile uint32_t *)(base + XHCI_HCCPARAMS1);
        if (c->cap_length < 0x20u || c->max_slots == 0u || c->max_ports == 0u) continue;

        volatile uint8_t *op = base + c->cap_length;
        c->usbcmd = *(volatile uint32_t *)(op + XHCI_USBCMD);
        c->usbsts = *(volatile uint32_t *)(op + XHCI_USBSTS);
        c->running = 0;
        if (reset_controller(op) != 0) continue;
        if (setup_runtime(c, op, &runtimes[count]) != 0) continue;
        *(volatile uint32_t *)(op + XHCI_USBCMD) |= XHCI_CMD_RS;
        if (wait_halted(op, 0) != 0) {
            release_runtime_pages(c);
            continue;
        }
        c->usbcmd = *(volatile uint32_t *)(op + XHCI_USBCMD);
        c->usbsts = *(volatile uint32_t *)(op + XHCI_USBSTS);
        c->running = 1;
        ++count;
    }
    return 0;
}

size_t xhci_controller_count(void) { return count; }
const rix_xhci_controller_t *xhci_controller(size_t index) { return index < count ? &controllers[index] : NULL; }

static volatile uint32_t *port_reg(const rix_xhci_controller_t *c, uint8_t port) {
    if (!c || !c->running || port == 0 || port > c->max_ports) return NULL;
    volatile uint8_t *base = (volatile uint8_t *)(uintptr_t)c->bar0;
    return (volatile uint32_t *)(base + c->cap_length + XHCI_PORTSC_BASE +
                                  (uint32_t)(port - 1u) * XHCI_PORT_STRIDE);
}

int xhci_port_status(size_t controller, uint8_t port, rix_xhci_port_status_t *out) {
    if (!out) return -1;
    const rix_xhci_controller_t *c = xhci_controller(controller);
    volatile uint32_t *reg = port_reg(c, port);
    if (!reg) return -2;
    uint32_t v = *reg;
    out->connected = (v & XHCI_PORT_CCS) != 0u;
    out->enabled = (v & XHCI_PORT_PED) != 0u;
    out->speed = (uint8_t)((v & XHCI_PORT_SPEED_MASK) >> XHCI_PORT_SPEED_SHIFT);
    out->reset_complete = (v & XHCI_PORT_PRC) != 0u;
    return 0;
}

int xhci_reset_port(size_t controller, uint8_t port) {
    const rix_xhci_controller_t *c = xhci_controller(controller);
    volatile uint32_t *reg = port_reg(c, port);
    if (!reg) return -1;
    uint32_t v = *reg;
    if ((v & XHCI_PORT_CCS) == 0u) return -2;
    /* CSC and PRC are write-one-to-clear bits; zeroing them in the write does not clear them. */
    v |= XHCI_PORT_CSC | XHCI_PORT_PRC | XHCI_PORT_PR;
    *reg = v;
    for (uint32_t i = 0; i < XHCI_POLL_LIMIT; ++i) {
        uint32_t s = *reg;
        if ((s & XHCI_PORT_PR) == 0u) {
            if ((s & XHCI_PORT_PRC) != 0u) return 0;
            if ((s & XHCI_PORT_CCS) == 0u) return -3;
            return -4;
        }
    }
    return -5;
}

static volatile uint8_t *runtime_base(const rix_xhci_controller_t *c) {
    volatile uint8_t *base = (volatile uint8_t *)(uintptr_t)c->bar0;
    uint32_t rt_off = *(volatile uint32_t *)(base + XHCI_RTSOFF) & ~0x1Fu;
    return base + rt_off;
}

static void acknowledge_event(const rix_xhci_controller_t *c, xhci_runtime_t *rt) {
    rt->event_dequeue++;
    if (rt->event_dequeue == XHCI_EVENT_RING_TRBS) {
        rt->event_dequeue = 0;
        rt->event_cycle ^= 1u;
    }
    uint64_t erdp = c->event_ring_phys + (uint64_t)rt->event_dequeue * sizeof(xhci_trb_t);
    *(volatile uint64_t *)(runtime_base(c) + 0x38) = erdp | XHCI_ERDP_EHB;
}

static int wait_command(const rix_xhci_controller_t *c, xhci_runtime_t *rt,
                        uint64_t command_phys, uint8_t *out_slot) {
    volatile xhci_trb_t *events = (volatile xhci_trb_t *)(uintptr_t)c->event_ring_phys;
    for (uint32_t i = 0; i < XHCI_POLL_LIMIT; ++i) {
        volatile xhci_trb_t *event = &events[rt->event_dequeue];
        uint32_t control = event->control;
        if ((control & XHCI_TRB_CYCLE) != rt->event_cycle) continue;
        uint32_t type = (control >> XHCI_TRB_TYPE_SHIFT) & 0x3fu;
        uint64_t parameter = ((uint64_t)event->parameter_hi << 32) | event->parameter_lo;
        uint8_t slot = (uint8_t)(control >> XHCI_TRB_SLOT_SHIFT);
        uint8_t completion = (uint8_t)(event->status >> 24);
        acknowledge_event(c, rt);
        if (type != XHCI_TRB_COMMAND_COMPLETION || parameter != command_phys) continue;
        if (out_slot) *out_slot = slot;
        if (completion == XHCI_COMPLETION_SUCCESS) return 0;
        return completion != 0u ? -(int)completion : -90;
    }
    return -100;
}

static int submit_command(size_t controller, uint64_t parameter, uint32_t control, uint8_t *out_slot) {
    if (controller >= count) return -1;
    const rix_xhci_controller_t *c = &controllers[controller];
    if (!c->running || !c->cmd_ring_phys || !c->event_ring_phys) return -2;
    xhci_runtime_t *rt = &runtimes[controller];
    volatile xhci_trb_t *ring = (volatile xhci_trb_t *)(uintptr_t)c->cmd_ring_phys;
    if (rt->command_enqueue >= XHCI_CMD_RING_TRBS - 1u) {
        volatile xhci_trb_t *link = &ring[XHCI_CMD_RING_TRBS - 1u];
        link->parameter_lo = (uint32_t)c->cmd_ring_phys;
        link->parameter_hi = (uint32_t)(c->cmd_ring_phys >> 32);
        link->status = 0;
        link->control = (XHCI_TRB_LINK << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_TC |
                        (rt->command_cycle ? XHCI_TRB_CYCLE : 0u);
        __asm__ volatile("mfence" ::: "memory");
        rt->command_enqueue = 0;
        rt->command_cycle ^= 1u;
    }
    uint16_t index = rt->command_enqueue;
    volatile xhci_trb_t *command_trb = &ring[index];
    uint64_t command_phys = c->cmd_ring_phys + (uint64_t)index * sizeof(xhci_trb_t);
    command_trb->parameter_lo = (uint32_t)parameter;
    command_trb->parameter_hi = (uint32_t)(parameter >> 32);
    command_trb->status = 0;
    command_trb->control = (control & ~XHCI_TRB_CYCLE) |
                           (rt->command_cycle ? XHCI_TRB_CYCLE : 0u);
    rt->command_enqueue = (uint16_t)(index + 1u);
    __asm__ volatile("mfence" ::: "memory");

    volatile uint8_t *base = (volatile uint8_t *)(uintptr_t)c->bar0;
    uint32_t db_off = *(volatile uint32_t *)(base + XHCI_DBOFF) & ~0x3u;
    *(volatile uint32_t *)(base + db_off) = 0;
    return wait_command(c, rt, command_phys, out_slot);
}

static uint16_t initial_ep0_mps(uint8_t speed) {
    if (speed == 3u) return 64u; /* high-speed */
    if (speed >= 4u) return 512u; /* SuperSpeed and later */
    return 8u; /* full- and low-speed default control endpoint */
}

static int allocate_slot_context(const rix_xhci_controller_t *c, xhci_slot_runtime_t *slot) {
    uint64_t device_context = dma_page(c);
    uint64_t input_context = dma_page(c);
    uint64_t ep0_ring = dma_page(c);
    if (!device_context || !input_context || !ep0_ring) {
        if (device_context) pmm_free_page(device_context);
        if (input_context) pmm_free_page(input_context);
        if (ep0_ring) pmm_free_page(ep0_ring);
        return -1;
    }
    zero_page(device_context);
    zero_page(input_context);
    zero_page(ep0_ring);
    slot->device_context_phys = device_context;
    slot->input_context_phys = input_context;
    slot->ep0_ring_phys = ep0_ring;
    return 0;
}

static void release_slot_context(const rix_xhci_controller_t *c, uint8_t slot_id) {
    (void)c;
    xhci_slot_runtime_t *slot = &runtimes[(size_t)(c - controllers)].slots[slot_id];
    if (slot->device_context_phys) {
        volatile uint64_t *dcbaa = (volatile uint64_t *)(uintptr_t)c->dcbaa_phys;
        dcbaa[slot_id] = 0;
        __asm__ volatile("mfence" ::: "memory");
        pmm_free_page(slot->device_context_phys);
    }
    if (slot->input_context_phys) pmm_free_page(slot->input_context_phys);
    if (slot->ep0_ring_phys) pmm_free_page(slot->ep0_ring_phys);
    slot->device_context_phys = 0;
    slot->input_context_phys = 0;
    slot->ep0_ring_phys = 0;
    slot->allocated = 0;
    slot->addressed = 0;
    slot->port = 0;
    slot->speed = 0;
}

static int prepare_address_context(size_t controller, uint8_t slot_id, uint8_t port, uint8_t speed) {
    const rix_xhci_controller_t *c = &controllers[controller];
    xhci_slot_runtime_t *slot = &runtimes[controller].slots[slot_id];
    if (allocate_slot_context(c, slot) != 0) return -1;
    volatile uint64_t *dcbaa = (volatile uint64_t *)(uintptr_t)c->dcbaa_phys;
    dcbaa[slot_id] = slot->device_context_phys;
    __asm__ volatile("mfence" ::: "memory");
    volatile xhci_trb_t *ep0_ring = (volatile xhci_trb_t *)(uintptr_t)slot->ep0_ring_phys;
    ep0_ring[XHCI_CMD_RING_TRBS - 1u].parameter_lo = (uint32_t)slot->ep0_ring_phys;
    ep0_ring[XHCI_CMD_RING_TRBS - 1u].parameter_hi = (uint32_t)(slot->ep0_ring_phys >> 32);
    ep0_ring[XHCI_CMD_RING_TRBS - 1u].control =
        (XHCI_TRB_LINK << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_TC | XHCI_TRB_CYCLE;

    uint32_t context_size = (c->hcc_params1 & XHCI_HCC_CSZ) != 0u ? 64u : 32u;
    volatile uint32_t *input = (volatile uint32_t *)(uintptr_t)slot->input_context_phys;
    volatile uint32_t *slot_context = (volatile uint32_t *)
        (uintptr_t)(slot->input_context_phys + context_size);
    volatile uint32_t *ep0_context = (volatile uint32_t *)
        (uintptr_t)(slot->input_context_phys + (uint64_t)context_size * 2u);
    input[1] = XHCI_INPUT_ADD_SLOT | XHCI_INPUT_ADD_EP0;
    slot_context[0] = ((uint32_t)(speed & 0x0Fu) << 20) | XHCI_SLOT_CONTEXT_ENTRIES;
    slot_context[1] = (uint32_t)port << 16;
    ep0_context[1] = (XHCI_EP0_CERR << 1) | (XHCI_EP0_TYPE_CONTROL << 3) |
                     ((uint32_t)initial_ep0_mps(speed) << 16);
    ep0_context[2] = (uint32_t)slot->ep0_ring_phys;
    ep0_context[3] = (uint32_t)(slot->ep0_ring_phys >> 32) | XHCI_TRB_CYCLE;
    slot->ep0_enqueue = 0;
    slot->ep0_cycle = 1;
    slot->port = port;
    slot->speed = speed;
    return 0;
}

int xhci_enable_slot(size_t controller, uint8_t *out_slot) {
    if (!out_slot) return -1;
    *out_slot = 0;
    uint8_t slot_id = 0;
    int rc = submit_command(controller, 0,
                            XHCI_TRB_ENABLE_SLOT << XHCI_TRB_TYPE_SHIFT, &slot_id);
    if (rc != 0) return rc;
    if (controller >= count || slot_id == 0u ||
        slot_id > controllers[controller].max_slots) return -3;
    xhci_slot_runtime_t *slot = &runtimes[controller].slots[slot_id];
    if (slot->allocated) return -4;
    slot->allocated = 1;
    *out_slot = slot_id;
    return 0;
}

int xhci_disable_slot(size_t controller, uint8_t slot_id) {
    if (controller >= count || slot_id == 0u ||
        slot_id > controllers[controller].max_slots) return -1;
    xhci_slot_runtime_t *slot = &runtimes[controller].slots[slot_id];
    if (!slot->allocated) return -2;
    int rc = submit_command(controller, 0,
                            (XHCI_TRB_DISABLE_SLOT << XHCI_TRB_TYPE_SHIFT) |
                            ((uint32_t)slot_id << XHCI_TRB_SLOT_SHIFT), NULL);
    if (rc != 0) return rc;
    release_slot_context(&controllers[controller], slot_id);
    return 0;
}

int xhci_address_device(size_t controller, uint8_t slot_id, uint8_t port, uint8_t speed) {
    if (controller >= count || slot_id == 0u ||
        slot_id > controllers[controller].max_slots || port == 0u ||
        port > controllers[controller].max_ports || speed == 0u) return -1;
    xhci_slot_runtime_t *slot = &runtimes[controller].slots[slot_id];
    if (!slot->allocated || slot->addressed) return -2;
    if (prepare_address_context(controller, slot_id, port, speed) != 0) return -3;
    int rc = submit_command(controller, slot->input_context_phys,
                            (XHCI_TRB_ADDRESS_DEVICE << XHCI_TRB_TYPE_SHIFT) |
                            ((uint32_t)slot_id << XHCI_TRB_SLOT_SHIFT), NULL);
    if (rc != 0) {
        release_slot_context(&controllers[controller], slot_id);
        return rc;
    }
    slot->addressed = 1;
    return 0;
}

static void ep0_write_link(const rix_xhci_controller_t *c, xhci_slot_runtime_t *slot) {
    volatile xhci_trb_t *ring = (volatile xhci_trb_t *)(uintptr_t)slot->ep0_ring_phys;
    ring[XHCI_CMD_RING_TRBS - 1u].parameter_lo = (uint32_t)slot->ep0_ring_phys;
    ring[XHCI_CMD_RING_TRBS - 1u].parameter_hi = (uint32_t)(slot->ep0_ring_phys >> 32);
    ring[XHCI_CMD_RING_TRBS - 1u].status = 0;
    ring[XHCI_CMD_RING_TRBS - 1u].control = (XHCI_TRB_LINK << XHCI_TRB_TYPE_SHIFT) |
        XHCI_TRB_TC | (slot->ep0_cycle ? XHCI_TRB_CYCLE : 0u);
    (void)c;
    slot->ep0_enqueue = 0;
    slot->ep0_cycle ^= 1u;
}

static uint64_t ep0_emit(const rix_xhci_controller_t *c, xhci_slot_runtime_t *slot,
                         uint64_t parameter, uint32_t status, uint32_t control) {
    if (slot->ep0_enqueue >= XHCI_CMD_RING_TRBS - 1u) ep0_write_link(c, slot);
    uint16_t index = slot->ep0_enqueue++;
    volatile xhci_trb_t *trb = &((volatile xhci_trb_t *)(uintptr_t)slot->ep0_ring_phys)[index];
    uint64_t physical = slot->ep0_ring_phys + (uint64_t)index * sizeof(xhci_trb_t);
    trb->parameter_lo = (uint32_t)parameter;
    trb->parameter_hi = (uint32_t)(parameter >> 32);
    trb->status = status;
    trb->control = (control & ~XHCI_TRB_CYCLE) |
                   (slot->ep0_cycle ? XHCI_TRB_CYCLE : 0u);
    return physical;
}

static int wait_transfer(const rix_xhci_controller_t *c, xhci_runtime_t *rt,
                         uint64_t last_trb, uint8_t slot_id, uint16_t requested,
                         uint16_t *actual) {
    volatile xhci_trb_t *events = (volatile xhci_trb_t *)(uintptr_t)c->event_ring_phys;
    for (uint32_t i = 0; i < XHCI_POLL_LIMIT; ++i) {
        volatile xhci_trb_t *event = &events[rt->event_dequeue];
        uint32_t control = event->control;
        if ((control & XHCI_TRB_CYCLE) != rt->event_cycle) continue;
        uint64_t parameter = ((uint64_t)event->parameter_hi << 32) | event->parameter_lo;
        uint32_t type = (control >> XHCI_TRB_TYPE_SHIFT) & 0x3fu;
        uint8_t event_slot = (uint8_t)(control >> XHCI_TRB_SLOT_SHIFT);
        uint8_t endpoint = (uint8_t)((control >> 16) & 0x1fu);
        uint8_t completion = (uint8_t)(event->status >> 24);
        uint32_t residual = event->status & 0x00ffffffu;
        acknowledge_event(c, rt);
        if (type != XHCI_TRB_TRANSFER_EVENT || parameter != last_trb ||
            event_slot != slot_id || endpoint != 1u) continue;
        if (actual) *actual = residual >= requested ? 0u : (uint16_t)(requested - residual);
        return completion == XHCI_COMPLETION_SUCCESS ? 0 :
               (completion != 0u ? -(int)completion : -90);
    }
    return -100;
}

int xhci_control_transfer(size_t controller, uint8_t slot_id,
                          const rix_usb_setup_packet_t *setup,
                          void *data, uint16_t *actual_length) {
    if (actual_length) *actual_length = 0;
    if (!setup || controller >= count || slot_id == 0u ||
        slot_id > controllers[controller].max_slots) return -1;
    xhci_slot_runtime_t *slot = &runtimes[controller].slots[slot_id];
    const rix_xhci_controller_t *c = &controllers[controller];
    if (!c->running || !slot->allocated || !slot->addressed || !slot->ep0_ring_phys ||
        (setup->length != 0u && !data)) return -2;
    if (setup->length != 0u && ((uint64_t)(uintptr_t)data > UINT64_MAX - setup->length)) return -3;
    uint8_t data_in = (setup->request_type & 0x80u) != 0u;
    uint32_t setup_control = (XHCI_TRB_SETUP_STAGE << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_IDT;
    if (setup->length != 0u) {
        setup_control |= XHCI_TRB_CH | (data_in ? (3u << 16) : (2u << 16));
    }
    uint32_t setup_parameter = (uint32_t)setup->request_type |
                               ((uint32_t)setup->request << 8) |
                               ((uint32_t)setup->value << 16);
    uint32_t setup_status = (uint32_t)setup->index | ((uint32_t)setup->length << 16);
    size_t needed = setup->length != 0u ? 3u : 2u;
    if ((size_t)slot->ep0_enqueue + needed > XHCI_CMD_RING_TRBS - 1u) ep0_write_link(c, slot);
    (void)ep0_emit(c, slot, setup_parameter, setup_status, setup_control);
    if (setup->length != 0u) {
        (void)ep0_emit(c, slot, (uint64_t)(uintptr_t)data, setup->length,
                       (XHCI_TRB_DATA_STAGE << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_CH |
                       (data_in ? XHCI_TRB_DIR : 0u));
    }
    uint64_t status_trb = ep0_emit(c, slot, 0, 0,
        (XHCI_TRB_STATUS_STAGE << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_IOC |
        ((setup->length == 0u || !data_in) ? XHCI_TRB_DIR : 0u));
    __asm__ volatile("mfence" ::: "memory");
    volatile uint8_t *base = (volatile uint8_t *)(uintptr_t)c->bar0;
    uint32_t db_off = *(volatile uint32_t *)(base + XHCI_DBOFF) & ~0x3u;
    *(volatile uint32_t *)(base + db_off + (uint32_t)slot_id * 4u) = 1u;
    return wait_transfer(c, &runtimes[controller], status_trb, slot_id,
                         setup->length, actual_length);
}

int xhci_get_descriptor(size_t controller, uint8_t slot_id, uint8_t descriptor_type,
                        uint8_t descriptor_index, uint16_t language_id,
                        void *buffer, uint16_t length, uint16_t *actual_length) {
    if (descriptor_type == 0u || (length != 0u && !buffer)) return -1;
    rix_usb_setup_packet_t setup = {
        .request_type = 0x80u,
        .request = 6u,
        .value = (uint16_t)(((uint16_t)descriptor_type << 8) | descriptor_index),
        .index = language_id,
        .length = length
    };
    return xhci_control_transfer(controller, slot_id, &setup, buffer, actual_length);
}

int xhci_device_attach(size_t controller, uint8_t port, rix_xhci_device_t *out) {
    if (!out || controller >= count) return -1;
    rix_xhci_port_status_t status;
    if (xhci_port_status(controller, port, &status) != 0 || !status.connected) return -2;
    for (uint16_t slot_index = 1; slot_index <= controllers[controller].max_slots; ++slot_index) {
        uint8_t slot_id = (uint8_t)slot_index;
        if (runtimes[controller].slots[slot_id].allocated &&
            runtimes[controller].slots[slot_id].port == port) return -3;
    }
    if (xhci_reset_port(controller, port) != 0) return -4;
    if (xhci_port_status(controller, port, &status) != 0 || !status.connected || !status.speed) return -5;

    uint8_t slot_id = 0;
    int rc = xhci_enable_slot(controller, &slot_id);
    if (rc != 0) return -6;
    rc = xhci_address_device(controller, slot_id, port, status.speed);
    if (rc != 0) {
        (void)xhci_disable_slot(controller, slot_id);
        return -7;
    }
    out->slot_id = slot_id;
    out->port = port;
    out->speed = status.speed;
    out->state = RIX_XHCI_DEVICE_ADDRESSED;
    return 0;
}

int xhci_device_detach(size_t controller, uint8_t slot_id) {
    if (controller >= count || slot_id == 0u ||
        slot_id > controllers[controller].max_slots) return -1;
    xhci_slot_runtime_t *slot = &runtimes[controller].slots[slot_id];
    if (!slot->allocated) return -2;
    return xhci_disable_slot(controller, slot_id);
}
