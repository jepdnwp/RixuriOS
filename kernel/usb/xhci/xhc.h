/* Internal shared header for the RixuriOS native xHCI driver.
 *
 * Provenance: structure follows Linux drivers/usb/host/xhci.h
 * (ring/cycle/segment model, command/event/TRB handling split across
 * xhci-ring.c, xhci-mem.c, xhci-hub.c); every access uses native RixuriOS
 * MMIO/DMA/sync primitives directly — no Linux compatibility layer.
 * Not included outside kernel/usb/xhci/.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../xhci.h"
#include "../../pci/pci.h"
#include "../../mm/vmm.h"
#include "../../mm/pmm.h"
#include "../../serial.h"
#include "../../time/time.h"
#include "kernel.h"

#include "regs.h"
#include "trb.h"
#include "portsc.h"
#include "caps.h"
#include "profile.h"
#include "../usb_ch9.h"
#include "../hid_defs.h"

/* ---- Compile-time trace flags (same defaults as the previous driver) ---- */
#define XHCI_ADDR_TRACE 1
#define XHCI_EP0_TRACE 1
#define XHCI_CC4_SNAPSHOT 1
#define XHCI_HID_TRACE 1
#define XHCI_RESET_TRACE 1

#define XHCI_MAX 4
#define XHCI_MAX_SLOTS 256u

#define PCI_CLASS_SERIAL 0x0C
#define PCI_SUBCLASS_USB 0x03
#define PCI_PROGIF_XHCI 0x30
#define PCI_COMMAND 0x04
#define PCI_COMMAND_MEMORY (1u << 1)
#define PCI_COMMAND_BUS_MASTER (1u << 2)

/* Bounded polls: never infinite, always paced (replaces Linux
 * completion/wait_event; 5000ms-class command timeouts). */
#define XHCI_POLL_LIMIT 1000000u
#define XHCI_RESET_POLL_LIMIT 5000000u
#define XHCI_INTR_POLL_LIMIT 25000u
/* Interrupt-IN idle bound (~5ms): interrupt endpoints NAK while idle, so a
 * full-length wait would busy-block the cooperative scheduler ~200ms per
 * keyboard per loop with no key pressed. The TD stays live (single-flight)
 * and later polls resume waiting on it, so nothing is lost by returning
 * XHCI_XFER_TIMEOUT early. Control/bulk keep the full bound. */

/* Native MMIO idiom (cf. e1000/rtl8125/nvme drivers): direct volatile
 * 32-bit access. 64-bit pointer registers (CRCR/DCBAAP/ERSTBA/ERDP) need
 * paired low-then-high dword stores via xhc_write_mmio_ptr() in core.c. */
#define XHCI_MMIO_READ32(base, off) (*(volatile uint32_t *)((base) + (off)))
#define XHCI_MMIO_WRITE32(base, off, v) (*(volatile uint32_t *)((base) + (off)) = (v))

/* Pure 64 KiB-boundary test for transfer buffers (xHCI 4.11.7.1: one
 * data TRB must not span a 64 KiB boundary; Linux splits such transfers
 * in queue_bulk_tx via TRB_MAX_BUFF_*. This driver keeps single-TRB TDs
 * by design, so buffers must not cross instead — enforced by
 * xhc_dma_linear_pa, guaranteed by aligning every transfer buffer to
 * its own size). */
static inline int xhc_pa_crosses_64k(uint64_t pa, uint64_t length) {
    uint64_t end;
    if (length == 0u) return 0;
    if (pa > UINT64_MAX - (length - 1u)) return 1;
    end = pa + length - 1u;
    return (pa & ~0xffffULL) != (end & ~0xffffULL);
}

/* Transfer wait timeout code (see xhc_wait_transfer): the TD stays owned
 * by the controller, so callers must not orphan another TD on top of it. */
#define XHCI_XFER_TIMEOUT (-100)

