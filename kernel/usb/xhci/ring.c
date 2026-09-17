/* Command ring submission and event ring consumption.
 *
 * Provenance: Linux drivers/usb/host/xhci-ring.c (queue_command,
 * command completion matching by command TRB pointer, event dequeue +
 * cycle tracking, ERDP update with EHB) and xhci-hub.c port-status-change
 * handling. Linux completions/IRQs become bounded paced polls; the
 * producer/consumer cycle semantics are preserved exactly.
 */

#include "xhc.h"

uint8_t xhc_pending_port[XHCI_MAX][XHCI_PENDING_PORTS];
uint8_t xhc_pending_conn[XHCI_MAX][XHCI_PENDING_PORTS];
uint8_t xhc_pending_head[XHCI_MAX];
uint8_t xhc_pending_tail[XHCI_MAX];
uint8_t xhc_pending_count[XHCI_MAX];
uint8_t xhc_pending_overflow[XHCI_MAX];

void xhc_pending_port_push(size_t ctl, uint8_t port, uint8_t connected) {
    if (ctl >= XHCI_MAX || port == 0u) return;
    for (uint8_t n = 0u, i = xhc_pending_head[ctl];
         n < xhc_pending_count[ctl]; ++n, i = (uint8_t)((i + 1u) % XHCI_PENDING_PORTS)) {
        if (xhc_pending_port[ctl][i] == port) {
            xhc_pending_conn[ctl][i] = connected;
            return;
        }
    }
    if (xhc_pending_count[ctl] >= XHCI_PENDING_PORTS) {
        xhc_pending_overflow[ctl] = 1;
        return;
    }
    xhc_pending_port[ctl][xhc_pending_tail[ctl]] = port;
    xhc_pending_conn[ctl][xhc_pending_tail[ctl]] = connected;
    xhc_pending_tail[ctl] = (uint8_t)((xhc_pending_tail[ctl] + 1u) % XHCI_PENDING_PORTS);
    xhc_pending_count[ctl]++;
}

int xhci_pending_port_pop(size_t ctl, uint8_t *port, uint8_t *connected) {
    if (!port || !connected || ctl >= XHCI_MAX || ctl >= xhc_count) return -1;
    if (xhc_pending_overflow[ctl]) {
        xhc_pending_overflow[ctl] = 0;
        return -2;
    }
    if (xhc_pending_count[ctl] == 0u) return 0;
    *port = xhc_pending_port[ctl][xhc_pending_head[ctl]];
    *connected = xhc_pending_conn[ctl][xhc_pending_head[ctl]];
    xhc_pending_head[ctl] =
        (uint8_t)((xhc_pending_head[ctl] + 1u) % XHCI_PENDING_PORTS);
    xhc_pending_count[ctl]--;
    return 1;
}

/* Advance the event-ring dequeue past one consumed TRB and publish it via
 * ERDP with the EHB handshake bit (Linux handle_tx_event / xhci_update_erst). */
void xhc_acknowledge_event(size_t ctl, xhci_runtime_t *rt) {
    uint64_t erdp;
    rt->event_dequeue++;
    if (rt->event_dequeue == XHCI_EVENT_RING_TRBS) {
        rt->event_dequeue = 0;
        rt->event_cycle ^= 1u;
    }
    erdp = xhc_controllers[ctl].event_ring_phys +
        (uint64_t)rt->event_dequeue * sizeof(rix_xhci_trb_t);
    xhc_write_mmio_ptr(xhc_run_base(ctl) + XHCI_ERDP_OFF,
                       erdp | XHCI_ERDP_EHB);
    rt->last_ev_ns = time_monotonic_ns();
}

/* Wait for the command-completion event whose command TRB pointer matches
 * (Linux xhci_wait_for_event command-completion path). Port Status Change
 * events met along the way are queued, never dropped. Returns 0 on
 * Success, -completion_code on HW error, -90 on zero code, -100 on timeout. */
int xhc_wait_command(size_t ctl, xhci_runtime_t *rt, uint64_t command_phys,
                     uint8_t *out_slot) {
    const rix_xhci_controller_t *c = &xhc_controllers[ctl];
    volatile rix_xhci_trb_t *events =
        (volatile rix_xhci_trb_t *)(uintptr_t)c->event_ring_phys;
    for (uint32_t i = 0; i < XHCI_POLL_LIMIT; ++i) {
        volatile rix_xhci_trb_t *event = &events[rt->event_dequeue];
        uint32_t control = event->control;
        if ((control & XHCI_TRB_CYCLE) != (rt->event_cycle ? XHCI_TRB_CYCLE : 0u)) {
            if ((i & 0xffu) == 0u) xhc_udelay(50u);
            continue;
        }
        uint32_t type = XHCI_TRB_FIELD_TO_TYPE(control);
        uint64_t parameter = ((uint64_t)event->parameter_hi << 32) | event->parameter_lo;
        uint8_t slot = (uint8_t)XHCI_TRB_TO_SLOT_ID(control);
        uint8_t completion = (uint8_t)XHCI_GET_COMP_CODE(event->status);
        uint64_t event_phys = c->event_ring_phys +
            (uint64_t)rt->event_dequeue * sizeof(rix_xhci_trb_t);
        if (type == XHCI_TRB_PORT_STATUS_CHANGE) {
            uint8_t p = (uint8_t)(event->parameter_lo >> 24);
            xhc_pending_port_push(ctl, p, 1);
            xhc_acknowledge_event(ctl, rt);
            XHCI_MMIO_WRITE32(xhc_op_base(ctl), XHCI_USBSTS,
                           XHCI_STS_PCD | XHCI_STS_EINT);
            continue;
        }
        xhc_acknowledge_event(ctl, rt);
        xhc_record_event(ctl, (uint8_t)type, event->status, slot, 0,
                         parameter);
        if (type != XHCI_TRB_COMMAND_COMPLETION || parameter != command_phys)
            continue;
        rt->last_cmd_done_ns = time_monotonic_ns();
        if (out_slot) *out_slot = slot;
        if (completion == XHCI_COMP_SUCCESS) return 0;
        if (completion == XHCI_COMP_CONTEXT_STATE_ERROR)
            xhc_trace_context_state_error(ctl, rt, event_phys, event->status, control);
        return completion != 0u ? -(int)completion : -90;
    }
    return -100;
}

