/* Port reset, device attach/detach and the hotplug policy.
 *
 * Provenance: Linux drivers/usb/host/xhci-hub.c (port reset, warm reset,
 * link-training waits) and the hub TT/reset paths. The USB2 escalation
 * ladder (plain reset -> RxDetect -> power cycle -> warm reset) and the
 * USB3 train-then-warm-reset sequence preserve the AMD-verified order.
 * Device attach follows Linux hub_port_connect_change logic shaped to the
 * RixuriOS slot model: reset, Enable Slot, Address Device with one
 * Transaction-Error re-address round.
 */

#include "xhc.h"

uint64_t xhc_port_reset_start_ns[XHCI_MAX][256];
uint8_t xhc_port_attach_failed[XHCI_MAX][256];
uint64_t xhc_port_fail_ns[XHCI_MAX][256];
uint64_t xhc_port_reset_done_ns[XHCI_MAX][256];
uint8_t xhc_last_selected[XHCI_MAX];

void xhc_clear_port_change(volatile uint32_t *reg) {
    /* The raw readback must never round-trip: PED is RW1CS, so writing
     * PED=1 back DISABLES the port. Neutralize, then write 1 only to the
     * W1C change bits. */
    *reg = xhci_portsc_clear_changes(*reg);
}

/* Wait for a USB3 port to reach the trained (Enabled) state without
 * issuing a reset. Returns 0 trained, -3 disconnect, -5 timeout. */
int xhc_wait_usb3_trained(volatile uint32_t *reg) {
    for (uint32_t i = 0; i < XHCI_RESET_POLL_LIMIT; ++i) {
        uint32_t s = *reg;
        uint8_t speed;
        if (!(s & XHCI_PORT_CCS)) return -3;
        speed = (uint8_t)((s & XHCI_PORT_SPEED_MASK) >> XHCI_PORT_SPEED_SHIFT);
        if ((s & XHCI_PORT_PED) && speed != 0u) {
            xhc_trace_portsc("trained", reg);
            xhc_clear_port_change(reg);
            return 0;
        }
        if ((i & 0x3ffu) == 0u) xhc_udelay(50u);
    }
    return -5;
}

int xhc_warm_reset_port(volatile uint32_t *reg) {
    uint32_t v = xhci_portsc_neutralize(*reg) | XHCI_PORT_CHANGE_MASK |
        XHCI_PORT_WPR;
    *reg = v;
    for (uint32_t i = 0; i < XHCI_RESET_POLL_LIMIT; ++i) {
        uint32_t s = *reg;
        if (!(s & XHCI_PORT_CCS)) return -3;
        if (!(s & XHCI_PORT_WPR) || (s & XHCI_PORT_WRC)) {
            xhc_trace_portsc("warm-done", reg);
            xhc_clear_port_change(reg);
            return xhc_wait_usb3_trained(reg);
        }
        if ((i & 0x3ffu) == 0u) xhc_udelay(50u);
    }
    xhc_trace_portsc("warm-timeout", reg);
    xhc_clear_port_change(reg);
    return -5;
}

/* Force RxDetect on a stuck USB2 port (xHCI 4.19.5: port must be Disabled
 * before PLS=RxDetect; PED=1 write is a no-op when already 0). */
void xhc_force_rxdetect(volatile uint32_t *reg) {
    uint32_t v = xhci_portsc_neutralize(*reg);
    v |= XHCI_PORT_CHANGE_MASK | XHCI_PORT_PED | XHCI_PORT_LWS | (5u << 5);
    *reg = v;
    xhc_udelay(10000u);
}

int xhc_power_cycle_port(volatile uint32_t *reg) {
    uint32_t v = xhci_portsc_neutralize(*reg);
    v = (v & ~XHCI_PORT_PP) | XHCI_PORT_CHANGE_MASK;
    *reg = v;
    xhc_udelay(100000u);
    v = xhci_portsc_neutralize(*reg) | XHCI_PORT_PP | XHCI_PORT_CHANGE_MASK;
    *reg = v;
    xhc_udelay(100000u);
    if (!(*reg & XHCI_PORT_CCS)) return -2;
    return 0;
}