/* Endpoint runtime: one transfer ring per DCI (Linux xhci_virt_ep ring). */
typedef struct {
    uint64_t ring_phys;
    uint8_t type;
    uint8_t cycle;
    uint16_t enqueue;
    /* Single-flight TD tracking: at most one TD is ever outstanding per
     * endpoint. in_flight_first/last bracket the live TD's TRB range;
     * cleared on completion/error, on ring replacement, and on slot
     * teardown. Never cleared by a timeout — the TD is still live. */
    uint8_t in_flight;
    uint64_t in_flight_first;
    uint64_t in_flight_last;
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
    uint32_t ep0_seq;
    uint64_t addr_done_ns;
    uint8_t usb_state;
    /* Hub topology: parent hub slot for hub-attached devices (0 when the
     * device sits on a root port). Only the hotplug/attach paths read it. */
    uint8_t tt_parent_slot;
    /* Root hub port number for the whole chain (xHCI RH Port field).
     * Equals port for root-attached devices; resolved through the parent
     * chain for hub children. Never a hub-relative number. */
    uint8_t root_port;
    xhci_endpoint_runtime_t endpoints[32];
} xhci_slot_runtime_t;

typedef struct {
    uint16_t command_enqueue;
    uint16_t event_dequeue;
    uint8_t command_cycle;
    uint8_t event_cycle;
    uint64_t last_ev_ns;
    uint64_t last_cmd_db_ns;
    uint64_t last_cmd_done_ns;
    xhci_slot_runtime_t slots[XHCI_MAX_SLOTS];
} xhci_runtime_t;

/* ---- Shared state (owned by core.c) ---- */
extern rix_xhci_controller_t xhc_controllers[XHCI_MAX];
extern xhci_runtime_t xhc_runtimes[XHCI_MAX];
extern size_t xhc_count;
extern uint64_t xhc_scratchpad_array_phys[XHCI_MAX];

/* Pending Port Status Change queue (owned by ring.c). */
#define XHCI_PENDING_PORTS 16u
extern uint8_t xhc_pending_port[XHCI_MAX][XHCI_PENDING_PORTS];
extern uint8_t xhc_pending_conn[XHCI_MAX][XHCI_PENDING_PORTS];
extern uint8_t xhc_pending_head[XHCI_MAX];
extern uint8_t xhc_pending_tail[XHCI_MAX];
extern uint8_t xhc_pending_count[XHCI_MAX];
extern uint8_t xhc_pending_overflow[XHCI_MAX];

/* Port diagnostics (owned by hub.c). */
extern uint64_t xhc_port_reset_start_ns[XHCI_MAX][256];
extern uint8_t xhc_port_attach_failed[XHCI_MAX][256];
extern uint64_t xhc_port_fail_ns[XHCI_MAX][256];
extern uint64_t xhc_port_reset_done_ns[XHCI_MAX][256];
extern uint8_t xhc_last_selected[XHCI_MAX];
#define XHCI_PORT_RETRY_NS 10000000000ULL

/* Diagnostic histories (owned by debug.c). */
#define XHCI_DIAG_HIST 64u
typedef struct { uint64_t t; uint8_t ctl, slot, ep; uint32_t value; } xhc_db_hist_t;
/* status is the raw event status dword (residual + completion code for
 * transfer events, parameter + code for command completions); the dump
 * decodes both, so a CC=4 shows whether anything arrived at all. */
typedef struct {
    uint64_t t; uint8_t ctl, type, slot, ep; uint32_t status; uint64_t param;
} xhc_ev_hist_t;
extern xhc_db_hist_t xhc_db_hist[XHCI_DIAG_HIST];
extern uint64_t xhc_db_hist_n;
extern xhc_ev_hist_t xhc_ev_hist[XHCI_DIAG_HIST];
extern uint64_t xhc_ev_hist_n;
extern uint32_t xhc_portsc_hist[XHCI_MAX][256][8];
extern uint8_t xhc_portsc_hist_n[XHCI_MAX][256];

