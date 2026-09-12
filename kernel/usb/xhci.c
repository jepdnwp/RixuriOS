#include "xhci.h"
#include "xhci_caps.h"
#include "../pci/pci.h"
#include "../mm/vmm.h"
#include "../mm/pmm.h"
#include "../serial.h"
#include "../time/time.h"
#include "kernel.h"
#include <stddef.h>

#define XHCI_MAX 4
#define XHCI_MAX_SLOTS 256u
#define PCI_CLASS_SERIAL 0x0C
#define PCI_SUBCLASS_USB 0x03
#define PCI_PROGIF_XHCI 0x30
#define PCI_VENDOR_AMD 0x1022u
#define PCI_AMD_XHCI_15B6 0x15B6u
#define PCI_AMD_XHCI_15B7 0x15B7u
#define PCI_AMD_XHCI_43F7 0x43F7u
#define PCI_AMD_XHCI_15B8 0x15B8u
#define PCI_COMMAND 0x04
#define PCI_COMMAND_MEMORY (1u << 1)
#define PCI_COMMAND_BUS_MASTER (1u << 2)
/* Upper bound for an xHCI MMIO BAR mapping. Real controllers expose tens of
 * KB; anything larger is treated as bogus firmware data instead of mapping
 * gigabytes of MMIO. */
#define XHCI_BAR_MAP_MAX 0x1000000ULL
/* xHCI extended-capability walk for the USB Legacy Support handoff. */
#define XHCI_EXT_CAP_ID_LEGACY 0x01u
#define XHCI_USBLSUP_BIOS_OWNED (1u << 16)
#define XHCI_USBLSUP_OS_OWNED (1u << 24)
#define XHCI_EXT_CAP_WALK_MAX 64u
#define XHCI_CAPLENGTH 0x00
#define XHCI_HCIVERSION 0x02
#define XHCI_HCSPARAMS1 0x04
#define XHCI_HCSPARAMS2 0x08
#define XHCI_HCCPARAMS1 0x10
#define XHCI_DBOFF 0x14
#define XHCI_RTSOFF 0x18
/* Operational-register offsets from the operational base (base+CAPLENGTH).
 * USBCMD=0x00/USBSTS=0x04 per xHCI 5.4; 0x80/0x84 was a reserved alias that
 * reads zero on QEMU but wedges real silicon (run/stop never asserted). */
#define XHCI_USBCMD 0x00
#define XHCI_USBSTS 0x04
#define XHCI_PAGESIZE 0x08
#define XHCI_DNCTRL 0x14
#define XHCI_CRCR 0x18
#define XHCI_DCBAAP 0x30
#define XHCI_CONFIG 0x38
#define XHCI_PORTSC_BASE 0x400
#define XHCI_PORT_STRIDE 0x10
#define XHCI_PORT_CCS (1u << 0)
#define XHCI_PORT_PED (1u << 1)
#define XHCI_PORT_OCA (1u << 3)
#define XHCI_PORT_PR (1u << 4)
#define XHCI_PORT_PLS_MASK (0xFu << 5)
#define XHCI_PORT_PP (1u << 9)
#define XHCI_PORT_SPEED_SHIFT 10
#define XHCI_PORT_SPEED_MASK (0xFu << XHCI_PORT_SPEED_SHIFT)
#define XHCI_PORT_LWS (1u << 16)
#define XHCI_PORT_WPR (1u << 31)
#define XHCI_PORT_CSC (1u << 17)
#define XHCI_PORT_PEC (1u << 18)
#define XHCI_PORT_WRC (1u << 19)
#define XHCI_PORT_OCC (1u << 20)
#define XHCI_PORT_PRC (1u << 21)
#define XHCI_PORT_PLC (1u << 22)
#define XHCI_PORT_CEC (1u << 23)
/* All port-change W1C bits cleared by writing 1. */
#define XHCI_PORT_CHANGE_MASK (XHCI_PORT_CSC | XHCI_PORT_PEC | XHCI_PORT_WRC | \
                               XHCI_PORT_OCC | XHCI_PORT_PRC | XHCI_PORT_PLC | \
                               XHCI_PORT_CEC)
#define XHCI_CMD_RS (1u << 0)
#define XHCI_CMD_HCRST (1u << 1)
#define XHCI_CMD_INTE (1u << 2)
#define XHCI_STS_HCH (1u << 0)
#define XHCI_STS_HSE (1u << 2)
#define XHCI_STS_EINT (1u << 3)
#define XHCI_STS_PCD (1u << 4)
#define XHCI_STS_CNR (1u << 11)
#define XHCI_STS_HCE (1u << 12)
#define XHCI_STS_W1C_MASK (XHCI_STS_HSE | XHCI_STS_EINT | XHCI_STS_PCD)
#define XHCI_TRB_LINK 6u
#define XHCI_TRB_NORMAL 1u
#define XHCI_TRB_SETUP_STAGE 2u
#define XHCI_TRB_DATA_STAGE 3u
#define XHCI_TRB_STATUS_STAGE 4u
#define XHCI_TRB_TRANSFER_EVENT 32u
#define XHCI_TRB_PORT_STATUS_CHANGE 34u
#define XHCI_TRB_CONFIGURE_ENDPOINT 12u
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
#define XHCI_COMPLETION_CONTEXT_STATE 11u
/* Real-silicon timing: port reset needs ~50ms (USB2 TDRST) plus link
 * training; command/event polls must survive millisecond stalls. The raw
 * iteration bound stays, but every poll now paces with pause + delay so the
 * bound covers hundreds of milliseconds instead of microseconds. */
#define XHCI_POLL_LIMIT 1000000u
#define XHCI_RESET_POLL_LIMIT 5000000u
#define XHCI_CMD_RING_TRBS 64u
#define XHCI_EVENT_RING_TRBS 64u
#define XHCI_ERDP_EHB (1ULL << 3)
#define XHCI_HCC_AC64 (1u << 0)
#define XHCI_HCC_CSZ (1u << 2)
#define XHCI_HCC_PPC (1u << 3)
#define XHCI_INPUT_ADD_SLOT (1u << 1)
#define XHCI_INPUT_ADD_EP0 (1u << 2)
#define XHCI_SLOT_CONTEXT_ENTRIES (1u << 27)
#define XHCI_SLOT_CONTEXT_ENTRIES_MASK (0x1Fu << 27)
#define XHCI_EP0_TYPE_CONTROL 4u
#define XHCI_EP0_CERR 3u
#define XHCI_EP_INTERRUPT_IN 7u
#define XHCI_EP_INTERRUPT_OUT 3u
#define XHCI_EP_BULK_IN 6u
#define XHCI_EP_BULK_OUT 2u

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
    uint64_t ring_phys;
    uint8_t type;
    uint8_t cycle;
    uint16_t enqueue;
} xhci_endpoint_runtime_t;

