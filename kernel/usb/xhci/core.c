/* xHCI controller detection, reset and runtime setup.
 *
 * Provenance: Linux drivers/usb/host/xhci-pci.c (probe, BIOS handoff via
 * pci-quirks), drivers/usb/host/xhci.c (xhci_reset, xhci_init, run/stop),
 * drivers/usb/host/xhci-mem.c (DCBAA, command/event rings, ERST, scratchpad).
 * Only the single-user desktop subset is carried: one interrupter, polling
 * mode (no MSI-X vector programming, no workqueues), no suspend/resume.
 */

#include "xhc.h"

rix_xhci_controller_t xhc_controllers[XHCI_MAX];
xhci_runtime_t xhc_runtimes[XHCI_MAX];
size_t xhc_count;
uint64_t xhc_scratchpad_array_phys[XHCI_MAX];

static int xhc_is_xhci_device(const rix_pci_device_t *d) {
    if (!d) return 0;
    /* The B650 chipset exposes several AMD functions with xHCI-like IDs.
     * Do not probe those as controllers unless PCI class/progif confirms
     * serial-bus USB xHCI; probing a false positive reads unrelated BARs. */
    return d->class_code == PCI_CLASS_SERIAL &&
           d->subclass == PCI_SUBCLASS_USB &&
           d->prog_if == PCI_PROGIF_XHCI;
}

int xhc_has_usb3_range(const rix_xhci_controller_t *c) {
    for (unsigned i = 0; i < c->proto_ranges && i < XHCI_SPC_MAX_RANGES; ++i)
        if (c->proto_major[i] == 3u) return 1;
    return 0;
}

void xhc_write_hex4(uint16_t v) {
    static const char digits[] = "0123456789abcdef";
    char buf[5];
    buf[0] = digits[(v >> 12) & 0xfu];
    buf[1] = digits[(v >> 8) & 0xfu];
    buf[2] = digits[(v >> 4) & 0xfu];
    buf[3] = digits[v & 0xfu];
    buf[4] = 0;
    serial_write(buf);
}

const xhci_profile_t *xhc_controller_profile(const rix_xhci_controller_t *c) {
    return c ? xhci_profile_lookup(c->vendor_id, c->device_id) : 0;
}

/* Apply the profile's quirk flags. The USB2-only flag is honored only when
 * the controller's own Supported-Protocol walk confirms it has no USB3
 * range; if the two disagree the controller wins, the quirk stays off. */
uint32_t xhc_apply_profile_quirks(const rix_xhci_controller_t *c) {
    const xhci_profile_t *profile = xhc_controller_profile(c);
    if (!profile) return XHCI_PROFILE_QUIRK_NONE;
    return ((profile->quirks & XHCI_PROFILE_QUIRK_USB2_ONLY) && !xhc_has_usb3_range(c))
               ? XHCI_PROFILE_QUIRK_USB2_ONLY
               : XHCI_PROFILE_QUIRK_NONE;
}

/* Supported-Protocol walk over MMIO. Snapshots 16 bytes per entry, decodes
 * pure. Stops at the first malformed step (never wedges init on firmware
 * garbage). Cf. Linux xhci_get_protocol_caps(). */