/* ---- Small MMIO/register helpers (Linux readl/writel/xhci helpers) ---- */
static inline volatile uint8_t *xhc_cap_base(size_t ctl) {
    return (volatile uint8_t *)(uintptr_t)xhc_controllers[ctl].mmio_va;
}
static inline volatile uint8_t *xhc_op_base(size_t ctl) {
    return xhc_cap_base(ctl) + xhc_controllers[ctl].cap_length;
}
static inline volatile uint8_t *xhc_run_base(size_t ctl) {
    uint32_t rt_off = XHCI_MMIO_READ32(xhc_cap_base(ctl), XHCI_RTSOFF) & ~0x1fu;
    return xhc_cap_base(ctl) + rt_off;
}
static inline volatile uint32_t *xhc_port_reg(size_t ctl, uint8_t port) {
    const rix_xhci_controller_t *c = &xhc_controllers[ctl];
    if (!c->running || port == 0u || port > c->max_ports) return 0;
    return (volatile uint32_t *)(xhc_cap_base(ctl) + c->cap_length +
        XHCI_PORTSC_BASE + (uint32_t)(port - 1u) * XHCI_PORT_STRIDE);
}
static inline uint32_t xhc_db_off(size_t ctl) {
    return XHCI_MMIO_READ32(xhc_cap_base(ctl), XHCI_DBOFF) & ~0x3u;
}
static inline void xhc_doorbell(size_t ctl, uint8_t slot, uint32_t target) {
    volatile uint32_t *db = (volatile uint32_t *)(xhc_cap_base(ctl) +
        xhc_db_off(ctl) + (uint32_t)slot * 4u);
    *db = target;
    /* Flush the posted write like Linux's readl after the doorbell writel. */
    (void)*db;
}

/* ---- core.c ---- */
uint64_t xhc_map_range(uint64_t base, uint64_t length);
void xhc_write_mmio_ptr(volatile void *reg, uint64_t value);
void xhc_zero_page(uint64_t phys);
uint64_t xhc_dma_page(const rix_xhci_controller_t *c);
void xhc_pause_delay(uint32_t pauses);
void xhc_udelay(uint32_t us);
int xhc_wait_halted(volatile uint8_t *op, int halted);
int xhc_wait_cnr_clear(volatile uint8_t *op);
int xhc_reset_controller(volatile uint8_t *op);
void xhc_release_runtime_pages(rix_xhci_controller_t *c);
int xhc_setup_runtime(rix_xhci_controller_t *c, volatile uint8_t *cap,
                      volatile uint8_t *op, xhci_runtime_t *rt);
void xhc_power_all_ports(const rix_xhci_controller_t *c);
int xhc_has_usb3_range(const rix_xhci_controller_t *c);
void xhc_write_hex4(uint16_t v);
const xhci_profile_t *xhc_controller_profile(const rix_xhci_controller_t *c);
uint32_t xhc_apply_profile_quirks(const rix_xhci_controller_t *c);
void xhc_scan_protocols(rix_xhci_controller_t *c, volatile uint8_t *base,
                        uint64_t mmio_size);
int xhc_bios_handoff(volatile uint8_t *base, uint64_t mmio_size,
                     uint32_t *was_owned);

/* ---- ring.c (Linux xhci-ring.c: command + event rings) ---- */
void xhc_acknowledge_event(size_t ctl, xhci_runtime_t *rt);
void xhc_pending_port_push(size_t ctl, uint8_t port, uint8_t connected);
int xhc_wait_command(size_t ctl, xhci_runtime_t *rt, uint64_t command_phys,
                     uint8_t *out_slot);
int xhc_submit_command(size_t ctl, uint64_t parameter, uint32_t control,
                       uint8_t *out_slot);

/* ---- slot.c (Linux xhci-mem.c device contexts + slot commands) ---- */
uint16_t xhc_initial_ep0_mps(uint8_t speed);
int xhc_allocate_slot_context(size_t ctl, xhci_slot_runtime_t *slot);
void xhc_release_slot_context(size_t ctl, uint8_t slot_id);
int xhc_prepare_address_context(size_t ctl, uint8_t slot_id, uint8_t port,
                                uint8_t speed,
                                const rix_xhci_tt_info_t *tt);