/* Enqueue one command TRB on the command ring and ring doorbell 0
 * (Linux queue_command for the command ring). Handles link-TRB wrap with
 * toggle-cycle exactly like the transfer rings. */
int xhc_submit_command(size_t ctl, uint64_t parameter, uint32_t control,
                       uint8_t *out_slot) {
    xhci_runtime_t *rt;
    volatile rix_xhci_trb_t *ring;
    uint16_t index;
    uint64_t command_phys;
    if (ctl >= xhc_count) return -1;
    if (!xhc_controllers[ctl].running || !xhc_controllers[ctl].cmd_ring_phys ||
        !xhc_controllers[ctl].event_ring_phys)
        return -2;
    rt = &xhc_runtimes[ctl];
    ring = (volatile rix_xhci_trb_t *)(uintptr_t)xhc_controllers[ctl].cmd_ring_phys;
    if (rt->command_enqueue >= XHCI_CMD_RING_TRBS - 1u) {
        ring[XHCI_CMD_RING_TRBS - 1u].parameter_lo =
            (uint32_t)xhc_controllers[ctl].cmd_ring_phys;
        ring[XHCI_CMD_RING_TRBS - 1u].parameter_hi =
            (uint32_t)(xhc_controllers[ctl].cmd_ring_phys >> 32);
        ring[XHCI_CMD_RING_TRBS - 1u].status = 0;
        ring[XHCI_CMD_RING_TRBS - 1u].control = XHCI_TRB_TYPE(XHCI_TRB_LINK) |
            XHCI_TRB_TC | (rt->command_cycle ? XHCI_TRB_CYCLE : 0u);
        __asm__ volatile("mfence" ::: "memory");
        rt->command_enqueue = 0;
        rt->command_cycle ^= 1u;
    }
    index = rt->command_enqueue;
    command_phys = xhc_controllers[ctl].cmd_ring_phys +
        (uint64_t)index * sizeof(rix_xhci_trb_t);
    ring[index].parameter_lo = (uint32_t)parameter;
    ring[index].parameter_hi = (uint32_t)(parameter >> 32);
    ring[index].status = 0;
    ring[index].control =
        (control & ~XHCI_TRB_CYCLE) | (rt->command_cycle ? XHCI_TRB_CYCLE : 0u);
    rt->command_enqueue = (uint16_t)(index + 1u);
    __asm__ volatile("mfence" ::: "memory");
    rt->last_cmd_db_ns = time_monotonic_ns();
    xhc_record_doorbell(ctl, 0, 0xfeu, 0);
    xhc_doorbell(ctl, 0, XHCI_DB_HOST);
    return xhc_wait_command(ctl, rt, command_phys, out_slot);
}

/* Non-destructive peek for Port Status Change events (Linux hub-event
 * path without an IRQ): consumes only matching event TRBs. Returns 1 with
 * the live connection state, 0 when no event is ready. */
int xhci_poll_port_status_change(size_t ctl, uint8_t *port, uint8_t *connected) {
    const rix_xhci_controller_t *c;
    xhci_runtime_t *rt;
    volatile rix_xhci_trb_t *event;
    uint32_t control, type;
    uint8_t event_port;
    rix_xhci_port_status_t status;
    if (!port || !connected) return -1;
    *port = 0;
    *connected = 0;
    if (ctl >= xhc_count) return -1;
    c = &xhc_controllers[ctl];
    if (!c->running || !c->event_ring_phys) return -2;
    rt = &xhc_runtimes[ctl];
    if (xhci_pending_port_pop(ctl, port, connected) == 1) {
        if (xhci_port_status(ctl, *port, &status) != 0) return -4;
        *connected = status.connected;
        return 1;
    }
    event = &((volatile rix_xhci_trb_t *)(uintptr_t)c->event_ring_phys)[rt->event_dequeue];
    control = event->control;
    if ((control & XHCI_TRB_CYCLE) != (rt->event_cycle ? XHCI_TRB_CYCLE : 0u))
        return 0;
    type = XHCI_TRB_FIELD_TO_TYPE(control);
    if (type != XHCI_TRB_PORT_STATUS_CHANGE) return 0;
    event_port = (uint8_t)(event->parameter_lo >> 24);
    xhc_acknowledge_event(ctl, rt);
    XHCI_MMIO_WRITE32(xhc_op_base(ctl), XHCI_USBSTS,
                   XHCI_STS_PCD | XHCI_STS_EINT);
    if (event_port == 0u || event_port > c->max_ports) return -3;
    if (xhci_port_status(ctl, event_port, &status) != 0) return -4;
    *port = event_port;
    *connected = status.connected;
    return 1;
}
