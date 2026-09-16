/* Diagnostics: completion-code names, bounded histories, snapshots.
 *
 * Provenance: replaces Linux xhci-trace.h / xhci-debugfs with serial-only
 * diagnostics (physical UC-VRAM consoles repaint per line, so detail stays
 * on COM1). The completion-code table mirrors xhci_trb_comp_code_string;
 * the CC4 snapshot decodes the failing TD the same way the old driver did.
 */

#include "xhc.h"

xhc_db_hist_t xhc_db_hist[XHCI_DIAG_HIST];
uint64_t xhc_db_hist_n;
xhc_ev_hist_t xhc_ev_hist[XHCI_DIAG_HIST];
uint64_t xhc_ev_hist_n;
uint32_t xhc_portsc_hist[XHCI_MAX][256][8];
uint8_t xhc_portsc_hist_n[XHCI_MAX][256];

const char *xhc_cc_name(uint8_t cc) {
    static const char *const names[] = {
        "Invalid", "Success", "Data Buffer Error", "Babble Detected",
        "USB Transaction Error", "TRB Error", "Stall Error",
        "Resource Error", "Bandwidth Error", "No Slots Available",
        "Invalid Stream Type", "Slot Not Enabled", "Endpoint Not Enabled",
        "Short Packet", "Ring Underrun", "Ring Overrun",
        "VF Event Ring Full", "Parameter Error", "Bandwidth Overrun",
        "Context State Error", "No Ping Response", "Event Ring Full",
        "Incompatible Device", "Missed Service", "Command Ring Stopped",
        "Command Aborted", "Stopped", "Stopped - Length Invalid",
        "Stopped - Short Packet", "Max Exit Latency Too Large",
        "Isoch Buffer Overrun", "Event Lost", "Undefined",
        "Invalid Stream ID", "Secondary Bandwidth", "Split Transaction"
    };
    if (cc < sizeof(names) / sizeof(names[0])) return names[cc];
    return "Unknown";
}

const char *xhc_speed_name(uint8_t speed) {
    switch (speed) {
    case 1u: return "Full";
    case 2u: return "Low";
    case 3u: return "High";
    case 4u: return "Super";
    case 5u: return "SuperPlus";
    default: return "?";
    }
}

const char *xhc_setup_req_name(uint8_t request_type, uint8_t request) {
    if ((request_type & USB_TYPE_MASK) == USB_TYPE_CLASS) {
        switch (request) {
        case HID_REQ_GET_REPORT: return "GET_REPORT";
        case HID_REQ_GET_IDLE: return "GET_IDLE";
        case HID_REQ_GET_PROTOCOL: return "GET_PROTOCOL";
        case HID_REQ_SET_REPORT: return "SET_REPORT";
        case HID_REQ_SET_IDLE: return "SET_IDLE";
        case HID_REQ_SET_PROTOCOL: return "SET_PROTOCOL";
        default: return "CLASS?";
        }
    }
    switch (request) {
    case USB_REQ_GET_STATUS: return "GET_STATUS";
    case USB_REQ_CLEAR_FEATURE: return "CLEAR_FEATURE";
    case USB_REQ_SET_FEATURE: return "SET_FEATURE";
    case USB_REQ_SET_ADDRESS: return "SET_ADDRESS";
    case USB_REQ_GET_DESCRIPTOR: return "GET_DESCRIPTOR";
    case USB_REQ_SET_DESCRIPTOR: return "SET_DESCRIPTOR";
    case USB_REQ_GET_CONFIGURATION: return "GET_CONFIGURATION";
    case USB_REQ_SET_CONFIGURATION: return "SET_CONFIGURATION";
    case USB_REQ_GET_INTERFACE: return "GET_INTERFACE";
    case USB_REQ_SET_INTERFACE: return "SET_INTERFACE";
    case USB_REQ_SYNCH_FRAME: return "SYNCH_FRAME";
    default: return "STD?";
    }
}