typedef struct {
    uint8_t allocated;
    uint8_t addressed;
    uint8_t port;
    uint8_t speed;
    uint32_t route_string;
    uint64_t device_context_phys;
    uint64_t input_context_phys;
    uint64_t ep0_ring_phys;
    uint16_t ep0_enqueue;
    uint8_t ep0_cycle;
    xhci_endpoint_runtime_t endpoints[32];
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
static int is_xhci_device(const rix_pci_device_t *d) {
    if (!d) return 0;
    /* The B650 chipset exposes several AMD functions with xHCI-like IDs.
     * Do not probe those as controllers unless PCI class/progif confirms
     * serial-bus USB xHCI; probing a false positive reads unrelated BARs. */
    return d->class_code == PCI_CLASS_SERIAL &&
           d->subclass == PCI_SUBCLASS_USB &&
           d->prog_if == PCI_PROGIF_XHCI;
}

/* Phase H6: the known-silicon table moved to xhci_profile.c (pure data,
 * host-tested in tests/xhci_profile_test.c). This TU only consumes it: the
 * profile names the part for the boot log, carries the expectations the
 * controller is verified against, and supplies the observed-failing-port
 * list that orders boot-device port selection. */
static int xhci_has_usb3_range(const rix_xhci_controller_t *c) {
    for (unsigned i = 0; i < c->proto_ranges && i < XHCI_SPC_MAX_RANGES; ++i)
        if (c->proto_major[i] == 3u) return 1;
    return 0;
}
/* PCI IDs and xHCI interface versions are 4 hex digits; serial_write_hex
 * pads to 16, which is unreadable for an ID, so print bare nibbles here. */
static void xhci_write_hex4(uint16_t v) {
    static const char digits[] = "0123456789abcdef";
    char buf[5];
    buf[0] = digits[(v >> 12) & 0xfu];
    buf[1] = digits[(v >> 8) & 0xfu];
    buf[2] = digits[(v >> 4) & 0xfu];
    buf[3] = digits[v & 0xfu];
    buf[4] = 0;
    serial_write(buf);
}
static const xhci_profile_t *xhci_controller_profile(const rix_xhci_controller_t *c) {
    return c ? xhci_profile_lookup(c->vendor_id, c->device_id) : NULL;
}
/* Apply the profile's quirk flags. The USB2-only flag is honored only when
 * the controller's own Supported-Protocol walk confirms it has no USB3
 * range; if the two disagree the controller wins, the quirk stays off and
 * xhci_init logs the mismatch. A profile must never override silicon. */
static uint32_t xhci_apply_profile_quirks(const rix_xhci_controller_t *c) {
    const xhci_profile_t *profile = xhci_controller_profile(c);
    if (!profile) return XHCI_PROFILE_QUIRK_NONE;
    return ((profile->quirks & XHCI_PROFILE_QUIRK_USB2_ONLY) && !xhci_has_usb3_range(c))
               ? XHCI_PROFILE_QUIRK_USB2_ONLY
               : XHCI_PROFILE_QUIRK_NONE;
}
/* Phase H4: Supported-Protocol walk over MMIO (handoff-walk pattern).
 * Snapshots 16 bytes per entry, decodes pure. Stops at the first
 * malformed step (never wedges init on firmware garbage). */
static void xhci_scan_protocols(rix_xhci_controller_t *c, volatile uint8_t *base, uint64_t mmio_size) {
    uint32_t hcc;
    uint32_t xecp;
    if (!c || !base || mmio_size < (uint64_t)XHCI_HCCPARAMS1 + 4u) return;
    hcc = *(volatile uint32_t *)(base + XHCI_HCCPARAMS1);
    xecp = ((hcc >> 16) & 0xffffu) * 4u;
    for (uint32_t step = 0; step < XHCI_EXT_CAP_WALK_MAX; ++step) {
        uint8_t raw[XHCI_SPC_ENTRY_SIZE];
        uint32_t header, next;
        xhci_proto_range_t range;
        if (!xecp || xecp + XHCI_SPC_ENTRY_SIZE > mmio_size) return;
        header = *(volatile uint32_t *)(base + xecp);
        if ((header & 0xffu) != XHCI_EXT_CAP_ID_PROTOCOL) {
            next = ((header >> 8) & 0xffu) * 4u;
            if (!next) return;
            xecp += next;
            continue;
        }
        for (unsigned b = 0; b < XHCI_SPC_ENTRY_SIZE; ++b) raw[b] = *(volatile uint8_t *)(base + xecp + b);
        if (xhci_spc_decode_entry(raw, &range) == 0 &&
            c->proto_ranges < XHCI_SPC_MAX_RANGES) {
            unsigned n = c->proto_ranges++;
            c->proto_major[n] = range.major;
            c->proto_start[n] = range.start;
            c->proto_count[n] = range.count;
        }
        next = ((header >> 8) & 0xffu) * 4u;
        if (!next) return;
        xecp += next;
    }
}
int xhci_port_protocol(size_t controller, uint8_t port) {
    if (controller >= count || port == 0u) return 0;
    const rix_xhci_controller_t *c = &controllers[controller];
    for (unsigned i = 0; i < c->proto_ranges && i < XHCI_SPC_MAX_RANGES; ++i)
        if (port >= c->proto_start[i] &&
            (unsigned)port < (unsigned)c->proto_start[i] + (unsigned)c->proto_count[i])
            return c->proto_major[i];
    return 0;
}
/* Take OS ownership from firmware via the USB Legacy Support capability.
 * No-op when the controller reports no extended capabilities. Bounded:
 * at most XHCI_EXT_CAP_WALK_MAX capability steps and a paced handoff
 * poll (~100ms worst case — a tight loop expires before a slow BIOS
 * SMI releases; never wedges the boot on a stuck semaphore).
 * Sets *was_owned when BIOS ownership was ever observed (diagnostic:
 * tells OS-takeover apart from never-owned on the boot log). */
static void xhci_udelay(uint32_t us);
static int xhci_bios_handoff(volatile uint8_t *base, uint64_t mmio_size,
                             uint32_t *was_owned) {
    uint32_t hcc;
    uint32_t xecp;
    if (was_owned) *was_owned = 0;
    if (!base || !mmio_size) return -1;
    if (mmio_size < (uint64_t)XHCI_HCCPARAMS1 + 4u) return -1;
    hcc = *(volatile uint32_t *)(base + XHCI_HCCPARAMS1);
    xecp = ((hcc >> 16) & 0xffffu) * 4u;
    for (uint32_t step = 0; step < XHCI_EXT_CAP_WALK_MAX; ++step) {
        uint32_t header, next;
        volatile uint32_t *legsup;
        if (!xecp || xecp + 4u > mmio_size) return 0;
        header = *(volatile uint32_t *)(base + xecp);
        if ((header & 0xffu) != XHCI_EXT_CAP_ID_LEGACY) {
            next = ((header >> 8) & 0xffu) * 4u;
            if (!next) return 0;
            xecp += next;
            continue;
        }
        legsup = (volatile uint32_t *)(base + xecp);
        if (!(*legsup & XHCI_USBLSUP_BIOS_OWNED)) return 0;
        if (was_owned) *was_owned = 1;
        *legsup |= XHCI_USBLSUP_OS_OWNED;
        for (uint32_t i = 0; i < 1000000u; ++i) {
            if (!(*legsup & XHCI_USBLSUP_BIOS_OWNED)) return 0;
            if ((i & 0x3ffu) == 0u) xhci_udelay(100u);
        }
        return -2;
    }
    return -3;
}

static uint64_t map_range(uint64_t base, uint64_t length) {
    if (length == 0 || base > UINT64_MAX - (length - 1u)) return 0;
    return vmm_map_mmio(base, length);
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

/* Pre-PIT busy delay: works both during xhci_init (PIT not up yet) and in
 * the hotplug worker. ~1us per 64 pauses is a rough underestimate on modern
 * cores, which is fine: we only need wall-time pacing, not precision. */
static void xhci_pause_delay(uint32_t pauses) {
    for (uint32_t i = 0; i < pauses; ++i) __asm__ volatile("pause" ::: "memory");
}

static void xhci_udelay(uint32_t us) {
    /* ~64 pauses ~= 1us; split to keep each loop bounded. */
    while (us--) xhci_pause_delay(64u);
}

static int wait_halted(volatile uint8_t *op, int halted) {
    volatile uint32_t *sts = (volatile uint32_t *)(op + XHCI_USBSTS);
    for (uint32_t i = 0; i < XHCI_POLL_LIMIT; ++i) {
        uint32_t s = *sts;
        int is_halted = (s & XHCI_STS_HCH) != 0u;
        if (is_halted == halted) return (s & XHCI_STS_HSE) ? -2 : 0;
        if ((i & 0xffu) == 0u) xhci_pause_delay(64u);
    }
    return -1;
}

/* Wait for Controller-Not-Ready to clear after reset. Programming op/runtime
 * regs while CNR=1 is ignored on real silicon and later surfaces as
 * Enable-Slot timeouts (hotplug error=6). */
static int wait_cnr_clear(volatile uint8_t *op) {
    volatile uint32_t *sts = (volatile uint32_t *)(op + XHCI_USBSTS);
    for (uint32_t i = 0; i < XHCI_RESET_POLL_LIMIT; ++i) {
        if ((*sts & XHCI_STS_CNR) == 0u) return 0;
        if ((i & 0x3ffu) == 0u) xhci_udelay(10u);
    }
    return -1;
}

static int reset_controller(volatile uint8_t *op) {
    volatile uint32_t *cmd = (volatile uint32_t *)(op + XHCI_USBCMD);
    volatile uint32_t *sts = (volatile uint32_t *)(op + XHCI_USBSTS);
    uint32_t cmd_v = *cmd;
    cmd_v &= ~(XHCI_CMD_RS | XHCI_CMD_INTE);
    *cmd = cmd_v;
    if (wait_halted(op, 1) != 0) return -1;
    *cmd |= XHCI_CMD_HCRST;
    for (uint32_t i = 0; i < XHCI_RESET_POLL_LIMIT; ++i) {
        uint32_t v = *cmd;
        uint32_t s = *sts;
        if ((s & XHCI_STS_HSE) != 0u) return -2;
        if ((v & XHCI_CMD_HCRST) == 0u) return wait_cnr_clear(op);
        if ((i & 0x3ffu) == 0u) xhci_udelay(10u);
    }
    return -3;
}

static uint64_t scratchpad_array_phys[XHCI_MAX];

static void release_runtime_pages(rix_xhci_controller_t *c) {
    size_t idx = (size_t)(c - controllers);
    if (idx < XHCI_MAX && scratchpad_array_phys[idx]) {
        volatile uint64_t *arr =
            (volatile uint64_t *)(uintptr_t)scratchpad_array_phys[idx];
        /* Best-effort: array page holds buffer addresses; free what we can.
         * Count is re-derived from caps when available. */
        for (uint32_t i = 0; i < 64u; ++i) {
            uint64_t buf = arr[i];
            if (buf && !(buf & 0xfffu)) pmm_free_page(buf);
            else if (buf) break;
            if (!buf && i > 0) {
                /* Heuristic stop: trailing zeros after first buffers. */
                int rest_zero = 1;
                for (uint32_t j = i + 1u; j < i + 8u && j < 64u; ++j)
                    if (arr[j]) { rest_zero = 0; break; }
                if (rest_zero) break;
            }
        }
        pmm_free_page(scratchpad_array_phys[idx]);
        scratchpad_array_phys[idx] = 0;
    }
    if (c->dcbaa_phys) pmm_free_page(c->dcbaa_phys);
    if (c->cmd_ring_phys) pmm_free_page(c->cmd_ring_phys);
    if (c->event_ring_phys) pmm_free_page(c->event_ring_phys);
    if (c->erst_phys) pmm_free_page(c->erst_phys);
    c->dcbaa_phys = 0;
    c->cmd_ring_phys = 0;
    c->event_ring_phys = 0;
    c->erst_phys = 0;
}

static int setup_runtime(rix_xhci_controller_t *c, volatile uint8_t *cap,
                         volatile uint8_t *op, xhci_runtime_t *rt) {
    size_t controller_index = (size_t)(c - controllers);
    /* PAGESIZE must advertise 4K support (bit0). Without it every DMA page
     * we allocate is unusable on real silicon. */
    {
        uint32_t pagesize = *(volatile uint32_t *)(op + XHCI_PAGESIZE);
        if ((pagesize & 1u) == 0u) {
            serial_write("xHCI: PAGESIZE lacks 4K support\r\n");
            return -5;
        }
    }
    /* Scratchpad: HCSPARAMS2 advertises N buffers; DCBAA[0] must point at an
     * array of N 64-bit buffer addresses. QEMU reports 0, real AMD/Intel
     * parts need several. Missing scratchpad surfaces later as Enable-Slot
     * timeouts (hotplug error=6). */
    uint32_t hcs2 = *(volatile uint32_t *)(cap + XHCI_HCSPARAMS2);
    uint32_t sp_hi = (hcs2 >> 21) & 0x1fu;
    uint32_t sp_lo = (hcs2 >> 27) & 0x1fu;
    uint32_t scratch_count = (sp_hi << 5) | sp_lo;
    if (scratch_count > 512u) {
        serial_write("xHCI: implausible scratchpad count\r\n");
        return -6;
    }
    uint64_t dcbaa = dma_page(c);
    uint64_t cmd_ring = dma_page(c);
    uint64_t event_ring = dma_page(c);
    uint64_t erst = dma_page(c);
    uint64_t scratch_array = 0;
    if (!dcbaa || !cmd_ring || !event_ring || !erst ||
        (scratch_count && !(scratch_array = dma_page(c)))) {
        if (dcbaa) pmm_free_page(dcbaa);
        if (cmd_ring) pmm_free_page(cmd_ring);
        if (event_ring) pmm_free_page(event_ring);
        if (erst) pmm_free_page(erst);
        if (scratch_array) pmm_free_page(scratch_array);
        return -1;
    }
    zero_page(dcbaa);
    zero_page(cmd_ring);
    zero_page(event_ring);
    zero_page(erst);
    if (scratch_array) zero_page(scratch_array);
    /* Allocate one DMA page per scratchpad buffer (buffers are 4K). */
    uint64_t scratch_bufs[64];
    uint32_t scratch_allocated = 0;
    if (scratch_count) {
        if (scratch_count > 64u) {
            /* Array page holds 512 entries; buffer count above 64 would
             * need multi-page tracking this early driver does not keep.
             * Fail closed instead of programming a partial array. */
            serial_write("xHCI: scratchpad count exceeds early-driver limit\r\n");
            pmm_free_page(dcbaa);
            pmm_free_page(cmd_ring);
            pmm_free_page(event_ring);
            pmm_free_page(erst);
            pmm_free_page(scratch_array);
            return -7;
        }
        for (uint32_t i = 0; i < scratch_count; ++i) {
            uint64_t buf = dma_page(c);
            if (!buf) break;
            zero_page(buf);
            scratch_bufs[i] = buf;
            scratch_allocated++;
        }
        if (scratch_allocated != scratch_count) {
            for (uint32_t i = 0; i < scratch_allocated; ++i)
                pmm_free_page(scratch_bufs[i]);
            pmm_free_page(dcbaa);
            pmm_free_page(cmd_ring);
            pmm_free_page(event_ring);
            pmm_free_page(erst);
            pmm_free_page(scratch_array);
            return -1;
        }
        volatile uint64_t *array = (volatile uint64_t *)(uintptr_t)scratch_array;
        for (uint32_t i = 0; i < scratch_count; ++i) array[i] = scratch_bufs[i];
        __asm__ volatile("mfence" ::: "memory");
    }
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
    dcbaa_ptr[0] = scratch_array;

    volatile uint64_t *dcbaap = (volatile uint64_t *)(op + XHCI_DCBAAP);
    volatile uint64_t *crcr = (volatile uint64_t *)(op + XHCI_CRCR);
    *dcbaap = dcbaa;
    *crcr = cmd_ring | XHCI_TRB_CYCLE;

    /* Clear stale status (W1C) and disable device notifications: polling
     * driver, no notification handler. */
    *(volatile uint32_t *)(op + XHCI_USBSTS) = XHCI_STS_W1C_MASK;
    *(volatile uint32_t *)(op + XHCI_DNCTRL) = 0u;
    __asm__ volatile("mfence" ::: "memory");

    /* DBOFF and RTSOFF are capability-register offsets; CRCR/DCBAAP and
     * CONFIG above are operational-register offsets. */
    uint32_t db_off = *(volatile uint32_t *)(cap + XHCI_DBOFF) & ~0x3u;
    uint32_t rt_off = *(volatile uint32_t *)(cap + XHCI_RTSOFF) & ~0x1Fu;
    if (rt_off < c->cap_length) {
        pmm_free_page(dcbaa);
        pmm_free_page(cmd_ring);
        pmm_free_page(event_ring);
        pmm_free_page(erst);
        if (scratch_array) {
            volatile uint64_t *arr = (volatile uint64_t *)(uintptr_t)scratch_array;
            for (uint32_t i = 0; i < scratch_count; ++i)
                if (arr[i]) pmm_free_page(arr[i]);
            pmm_free_page(scratch_array);
        }
        return -2;
    }
    volatile uint8_t *runtime = cap + rt_off;
    volatile uint32_t *iman = (volatile uint32_t *)(runtime + 0x20);
    volatile uint32_t *erstsz = (volatile uint32_t *)(runtime + 0x28);
    volatile uint64_t *erstba = (volatile uint64_t *)(runtime + 0x30);
    volatile uint64_t *erdp = (volatile uint64_t *)(runtime + 0x38);
    /* Polling mode: clear any pending IP (W1C) and keep IE disabled. There
     * is no xHCI IRQ handler routed, so IE=1 would leave Event-Interrupt
     * pending on real silicon. */
    {
        uint32_t iman_v = *iman;
        iman_v &= ~(1u << 1);
        iman_v |= (1u << 0);
        *iman = iman_v;
    }
    *erstsz = 1u;
    *erstba = erst;
    *erdp = event_ring | XHCI_ERDP_EHB;
    {
        uint32_t iman_v = *iman;
        iman_v &= ~(1u << 1);
        *iman = iman_v;
    }

    volatile uint32_t *config = (volatile uint32_t *)(op + XHCI_CONFIG);
    *config = c->max_slots;
    volatile uint32_t *db0 = (volatile uint32_t *)(cap + db_off);
    *db0 = 0;

    c->dcbaa_phys = dcbaa;
    c->cmd_ring_phys = cmd_ring;
    c->event_ring_phys = event_ring;
    c->erst_phys = erst;
    scratchpad_array_phys[controller_index] = scratch_array;
    c->running = 0;
    return 0;
}

/* Ensure every port is powered (PP=1). After HCRST real-silicon ports come
 * up unpowered when HCC PPC=1; CCS never asserts and port reset fails
 * (hotplug error=4). QEMU ignores PP, which is why this only bites on
 * hardware. Safe when PPC=0 (write ignored / already 1). */
static void xhci_power_all_ports(const rix_xhci_controller_t *c) {
    if (!c || !c->mmio_va || !c->max_ports) return;
    volatile uint8_t *base = (volatile uint8_t *)(uintptr_t)c->mmio_va;
    for (uint8_t port = 1; port <= c->max_ports; ++port) {
        volatile uint32_t *reg = (volatile uint32_t *)(base + c->cap_length +
            XHCI_PORTSC_BASE + (uint32_t)(port - 1u) * XHCI_PORT_STRIDE);
        uint32_t v = *reg;
        if (v & XHCI_PORT_PP) continue;
        /* Set PP, keep PED/PLS untouched (LWS=0, PR=0), clear stale W1C. */
        v = (v & ~(XHCI_PORT_PR | XHCI_PORT_LWS)) |
            XHCI_PORT_PP | XHCI_PORT_CHANGE_MASK;
        *reg = v;
    }
    /* USB 2.0 power-stable delay (TPPWR ~20ms). */
    xhci_udelay(20000u);
}

int xhci_init(void) {
    count = 0;
    for (size_t i = 0; i < pci_device_count() && count < XHCI_MAX; ++i) {
        const rix_pci_device_t *d = pci_device(i);
        if (!is_xhci_device(d)) continue;
        /* Phase H6: name the part here too, not only on the success line, so
         * a controller that fails before becoming ctl=N is still
         * identifiable from the log. */
        {
            const xhci_profile_t *candidate_profile =
                xhci_profile_lookup(d->vendor_id, d->device_id);
            serial_write("xHCI: AMD/PCI candidate ");
            xhci_write_hex4(d->vendor_id);
            serial_write(":");
            xhci_write_hex4(d->device_id);
            serial_write(" bus=");
            serial_write_dec(d->bus);
            serial_write(" dev=");
            serial_write_dec(d->device);
            serial_write(" fn=");
            serial_write_dec(d->function);
            serial_write(" ");
            serial_write(candidate_profile ? candidate_profile->name : "unknown");
            serial_write("\r\n");
        }
        {
            uint32_t cmd_save = pci_config_read32(d->bus, d->device, d->function, PCI_COMMAND);
            uint64_t bar_size = 0, bar_base = 0;
            int bar_io = 0, bar_rc;
            /* Quiesce decode while sizing so a firmware-active controller is
             * never observed with an all-ones BAR address. */
            pci_config_write32(d->bus, d->device, d->function, PCI_COMMAND,
                               cmd_save & ~(PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER));
            bar_rc = pci_bar_size(d, 0, &bar_size, &bar_base, &bar_io);
            pci_config_write32(d->bus, d->device, d->function, PCI_COMMAND, cmd_save);
            if (bar_rc != 0 || bar_io || !bar_base || !bar_size) {
                serial_write("xHCI: candidate BAR unavailable\r\n");
                continue;
            }
            if (bar_size > XHCI_BAR_MAP_MAX) {
                serial_write("xHCI: candidate BAR size implausible\r\n");
                continue;
            }
            uint64_t regs_va = map_range(bar_base, bar_size);
            if (!regs_va) {
                serial_write("xHCI: candidate BAR map failed\r\n");
                continue;
            }
            {
                uint32_t command = pci_config_read32(d->bus, d->device, d->function, PCI_COMMAND);
                command |= PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER;
                if (pci_config_write32(d->bus, d->device, d->function, PCI_COMMAND, command) != 0) {
                    serial_write("xHCI: candidate bus-master enable failed\r\n");
                    continue;
                }
                command = pci_config_read32(d->bus, d->device, d->function, PCI_COMMAND);
                if ((command & (PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER)) !=
                    (PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER)) {
                    serial_write("xHCI: candidate decode enable rejected\r\n");
                    continue;
                }
            }
            {
                volatile uint8_t *base = (volatile uint8_t *)(uintptr_t)regs_va;
                rix_xhci_controller_t *c = &controllers[count];
                int handoff;
                c->bus = d->bus;
                c->device = d->device;
                c->function = d->function;
                c->vendor_id = d->vendor_id;
                c->device_id = d->device_id;
                c->bar0 = bar_base;
                c->mmio_va = regs_va;
                c->cap_length = base[XHCI_CAPLENGTH];
                c->hci_version = *(volatile uint16_t *)(base + XHCI_HCIVERSION);
                {
                    uint32_t hcs = *(volatile uint32_t *)(base + XHCI_HCSPARAMS1);
                    c->max_slots = (uint8_t)(hcs & 0xffu);
                    c->max_intrs = (uint8_t)((hcs >> 8) & 0x7ffu);
                    c->max_ports = (uint8_t)((hcs >> 24) & 0xffu);
                    c->hcc_params1 = *(volatile uint32_t *)(base + XHCI_HCCPARAMS1);
                }
                if (c->cap_length < 0x20u || c->max_slots == 0u || c->max_ports == 0u) {
                    serial_write("xHCI: candidate reports no usable registers/slots/ports\r\n");
                    continue;
                }
                /* Phase H4: protocol map before handoff/runtime (MMIO
                 * reads only; never fails init). */
                xhci_scan_protocols(c, base, bar_size);
                /* Phase H6: resolve the profile now that the controller has
                 * described itself (HCSPARAMS1 + protocol walk), and apply
                 * only the quirks those capabilities confirm. */
                c->quirks = xhci_apply_profile_quirks(c);
                const xhci_profile_t *profile = xhci_controller_profile(c);
                unsigned profile_verdict = xhci_profile_verify(profile, c->max_ports,
                                                               (uint16_t)c->hci_version,
                                                               xhci_has_usb3_range(c));
                {
                    uint32_t was_owned = 0;
                    handoff = xhci_bios_handoff(base, bar_size, &was_owned);
                    serial_write("xHCI: handoff BIOS-owned=");
                    serial_write_dec(was_owned);
                    serial_write(" rc=");
                    serial_write_dec((uint64_t)(handoff < 0 ? -handoff : handoff));
                    serial_write("\r\n");
                }
                if (handoff != 0) {
                    serial_write("xHCI: candidate BIOS handoff failed\r\n");
                    continue;
                }
                {
                    volatile uint8_t *op = base + c->cap_length;
                    c->usbcmd = *(volatile uint32_t *)(op + XHCI_USBCMD);
                    c->usbsts = *(volatile uint32_t *)(op + XHCI_USBSTS);
                    c->running = 0;
                    if (reset_controller(op) != 0) {
                        serial_write("xHCI: candidate reset failed\r\n");
                        continue;
                    }
                    int runtime_rc = setup_runtime(c, base, op, &runtimes[count]);
                    if (runtime_rc != 0) {
                        serial_write("xHCI: candidate runtime failed rc=");
                        serial_write_dec((uint64_t)(runtime_rc < 0 ? -runtime_rc : runtime_rc));
                        serial_write(" free=");
                        serial_write_dec(pmm_free_pages());
                        serial_write(" ac64=");
                        serial_write_dec((uint64_t)((c->hcc_params1 & XHCI_HCC_AC64) != 0u));
                        serial_write("\r\n");
                        continue;
                    }
                    xhci_power_all_ports(c);
                    *(volatile uint32_t *)(op + XHCI_USBCMD) |= XHCI_CMD_RS;
                    if (wait_halted(op, 0) != 0 || wait_cnr_clear(op) != 0) {
                        release_runtime_pages(c);
                        serial_write("xHCI: candidate run failed\r\n");
                        continue;
                    }
                    /* Power may have been lost across RS on some parts;
                     * enforce PP again now that the controller runs. */
                    xhci_power_all_ports(c);
                    c->usbcmd = *(volatile uint32_t *)(op + XHCI_USBCMD);
                    c->usbsts = *(volatile uint32_t *)(op + XHCI_USBSTS);
                    c->running = 1;
                    serial_write("xHCI: controller="); serial_write_dec(count);
                    serial_write(" PCI="); serial_write_hex(d->vendor_id);
                    serial_write(":"); serial_write_hex(d->device_id);
                    serial_write(" BAR="); serial_write_hex(bar_base);
                    serial_write(" operational="); serial_write_hex(bar_base + c->cap_length);
                    serial_write(" ports="); serial_write_dec(c->max_ports);
                    serial_write("\r\n");
                    /* Phase H6: identity line. The ctl index depends on the
                     * kernel's PCI scan order, so bus/dev/fn + PCI ID + port
                     * count are printed together with the part name: this one
                     * line is what maps a ctl number to physical silicon. The
                     * profile verdict compares the Windows-side board facts
                     * against what this controller reported; a mismatch is
                     * logged, never silently accepted. */
                    {
                        serial_write("xHCI: ctl=");
                        serial_write_dec(count);
                        serial_write(" bus=");
                        serial_write_dec(d->bus);
                        serial_write(" dev=");
                        serial_write_dec(d->device);
                        serial_write(" fn=");
                        serial_write_dec(d->function);
                        serial_write(" id=");
                        xhci_write_hex4(c->vendor_id);
                        serial_write(":");
                        xhci_write_hex4(c->device_id);
                        serial_write(" hci=");
                        xhci_write_hex4((uint16_t)c->hci_version);
                        serial_write(" slots=");
                        serial_write_dec(c->max_slots);
                        serial_write(" ports=");
                        serial_write_dec(c->max_ports);
                        serial_write(" ");
                        serial_write(profile ? profile->name : "unknown");
                        serial_write(" quirks=");
                        serial_write((c->quirks & XHCI_PROFILE_QUIRK_USB2_ONLY)
                                         ? "usb2-only"
                                         : "none");
                        serial_write(" profile=");
                        if (!profile) serial_write("unknown");
                        else if (profile_verdict == XHCI_PROFILE_OK) serial_write("ok");
                        else {
                            serial_write("mismatch");
                            if (profile_verdict & XHCI_PROFILE_VERIFY_PORTS) {
                                serial_write(" ports=");
                                serial_write_dec(c->max_ports);
                                serial_write("/");
                                serial_write_dec(profile->expected_ports);
                            }
                            if (profile_verdict & XHCI_PROFILE_VERIFY_HCI) {
                                serial_write(" hci=");
                                xhci_write_hex4((uint16_t)c->hci_version);
                                serial_write("/");
                                xhci_write_hex4(profile->expected_hci);
                            }
                            if (profile_verdict & XHCI_PROFILE_VERIFY_USB3)
                                serial_write(" usb3-range-on-usb2-only-id");
                        }
                        serial_write(" proto=");
                        if (!c->proto_ranges) serial_write("unknown");
                        for (unsigned pi = 0; pi < c->proto_ranges && pi < XHCI_SPC_MAX_RANGES; ++pi) {
                            if (pi) serial_write(",");
                            serial_write("U");
                            serial_write_dec(c->proto_major[pi]);
                            serial_write(":");
                            serial_write_dec(c->proto_start[pi]);
                            serial_write("-");
                            serial_write_dec((uint64_t)c->proto_start[pi] + (uint64_t)c->proto_count[pi] - 1u);
                        }
                        serial_write("\r\n");
                    }
                    /* Phase H6: the ports this exact board was observed
                     * failing to enable, so the log shows why a port is
                     * ordered last instead of first. Bounded by
                     * XHCI_PROFILE_BAD_PORT_MAX. */
                    if (profile && profile->bad_ports && profile->bad_port_count) {
                        serial_write("xHCI: ctl=");
                        serial_write_dec(count);
                        serial_write(" known-bad=");
                        for (uint8_t bi = 0;
                             bi < profile->bad_port_count &&
                             bi < (uint8_t)XHCI_PROFILE_BAD_PORT_MAX; ++bi) {
                            if (bi) serial_write(",");
                            serial_write_dec(profile->bad_ports[bi]);
                        }
                        serial_write("\r\n");
                    }
                    ++count;
                }
            }
        }
    }
    return 0;
}

size_t xhci_controller_count(void) { return count; }
const rix_xhci_controller_t *xhci_controller(size_t index) { return index < count ? &controllers[index] : NULL; }

static volatile uint32_t *port_reg(const rix_xhci_controller_t *c, uint8_t port) {
    if (!c || !c->running || port == 0 || port > c->max_ports) return NULL;
    volatile uint8_t *base = (volatile uint8_t *)(uintptr_t)c->mmio_va;
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

static void xhci_clear_port_change(volatile uint32_t *reg) {
    uint32_t v = *reg;
    /* Preserve RW bits, force PR=0/WPR=0/LWS=0, write 1 to all W1C to clear. */
    v = (v & ~(XHCI_PORT_PR | XHCI_PORT_WPR | XHCI_PORT_LWS)) | XHCI_PORT_CHANGE_MASK;
    /* Never clear PP here. */
    v |= (*reg & XHCI_PORT_PP);
    *reg = v;
}

/* One-time boot inventory: one line per controller plus one line per
 * connected port with raw PORTSC. kernel_log renders on the framebuffer
 * console (not just COM1), so this is visible on real hardware without a
 * serial capture. Compact on purpose: the physical console repaints lines. */
void xhci_dump_ports(void) {
    for (size_t ci = 0; ci < count; ++ci) {
        const rix_xhci_controller_t *c = &controllers[ci];
        /* Phase H6: the inventory names each controller and marks the ports
         * this board is known to fail on, so a physical port walk can be
         * matched against the running system without another build. */
        const xhci_profile_t *profile = xhci_controller_profile(c);
        kernel_log("xHCI: ctl=");
        kernel_log_dec(ci);
        kernel_log(" ports=");
        kernel_log_dec(c->max_ports);
        kernel_log(" slots=");
        kernel_log_dec(c->max_slots);
        kernel_log(" ");
        kernel_log(profile ? profile->name : "unknown");
        kernel_log("\r\n");
        for (uint8_t port = 1; port <= c->max_ports; ++port) {
            volatile uint8_t *base = (volatile uint8_t *)(uintptr_t)c->mmio_va;
            volatile uint32_t *reg = (volatile uint32_t *)(base + c->cap_length +
                XHCI_PORTSC_BASE + (uint32_t)(port - 1u) * XHCI_PORT_STRIDE);
            uint32_t v = *reg;
            if ((v & XHCI_PORT_CCS) == 0u) continue;
            kernel_log("xHCI: ctl=");
            kernel_log_dec(ci);
            kernel_log(" port=");
            kernel_log_dec(port);
            kernel_log(" PED=");
            kernel_log_dec((uint64_t)((v & XHCI_PORT_PED) != 0u));
            kernel_log(" speed=");
            kernel_log_dec((uint64_t)((v & XHCI_PORT_SPEED_MASK) >> XHCI_PORT_SPEED_SHIFT));
            kernel_log(" proto=U");
            kernel_log_dec((uint64_t)xhci_port_protocol(ci, port));
            kernel_log(" known-bad=");
            kernel_log_dec((uint64_t)xhci_profile_port_known_bad(profile, port));
            kernel_log(" PLS=");
            kernel_log_dec((uint64_t)((v & XHCI_PORT_PLS_MASK) >> 5));
            kernel_log(" PORTSC=");
            kernel_log_hex(v);
            kernel_log("\r\n");
        }
    }
}

/* USB3 (SuperSpeed) ports must NOT get a Hot Reset (PR) while the link is
 * still training: PR sticks at 1 forever (PORTSC=0x331: CCS=1 PED=0 PR=1
 * PP=1 speed=0 PLS=9 Hot Reset) and the attach times out as error=4.
 * USB3 links train on connect on their own; software only waits for PED,
 * and uses Warm Reset (WPR) when training stalls. USB2 ports always need
 * the PR sequence. Speed>=4 means SuperSpeed; speed==0 means the link has
 * not reported yet (early USB3 training or pre-reset USB2). */
static int xhci_wait_usb3_trained(volatile uint32_t *reg) {
    for (uint32_t i = 0; i < XHCI_RESET_POLL_LIMIT; ++i) {
        uint32_t s = *reg;
        if ((s & XHCI_PORT_CCS) == 0u) return -3;
        if ((s & XHCI_PORT_PED) != 0u) {
            uint32_t speed = (s & XHCI_PORT_SPEED_MASK) >> XHCI_PORT_SPEED_SHIFT;
            if (speed != 0u) {
                xhci_clear_port_change(reg);
                return 0;
            }
        }
        if ((i & 0x3ffu) == 0u) xhci_udelay(50u);
    }
    return -5;
}

static int xhci_warm_reset_port(volatile uint32_t *reg) {
    uint32_t v = (*reg & ~(XHCI_PORT_PR | XHCI_PORT_LWS)) |
        XHCI_PORT_PP | XHCI_PORT_CHANGE_MASK | XHCI_PORT_WPR;
    *reg = v;
    for (uint32_t i = 0; i < XHCI_RESET_POLL_LIMIT; ++i) {
        uint32_t s = *reg;
        if ((s & XHCI_PORT_CCS) == 0u) return -3;
        if (((s & XHCI_PORT_WPR) == 0u) || (s & XHCI_PORT_WRC) != 0u) {
            xhci_clear_port_change(reg);
            return xhci_wait_usb3_trained(reg);
        }
        if ((i & 0x3ffu) == 0u) xhci_udelay(50u);
    }
    v = *reg;
    v &= ~XHCI_PORT_WPR;
    v = (v & ~XHCI_PORT_LWS) | XHCI_PORT_CHANGE_MASK | (*reg & XHCI_PORT_PP);
    *reg = v;
    return -5;
}

/* Phase H2: force the link to RxDetect via LWS before a port reset.
 * A link stuck in Polling/Compliance (the 0x6e1/0xae1 family: speed
 * known, PED never set) ignores PR forever; asking for RxDetect
 * re-arms detection first. Ignored by ports that don't need it. */
static void xhci_force_rxdetect(volatile uint32_t *reg) {
    uint32_t v = (*reg & ~(XHCI_PORT_PR | XHCI_PORT_WPR | XHCI_PORT_LWS |
                           XHCI_PORT_PLS_MASK)) |
                 XHCI_PORT_PP | XHCI_PORT_CHANGE_MASK | XHCI_PORT_LWS |
                 (5u << 5);
    *reg = v;
    xhci_udelay(10000u);
}

/* Phase H2: power-cycle a port (off, settle, on). Some devices only
 * enumerate after a power drop; also clears electrically stuck links.
 * Returns 0 with CCS still set, -2 if the device vanished. */
static int xhci_power_cycle_port(volatile uint32_t *reg) {
    uint32_t v = (*reg & ~(XHCI_PORT_PR | XHCI_PORT_WPR | XHCI_PORT_LWS |
                           XHCI_PORT_PP)) | XHCI_PORT_CHANGE_MASK;
    *reg = v;
    xhci_udelay(100000u);
    v = (*reg & ~(XHCI_PORT_PR | XHCI_PORT_WPR | XHCI_PORT_LWS)) |
        XHCI_PORT_PP | XHCI_PORT_CHANGE_MASK;
    *reg = v;
    xhci_udelay(100000u);
    if ((*reg & XHCI_PORT_CCS) == 0u) return -2;
    return 0;
}

/* Phase H2: PED verification. A "recovered" reset without an enabled
 * link always fails downstream at Address Device, so report it as
 * failure here instead of a false success. */
static int xhci_port_enabled(volatile uint32_t *reg) {
    uint32_t s = *reg;
    return ((s & XHCI_PORT_CCS) && (s & XHCI_PORT_PED)) ? 0 : -1;
}

/* Pure USB2 bus-reset attempt (PR sequence only, no fallbacks).
 * Returns 0 with PRC observed, -3 on disconnect, -4/-5 on timeout. */
static int xhci_usb2_reset(volatile uint32_t *reg) {
    for (uint32_t i = 0; i < XHCI_RESET_POLL_LIMIT; ++i) {
        uint32_t s = *reg;
        if ((s & XHCI_STS_HSE) != 0u) { (void)s; }
        if ((s & XHCI_PORT_PR) == 0u) {
            if ((s & XHCI_PORT_PRC) != 0u) {
                xhci_clear_port_change(reg);
                /* Post-reset: device must still be present with a speed. */
                uint32_t after = *reg;
                if ((after & XHCI_PORT_CCS) == 0u) return -3;
                return 0;
            }
            if ((s & XHCI_PORT_CCS) == 0u) return -3;
            /* PR cleared without PRC: USB3 link may still be training;
             * give it a little more time before failing. */
            xhci_udelay(1000u);
            uint32_t retry = *reg;
            if (retry & XHCI_PORT_PRC) {
                xhci_clear_port_change(reg);
                return 0;
            }
            return -4;
        }
        if ((i & 0x3ffu) == 0u) xhci_udelay(50u);
    }
    /* Timeout with PR still asserted: deassert to leave the port sane. */
    xhci_clear_port_change(reg);
    return -5;
}

int xhci_reset_port(size_t controller, uint8_t port) {
    const rix_xhci_controller_t *c = xhci_controller(controller);
    volatile uint32_t *reg = port_reg(c, port);
    if (!reg) return -1;
    uint32_t v = *reg;
    /* Real silicon: unpowered ports report CCS=0 forever. Power first. */
    if ((v & XHCI_PORT_PP) == 0u) {
        v = (v & ~(XHCI_PORT_PR | XHCI_PORT_LWS)) |
            XHCI_PORT_PP | XHCI_PORT_CHANGE_MASK;
        *reg = v;
        xhci_udelay(20000u);
        v = *reg;
    }
    if ((v & XHCI_PORT_CCS) == 0u) return -2;
    /* Already enabled (trained USB3 in U0, or enabled USB2): no reset. */
    {
        uint32_t ped = v & XHCI_PORT_PED;
        uint32_t speed = (v & XHCI_PORT_SPEED_MASK) >> XHCI_PORT_SPEED_SHIFT;
        if (ped && speed) {
            xhci_clear_port_change(reg);
            return 0;
        }
        /* Phase H4: protocol-aware branch selection. A training SS
         * port can read speed 0/stale-USB2, which is exactly what
         * wedged PR before — so protocol 3 forces the SS path even
         * when the speed field disagrees, and known-USB2 ports skip
         * the training wait (their speed is valid at connect; a
         * persistent 0 is a ghost, failed fast by the PR attempt).
         * Unknown protocol keeps the legacy heuristic. Phase H6 adds
         * the profile's USB2-only fact: such a controller has no
         * SuperSpeed ports by construction, so a stale/nonzero speed
         * field must never send this port down the SS wait-train path
         * (the 0x331 wedge) and a persistent speed 0 is a ghost that
         * should fail fast at the PR attempt instead of burning the
         * 4x training window. */
        int proto = xhci_port_protocol(controller, port);
        uint32_t usb2_only = c->quirks & XHCI_PROFILE_QUIRK_USB2_ONLY;
        if (usb2_only) proto = 2;
        if (!usb2_only && (speed >= 4u || proto == 3)) {
            int wrc = xhci_wait_usb3_trained(reg);
            if (wrc == 0) return 0;
            return xhci_warm_reset_port(reg);
        }
        if (speed == 0u && proto != 2) {
            /* Ambiguous: USB3 in RxDetect/Polling (needs hundreds of ms to
             * train, PR would wedge it to 0x331), or ghost CCS with no
             * device. Give training a real window before any PR: 4x the
             * normal poll budget (~2s). Seen necessary on AMD silicon
             * where ports report CCS=1 long before the link trains. */
            for (uint32_t i = 0; i < 4u * XHCI_RESET_POLL_LIMIT; ++i) {
                uint32_t s = *reg;
                if ((s & XHCI_PORT_CCS) == 0u) return -2;
                if ((s & XHCI_PORT_PED) != 0u) break;
                uint32_t s2 = (s & XHCI_PORT_SPEED_MASK) >> XHCI_PORT_SPEED_SHIFT;
                if (s2 != 0u) break;
                if ((i & 0x3ffu) == 0u) xhci_udelay(50u);
            }
            v = *reg;
            uint32_t now_speed = (v & XHCI_PORT_SPEED_MASK) >> XHCI_PORT_SPEED_SHIFT;
            if (now_speed >= 4u || (v & XHCI_PORT_PED) != 0u) {
                int wrc = xhci_wait_usb3_trained(reg);
                if (wrc == 0) return 0;
                return xhci_warm_reset_port(reg);
            }
        }
    }
    /* Phase H2 escalation: plain USB2 reset first; then RxDetect
     * force + retry, power cycle + retry, warm reset. First ENABLED
     * result wins; PED is verified at every step (an "enabled-looking"
     * but un-enabled link fails downstream at Address Device anyway). */
    if (xhci_usb2_reset(reg) == 0 && xhci_port_enabled(reg) == 0) return 0;
    xhci_force_rxdetect(reg);
    if (xhci_usb2_reset(reg) == 0 && xhci_port_enabled(reg) == 0) goto h2_recovered;
    if (xhci_power_cycle_port(reg) == 0 &&
        xhci_usb2_reset(reg) == 0 && xhci_port_enabled(reg) == 0) goto h2_recovered;
    if (xhci_warm_reset_port(reg) == 0 && xhci_port_enabled(reg) == 0) goto h2_recovered;
    return -5;
h2_recovered:
    serial_write("xHCI: port recovered after reset escalation\r\n");
    return 0;
}

static volatile uint8_t *runtime_base(const rix_xhci_controller_t *c) {
    volatile uint8_t *base = (volatile uint8_t *)(uintptr_t)c->mmio_va;
    uint32_t rt_off = *(volatile uint32_t *)(base + XHCI_RTSOFF) & ~0x1Fu;
    return base + rt_off;
}

/* Stashed port-change events observed by command/transfer waiters. The event
 * ring is shared; a waiter must consume the head to reach its completion,
 * so port events seen mid-wait are queued here for the hotplug poller. */
#define XHCI_PENDING_PORTS 16u
static uint8_t pending_port[XHCI_MAX][XHCI_PENDING_PORTS];
static uint8_t pending_conn[XHCI_MAX][XHCI_PENDING_PORTS];
static uint8_t pending_head[XHCI_MAX];
static uint8_t pending_tail[XHCI_MAX];
static uint8_t pending_count[XHCI_MAX];

static void xhci_pending_port_push(size_t controller, uint8_t port, uint8_t connected) {
    if (controller >= XHCI_MAX || port == 0u) return;
    /* Coalesce duplicates for the same port. */
    for (uint8_t i = 0, idx = pending_head[controller]; i < pending_count[controller]; ++i) {
        if (pending_port[controller][idx] == port) {
            pending_conn[controller][idx] = connected;
            return;
        }
        idx = (uint8_t)((idx + 1u) % XHCI_PENDING_PORTS);
    }
    if (pending_count[controller] >= XHCI_PENDING_PORTS) return;
    pending_port[controller][pending_tail[controller]] = port;
    pending_conn[controller][pending_tail[controller]] = connected;
    pending_tail[controller] = (uint8_t)((pending_tail[controller] + 1u) % XHCI_PENDING_PORTS);
    pending_count[controller]++;
}

int xhci_pending_port_pop(size_t controller, uint8_t *port, uint8_t *connected) {
    if (controller >= XHCI_MAX || !port || !connected) return -1;
    if (controller >= count) return -1;
    if (!pending_count[controller]) return 0;
    *port = pending_port[controller][pending_head[controller]];
    *connected = pending_conn[controller][pending_head[controller]];
    pending_head[controller] =
        (uint8_t)((pending_head[controller] + 1u) % XHCI_PENDING_PORTS);
    pending_count[controller]--;
    return 1;
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

static void trace_context_state_error(const rix_xhci_controller_t *c, xhci_runtime_t *rt,
                                      uint64_t event_phys, uint64_t parameter,
                                      uint32_t control, uint32_t status,
                                      uint8_t slot_id, uint8_t endpoint_id) {
    if ((status >> 24) != XHCI_COMPLETION_CONTEXT_STATE) return;
    serial_write("xHCI: completion-code-11 context-state-error\r\n");
    serial_write("xHCI: controller=");
    serial_write_dec((uint64_t)(c - controllers));
    serial_write(" event-trb=");
    serial_write_hex(event_phys);
    serial_write(" parameter=");
    serial_write_hex(parameter);
    serial_write(" control=");
    serial_write_hex(control);
    serial_write(" status=");
    serial_write_hex(status);
    serial_write(" slot=");
    serial_write_dec(slot_id);
    serial_write(" endpoint=");
    serial_write_dec(endpoint_id);
    serial_write("\r\n");
    if (slot_id == 0u) return;
    const xhci_slot_runtime_t *slot = &rt->slots[slot_id];
    serial_write("xHCI: port=");
    serial_write_dec(slot->port);
    serial_write(" speed=");
    serial_write_dec(slot->speed);
    serial_write(" route=");
    serial_write_hex(slot->route_string);
    serial_write(" dcba=");
    serial_write_hex(slot->device_context_phys);
    serial_write(" input-context=");
    serial_write_hex(slot->input_context_phys);
    serial_write(" ep0-ring=");
    serial_write_hex(slot->ep0_ring_phys);
    serial_write("\r\n");
    if (endpoint_id < 32u) {
        serial_write("xHCI: endpoint-ring=");
        serial_write_hex(slot->endpoints[endpoint_id].ring_phys);
        serial_write(" cycle=");
        serial_write_dec(slot->endpoints[endpoint_id].cycle);
        serial_write(" enqueue=");
        serial_write_dec(slot->endpoints[endpoint_id].enqueue);
        serial_write("\r\n");
    }
}

int xhci_poll_port_status_change(size_t controller, uint8_t *port, uint8_t *connected) {
    if (port) *port = 0;
    if (connected) *connected = 0;
    if (controller >= count || !port || !connected) return -1;
    const rix_xhci_controller_t *c = &controllers[controller];
    xhci_runtime_t *rt = &runtimes[controller];
    if (!c->running || !c->event_ring_phys) return -2;
    /* Drain stashed port events saved by wait_command/wait_transfer while
     * they were scanning for completions. Without this, port changes that
     * arrive mid-transfer are swallowed and only the fallback scan sees
     * them (or misses disconnects). */
    {
        uint8_t p = 0, conn = 0;
        if (xhci_pending_port_pop(controller, &p, &conn) == 1) {
            rix_xhci_port_status_t status;
            if (xhci_port_status(controller, p, &status) != 0) return -4;
            *port = p;
            *connected = status.connected;
            return 1;
        }
    }
    volatile xhci_trb_t *event = &((volatile xhci_trb_t *)(uintptr_t)c->event_ring_phys)
        [rt->event_dequeue];
    if ((event->control & XHCI_TRB_CYCLE) != rt->event_cycle) return 0;
    uint32_t type = (event->control >> XHCI_TRB_TYPE_SHIFT) & 0x3fu;
    if (type != XHCI_TRB_PORT_STATUS_CHANGE) return 0;
    uint8_t event_port = (uint8_t)(event->parameter_lo >> 24);
    acknowledge_event(c, rt);
    /* Clear Port-Change-Detect so USBSTS does not stick on real silicon. */
    {
        volatile uint8_t *base = (volatile uint8_t *)(uintptr_t)c->mmio_va;
        volatile uint8_t *op = base + c->cap_length;
        *(volatile uint32_t *)(op + XHCI_USBSTS) =
            (XHCI_STS_PCD | XHCI_STS_EINT);
    }
    if (event_port == 0u || event_port > c->max_ports) return -3;
    rix_xhci_port_status_t status;
    if (xhci_port_status(controller, event_port, &status) != 0) return -4;
    *port = event_port;
    *connected = status.connected;
    return 1;
}

/* Ports whose last attach attempt failed. The fallback scan must not retry
 * them every poll (that is the log flood in the photo: error=4/6 forever).
 * A failed port is retried after a physical disconnect/reconnect (CCS=0
 * clears the flag), after a successful detach, or after a quiet interval
 * (timed retry below: training on real silicon can take seconds, and a
 * park-forever policy would never pick up a late-training keyboard). */
static uint8_t port_attach_failed[XHCI_MAX][256];
/* Monotonic-ns timestamp of the park; 0 means unparked. */
static uint64_t port_attach_fail_ns[XHCI_MAX][256];
#define XHCI_PORT_RETRY_NS 10000000000ULL

int xhci_service_hotplug(size_t controller, rix_xhci_device_t *device, uint8_t *connected) {
    if (controller >= count || !device || !connected) return -1;
    device->slot_id = 0;
    device->port = 0;
    device->speed = 0;
    device->state = RIX_XHCI_DEVICE_DETACHED;
    uint8_t port = 0;
    uint8_t is_connected = 0;
    int rc = xhci_poll_port_status_change(controller, &port, &is_connected);
    if (rc == 0) {
        /* A device already present before the controller starts may not
           generate a port-status event. Scan connected ports once per poll,
           but never retry a port whose last attach already failed: that is
           the error=4/6 flood. Failed ports retry only after disconnect. */
        const rix_xhci_controller_t *c = &controllers[controller];
        /* Reap failed flags for ports that are now physically gone, or
         * whose quiet interval elapsed (timed retry for slow training).
         * Subtraction is wrap-safe on monotonic time. */
        for (uint8_t p = 1; p <= c->max_ports; ++p) {
            if (!port_attach_failed[controller][p]) continue;
            rix_xhci_port_status_t st;
            if (xhci_port_status(controller, p, &st) != 0 || !st.connected) {
                port_attach_failed[controller][p] = 0;
                port_attach_fail_ns[controller][p] = 0;
            } else if (time_monotonic_ns() - port_attach_fail_ns[controller][p] >
                       XHCI_PORT_RETRY_NS) {
                port_attach_failed[controller][p] = 0;
                port_attach_fail_ns[controller][p] = 0;
            }
        }
        /* Phase H6 boot-device order: among connected, unparked, unoccupied
         * ports pick the best-scoring one instead of the first (lowest)
         * port. The score prefers a known-good USB2 port — the documented
         * aim for the boot keyboard — and deprioritizes, never bans, the
         * ports this exact board was observed failing: such a port is still
         * attempted once every known-good candidate is exhausted. Ties keep
         * the lowest port number, so the choice cannot oscillate. */
        const xhci_profile_t *profile = xhci_controller_profile(c);
        uint8_t best_port = 0;
        int best_score = 0;
        for (uint8_t candidate = 1; candidate <= c->max_ports; ++candidate) {
            if (port_attach_failed[controller][candidate]) continue;
            rix_xhci_port_status_t status;
            if (xhci_port_status(controller, candidate, &status) != 0 || !status.connected) continue;
            int occupied = 0;
            for (uint16_t slot_id = 1; slot_id <= c->max_slots; ++slot_id)
                if (runtimes[controller].slots[slot_id].allocated &&
                    runtimes[controller].slots[slot_id].port == candidate) { occupied = 1; break; }
            if (occupied) continue;
            int score = xhci_profile_port_priority(
                xhci_port_protocol(controller, candidate),
                xhci_profile_port_known_bad(profile, candidate));
            if (best_port == 0u || score < best_score) {
                best_port = candidate;
                best_score = score;
            }
        }
        if (best_port != 0u) {
            port = best_port;
            is_connected = 1;
            rc = 1;
            /* One line per selection change. A nonzero score means the
             * chosen port is USB3, of unknown protocol, or listed as
             * failing — exactly the case worth seeing on the boot log
             * without flooding it on every poll. */
            static uint8_t last_selected[XHCI_MAX];
            if (best_score > 0 && last_selected[controller] != best_port) {
                last_selected[controller] = best_port;
                serial_write("xHCI: port order ctl=");
                serial_write_dec(controller);
                serial_write(" port=");
                serial_write_dec(best_port);
                serial_write(" score=");
                serial_write_dec((uint64_t)best_score);
                serial_write(" proto=U");
                serial_write_dec((uint64_t)xhci_port_protocol(controller, best_port));
                serial_write("\r\n");
            }
        }
    } else if (rc == 1 && port != 0u) {
        if (!is_connected) {
            /* Physical disconnect: allow a future reconnect to retry. */
            port_attach_failed[controller][port] = 0;
        } else if (port_attach_failed[controller][port]) {
            /* Duplicate connect event for an already-failed port without an
             * intervening disconnect: acked already, nothing to do. */
            volatile uint32_t *preg = port_reg(&controllers[controller], port);
            if (preg) xhci_clear_port_change(preg);
            return 0;
        }
    }
    if (rc <= 0) return rc;
    *connected = is_connected;
    if (is_connected) {
        int attach_rc = xhci_device_attach(controller, port, device);
        if (attach_rc != 0) {
            device->port = port;
            device->state = RIX_XHCI_DEVICE_ERROR;
            /* First failure: detailed diagnostics + park the port. Repeats
             * are suppressed until disconnect. */
            if (!port_attach_failed[controller][port]) {
                volatile uint32_t *preg = port_reg(&controllers[controller], port);
                uint32_t portsc = preg ? *preg : 0u;
                serial_write("xHCI: attach failed controller=");
                serial_write_dec(controller);
                serial_write(" port=");
                serial_write_dec(port);
                serial_write(" rc=");
                serial_write_dec((uint64_t)(attach_rc < 0 ? -attach_rc : attach_rc));
                serial_write(" PORTSC=");
                serial_write_hex(portsc);
                serial_write("\r\n");
                if (preg) xhci_clear_port_change(preg);
            }
            port_attach_failed[controller][port] = 1;
            port_attach_fail_ns[controller][port] = time_monotonic_ns();
        } else {
            port_attach_failed[controller][port] = 0;
            port_attach_fail_ns[controller][port] = 0;
        }
        return attach_rc;
    }
    for (uint16_t slot_id = 1; slot_id <= controllers[controller].max_slots; ++slot_id) {
        xhci_slot_runtime_t *slot = &runtimes[controller].slots[slot_id];
        if (slot->allocated && slot->port == port) {
            device->slot_id = (uint8_t)slot_id;
            device->port = port;
            device->speed = slot->speed;
            int detach_rc = xhci_device_detach(controller, (uint8_t)slot_id);
            device->state = detach_rc == 0 ? RIX_XHCI_DEVICE_DETACHED : RIX_XHCI_DEVICE_ERROR;
            if (detach_rc == 0 && port != 0u) { port_attach_failed[controller][port] = 0; port_attach_fail_ns[controller][port] = 0; }
            return detach_rc;
        }
    }
    device->slot_id = 0;
    device->port = port;
    device->speed = 0;
        device->state = RIX_XHCI_DEVICE_DETACHED;
    if (port != 0u) port_attach_failed[controller][port] = 0;
    return 0;
}
static int wait_command(const rix_xhci_controller_t *c, xhci_runtime_t *rt,
                        uint64_t command_phys, uint8_t *out_slot) {
    size_t controller_index = (size_t)(c - controllers);
    volatile xhci_trb_t *events = (volatile xhci_trb_t *)(uintptr_t)c->event_ring_phys;
    for (uint32_t i = 0; i < XHCI_POLL_LIMIT; ++i) {
        volatile xhci_trb_t *event = &events[rt->event_dequeue];
        uint32_t control = event->control;
        if ((control & XHCI_TRB_CYCLE) != rt->event_cycle) {
            if ((i & 0xffu) == 0u) xhci_pause_delay(64u);
            continue;
        }
        uint32_t type = (control >> XHCI_TRB_TYPE_SHIFT) & 0x3fu;
        uint64_t parameter = ((uint64_t)event->parameter_hi << 32) | event->parameter_lo;
        uint8_t slot = (uint8_t)(control >> XHCI_TRB_SLOT_SHIFT);
        uint8_t completion = (uint8_t)(event->status >> 24);
        uint64_t event_phys = c->event_ring_phys +
                              (uint64_t)rt->event_dequeue * sizeof(xhci_trb_t);
        /* Stash port changes instead of dropping them: the hotplug poller
         * owns them. */
        if (type == XHCI_TRB_PORT_STATUS_CHANGE) {
            uint8_t p = (uint8_t)(event->parameter_lo >> 24);
            rix_xhci_port_status_t st = {0, 0, 0, 0};
            /* Best-effort connected bit; poller re-reads PORTSC anyway. */
            xhci_pending_port_push(controller_index, p, 1u);
            (void)st;
            trace_context_state_error(c, rt, event_phys, parameter, control, event->status,
                                      slot, 0u);
            acknowledge_event(c, rt);
            {
                volatile uint8_t *base = (volatile uint8_t *)(uintptr_t)c->mmio_va;
                volatile uint8_t *op = base + c->cap_length;
                *(volatile uint32_t *)(op + XHCI_USBSTS) =
                    (XHCI_STS_PCD | XHCI_STS_EINT);
            }
            continue;
        }
        trace_context_state_error(c, rt, event_phys, parameter, control, event->status,
                                  slot, 0u);
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

    volatile uint8_t *base = (volatile uint8_t *)(uintptr_t)c->mmio_va;
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
    for (size_t endpoint_id = 2u; endpoint_id < 32u; ++endpoint_id) {
        if (slot->endpoints[endpoint_id].ring_phys) {
            pmm_free_page(slot->endpoints[endpoint_id].ring_phys);
            slot->endpoints[endpoint_id].ring_phys = 0;
        }
        slot->endpoints[endpoint_id].type = 0;
        slot->endpoints[endpoint_id].cycle = 0;
        slot->endpoints[endpoint_id].enqueue = 0;
    }
    slot->device_context_phys = 0;
    slot->input_context_phys = 0;
    slot->ep0_ring_phys = 0;
    slot->allocated = 0;
    slot->addressed = 0;
    slot->port = 0;
    slot->speed = 0;
    slot->route_string = 0;
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
                         uint64_t last_trb, uint8_t slot_id, uint8_t expected_endpoint,
                         uint16_t requested,
                         uint16_t *actual) {
    size_t controller_index = (size_t)(c - controllers);
    volatile xhci_trb_t *events = (volatile xhci_trb_t *)(uintptr_t)c->event_ring_phys;
    for (uint32_t i = 0; i < XHCI_POLL_LIMIT; ++i) {
        volatile xhci_trb_t *event = &events[rt->event_dequeue];
        uint32_t control = event->control;
        if ((control & XHCI_TRB_CYCLE) != rt->event_cycle) {
            if ((i & 0xffu) == 0u) xhci_pause_delay(64u);
            continue;
        }
        uint64_t parameter = ((uint64_t)event->parameter_hi << 32) | event->parameter_lo;
        uint32_t type = (control >> XHCI_TRB_TYPE_SHIFT) & 0x3fu;
        uint8_t event_slot = (uint8_t)(control >> XHCI_TRB_SLOT_SHIFT);
        uint8_t endpoint = (uint8_t)((control >> 16) & 0x1fu);
        uint8_t completion = (uint8_t)(event->status >> 24);
        uint32_t residual = event->status & 0x00ffffffu;
        uint64_t event_phys = c->event_ring_phys +
                              (uint64_t)rt->event_dequeue * sizeof(xhci_trb_t);
        if (type == XHCI_TRB_PORT_STATUS_CHANGE) {
            uint8_t p = (uint8_t)(event->parameter_lo >> 24);
            xhci_pending_port_push(controller_index, p, 1u);
            trace_context_state_error(c, rt, event_phys, parameter, control, event->status,
                                      event_slot, endpoint);
            acknowledge_event(c, rt);
            {
                volatile uint8_t *base = (volatile uint8_t *)(uintptr_t)c->mmio_va;
                volatile uint8_t *op = base + c->cap_length;
                *(volatile uint32_t *)(op + XHCI_USBSTS) =
                    (XHCI_STS_PCD | XHCI_STS_EINT);
            }
            continue;
        }
        trace_context_state_error(c, rt, event_phys, parameter, control, event->status,
                                  event_slot, endpoint);
        acknowledge_event(c, rt);
        if (type != XHCI_TRB_TRANSFER_EVENT || parameter != last_trb ||
            event_slot != slot_id || endpoint != expected_endpoint) continue;
        if (actual) *actual = residual >= requested ? 0u : (uint16_t)(requested - residual);
        return completion == XHCI_COMPLETION_SUCCESS ? 0 :
               (completion != 0u ? -(int)completion : -90);
    }
    return -100;
}

static uint64_t xhci_dma_linear_pa(const void *buffer, uint64_t length) {
    if (!buffer || !length) return 0;
    uint64_t va = (uint64_t)(uintptr_t)buffer;
    if (va > UINT64_MAX - length) return 0;
    uint64_t first = vmm_translate(va);
    if (!first) return 0;
    uint64_t first_base = first & ~0xfffULL;
    uint64_t page_va = va & ~0xfffULL;
    uint64_t last_va = (va + length - 1u) & ~0xfffULL;
    for (;;) {
        uint64_t page_pa = vmm_translate(page_va);
        if (!page_pa || (page_pa & ~0xfffULL) !=
            first_base + (page_va - (va & ~0xfffULL))) return 0;
        if (page_va == last_va) break;
        page_va += 0x1000ULL;
    }
    return first;
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
    /* The Setup Stage TRB carries the complete USB setup packet in its
       64-bit parameter field.  Putting wIndex/wLength in status produces
       malformed requests on real xHCI controllers. */
    uint64_t setup_parameter = (uint64_t)setup->request_type |
                               ((uint64_t)setup->request << 8) |
                               ((uint64_t)setup->value << 16) |
                               ((uint64_t)setup->index << 32) |
                               ((uint64_t)setup->length << 48);
    uint32_t setup_status = 0;
    size_t needed = setup->length != 0u ? 3u : 2u;
    if ((size_t)slot->ep0_enqueue + needed > XHCI_CMD_RING_TRBS - 1u) ep0_write_link(c, slot);
    (void)ep0_emit(c, slot, setup_parameter, setup_status, setup_control);
    if (setup->length != 0u) {
        uint64_t data_pa = xhci_dma_linear_pa(data, setup->length);
        if (!data_pa) return -4;
        (void)ep0_emit(c, slot, data_pa, setup->length,
                       (XHCI_TRB_DATA_STAGE << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_CH |
                       (data_in ? XHCI_TRB_DIR : 0u));
    }
    uint64_t status_trb = ep0_emit(c, slot, 0, 0,
        (XHCI_TRB_STATUS_STAGE << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_IOC |
        ((setup->length == 0u || !data_in) ? XHCI_TRB_DIR : 0u));
    __asm__ volatile("mfence" ::: "memory");
    volatile uint8_t *base = (volatile uint8_t *)(uintptr_t)c->mmio_va;
    uint32_t db_off = *(volatile uint32_t *)(base + XHCI_DBOFF) & ~0x3u;
    *(volatile uint32_t *)(base + db_off + (uint32_t)slot_id * 4u) = 1u;
    return wait_transfer(c, &runtimes[controller], status_trb, slot_id, 1u,
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

int xhci_get_hid_report_descriptor(size_t controller, uint8_t slot_id,
                                   uint8_t interface_number, void *buffer,
                                   uint16_t length, uint16_t *actual_length) {
    if (interface_number >= 32u || length == 0u || !buffer) return -1;
    rix_usb_setup_packet_t setup = {
        .request_type = 0x81u,
        .request = 6u,
        .value = (uint16_t)((uint16_t)RIX_USB_DESC_HID_REPORT << 8),
        .index = interface_number,
        .length = length
    };
    return xhci_control_transfer(controller, slot_id, &setup, buffer, actual_length);
}

int xhci_hid_set_protocol(size_t controller, uint8_t slot_id, uint8_t interface_number,
                          uint8_t protocol) {
    if (interface_number >= 32u || protocol > 1u) return -1;
    rix_usb_setup_packet_t setup = {
        .request_type = 0x21u,
        .request = 0x0bu,
        .value = protocol,
        .index = interface_number,
        .length = 0
    };
    return xhci_control_transfer(controller, slot_id, &setup, NULL, NULL);
}

int xhci_hid_set_idle(size_t controller, uint8_t slot_id, uint8_t interface_number,
                      uint8_t report_id, uint8_t duration_4ms) {
    if (interface_number >= 32u) return -1;
    rix_usb_setup_packet_t setup = {
        .request_type = 0x21u,
        .request = 0x0au,
        .value = (uint16_t)(((uint16_t)duration_4ms << 8) | report_id),
        .index = interface_number,
        .length = 0
    };
    return xhci_control_transfer(controller, slot_id, &setup, NULL, NULL);
}

int xhci_hid_get_protocol(size_t controller, uint8_t slot_id, uint8_t interface_number,
                          uint8_t *protocol) {
    if (interface_number >= 32u || !protocol) return -1;
    uint16_t actual = 0;
    rix_usb_setup_packet_t setup = {
        .request_type = 0xa1u,
        .request = 0x03u,
        .value = 0,
        .index = interface_number,
        .length = 1
    };
    int rc = xhci_control_transfer(controller, slot_id, &setup, protocol, &actual);
    if (rc != 0) return rc;
    return actual == 1u && *protocol <= 1u ? 0 : -2;
}

int xhci_enumerate_device(size_t controller, uint8_t slot_id,
                          rix_usb_device_descriptor_t *device,
                          uint8_t *configuration, uint16_t configuration_capacity,
                          rix_usb_configuration_info_t *configuration_info,
                          rix_usb_interface_info_t *interfaces, size_t interface_capacity,
                          rix_usb_endpoint_info_t *endpoints, size_t endpoint_capacity,
                          size_t *interface_count, size_t *endpoint_count) {
    if (!device || !configuration || !configuration_info || configuration_capacity < 9u ||
        !interface_count || !endpoint_count) return -1;
    uint8_t device_bytes[18];
    uint16_t actual = 0;
    int rc = xhci_get_descriptor(controller, slot_id, RIX_USB_DESC_DEVICE, 0, 0,
                                  device_bytes, sizeof(device_bytes), &actual);
    if (rc != 0 || actual < sizeof(device_bytes) ||
        usb_parse_device_descriptor(device_bytes, actual, device) != 0) return -2;

    rc = xhci_get_descriptor(controller, slot_id, RIX_USB_DESC_CONFIGURATION, 0, 0,
                              configuration, 9u, &actual);
    if (rc != 0 || actual < 9u || configuration[1] != RIX_USB_DESC_CONFIGURATION) return -3;
    uint16_t total_length = (uint16_t)configuration[2] |
                            ((uint16_t)configuration[3] << 8);
    if (total_length < 9u || total_length > configuration_capacity) return -4;
    rc = xhci_get_descriptor(controller, slot_id, RIX_USB_DESC_CONFIGURATION, 0, 0,
                              configuration, total_length, &actual);
    if (rc != 0 || actual < total_length) return -5;
    if (usb_parse_configuration_descriptor(configuration, actual, configuration_info,
                                           interfaces, interface_capacity, endpoints,
                                           endpoint_capacity, interface_count,
                                           endpoint_count) != 0) return -6;
    return 0;
}

int xhci_configure_endpoint(size_t controller, uint8_t slot_id,
                            const rix_xhci_endpoint_config_t *config) {
    uint8_t transfer_type = config ? (config->attributes & RIX_USB_EP_TRANSFER_MASK) : 0u;
    if (!config || controller >= count || slot_id == 0u ||
        slot_id > controllers[controller].max_slots ||
        (transfer_type != RIX_USB_EP_INTERRUPT && transfer_type != RIX_USB_EP_BULK) ||
        config->max_packet_size == 0u || config->interval == 0u) return -1;
    uint8_t endpoint_number = config->endpoint_address & 0x0fu;
    if (endpoint_number == 0u || (config->endpoint_address & 0x70u) != 0u) return -2;
    if (endpoint_number >= 15u) return -2;
    xhci_slot_runtime_t *slot = &runtimes[controller].slots[slot_id];
    const rix_xhci_controller_t *c = &controllers[controller];
    uint8_t direction = (config->endpoint_address & 0x80u) != 0u;
    uint8_t endpoint_id = (uint8_t)((config->endpoint_address & 0x0fu) * 2u + direction);
    xhci_endpoint_runtime_t *endpoint_runtime = &slot->endpoints[endpoint_id];
    if (!c->running || !slot->allocated || !slot->addressed || endpoint_runtime->ring_phys) return -3;
    uint64_t ring_phys = dma_page(c);
    if (!ring_phys) return -4;
    zero_page(ring_phys);
    volatile xhci_trb_t *ring = (volatile xhci_trb_t *)(uintptr_t)ring_phys;
    ring[XHCI_CMD_RING_TRBS - 1u].parameter_lo = (uint32_t)ring_phys;
    ring[XHCI_CMD_RING_TRBS - 1u].parameter_hi = (uint32_t)(ring_phys >> 32);
    ring[XHCI_CMD_RING_TRBS - 1u].control = (XHCI_TRB_LINK << XHCI_TRB_TYPE_SHIFT) |
        XHCI_TRB_TC | XHCI_TRB_CYCLE;
    uint32_t context_size = (c->hcc_params1 & XHCI_HCC_CSZ) != 0u ? 64u : 32u;
    volatile uint32_t *input = (volatile uint32_t *)(uintptr_t)slot->input_context_phys;
    volatile uint32_t *slot_context = (volatile uint32_t *)
        (uintptr_t)(slot->input_context_phys + context_size);
    volatile uint32_t *endpoint = (volatile uint32_t *)
        (uintptr_t)(slot->input_context_phys + (uint64_t)context_size * (endpoint_id + 1u));
    uint8_t context_entries = endpoint_id > 1u ? endpoint_id : 1u;
    slot_context[0] = (slot_context[0] & ~XHCI_SLOT_CONTEXT_ENTRIES_MASK) |
                      ((uint32_t)context_entries << 27);
    input[1] = XHCI_INPUT_ADD_SLOT | (1u << (endpoint_id + 1u));
    /* Phase H3: Max ESIT Payload (dword0 low half) for SuperSpeed
     * periodic endpoints; 0 elsewhere keeps the USB2 path identical.
     * Prefer the companion value, else derive MPS*(burst+1). */
    uint32_t esit = 0u;
    if (slot->speed >= 4u && transfer_type == RIX_USB_EP_INTERRUPT) {
        esit = config->esit_payload ? (uint32_t)config->esit_payload :
               (uint32_t)config->max_packet_size * ((uint32_t)config->max_burst + 1u);
        if (esit > 0xffffu) esit = 0xffffu;
    }
    endpoint[0] = (esit & 0xffffu) | ((uint32_t)config->interval << 16);
    uint8_t endpoint_type = transfer_type == RIX_USB_EP_INTERRUPT
        ? (direction ? XHCI_EP_INTERRUPT_IN : XHCI_EP_INTERRUPT_OUT)
        : (direction ? XHCI_EP_BULK_IN : XHCI_EP_BULK_OUT);
    endpoint[1] = (3u << 1) | (endpoint_type << 3) |
                  ((uint32_t)config->max_burst << 8) |
                  ((uint32_t)config->max_packet_size << 16);
    endpoint[2] = (uint32_t)ring_phys;
    endpoint[3] = (uint32_t)(ring_phys >> 32) | XHCI_TRB_CYCLE;
    __asm__ volatile("mfence" ::: "memory");
    int rc = submit_command(controller, slot->input_context_phys,
                            (XHCI_TRB_CONFIGURE_ENDPOINT << XHCI_TRB_TYPE_SHIFT) |
                            ((uint32_t)slot_id << XHCI_TRB_SLOT_SHIFT), NULL);
    if (rc != 0) {
        pmm_free_page(ring_phys);
        return rc;
    }
    endpoint_runtime->ring_phys = ring_phys;
    endpoint_runtime->type = transfer_type;
    endpoint_runtime->cycle = 1u;
    endpoint_runtime->enqueue = 0;
    return 0;
}

static int endpoint_transfer(size_t controller, uint8_t slot_id, uint8_t endpoint_address,
                             void *buffer, uint16_t length, uint16_t *actual_length,
                             uint8_t expected_type) {
    if (actual_length) *actual_length = 0;
    if (!buffer || length == 0u || controller >= count || slot_id == 0u ||
        slot_id > controllers[controller].max_slots) return -1;
    xhci_slot_runtime_t *slot = &runtimes[controller].slots[slot_id];
    const rix_xhci_controller_t *c = &controllers[controller];
    uint8_t endpoint_id = (uint8_t)((endpoint_address & 0x0fu) * 2u +
                                    ((endpoint_address & 0x80u) != 0u));
    if ((endpoint_address & 0x70u) != 0u || endpoint_id >= 32u) return -2;
    xhci_endpoint_runtime_t *endpoint_runtime = &slot->endpoints[endpoint_id];
    if (!c->running || !endpoint_runtime->ring_phys ||
        endpoint_runtime->type != expected_type) return -2;
    if (endpoint_runtime->enqueue >= XHCI_CMD_RING_TRBS - 1u) {
        volatile xhci_trb_t *link = (volatile xhci_trb_t *)(uintptr_t)endpoint_runtime->ring_phys;
        link[XHCI_CMD_RING_TRBS - 1u].parameter_lo = (uint32_t)endpoint_runtime->ring_phys;
        link[XHCI_CMD_RING_TRBS - 1u].parameter_hi = (uint32_t)(endpoint_runtime->ring_phys >> 32);
        link[XHCI_CMD_RING_TRBS - 1u].status = 0;
        link[XHCI_CMD_RING_TRBS - 1u].control = (XHCI_TRB_LINK << XHCI_TRB_TYPE_SHIFT) |
            XHCI_TRB_TC | (endpoint_runtime->cycle ? XHCI_TRB_CYCLE : 0u);
        endpoint_runtime->enqueue = 0;
        endpoint_runtime->cycle ^= 1u;
    }
    uint16_t index = endpoint_runtime->enqueue++;
    uint64_t trb_phys = endpoint_runtime->ring_phys + (uint64_t)index * sizeof(xhci_trb_t);
    volatile xhci_trb_t *trb = &((volatile xhci_trb_t *)(uintptr_t)endpoint_runtime->ring_phys)[index];
    uint64_t buffer_pa = xhci_dma_linear_pa(buffer, length);
    if (!buffer_pa) return -3;
    trb->parameter_lo = (uint32_t)buffer_pa;
    trb->parameter_hi = (uint32_t)(buffer_pa >> 32);
    trb->status = length;
    trb->control = (XHCI_TRB_NORMAL << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_IOC |
                   (endpoint_runtime->cycle ? XHCI_TRB_CYCLE : 0u);
    __asm__ volatile("mfence" ::: "memory");
    volatile uint8_t *base = (volatile uint8_t *)(uintptr_t)c->mmio_va;
    uint32_t db_off = *(volatile uint32_t *)(base + XHCI_DBOFF) & ~0x3u;
    *(volatile uint32_t *)(base + db_off + (uint32_t)slot_id * 4u) = endpoint_id;
    return wait_transfer(c, &runtimes[controller], trb_phys, slot_id,
                         endpoint_id, length, actual_length);
}

int xhci_interrupt_transfer(size_t controller, uint8_t slot_id, uint8_t endpoint_address,
                            void *buffer, uint16_t length, uint16_t *actual_length) {
    return endpoint_transfer(controller, slot_id, endpoint_address, buffer, length,
                             actual_length, RIX_USB_EP_INTERRUPT);
}

int xhci_bulk_transfer(size_t controller, uint8_t slot_id, uint8_t endpoint_address,
                       void *buffer, uint16_t length, uint16_t *actual_length) {
    return endpoint_transfer(controller, slot_id, endpoint_address, buffer, length,
                             actual_length, RIX_USB_EP_BULK);
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
