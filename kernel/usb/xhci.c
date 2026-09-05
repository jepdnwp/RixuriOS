#include "xhci.h"
#include "../pci/pci.h"
#include "../mm/vmm.h"
#include "../mm/pmm.h"
#include <stddef.h>

#define XHCI_MAX 4
#define PCI_CLASS_SERIAL 0x0C
#define PCI_SUBCLASS_USB 0x03
#define PCI_PROGIF_XHCI 0x30
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
#define XHCI_CMD_RS (1u << 0)
#define XHCI_CMD_HCRST (1u << 1)
#define XHCI_STS_HCH (1u << 0)
#define XHCI_STS_HSE (1u << 2)
#define XHCI_TRB_LINK 6u
#define XHCI_TRB_TC (1u << 1)
#define XHCI_TRB_C (1u << 0)
#define XHCI_TRB_IOC (1u << 5)
#define XHCI_POLL_LIMIT 1000000u

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

static rix_xhci_controller_t controllers[XHCI_MAX];
static size_t count;

static volatile uint8_t *map_regs(uint64_t bar) {
    uint64_t page = bar & ~0xFFFULL;
    if (vmm_map_page(page, page, RIXURI_PTE_PRESENT | RIXURI_PTE_WRITE | RIXURI_PTE_NX) != 0) return NULL;
    return (volatile uint8_t *)(uintptr_t)page;
}

static void zero_page(uint64_t phys) {
    volatile uint8_t *p = (volatile uint8_t *)(uintptr_t)phys;
    for (size_t i = 0; i < 4096; ++i) p[i] = 0;
}

static int wait_halted(volatile uint8_t *op, int halted) {
    volatile uint32_t *sts = (volatile uint32_t *)(op + XHCI_USBSTS);
    for (uint32_t i = 0; i < XHCI_POLL_LIMIT; ++i) {
        int is_halted = ((*sts & XHCI_STS_HCH) != 0u);
        if (is_halted == halted) return (*sts & XHCI_STS_HSE) ? -2 : 0;
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

static int setup_runtime(rix_xhci_controller_t *c, volatile uint8_t *base) {
    uint64_t dcbaa = pmm_alloc_page();
    uint64_t cmd_ring = pmm_alloc_page();
    uint64_t event_ring = pmm_alloc_page();
    uint64_t erst = pmm_alloc_page();
    if (!dcbaa || !cmd_ring || !event_ring || !erst) {
        if (dcbaa) pmm_free_page(dcbaa);
        if (cmd_ring) pmm_free_page(cmd_ring);
        if (event_ring) pmm_free_page(event_ring);
        if (erst) pmm_free_page(erst);
        return -1;
    }
    zero_page(dcbaa); zero_page(cmd_ring); zero_page(event_ring); zero_page(erst);

    /* One 4-KiB segment gives 64 TRBs. The final command TRB links back to 0. */
    volatile xhci_trb_t *ring = (volatile xhci_trb_t *)(uintptr_t)cmd_ring;
    ring[63].parameter_lo = (uint32_t)cmd_ring;
    ring[63].parameter_hi = (uint32_t)(cmd_ring >> 32);
    ring[63].control = (XHCI_TRB_LINK << 10) | XHCI_TRB_TC;

    volatile xhci_erst_entry_t *entry = (volatile xhci_erst_entry_t *)(uintptr_t)erst;
    entry[0].ring_segment_base = event_ring;
    entry[0].ring_segment_size = 256;

    volatile uint64_t *dcbaa_ptr = (volatile uint64_t *)(uintptr_t)dcbaa;
    dcbaa_ptr[0] = 0;

    volatile uint64_t *dcbaap = (volatile uint64_t *)(base + XHCI_DCBAAP);
    volatile uint64_t *crcr = (volatile uint64_t *)(base + XHCI_CRCR);
    *dcbaap = dcbaa;
    *crcr = cmd_ring | 1u; /* command ring cycle state = 1 */

    uint32_t db_off = *(volatile uint32_t *)(base + XHCI_DBOFF) & ~0x3u;
    uint32_t rt_off = *(volatile uint32_t *)(base + XHCI_RTSOFF) & ~0x1Fu;
    volatile uint8_t *runtime = base + rt_off;
    volatile uint32_t *ir0 = (volatile uint32_t *)runtime;
    (void)ir0;
    volatile uint32_t *iman = (volatile uint32_t *)(runtime + 0x20);
    volatile uint32_t *erstsz = (volatile uint32_t *)(runtime + 0x28);
    volatile uint64_t *erstba = (volatile uint64_t *)(runtime + 0x30);
    volatile uint64_t *erdp = (volatile uint64_t *)(runtime + 0x38);
    *iman &= ~1u;
    *erstsz = 1;
    *erstba = erst;
    *erdp = event_ring;

    /* Interrupts are enabled only after a valid event ring exists. */
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
        volatile uint8_t *base = map_regs(bar);
        if (!base) continue;

        rix_xhci_controller_t *c = &controllers[count++];
        c->bus = d->bus; c->device = d->device; c->function = d->function; c->bar0 = bar;
        c->cap_length = base[XHCI_CAPLENGTH];
        c->hci_version = *(volatile uint16_t *)(base + XHCI_HCIVERSION);
        uint32_t hcs = *(volatile uint32_t *)(base + XHCI_HCSPARAMS1);
        c->max_slots = (uint8_t)(hcs & 0xffu);
        c->max_intrs = (uint8_t)((hcs >> 8) & 0x7ffu);
        c->max_ports = (uint8_t)((hcs >> 24) & 0xffu);
        c->hcc_params1 = *(volatile uint32_t *)(base + XHCI_HCCPARAMS1);

        volatile uint8_t *op = base + c->cap_length;
        c->usbcmd = *(volatile uint32_t *)(op + XHCI_USBCMD);
        c->usbsts = *(volatile uint32_t *)(op + XHCI_USBSTS);
        c->running = 0;
        if (reset_controller(op) != 0) continue;
        if (setup_runtime(c, op) != 0) continue;
        *(volatile uint32_t *)(op + XHCI_USBCMD) |= XHCI_CMD_RS;
        if (wait_halted(op, 0) != 0) continue;
        c->usbcmd = *(volatile uint32_t *)(op + XHCI_USBCMD);
        c->usbsts = *(volatile uint32_t *)(op + XHCI_USBSTS);
        c->running = 1;
    }
    return 0;
}

size_t xhci_controller_count(void) { return count; }
const rix_xhci_controller_t *xhci_controller(size_t index) { return index < count ? &controllers[index] : NULL; }