int xhc_port_enabled(volatile uint32_t *reg) {
    uint32_t s = *reg;
    return ((s & XHCI_PORT_CCS) && (s & XHCI_PORT_PED)) ? 0 : -1;
}

/* Plain USB2 port reset (PR + PRC wait) with the 10ms reset-recovery delay
 * before Address Device. Returns 0 enabled, -3 disconnect, -5 timeout. */
int xhc_usb2_reset(volatile uint32_t *reg) {
    uint32_t v = xhci_portsc_neutralize(*reg) | XHCI_PORT_CHANGE_MASK |
        XHCI_PORT_PR;
    *reg = v;
    xhc_udelay(100u);
    for (uint32_t i = 0; i < XHCI_RESET_POLL_LIMIT; ++i) {
        uint32_t s = *reg;
        if (s & XHCI_PORT_PRC) {
            uint32_t after;
            xhc_clear_port_change(reg);
            after = *reg;
            if (!(after & XHCI_PORT_CCS)) return -3;
            xhc_udelay(10000u);
            return 0;
        }
        if ((i & 0x3ffu) == 0u) xhc_udelay(50u);
    }
    xhc_clear_port_change(reg);
    return -5;
}

int xhci_reset_port(size_t controller, uint8_t port) {
    volatile uint32_t *reg;
    uint32_t v;
    uint8_t speed;
    int proto;
    uint32_t usb2_only;
    if (controller >= xhc_count) return -1;
    reg = xhc_port_reg(controller, port);
    if (!reg) return -1;
    if (controller < XHCI_MAX)
        xhc_port_reset_start_ns[controller][port] = time_monotonic_ns();
    v = *reg;
    if (!(v & XHCI_PORT_PP)) {
        v = xhci_portsc_neutralize(v) | XHCI_PORT_PP | XHCI_PORT_CHANGE_MASK;
        *reg = v;
        xhc_udelay(20000u);
        v = *reg;
    }
    if (!(v & XHCI_PORT_CCS)) return -2;
    speed = (uint8_t)((v & XHCI_PORT_SPEED_MASK) >> XHCI_PORT_SPEED_SHIFT);
    proto = xhci_port_protocol(controller, port);
    usb2_only =
        xhc_controllers[controller].quirks & XHCI_PROFILE_QUIRK_USB2_ONLY;
    if (usb2_only) proto = 2;
    if ((v & XHCI_PORT_PED) && speed != 0u && !usb2_only &&
        (speed >= 4u || proto == 3)) {
        /* Already-trained SuperSpeed: PR wedges these ports, leave them. */
        xhc_clear_port_change(reg);
        return 0;
    }
    if (!usb2_only && (speed >= 4u || proto == 3)) {
        int wrc = xhc_wait_usb3_trained(reg);
        if (wrc == 0) return 0;
        return xhc_warm_reset_port(reg);
    }
    if (speed == 0u && proto != 2) {
        /* Late-training link: give AMD link training ~2s before USB2 path. */
        for (uint32_t i = 0; i < 4u * XHCI_RESET_POLL_LIMIT; ++i) {
            uint32_t s = *reg;
            uint8_t s2;
            if (!(s & XHCI_PORT_CCS)) return -2;
            if (s & XHCI_PORT_PED) break;
            s2 = (uint8_t)((s & XHCI_PORT_SPEED_MASK) >> XHCI_PORT_SPEED_SHIFT);
            if (s2 != 0u) break;
            if ((i & 0x3ffu) == 0u) xhc_udelay(50u);
        }
        v = *reg;
        speed = (uint8_t)((v & XHCI_PORT_SPEED_MASK) >> XHCI_PORT_SPEED_SHIFT);
        if (speed >= 4u || (v & XHCI_PORT_PED)) {
            int wrc = xhc_wait_usb3_trained(reg);
            if (wrc == 0) return 0;
            return xhc_warm_reset_port(reg);
        }
    }
    if (xhc_usb2_reset(reg) == 0 && xhc_port_enabled(reg) == 0) return 0;
    xhc_force_rxdetect(reg);
    if (xhc_usb2_reset(reg) == 0 && xhc_port_enabled(reg) == 0) goto recovered;
    if (xhc_power_cycle_port(reg) == 0 && xhc_usb2_reset(reg) == 0 &&
        xhc_port_enabled(reg) == 0)
        goto recovered;
    if (xhc_warm_reset_port(reg) == 0 && xhc_port_enabled(reg) == 0)
        goto recovered;
    return -5;
recovered:
    xhc_trace_portsc("recovered", reg);
    return 0;
}