int xhc_reset_ep0_for_retry(size_t ctl, uint8_t slot_id);
int xhc_set_ep0_dequeue_for_retry(size_t ctl, uint8_t slot_id,
                                  uint16_t enqueue, uint8_t cycle);
int xhc_evaluate_ep0_mps(size_t ctl, uint8_t slot_id, uint8_t mps);

/* ---- xfer.c (Linux xhci-ring.c transfer path + enumeration) ---- */
void xhc_ep0_write_link(size_t ctl, xhci_slot_runtime_t *slot);
uint64_t xhc_ep0_emit(size_t ctl, xhci_slot_runtime_t *slot,
                      uint64_t parameter, uint32_t status, uint32_t control);
uint64_t xhc_dma_linear_pa(const void *buffer, uint64_t length);
int xhc_wait_transfer(size_t ctl, xhci_runtime_t *rt, uint64_t first_phys,
                      uint64_t last_phys, uint8_t slot_id, uint8_t ep_id,
                      uint16_t requested, uint16_t *actual);
int xhc_wait_transfer_limit(size_t ctl, xhci_runtime_t *rt, uint64_t first_phys,
                            uint64_t last_phys, uint8_t slot_id, uint8_t ep_id,
                            uint16_t requested, uint16_t *actual,
                            uint32_t poll_limit);

/* ---- ep.c (non-control endpoints) ---- */
unsigned xhc_ep_interval(uint8_t speed, uint8_t ep_type, uint8_t binterval);
int xhc_endpoint_transfer(size_t ctl, uint8_t slot_id, uint8_t endpoint_address,
                          uint8_t ep_type, void *buffer, uint16_t length,
                          uint16_t *actual_length);

/* ---- hub.c (Linux xhci-hub.c: ports + hotplug) ---- */
int xhc_wait_usb3_trained(volatile uint32_t *reg);
int xhc_warm_reset_port(volatile uint32_t *reg);
void xhc_force_rxdetect(volatile uint32_t *reg);
int xhc_power_cycle_port(volatile uint32_t *reg);
int xhc_port_enabled(volatile uint32_t *reg);
int xhc_usb2_reset(volatile uint32_t *reg);
void xhc_clear_port_change(volatile uint32_t *reg);

/* ---- debug.c (replaces Linux xhci trace/debugfs with serial diagnostics) ---- */
const char *xhc_cc_name(uint8_t cc);
const char *xhc_speed_name(uint8_t speed);
const char *xhc_setup_req_name(uint8_t request_type, uint8_t request);
const char *xhc_desc_name(uint8_t desc_type);
const char *xhc_usb_state_name(uint8_t s);
void xhc_record_doorbell(size_t ctl, uint8_t slot, uint8_t ep, uint32_t value);
void xhc_record_event(size_t ctl, uint8_t type, uint32_t status,
                      uint8_t slot, uint8_t ep, uint64_t param);
void xhc_record_portsc(size_t ctl, uint8_t port, uint32_t raw);
void xhc_dump_portsc_hist(size_t ctl, uint8_t port);
void xhc_dump_histories(void);
void xhc_trace_portsc(const char *tag, volatile uint32_t *reg);
void xhc_trace_context_state_error(size_t ctl, xhci_runtime_t *rt,
                                   uint64_t event_phys, uint32_t status,
                                   uint32_t control);
void xhc_log_address_device_begin(size_t ctl, uint8_t slot_id, uint8_t port,
                                  uint8_t speed);
void xhc_log_ep0_trb(const char *tag, uint64_t phys);
void xhc_log_ep0_context_and_ring(size_t ctl, uint8_t slot_id);
void xhc_cc4_snapshot(size_t ctl, xhci_runtime_t *rt, uint8_t slot_id,
                      uint8_t ep_id, uint64_t first_phys, uint64_t last_phys);