const char *xhc_desc_name(uint8_t desc_type) {
    switch (desc_type) {
    case USB_DT_DEVICE: return "DEVICE";
    case USB_DT_CONFIG: return "CONFIG";
    case USB_DT_STRING: return "STRING";
    case USB_DT_INTERFACE: return "INTERFACE";
    case USB_DT_ENDPOINT: return "ENDPOINT";
    case USB_DT_DEVICE_QUALIFIER: return "DEV_QUAL";
    case USB_DT_OTHER_SPEED_CONFIG: return "OTHER_SPEED";
    case USB_DT_OTG: return "OTG";
    case USB_DT_INTERFACE_ASSOCIATION: return "ASSOC";
    case HID_DT_HID: return "HID";
    case HID_DT_REPORT: return "HID_REPORT";
    case USB_DT_CS_INTERFACE: return "CS_IFACE";
    case USB_DT_CS_ENDPOINT: return "CS_EP";
    case USB_DT_SS_ENDPOINT_COMP: return "SS_COMP";
    default: return "?";
    }
}

const char *xhc_usb_state_name(uint8_t s) {
    switch (s) {
    case XHCI_USB_DETACHED: return "DETACHED";
    case XHCI_USB_DEFAULT: return "DEFAULT";
    case XHCI_USB_ADDRESSED: return "ADDRESSED";
    case XHCI_USB_CONFIGURED: return "CONFIGURED";
    default: return "?";
    }
}

void xhci_usb_state_transition(size_t controller, uint8_t slot_id,
                               uint8_t new_state, const char *reason) {
    xhci_slot_runtime_t *slot;
    if (controller >= xhc_count || slot_id == 0u ||
        slot_id > xhc_controllers[controller].max_slots || !reason)
        return;
    slot = &xhc_runtimes[controller].slots[slot_id];
#if XHCI_ADDR_TRACE
    serial_write("xHCI: usb-state ctl=");
    serial_write_dec(controller);
    serial_write(" slot=");
    serial_write_dec(slot_id);
    serial_write(" ");
    serial_write(xhc_usb_state_name(slot->usb_state));
    serial_write("->");
    serial_write(xhc_usb_state_name(new_state));
    serial_write(" ");
    serial_write(reason);
    serial_write("\r\n");
#else
    (void)slot;
#endif
    slot->usb_state = new_state;
}

void xhc_record_doorbell(size_t ctl, uint8_t slot, uint8_t ep,
                         uint32_t value) {
    xhc_db_hist[xhc_db_hist_n % XHCI_DIAG_HIST].t = time_monotonic_ns();
    xhc_db_hist[xhc_db_hist_n % XHCI_DIAG_HIST].ctl = (uint8_t)ctl;
    xhc_db_hist[xhc_db_hist_n % XHCI_DIAG_HIST].slot = slot;
    xhc_db_hist[xhc_db_hist_n % XHCI_DIAG_HIST].ep = ep;
    xhc_db_hist[xhc_db_hist_n % XHCI_DIAG_HIST].value = value;
    xhc_db_hist_n++;
}

void xhc_record_event(size_t ctl, uint8_t type, uint8_t cc, uint8_t slot,
                      uint8_t ep, uint64_t param) {
    xhc_ev_hist[xhc_ev_hist_n % XHCI_DIAG_HIST].t = time_monotonic_ns();
    xhc_ev_hist[xhc_ev_hist_n % XHCI_DIAG_HIST].ctl = (uint8_t)ctl;
    xhc_ev_hist[xhc_ev_hist_n % XHCI_DIAG_HIST].type = type;
    xhc_ev_hist[xhc_ev_hist_n % XHCI_DIAG_HIST].cc = cc;
    xhc_ev_hist[xhc_ev_hist_n % XHCI_DIAG_HIST].slot = slot;
    xhc_ev_hist[xhc_ev_hist_n % XHCI_DIAG_HIST].ep = ep;
    xhc_ev_hist[xhc_ev_hist_n % XHCI_DIAG_HIST].param = param;
    xhc_ev_hist_n++;
}

void xhc_record_portsc(size_t ctl, uint8_t port, uint32_t raw) {
    uint8_t n;
    if (ctl >= XHCI_MAX) return;
    n = xhc_portsc_hist_n[ctl][port];
    xhc_portsc_hist[ctl][port][n % 8u] = raw;
    xhc_portsc_hist_n[ctl][port] = (uint8_t)(n + 1u);
}

void xhc_dump_portsc_hist(size_t ctl, uint8_t port) {
    uint8_t n;
    uint8_t count;
    uint8_t base;
    if (ctl >= XHCI_MAX) return;
    n = xhc_portsc_hist_n[ctl][port];
    count = n > 8u ? 8u : n;
    base = (uint8_t)((n - count) % 8u);
    serial_write("xHCI: PORTSC-hist ctl=");
    serial_write_dec(ctl);
    serial_write(" port=");
    serial_write_dec(port);
    for (uint8_t i = 0; i < count; ++i) {
        serial_write(" ");
        serial_write_hex(xhc_portsc_hist[ctl][port][(uint8_t)((base + i) % 8u)]);
    }
    serial_write("\r\n");
}