void xhc_scan_protocols(rix_xhci_controller_t *c, volatile uint8_t *base,
                        uint64_t mmio_size) {
    uint32_t hcc;
    uint32_t xecp;
    if (!c || !base || mmio_size < (uint64_t)XHCI_HCCPARAMS1 + 4u) return;
    hcc = XHCI_MMIO_READ32(base, XHCI_HCCPARAMS1);
    xecp = ((hcc & XHCI_HCC_XECP_MASK) >> XHCI_HCC_XECP_SHIFT) * 4u;
    for (uint32_t step = 0; step < XHCI_EXT_CAP_WALK_MAX; ++step) {
        uint8_t raw[XHCI_SPC_ENTRY_SIZE];
        uint32_t header, next;
        xhci_proto_range_t range;
        if (!xecp || xecp + XHCI_SPC_ENTRY_SIZE > mmio_size) return;
        header = XHCI_MMIO_READ32(base, xecp);
        if ((header & 0xffu) != XHCI_EXT_CAP_ID_PROTOCOL) {
            next = ((header >> 8) & 0xffu) * 4u;
            if (!next) return;
            xecp += next;
            continue;
        }
        for (unsigned b = 0; b < XHCI_SPC_ENTRY_SIZE; ++b)
            raw[b] = *(volatile uint8_t *)(base + xecp + b);
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
    if (controller >= xhc_count || port == 0u) return 0;
    const rix_xhci_controller_t *c = &xhc_controllers[controller];
    for (unsigned i = 0; i < c->proto_ranges && i < XHCI_SPC_MAX_RANGES; ++i)
        if (port >= c->proto_start[i] &&
            (unsigned)port < (unsigned)c->proto_start[i] + (unsigned)c->proto_count[i])
            return c->proto_major[i];
    return 0;
}

/* Take OS ownership from firmware via the USB Legacy Support capability
 * (Linux pci-quirks quirk_usb_handoff_xhci). Bounded handoff poll
 * (~100ms worst case); never wedges the boot on a stuck semaphore. */
int xhc_bios_handoff(volatile uint8_t *base, uint64_t mmio_size,
                     uint32_t *was_owned) {
    uint32_t hcc;
    uint32_t xecp;
    if (was_owned) *was_owned = 0;
    if (!base || !mmio_size) return -1;
    if (mmio_size < (uint64_t)XHCI_HCCPARAMS1 + 4u) return -1;
    hcc = XHCI_MMIO_READ32(base, XHCI_HCCPARAMS1);
    xecp = ((hcc & XHCI_HCC_XECP_MASK) >> XHCI_HCC_XECP_SHIFT) * 4u;
    for (uint32_t step = 0; step < XHCI_EXT_CAP_WALK_MAX; ++step) {
        uint32_t header, next;
        volatile uint32_t *legsup;
        if (!xecp || xecp + 4u > mmio_size) return 0;
        header = XHCI_MMIO_READ32(base, xecp);
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
            if ((i & 0x3ffu) == 0u) xhc_udelay(100u);
        }
        return -2;
    }
    return -3;
}

uint64_t xhc_map_range(uint64_t base, uint64_t length) {
    if (length == 0 || base > UINT64_MAX - (length - 1u)) return 0;
    return vmm_map_mmio(base, length);
}

/* xHCI 64-bit MMIO pointer registers are two consecutive dwords (Linux
 * xhci_write_64 / lo-hi ordering). Real controllers require the low dword
 * before the high dword; do not use a single 64-bit store. */
void xhc_write_mmio_ptr(volatile void *reg, uint64_t value) {
    volatile uint32_t *p = (volatile uint32_t *)reg;
    p[0] = (uint32_t)value;
    p[1] = (uint32_t)(value >> 32);
}

void xhc_zero_page(uint64_t phys) {
    volatile uint8_t *p = (volatile uint8_t *)(uintptr_t)phys;
    for (size_t i = 0; i < 4096; ++i) p[i] = 0;
}

static void xhc_zero_runtime(xhci_runtime_t *rt) {
    volatile uint8_t *p = (volatile uint8_t *)rt;
    for (size_t i = 0; i < sizeof(*rt); ++i) p[i] = 0;
    rt->command_cycle = 1;
    rt->event_cycle = 1;
}

uint64_t xhc_dma_page(const rix_xhci_controller_t *c) {
    /* xHCI without AC64 can only address the first 4 GiB (Linux
     * dma_mask handling in xhci-mem.c). */
    uint64_t limit = (c->hcc_params1 & XHCI_HCC_AC64) != 0u ? UINT64_MAX : 0x100000000ULL;
    return pmm_alloc_page_below(limit);
}

void xhc_pause_delay(uint32_t pauses) {
    for (uint32_t i = 0; i < pauses; ++i) __asm__ volatile("pause" ::: "memory");
}

void xhc_udelay(uint32_t us) {
    /* ~64 pauses ~= 1us; pre-PIT safe, works during xhci_init and later. */
    while (us--) xhc_pause_delay(64u);
}

/* Public pre-PIT-safe pacing for boot code (USB settle); same idiom. */
void xhci_udelay(uint32_t us) {
    xhc_udelay(us);
}