void xhci_dump_ports(void) {
    for (size_t ctl = 0; ctl < xhc_count; ++ctl) {
        const rix_xhci_controller_t *c = &xhc_controllers[ctl];
        const xhci_profile_t *profile = xhc_controller_profile(c);
        kernel_log("xHCI: ports ctl=");
        kernel_log_dec(ctl);
        kernel_log(" max=");
        kernel_log_dec(c->max_ports);
        kernel_log("\r\n");
        for (unsigned p_ = 1; p_ <= c->max_ports; ++p_) {
            uint8_t port = (uint8_t)p_;
            volatile uint32_t *reg = xhc_port_reg(ctl, port);
            uint32_t v;
            if (!reg) continue;
            v = *reg;
            if (!(v & XHCI_PORT_CCS)) continue;
            kernel_log("xHCI: port ctl=");
            kernel_log_dec(ctl);
            kernel_log(" port=");
            kernel_log_dec(port);
            kernel_log(" PED=");
            kernel_log_dec((v & XHCI_PORT_PED) != 0u);
            kernel_log(" speed=");
            kernel_log_dec((v & XHCI_PORT_SPEED_MASK) >> XHCI_PORT_SPEED_SHIFT);
            kernel_log(" proto=");
            kernel_log_dec((uint64_t)xhci_port_protocol(ctl, port));
            kernel_log(" known-bad=");
            kernel_log_dec((uint64_t)(profile &&
                xhci_profile_port_known_bad(profile, port)));
            kernel_log(" PLS=");
            kernel_log_dec((v & XHCI_PORT_PLS_MASK) >> 5);
            kernel_log(" PORTSC=");
            kernel_log_hex(v);
            kernel_log("\r\n");
        }
    }
}

int xhci_device_attach(size_t controller, uint8_t port,
                       rix_xhci_device_t *out) {
    rix_xhci_port_status_t status;
    uint8_t slot_id = 0;
    int rc;
    if (!out || controller >= xhc_count) return -1;
    out->slot_id = 0;
    out->port = 0;
    out->speed = 0;
    out->state = XHCI_DEVICE_DETACHED;
    out->parent_hub_slot = 0;
    out->route = 0;
    out->level = 1;
    if (xhci_port_status(controller, port, &status) != 0 || !status.connected)
        return -2;
    for (uint16_t s = 1; s <= xhc_controllers[controller].max_slots; ++s) {
        if (xhc_runtimes[controller].slots[s].allocated &&
            xhc_runtimes[controller].slots[s].tt_parent_slot == 0u &&
            xhc_runtimes[controller].slots[s].port == port)
            return -3;
    }
    if (xhci_reset_port(controller, port) != 0) return -4;
    if (controller < XHCI_MAX)
        xhc_port_reset_done_ns[controller][port] = time_monotonic_ns();
    if (xhci_port_status(controller, port, &status) != 0 ||
        !status.connected || status.speed == 0u)
        return -5;
    if (xhci_enable_slot(controller, &slot_id) != 0) return -6;
    xhci_usb_state_transition(controller, slot_id, XHCI_USB_DEFAULT,
                              "enable-slot-ok");
    rc = xhci_address_device(controller, slot_id, port, status.speed, 0);
    if (rc == 0) {
        out->slot_id = slot_id;
        out->port = port;
        out->speed = status.speed;
        out->state = XHCI_DEVICE_ADDRESSED;
        return 0;
    }
    if (rc == -(int)XHCI_COMP_USB_TRANSACTION_ERROR) {
        /* One re-address round: drop the slot, reset again, retry. */
        (void)xhci_disable_slot(controller, slot_id);
        if (xhci_reset_port(controller, port) != 0) return -5;
        if (xhci_port_status(controller, port, &status) != 0 ||
            !status.connected || status.speed == 0u)
            return -5;
        if (xhci_enable_slot(controller, &slot_id) != 0) return -6;
        rc = xhci_address_device(controller, slot_id, port, status.speed, 0);
        if (rc == 0) {
            out->slot_id = slot_id;
            out->port = port;
            out->speed = status.speed;
            out->state = XHCI_DEVICE_ADDRESSED;
            return 0;
        }
    }
    (void)xhci_disable_slot(controller, slot_id);
    return -7;
}