void xhc_dump_histories(void) {
    uint64_t n = xhc_db_hist_n < XHCI_DIAG_HIST ? xhc_db_hist_n : XHCI_DIAG_HIST;
    uint64_t base = xhc_db_hist_n < XHCI_DIAG_HIST ? 0u : xhc_db_hist_n - XHCI_DIAG_HIST;
    serial_write("xHCI: doorbell-hist n=");
    serial_write_dec(n);
    serial_write("\r\n");
    for (uint64_t i = 0; i < n; ++i) {
        const xhc_db_hist_t *h = &xhc_db_hist[(base + i) % XHCI_DIAG_HIST];
        serial_write(" db t=");
        serial_write_dec(h->t);
        serial_write(" ctl=");
        serial_write_dec(h->ctl);
        serial_write(" slot=");
        serial_write_dec(h->slot);
        serial_write(" ep=");
        serial_write_dec(h->ep);
        serial_write(" v=");
        serial_write_hex(h->value);
        serial_write("\r\n");
    }
    n = xhc_ev_hist_n < XHCI_DIAG_HIST ? xhc_ev_hist_n : XHCI_DIAG_HIST;
    base = xhc_ev_hist_n < XHCI_DIAG_HIST ? 0u : xhc_ev_hist_n - XHCI_DIAG_HIST;
    serial_write("xHCI: event-hist n=");
    serial_write_dec(n);
    serial_write("\r\n");
    for (uint64_t i = 0; i < n; ++i) {
        const xhc_ev_hist_t *h = &xhc_ev_hist[(base + i) % XHCI_DIAG_HIST];
        serial_write(" ev t=");
        serial_write_dec(h->t);
        serial_write(" ctl=");
        serial_write_dec(h->ctl);
        serial_write(" type=");
        serial_write_dec(h->type);
        serial_write(" cc=");
        serial_write_dec(h->cc);
        serial_write(" ");
        serial_write(xhc_cc_name(h->cc));
        serial_write(" slot=");
        serial_write_dec(h->slot);
        serial_write(" ep=");
        serial_write_dec(h->ep);
        serial_write(" param=");
        serial_write_hex(h->param);
        serial_write("\r\n");
    }
}

#if XHCI_RESET_TRACE
void xhc_trace_portsc(const char *tag, volatile uint32_t *reg) {
    size_t ctl = 0;
    uint8_t port = 0;
    int found = 0;
    uint32_t v;
    for (size_t ci = 0; ci < xhc_count; ++ci) {
        const rix_xhci_controller_t *c = &xhc_controllers[ci];
        uintptr_t base = (uintptr_t)c->mmio_va;
        uintptr_t portsc0 = base + c->cap_length + XHCI_PORTSC_BASE;
        uintptr_t r = (uintptr_t)reg;
        if (c->mmio_va && c->max_ports &&
            r >= portsc0 &&
            r < portsc0 + (uintptr_t)c->max_ports * XHCI_PORT_STRIDE &&
            ((r - portsc0) % XHCI_PORT_STRIDE) == 0u) {
            ctl = ci;
            port = (uint8_t)((r - portsc0) / XHCI_PORT_STRIDE + 1u);
            found = 1;
            break;
        }
    }
    if (!found) return;
    v = *reg;
    serial_write("xHCI: ");
    serial_write(tag);
    serial_write(" ctl=");
    serial_write_dec(ctl);
    serial_write(" port=");
    serial_write_dec(port);
    serial_write(" PORTSC=");
    serial_write_hex(v);
    serial_write(" CCS=");
    serial_write_dec((uint64_t)((v & XHCI_PORT_CCS) != 0u));
    serial_write(" PED=");
    serial_write_dec((uint64_t)((v & XHCI_PORT_PED) != 0u));
    serial_write(" PR=");
    serial_write_dec((uint64_t)((v & XHCI_PORT_PR) != 0u));
    serial_write(" PRC=");
    serial_write_dec((uint64_t)((v & XHCI_PORT_PRC) != 0u));
    serial_write(" PLS=");
    serial_write_dec((uint64_t)((v & XHCI_PORT_PLS_MASK) >> 5));
    serial_write(" PP=");
    serial_write_dec((uint64_t)((v & XHCI_PORT_PP) != 0u));
    serial_write(" speed=");
    serial_write_dec((uint64_t)((v & XHCI_PORT_SPEED_MASK) >> XHCI_PORT_SPEED_SHIFT));
    serial_write(" CSC=");
    serial_write_dec((uint64_t)((v & XHCI_PORT_CSC) != 0u));
    serial_write(" PEC=");
    serial_write_dec((uint64_t)((v & XHCI_PORT_PEC) != 0u));
    serial_write(" WRC=");
    serial_write_dec((uint64_t)((v & XHCI_PORT_WRC) != 0u));
    serial_write(" OCC=");
    serial_write_dec((uint64_t)((v & XHCI_PORT_OCC) != 0u));
    serial_write(" PLC=");
    serial_write_dec((uint64_t)((v & XHCI_PORT_PLC) != 0u));
    serial_write(" CEC=");
    serial_write_dec((uint64_t)((v & XHCI_PORT_CEC) != 0u));
    serial_write("\r\n");
}
#else
void xhc_trace_portsc(const char *tag, volatile uint32_t *reg) {
    (void)tag;
    (void)reg;
}
#endif