/* Wait for HCHalted to reach the wanted state (Linux xhci_handshake on
 * STS_HALT). Returns -2 on host system error, -1 on timeout. */
int xhc_wait_halted(volatile uint8_t *op, int halted) {
    volatile uint32_t *sts = (volatile uint32_t *)(op + XHCI_USBSTS);
    for (uint32_t i = 0; i < XHCI_POLL_LIMIT; ++i) {
        uint32_t s = *sts;
        int is_halted = (s & XHCI_STS_HCH) != 0u;
        if (is_halted == halted) return (s & XHCI_STS_HSE) ? -2 : 0;
        if ((i & 0xffu) == 0u) xhc_pause_delay(64u);
    }
    return -1;
}

/* Wait for Controller-Not-Ready to clear after reset (Linux
 * xhci_handshake STS_CNR). Programming op/runtime regs while CNR=1 is
 * ignored on real silicon and later surfaces as Enable-Slot timeouts. */
int xhc_wait_cnr_clear(volatile uint8_t *op) {
    volatile uint32_t *sts = (volatile uint32_t *)(op + XHCI_USBSTS);
    for (uint32_t i = 0; i < XHCI_RESET_POLL_LIMIT; ++i) {
        if ((*sts & XHCI_STS_CNR) == 0u) return 0;
        if ((i & 0x3ffu) == 0u) xhc_udelay(10u);
    }
    return -1;
}

/* Controller reset (Linux xhci_reset): stop, assert HCRST, wait for clear
 * plus CNR. All waits bounded and paced for real-silicon timing. */
int xhc_reset_controller(volatile uint8_t *op) {
    volatile uint32_t *cmd = (volatile uint32_t *)(op + XHCI_USBCMD);
    volatile uint32_t *sts = (volatile uint32_t *)(op + XHCI_USBSTS);
#if XHCI_ADDR_TRACE
    uint64_t hc_t0 = time_monotonic_ns();
    serial_write("xHCI: HCRESET BEFORE usbcmd=");
    serial_write_hex(*cmd);
    serial_write(" usbsts=");
    serial_write_hex(*sts);
    serial_write("\r\n");
#endif
    uint32_t cmd_v = *cmd;
    cmd_v &= ~(XHCI_CMD_RUN | XHCI_CMD_INTE);
    *cmd = cmd_v;
    if (xhc_wait_halted(op, 1) != 0) {
#if XHCI_ADDR_TRACE
        serial_write("xHCI: HCRESET HALT-FAIL usbcmd=");
        serial_write_hex(*cmd);
        serial_write(" elapsed_ns=");
        serial_write_dec(time_monotonic_ns() - hc_t0);
        serial_write("\r\n");
#endif
        return -1;
    }
    *cmd |= XHCI_CMD_HCRST;
    for (uint32_t i = 0; i < XHCI_RESET_POLL_LIMIT; ++i) {
        uint32_t v = *cmd;
        uint32_t s = *sts;
        if ((s & XHCI_STS_HSE) != 0u) {
#if XHCI_ADDR_TRACE
            serial_write("xHCI: HCRESET HSE-FAIL usbsts=");
            serial_write_hex(s);
            serial_write(" elapsed_ns=");
            serial_write_dec(time_monotonic_ns() - hc_t0);
            serial_write("\r\n");
#endif
            return -2;
        }
        if ((v & XHCI_CMD_HCRST) == 0u) {
            int cnr = xhc_wait_cnr_clear(op);
#if XHCI_ADDR_TRACE
            serial_write("xHCI: HCRESET AFTER usbcmd=");
            serial_write_hex(*cmd);
            serial_write(" usbsts=");
            serial_write_hex(*sts);
            serial_write(" cnr_rc=");
            serial_write_dec((uint64_t)(cnr < 0 ? -cnr : cnr));
            serial_write(" elapsed_ns=");
            serial_write_dec(time_monotonic_ns() - hc_t0);
            serial_write("\r\n");
#endif
            return cnr;
        }
        if ((i & 0x3ffu) == 0u) xhc_udelay(10u);
    }
#if XHCI_ADDR_TRACE
    serial_write("xHCI: HCRESET TIMEOUT elapsed_ns=");
    serial_write_dec(time_monotonic_ns() - hc_t0);
    serial_write("\r\n");
#endif
    return -3;
}