int xhci_device_detach(size_t controller, uint8_t slot_id) {
    if (controller >= xhc_count || slot_id == 0u ||
        slot_id > xhc_controllers[controller].max_slots)
        return -1;
    if (!xhc_runtimes[controller].slots[slot_id].allocated) return -2;
    xhci_usb_state_transition(controller, slot_id, XHCI_USB_DETACHED,
                              "detach");
    return xhci_disable_slot(controller, slot_id);
}

int xhci_slot_active(size_t controller, uint8_t slot_id) {
    if (controller >= xhc_count || slot_id == 0u ||
        slot_id > xhc_controllers[controller].max_slots)
        return 0;
    return xhc_runtimes[controller].slots[slot_id].allocated ? 1 : 0;
}

void xhci_park_port(size_t controller, uint8_t port) {
    if (controller >= xhc_count || port == 0u ||
        port > xhc_controllers[controller].max_ports)
        return;
    if (controller >= XHCI_MAX) return;
    xhc_port_attach_failed[controller][port] = 1;
    xhc_port_fail_ns[controller][port] = time_monotonic_ns();
}

int xhci_service_hotplug(size_t controller, rix_xhci_device_t *device,
                         uint8_t *connected) {
    rix_xhci_controller_t *c;
    uint8_t port = 0;
    uint8_t is_connected = 0;
    int rc;
    const xhci_profile_t *profile;
    uint64_t now;
    uint8_t best_port = 0;
    int best_score = 0;
    if (!device || !connected || controller >= xhc_count) return -1;
    c = &xhc_controllers[controller];
    device->slot_id = 0;
    device->port = 0;
    device->speed = 0;
    device->state = XHCI_DEVICE_DETACHED;
    device->parent_hub_slot = 0;
    device->route = 0;
    device->level = 1;
    *connected = 0;
    profile = xhc_controller_profile(c);
    rc = xhci_poll_port_status_change(controller, &port, &is_connected);
    if (rc == 0) {
        /* Fallback scan: reap stale failures, then pick the best
         * connected unoccupied port (known-good USB2 first). */
        now = time_monotonic_ns();
        /* Unsigned counters: uint8_t would wrap at max_ports == 255. */
        for (unsigned rp = 1; rp <= c->max_ports; ++rp) {
            uint8_t p = (uint8_t)rp;
            rix_xhci_port_status_t ps;
            if (controller >= XHCI_MAX) break;
            if (!xhc_port_attach_failed[controller][p]) continue;
            if (xhci_port_status(controller, p, &ps) != 0 ||
                !ps.connected) {
                xhc_port_attach_failed[controller][p] = 0;
                xhc_port_fail_ns[controller][p] = 0;
            } else if (now - xhc_port_fail_ns[controller][p] >
                       XHCI_PORT_RETRY_NS) {
                xhc_port_attach_failed[controller][p] = 0;
                xhc_port_fail_ns[controller][p] = 0;
            }
        }
        for (unsigned cp = 1; cp <= c->max_ports; ++cp) {
            uint8_t candidate = (uint8_t)cp;
            rix_xhci_port_status_t ps;
            int occupied = 0;
            int score;
            if (controller < XHCI_MAX &&
                xhc_port_attach_failed[controller][candidate])
                continue;
            if (xhci_port_status(controller, candidate, &ps) != 0 ||
                !ps.connected)
                continue;
            for (uint16_t s = 1; s <= c->max_slots; ++s) {
                /* Hub children carry hub-relative ports; only
                 * root-attached slots occupy root ports. */
                if (xhc_runtimes[controller].slots[s].allocated &&
                    xhc_runtimes[controller].slots[s].tt_parent_slot == 0u &&
                    xhc_runtimes[controller].slots[s].port == candidate) {
                    occupied = 1;
                    break;
                }
            }
            if (occupied) continue;
            score = xhci_profile_port_priority(
                xhci_port_protocol(controller, candidate),
                profile && xhci_profile_port_known_bad(profile, candidate));
            if (best_port == 0u || score < best_score) {
                best_port = candidate;
                best_score = score;
            }
        }
        if (best_port != 0u) {
            port = best_port;
            is_connected = 1;
            rc = 1;
            if (controller < XHCI_MAX && best_score > 0 &&
                xhc_last_selected[controller] != best_port) {
                serial_write("xHCI: selected non-preferred port ctl=");
                serial_write_dec(controller);
                serial_write(" port=");
                serial_write_dec(best_port);
                serial_write("\r\n");
                xhc_last_selected[controller] = best_port;
            }
        }
    }
    if (rc == 1 && port != 0u) {
        if (!is_connected) {
            if (controller < XHCI_MAX)
                xhc_port_attach_failed[controller][port] = 0;
        } else if (controller < XHCI_MAX &&
                   xhc_port_attach_failed[controller][port]) {
            volatile uint32_t *preg = xhc_port_reg(controller, port);
            if (preg) xhc_clear_port_change(preg);
            return 0;
        }
    }
    if (rc <= 0) return rc;
    *connected = is_connected;
    if (is_connected) {
        int attach_rc = xhci_device_attach(controller, port, device);
        if (attach_rc == -3) {
            /* Already attached: refill the existing slot identity. */
            for (uint16_t s = 1; s <= c->max_slots; ++s) {
                xhci_slot_runtime_t *slot = &xhc_runtimes[controller].slots[s];
                if (slot->allocated && slot->tt_parent_slot == 0u &&
                    slot->port == port) {
                    device->slot_id = (uint8_t)s;
                    device->port = port;
                    device->speed = slot->speed;
                    device->state = XHCI_DEVICE_ADDRESSED;
                    device->parent_hub_slot = 0;
                    device->route = 0;
                    device->level = 1;
                    return 0;
                }
            }
            return -3;
        }
        if (attach_rc != 0) {
            volatile uint32_t *preg = xhc_port_reg(controller, port);
            uint32_t portsc = preg ? *preg : 0u;
            device->state = XHCI_DEVICE_ERROR;
            serial_write("xHCI: attach failed ctl=");
            serial_write_dec(controller);
            serial_write(" port=");
            serial_write_dec(port);
            serial_write(" rc=");
            serial_write_dec((uint64_t)(attach_rc < 0 ? -attach_rc : attach_rc));
            serial_write(" PORTSC=");
            serial_write_hex(portsc);
            serial_write("\r\n");
            if (controller < XHCI_MAX) {
                xhc_port_attach_failed[controller][port] = 1;
                xhc_port_fail_ns[controller][port] = time_monotonic_ns();
            }
            return attach_rc;
        }
        return 1;
    }
    /* Disconnect: find the root-attached slot bound to this port and
     * disable it. Hub children carry hub-relative ports and are owned
     * by the hub sweep, never by root port numbers. */
    for (uint16_t s = 1; s <= c->max_slots; ++s) {
        if (xhc_runtimes[controller].slots[s].allocated &&
            xhc_runtimes[controller].slots[s].tt_parent_slot == 0u &&
            xhc_runtimes[controller].slots[s].port == port) {
            int detach_rc = xhci_device_detach(controller, (uint8_t)s);
            if (detach_rc == 0) {
                if (controller < XHCI_MAX)
                    xhc_port_attach_failed[controller][port] = 0;
                device->slot_id = (uint8_t)s;
                device->port = port;
                device->state = XHCI_DEVICE_DETACHED;
                return 1;
            }
            device->slot_id = (uint8_t)s;
            device->port = port;
            device->state = XHCI_DEVICE_ERROR;
            return detach_rc;
        }
    }
    device->port = port;
    device->state = XHCI_DEVICE_DETACHED;
    return 0;
}