/* Context-State-Error forensics: log the event plus the addressed slot's
 * port/speed/route, contexts and endpoint ring state (read-only). */
void xhc_trace_context_state_error(size_t ctl, xhci_runtime_t *rt,
                                   uint64_t event_phys, uint32_t status,
                                   uint32_t control) {
    uint8_t cc = (uint8_t)XHCI_GET_COMP_CODE(status);
    uint8_t slot_id;
    uint8_t ep_id;
    if (cc != XHCI_COMP_CONTEXT_STATE_ERROR) return;
    slot_id = (uint8_t)XHCI_TRB_TO_SLOT_ID(control);
    ep_id = (uint8_t)XHCI_TRB_TO_EP_ID(control);
    serial_write("xHCI: CTX-STATE-ERR ctl=");
    serial_write_dec(ctl);
    serial_write(" ev=");
    serial_write_hex(event_phys);
    serial_write(" status=");
    serial_write_hex(status);
    serial_write(" control=");
    serial_write_hex(control);
    serial_write(" slot=");
    serial_write_dec(slot_id);
    serial_write(" ep=");
    serial_write_dec(ep_id);
    serial_write("\r\n");
    if (ctl >= xhc_count || slot_id == 0u ||
        slot_id > xhc_controllers[ctl].max_slots)
        return;
    {
        xhci_slot_runtime_t *slot = &rt->slots[slot_id];
        serial_write("xHCI: slot port=");
        serial_write_dec(slot->port);
        serial_write(" speed=");
        serial_write_dec(slot->speed);
        serial_write(" route=");
        serial_write_hex(slot->route_string);
        serial_write(" dcba=");
        serial_write_hex(slot->device_context_phys);
        serial_write(" input=");
        serial_write_hex(slot->input_context_phys);
        serial_write(" ep0ring=");
        serial_write_hex(slot->ep0_ring_phys);
        serial_write("\r\n");
        if (ep_id < 32u) {
            serial_write("xHCI: ep ring=");
            serial_write_hex(slot->endpoints[ep_id].ring_phys);
            serial_write(" cycle=");
            serial_write_dec(slot->endpoints[ep_id].cycle);
            serial_write(" enqueue=");
            serial_write_dec(slot->endpoints[ep_id].enqueue);
            serial_write("\r\n");
        }
    }
}

#if XHCI_ADDR_TRACE
void xhc_log_address_device_begin(size_t ctl, uint8_t slot_id, uint8_t port,
                                  uint8_t speed) {
    serial_write("xHCI: ADDR-BEGIN ctl=");
    serial_write_dec(ctl);
    serial_write(" slot=");
    serial_write_dec(slot_id);
    serial_write(" port=");
    serial_write_dec(port);
    serial_write(" speed=");
    serial_write_dec(speed);
    serial_write(" ");
    serial_write(xhc_speed_name(speed));
    serial_write("\r\n");
}