void xhc_release_runtime_pages(rix_xhci_controller_t *c) {
    size_t idx = (size_t)(c - xhc_controllers);
    if (idx < XHCI_MAX && xhc_scratchpad_array_phys[idx]) {
        volatile uint64_t *arr =
            (volatile uint64_t *)(uintptr_t)xhc_scratchpad_array_phys[idx];
        /* Best-effort: array page holds buffer addresses; free what we can. */
        for (uint32_t i = 0; i < 64u; ++i) {
            uint64_t buf = arr[i];
            if (buf && !(buf & 0xfffu)) pmm_free_page(buf);
            else if (buf) break;
            if (!buf && i > 0) {
                int rest_zero = 1;
                for (uint32_t j = i + 1u; j < i + 8u && j < 64u; ++j)
                    if (arr[j]) { rest_zero = 0; break; }
                if (rest_zero) break;
            }
        }
        pmm_free_page(xhc_scratchpad_array_phys[idx]);
        xhc_scratchpad_array_phys[idx] = 0;
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

/* Runtime setup (Linux xhci_init + xhci_mem_init, single-interrupter
 * polling subset): PAGESIZE check, scratchpad, DCBAA, command ring with
 * link TRB, ERST, interrupter 0 in polling mode (IE off), CONFIG. */
int xhc_setup_runtime(rix_xhci_controller_t *c, volatile uint8_t *cap,
                      volatile uint8_t *op, xhci_runtime_t *rt) {
    size_t controller_index = (size_t)(c - xhc_controllers);
    {
        uint32_t pagesize = XHCI_MMIO_READ32(op, XHCI_PAGESIZE);
        if ((pagesize & 1u) == 0u) {
            serial_write("xHCI: PAGESIZE lacks 4K support\r\n");
            return -5;
        }
    }
    uint32_t hcs2 = XHCI_MMIO_READ32(cap, XHCI_HCSPARAMS2);
    uint32_t sp_hi = (hcs2 >> 21) & 0x1fu;
    uint32_t sp_lo = (hcs2 >> 27) & 0x1fu;
    uint32_t scratch_count = (sp_hi << 5) | sp_lo;
    if (scratch_count > 512u) {
        serial_write("xHCI: implausible scratchpad count\r\n");
        return -6;
    }
    uint64_t dcbaa = xhc_dma_page(c);
    uint64_t cmd_ring = xhc_dma_page(c);
    uint64_t event_ring = xhc_dma_page(c);
    uint64_t erst = xhc_dma_page(c);
    uint64_t scratch_array = 0;
    if (!dcbaa || !cmd_ring || !event_ring || !erst ||
        (scratch_count && !(scratch_array = xhc_dma_page(c)))) {
        if (dcbaa) pmm_free_page(dcbaa);
        if (cmd_ring) pmm_free_page(cmd_ring);
        if (event_ring) pmm_free_page(event_ring);
        if (erst) pmm_free_page(erst);
        if (scratch_array) pmm_free_page(scratch_array);
        return -1;
    }
    xhc_zero_page(dcbaa);
    xhc_zero_page(cmd_ring);
    xhc_zero_page(event_ring);
    xhc_zero_page(erst);
    if (scratch_array) xhc_zero_page(scratch_array);
    uint64_t scratch_bufs[64];
    uint32_t scratch_allocated = 0;
    if (scratch_count) {
        if (scratch_count > 64u) {
            serial_write("xHCI: scratchpad count exceeds early-driver limit\r\n");
            pmm_free_page(dcbaa);
            pmm_free_page(cmd_ring);
            pmm_free_page(event_ring);
            pmm_free_page(erst);
            pmm_free_page(scratch_array);
            return -7;
        }
        for (uint32_t i = 0; i < scratch_count; ++i) {
            uint64_t buf = xhc_dma_page(c);
            if (!buf) break;
            xhc_zero_page(buf);
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
    xhc_zero_runtime(rt);

    volatile rix_xhci_trb_t *ring = (volatile rix_xhci_trb_t *)(uintptr_t)cmd_ring;
    ring[XHCI_CMD_RING_TRBS - 1u].parameter_lo = (uint32_t)cmd_ring;
    ring[XHCI_CMD_RING_TRBS - 1u].parameter_hi = (uint32_t)(cmd_ring >> 32);
    ring[XHCI_CMD_RING_TRBS - 1u].status = 0;
    ring[XHCI_CMD_RING_TRBS - 1u].control =
        XHCI_TRB_TYPE(XHCI_TRB_LINK) | XHCI_TRB_TC | XHCI_TRB_CYCLE;

    volatile rix_xhci_erst_entry_t *entry =
        (volatile rix_xhci_erst_entry_t *)(uintptr_t)erst;
    entry[0].ring_segment_base = event_ring;
    entry[0].ring_segment_size = XHCI_EVENT_RING_TRBS;
    entry[0].reserved = 0;

    volatile uint64_t *dcbaa_ptr = (volatile uint64_t *)(uintptr_t)dcbaa;
    dcbaa_ptr[0] = scratch_array;

    xhc_write_mmio_ptr(op + XHCI_DCBAAP, dcbaa);
    xhc_write_mmio_ptr(op + XHCI_CRCR, cmd_ring | XHCI_CRCR_CYCLE);

    XHCI_MMIO_WRITE32(op, XHCI_USBSTS, XHCI_STS_W1C_MASK);
    XHCI_MMIO_WRITE32(op, XHCI_DNCTRL, 0u);
    __asm__ volatile("mfence" ::: "memory");

    uint32_t db_off = XHCI_MMIO_READ32(cap, XHCI_DBOFF) & ~0x3u;
    uint32_t rt_off = XHCI_MMIO_READ32(cap, XHCI_RTSOFF) & ~0x1fu;
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
    volatile uint32_t *iman = (volatile uint32_t *)(runtime + XHCI_IMAN_OFF);
    volatile uint32_t *erstsz = (volatile uint32_t *)(runtime + XHCI_ERSTSZ_OFF);
    volatile uint32_t *erstba = (volatile uint32_t *)(runtime + XHCI_ERSTBA_OFF);
    volatile uint32_t *erdp = (volatile uint32_t *)(runtime + XHCI_ERDP_OFF);
    /* Polling mode: clear any pending IP (W1C) and keep IE disabled. There
     * is no xHCI IRQ handler routed, so IE=1 would leave Event-Interrupt
     * pending on real silicon. */
    {
        uint32_t iman_v = *iman;
        iman_v &= ~(XHCI_IMAN_IE);
        iman_v |= XHCI_IMAN_IP;
        *iman = iman_v;
    }
    *erstsz = 1u;
    xhc_write_mmio_ptr(erstba, erst);
    xhc_write_mmio_ptr(erdp, event_ring | XHCI_ERDP_EHB);
    {
        uint32_t iman_v = *iman;
        iman_v &= ~(XHCI_IMAN_IE);
        *iman = iman_v;
    }

    XHCI_MMIO_WRITE32(op, XHCI_CONFIG, c->max_slots & XHCI_CONFIG_SLOTS_MASK);
    *(volatile uint32_t *)(cap + db_off) = XHCI_DB_HOST;

    c->dcbaa_phys = dcbaa;
    c->cmd_ring_phys = cmd_ring;
    c->event_ring_phys = event_ring;
    c->erst_phys = erst;
#if XHCI_ADDR_TRACE
    {
        uint32_t rt_off2 = XHCI_MMIO_READ32(cap, XHCI_RTSOFF) & ~0x1fu;
        volatile uint8_t *rbase2 = cap + rt_off2;
        uint32_t erdp_lo = XHCI_MMIO_READ32(rbase2, XHCI_ERDP_OFF);
        uint32_t erdp_hi = XHCI_MMIO_READ32(rbase2, XHCI_ERDP_OFF + 4u);
        serial_write("xHCI: REGS AFTER INIT ctl=");
        serial_write_dec(controller_index);
        serial_write(" usbcmd=");
        serial_write_hex(XHCI_MMIO_READ32(op, XHCI_USBCMD));
        serial_write(" usbsts=");
        serial_write_hex(XHCI_MMIO_READ32(op, XHCI_USBSTS));
        serial_write(" pagesize=");
        serial_write_hex(XHCI_MMIO_READ32(op, XHCI_PAGESIZE));
        serial_write(" dnctrl=");
        serial_write_hex(XHCI_MMIO_READ32(op, XHCI_DNCTRL));
        serial_write(" crcr=");
        serial_write_hex(cmd_ring | XHCI_CRCR_CYCLE);
        serial_write(" dcbaap=");
        serial_write_hex(dcbaa);
        serial_write(" config=");
        serial_write_hex(XHCI_MMIO_READ32(op, XHCI_CONFIG));
        serial_write(" iman=");
        serial_write_hex(XHCI_MMIO_READ32(rbase2, XHCI_IMAN_OFF));
        serial_write(" imod=");
        serial_write_hex(XHCI_MMIO_READ32(rbase2, XHCI_IMOD));
        serial_write(" erstsz=");
        serial_write_hex(XHCI_MMIO_READ32(rbase2, XHCI_ERSTSZ_OFF));
        serial_write(" erdp=");
        serial_write_hex(((uint64_t)erdp_hi << 32) | erdp_lo);
        serial_write("\r\n");
    }
#endif
    xhc_scratchpad_array_phys[controller_index] = scratch_array;
    c->running = 0;
    return 0;
}

/* Ensure every port is powered (PP=1). After HCRST real-silicon ports come
 * up unpowered when HCC PPC=1; CCS never asserts and port reset fails.
 * QEMU ignores PP, which is why this only bites on hardware. */
void xhc_power_all_ports(const rix_xhci_controller_t *c) {
    if (!c || !c->mmio_va || !c->max_ports) return;
    volatile uint8_t *base = (volatile uint8_t *)(uintptr_t)c->mmio_va;
    /* Unsigned iteration: a uint8_t counter would wrap to 0 and never
     * terminate if a controller ever reported max_ports == 255. */
    for (unsigned p = 1; p <= c->max_ports; ++p) {
        uint8_t port = (uint8_t)p;
        volatile uint32_t *reg = (volatile uint32_t *)(base + c->cap_length +
            XHCI_PORTSC_BASE + (uint32_t)(port - 1u) * XHCI_PORT_STRIDE);
        uint32_t v = *reg;
        if (v & XHCI_PORT_PP) continue;
        v = xhci_portsc_neutralize(v) | XHCI_PORT_PP | XHCI_PORT_CHANGE_MASK;
        *reg = v;
    }
    /* USB 2.0 power-stable delay (TPPWR ~20ms). */
    xhc_udelay(20000u);
}

int xhci_init(void) {
    xhc_count = 0;
    for (size_t i = 0; i < pci_device_count() && xhc_count < XHCI_MAX; ++i) {
        const rix_pci_device_t *d = pci_device(i);
        if (!xhc_is_xhci_device(d)) continue;
        {
            const xhci_profile_t *candidate_profile =
                xhci_profile_lookup(d->vendor_id, d->device_id);
            serial_write("xHCI: AMD/PCI candidate ");
            xhc_write_hex4(d->vendor_id);
            serial_write(":");
            xhc_write_hex4(d->device_id);
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
            uint64_t regs_va = xhc_map_range(bar_base, bar_size);
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
                rix_xhci_controller_t *c = &xhc_controllers[xhc_count];
                int handoff;
                c->bus = d->bus;
                c->device = d->device;
                c->function = d->function;
                c->vendor_id = d->vendor_id;
                c->device_id = d->device_id;
                c->bar0 = bar_base;
                c->mmio_va = regs_va;
                /* Fresh probe state: a previous candidate that failed at this
                 * index must not leak its protocol map into this one. */
                c->proto_ranges = 0;
                c->cap_length = base[XHCI_CAPLENGTH];
                c->hci_version = *(volatile uint16_t *)(base + XHCI_HCIVERSION);
                {
                    uint32_t hcs = XHCI_MMIO_READ32(base, XHCI_HCSPARAMS1);
                    c->max_slots = (uint8_t)(hcs & XHCI_MAX_SLOTS_MASK);
                    c->max_intrs = (uint8_t)((hcs & XHCI_MAX_INTRS_MASK) >> XHCI_MAX_INTRS_SHIFT);
                    c->max_ports = (uint8_t)((hcs & XHCI_MAX_PORTS_MASK) >> XHCI_MAX_PORTS_SHIFT);
                    c->hcc_params1 = XHCI_MMIO_READ32(base, XHCI_HCCPARAMS1);
                }
                if (c->cap_length < 0x20u || c->max_slots == 0u || c->max_ports == 0u) {
                    serial_write("xHCI: candidate reports no usable registers/slots/ports\r\n");
                    continue;
                }
                xhc_scan_protocols(c, base, bar_size);
                c->quirks = xhc_apply_profile_quirks(c);
                const xhci_profile_t *profile = xhc_controller_profile(c);
                unsigned profile_verdict = xhci_profile_verify(profile, c->max_ports,
                                                               (uint16_t)c->hci_version,
                                                               xhc_has_usb3_range(c));
                {
                    uint32_t was_owned = 0;
                    handoff = xhc_bios_handoff(base, bar_size, &was_owned);
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
                    c->usbcmd = XHCI_MMIO_READ32(op, XHCI_USBCMD);
                    c->usbsts = XHCI_MMIO_READ32(op, XHCI_USBSTS);
                    c->running = 0;
                    if (xhc_reset_controller(op) != 0) {
                        serial_write("xHCI: candidate reset failed\r\n");
                        continue;
                    }
                    int runtime_rc = xhc_setup_runtime(c, base, op, &xhc_runtimes[xhc_count]);
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
                    xhc_power_all_ports(c);
                    int rs_wait = 0;
#if XHCI_ADDR_TRACE
                    {
                        uint64_t rs_t0 = time_monotonic_ns();
                        serial_write("xHCI: RUN BEFORE usbcmd=");
                        serial_write_hex(XHCI_MMIO_READ32(op, XHCI_USBCMD));
                        serial_write(" usbsts=");
                        serial_write_hex(XHCI_MMIO_READ32(op, XHCI_USBSTS));
                        serial_write("\r\n");
#endif
                    *(volatile uint32_t *)(op + XHCI_USBCMD) |= XHCI_CMD_RUN;
                    rs_wait = (xhc_wait_halted(op, 0) != 0 || xhc_wait_cnr_clear(op) != 0);
#if XHCI_ADDR_TRACE
                        serial_write("xHCI: RUN AFTER usbcmd=");
                        serial_write_hex(XHCI_MMIO_READ32(op, XHCI_USBCMD));
                        serial_write(" usbsts=");
                        serial_write_hex(XHCI_MMIO_READ32(op, XHCI_USBSTS));
                        serial_write(" rc=");
                        serial_write_dec((uint64_t)(rs_wait < 0 ? -rs_wait : rs_wait));
                        serial_write(" elapsed_ns=");
                        serial_write_dec(time_monotonic_ns() - rs_t0);
                        serial_write("\r\n");
                    }
#endif
                    if (rs_wait) {
                        xhc_release_runtime_pages(c);
                        serial_write("xHCI: candidate run failed\r\n");
                        continue;
                    }
                    /* Power may have been lost across RS on some parts;
                     * enforce PP again now that the controller runs. */
                    xhc_power_all_ports(c);
                    c->usbcmd = XHCI_MMIO_READ32(op, XHCI_USBCMD);
                    c->usbsts = XHCI_MMIO_READ32(op, XHCI_USBSTS);
                    c->running = 1;
                    serial_write("xHCI: controller="); serial_write_dec(xhc_count);
                    serial_write(" PCI="); serial_write_hex(d->vendor_id);
                    serial_write(":"); serial_write_hex(d->device_id);
                    serial_write(" BAR="); serial_write_hex(bar_base);
                    serial_write(" operational="); serial_write_hex(bar_base + c->cap_length);
                    serial_write(" ports="); serial_write_dec(c->max_ports);
                    serial_write("\r\n");
                    {
                        uint32_t hcs1 = XHCI_MMIO_READ32(base, XHCI_HCSPARAMS1);
                        uint32_t hcs2 = XHCI_MMIO_READ32(base, XHCI_HCSPARAMS2);
                        uint32_t hcs3 = XHCI_MMIO_READ32(base, XHCI_HCSPARAMS3);
                        uint32_t hcc1 = XHCI_MMIO_READ32(base, XHCI_HCCPARAMS1);
                        uint32_t dboff = XHCI_MMIO_READ32(base, XHCI_DBOFF);
                        uint32_t rtsoff = XHCI_MMIO_READ32(base, XHCI_RTSOFF);
                        serial_write("xHCI: CAPS ctl=");
                        serial_write_dec(xhc_count);
                        serial_write(" class=");
                        serial_write_hex(d->class_code);
                        serial_write(" sub=");
                        serial_write_hex(d->subclass);
                        serial_write(" prog=");
                        serial_write_hex(d->prog_if);
                        serial_write(" barsz=");
                        serial_write_hex(bar_size);
                        serial_write(" mmio_va=");
                        serial_write_hex(regs_va);
                        serial_write(" caplen=");
                        serial_write_dec(c->cap_length);
                        serial_write(" hcs1=");
                        serial_write_hex(hcs1);
                        serial_write(" slots=");
                        serial_write_dec((uint64_t)(hcs1 & 0xffu));
                        serial_write(" intrs=");
                        serial_write_dec((uint64_t)((hcs1 >> 8) & 0x7ffu));
                        serial_write(" ports=");
                        serial_write_dec((uint64_t)((hcs1 >> 24) & 0xffu));
                        serial_write(" hcs2=");
                        serial_write_hex(hcs2);
                        serial_write(" hcs3=");
                        serial_write_hex(hcs3);
                        serial_write(" hcc1=");
                        serial_write_hex(hcc1);
                        serial_write(" ac64=");
                        serial_write_dec((uint64_t)((hcc1 & XHCI_HCC_AC64) != 0u));
                        serial_write(" dboff=");
                        serial_write_hex(dboff & ~0x3u);
                        serial_write(" rtsoff=");
                        serial_write_hex(rtsoff & ~0x1fu);
                        serial_write(" version=");
                        serial_write_hex(c->hci_version);
                        serial_write("\r\n");
                    }
                    {
                        serial_write("xHCI: ctl=");
                        serial_write_dec(xhc_count);
                        serial_write(" bus=");
                        serial_write_dec(d->bus);
                        serial_write(" dev=");
                        serial_write_dec(d->device);
                        serial_write(" fn=");
                        serial_write_dec(d->function);
                        serial_write(" id=");
                        xhc_write_hex4(c->vendor_id);
                        serial_write(":");
                        xhc_write_hex4(c->device_id);
                        serial_write(" hci=");
                        xhc_write_hex4((uint16_t)c->hci_version);
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
                                xhc_write_hex4((uint16_t)c->hci_version);
                                serial_write("/");
                                xhc_write_hex4(profile->expected_hci);
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
                    if (profile && profile->bad_ports && profile->bad_port_count) {
                        serial_write("xHCI: ctl=");
                        serial_write_dec(xhc_count);
                        serial_write(" known-bad=");
                        for (uint8_t bi = 0;
                             bi < profile->bad_port_count &&
                             bi < (uint8_t)XHCI_PROFILE_BAD_PORT_MAX; ++bi) {
                            if (bi) serial_write(",");
                            serial_write_dec(profile->bad_ports[bi]);
                        }
                        serial_write("\r\n");
                    }
                    ++xhc_count;
                }
            }
        }
    }
    return 0;
}

size_t xhci_controller_count(void) { return xhc_count; }
const rix_xhci_controller_t *xhci_controller(size_t index) {
    return index < xhc_count ? &xhc_controllers[index] : 0;
}

int xhci_port_status(size_t controller, uint8_t port, rix_xhci_port_status_t *out) {
    if (!out) return -1;
    if (controller >= xhc_count) return -2;
    volatile uint32_t *reg = xhc_port_reg(controller, port);
    if (!reg) return -2;
    uint32_t v = *reg;
    xhc_record_portsc(controller, port, v);
    out->connected = (v & XHCI_PORT_CCS) != 0u;
    out->enabled = (v & XHCI_PORT_PED) != 0u;
    out->speed = (uint8_t)((v & XHCI_PORT_SPEED_MASK) >> XHCI_PORT_SPEED_SHIFT);
    out->reset_complete = (v & XHCI_PORT_PRC) != 0u;
    return 0;
}