void xhc_log_ep0_trb(const char *tag, uint64_t phys) {
    volatile rix_xhci_trb_t *trb = (volatile rix_xhci_trb_t *)(uintptr_t)phys;
    uint32_t control = trb->control;
    uint64_t param = ((uint64_t)trb->parameter_hi << 32) | trb->parameter_lo;
    uint8_t setup_type = 0;
    uint8_t setup_req = 0;
    if (XHCI_TRB_FIELD_TO_TYPE(control) == XHCI_TRB_SETUP_STAGE) {
        setup_type = (uint8_t)(param & 0xffu);
        setup_req = (uint8_t)((param >> 8) & 0xffu);
    }
    serial_write("xHCI: ");
    serial_write(tag);
    serial_write(" phys=");
    serial_write_hex(phys);
    serial_write(" type=");
    serial_write_dec(XHCI_TRB_FIELD_TO_TYPE(control));
    serial_write(" status=");
    serial_write_hex(trb->status);
    if (XHCI_TRB_FIELD_TO_TYPE(control) == XHCI_TRB_SETUP_STAGE) {
        serial_write(" ");
        serial_write(xhc_setup_req_name(setup_type, setup_req));
        serial_write(" wVal=");
        serial_write_hex((param >> 16) & 0xffffu);
        serial_write(" wIdx=");
        serial_write_hex((param >> 32) & 0xffffu);
        serial_write(" wLen=");
        serial_write_hex((param >> 48) & 0xffffu);
        if (setup_req == 6u) {
            serial_write(" desc=");
            serial_write(xhc_desc_name((uint8_t)((param >> 24) & 0xffu)));
        }
    }
    serial_write("\r\n");
}

void xhc_log_ep0_context_and_ring(size_t ctl, uint8_t slot_id) {
    const rix_xhci_controller_t *c;
    xhci_slot_runtime_t *slot;
    uint32_t csz;
    volatile uint32_t *dev;
    volatile uint32_t *ep0;
    if (ctl >= xhc_count || slot_id == 0u ||
        slot_id > xhc_controllers[ctl].max_slots)
        return;
    c = &xhc_controllers[ctl];
    slot = &xhc_runtimes[ctl].slots[slot_id];
    if (!slot->device_context_phys || !slot->ep0_ring_phys) return;
    csz = (c->hcc_params1 & XHCI_HCC_CSZ) != 0u ? 64u : 32u;
    dev = (volatile uint32_t *)(uintptr_t)slot->device_context_phys;
    ep0 = dev + csz / 4u;
    serial_write("xHCI: EP0-CTX ctl=");
    serial_write_dec(ctl);
    serial_write(" slot=");
    serial_write_dec(slot_id);
    serial_write(" speed=");
    serial_write_dec((dev[0] >> 20) & 0xfu);
    serial_write(" entries=");
    serial_write_dec((dev[0] >> 27) & 0x1fu);
    serial_write(" addr=");
    serial_write_dec(dev[3] & 0xffu);
    serial_write(" epstate=");
    serial_write_dec(ep0[0] & 0x7u);
    serial_write(" mps=");
    serial_write_dec((ep0[1] >> 16) & 0xffffu);
    serial_write(" enqueue=");
    serial_write_dec(slot->ep0_enqueue);
    serial_write("\r\n");
}
#else
void xhc_log_address_device_begin(size_t ctl, uint8_t slot_id, uint8_t port,
                                  uint8_t speed) {
    (void)ctl; (void)slot_id; (void)port; (void)speed;
}
void xhc_log_ep0_trb(const char *tag, uint64_t phys) {
    (void)tag; (void)phys;
}
void xhc_log_ep0_context_and_ring(size_t ctl, uint8_t slot_id) {
    (void)ctl; (void)slot_id;
}
#endif

#if XHCI_CC4_SNAPSHOT
/* Transaction-Error snapshot: decode the failing Setup (if any), the slot
 * and EP0 context, and the unconsumed EP0 ring tail. Read-only. */
void xhc_cc4_snapshot(size_t ctl, xhci_runtime_t *rt, uint8_t slot_id,
                      uint64_t first_phys, uint64_t last_phys) {
    xhci_slot_runtime_t *slot;
    const rix_xhci_controller_t *c;
    uint32_t csz;
    volatile uint32_t *dev;
    volatile uint32_t *ep0;
    volatile rix_xhci_trb_t *first;
    uint64_t sp;
    uint64_t now;
    if (ctl >= xhc_count || slot_id == 0u ||
        slot_id > xhc_controllers[ctl].max_slots)
        return;
    c = &xhc_controllers[ctl];
    slot = &rt->slots[slot_id];
    serial_write("xHCI: CC4-SNAPSHOT ctl=");
    serial_write_dec(ctl);
    serial_write(" slot=");
    serial_write_dec(slot_id);
    serial_write("\r\n");
    if (last_phys < first_phys ||
        last_phys - first_phys > 2u * sizeof(rix_xhci_trb_t))
        return;
    first = (volatile rix_xhci_trb_t *)(uintptr_t)first_phys;
    sp = ((uint64_t)first->parameter_hi << 32) | first->parameter_lo;
    serial_write("xHCI: setup brt=");
    serial_write_hex(sp & 0xffu);
    serial_write(" br=");
    serial_write_hex((sp >> 8) & 0xffu);
    serial_write(" wVal=");
    serial_write_hex((sp >> 16) & 0xffffu);
    serial_write(" wIdx=");
    serial_write_hex((sp >> 32) & 0xffffu);
    serial_write(" wLen=");
    serial_write_hex((sp >> 48) & 0xffffu);
    if (((sp >> 8) & 0xffu) == 6u) {
        serial_write(" desc=");
        serial_write(xhc_desc_name((uint8_t)((sp >> 24) & 0xffu)));
    }
    serial_write("\r\n");
    if (!slot->device_context_phys) return;
    csz = (c->hcc_params1 & XHCI_HCC_CSZ) != 0u ? 64u : 32u;
    dev = (volatile uint32_t *)(uintptr_t)slot->device_context_phys;
    ep0 = dev + csz / 4u;
    serial_write("xHCI: slot route=");
    serial_write_hex(dev[0] & 0xfffffu);
    serial_write(" speed=");
    serial_write_dec((dev[0] >> 20) & 0xfu);
    serial_write(" entries=");
    serial_write_dec((dev[0] >> 27) & 0x1fu);
    serial_write(" rh=");
    serial_write_dec((dev[1] >> 16) & 0xffu);
    serial_write(" addr=");
    serial_write_dec(dev[3] & 0xffu);
    serial_write(" state=");
    serial_write_dec((dev[3] >> 27) & 0x1fu);
    serial_write("\r\n");
    serial_write("xHCI: ep0 state=");
    serial_write_dec(ep0[0] & 0x7u);
    serial_write(" cerr=");
    serial_write_dec((ep0[1] >> 1) & 0x3u);
    serial_write(" type=");
    serial_write_dec((ep0[1] >> 3) & 0x7u);
    serial_write(" mps=");
    serial_write_dec((ep0[1] >> 16) & 0xffffu);
    serial_write(" deq=");
    serial_write_hex(((uint64_t)ep0[3] << 32) | (ep0[2] & ~0xfu));
    serial_write(" dcs=");
    serial_write_dec(ep0[2] & 0x1u);
    serial_write("\r\n");
    {
        volatile rix_xhci_trb_t *ring =
            (volatile rix_xhci_trb_t *)(uintptr_t)slot->ep0_ring_phys;
        uint16_t tail = slot->ep0_enqueue > 16u ? (uint16_t)(slot->ep0_enqueue - 16u) : 0u;
        uint16_t cap = XHCI_CMD_RING_TRBS - 1u;
        serial_write("xHCI: ep0-tail enqueue=");
        serial_write_dec(slot->ep0_enqueue);
        serial_write("\r\n");
        for (uint16_t i = tail; i < slot->ep0_enqueue && i < cap; ++i) {
            serial_write(" trb[");
            serial_write_dec(i);
            serial_write("]=");
            serial_write_hex(ring[i].control);
            serial_write("\r\n");
        }
    }
    now = time_monotonic_ns();
    serial_write("xHCI: timing db->done=");
    serial_write_dec(rt->last_cmd_done_ns - rt->last_cmd_db_ns);
    serial_write(" addr-done=");
    serial_write_dec(now - slot->addr_done_ns);
    serial_write(" usb_state=");
    serial_write_dec(slot->usb_state);
    serial_write("\r\n");
    xhc_dump_histories();
}
#else
void xhc_cc4_snapshot(size_t ctl, xhci_runtime_t *rt, uint8_t slot_id,
                      uint64_t first_phys, uint64_t last_phys) {
    (void)ctl; (void)rt; (void)slot_id; (void)first_phys; (void)last_phys;
}
#endif
