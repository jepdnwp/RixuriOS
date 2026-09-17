/* USB hub class driver: hub descriptor, port control, TT/route, registry.
 *
 * Provenance: USB 2.0 ch11 + Linux drivers/usb/core/hub.c (hub_configure
 * descriptor handling, hub_port_reset/wait_reset sequencing, TT think
 * time, route computation, depth limit) and the xHCI slot TT/route
 * programming of xhci-mem.c. Only the polling subset: no status-change
 * URB (ports are polled with GET_STATUS), no power-budgeting, no
 * wireless/SS hub support, at most USB_HUB_MAX_DEPTH external tiers.
 */

#include "hub.h"
#include "usb_ch9.h"
#include "xhci/trb.h"
#include "storage.h"
#include "../serial.h"
#include "../time/time.h"

static usb_hub_record_t hubs[USB_HUB_MAX_HUBS];
static struct {
    uint8_t used;
    size_t controller;
    uint8_t hub_slot;
    uint8_t hub_port;
    uint8_t child_slot;
} hub_children[USB_HUB_MAX_CHILDREN];
/* Attach-failure park per hub-table slot and port (mirrors the 10s root
 * park): a broken hub port must not wedge the rescan on every round. */
#define USB_HUB_RETRY_NS 10000000000ULL
static uint8_t hub_attach_failed[USB_HUB_MAX_HUBS][USB_HUB_MAX_PORTS + 1u];
static uint64_t hub_attach_fail_ns[USB_HUB_MAX_HUBS][USB_HUB_MAX_PORTS + 1u];

/* Pre-PIT pacing shared with the xHCI driver idiom: ~64 pauses ~= 1us.
 * RixuriOS has no driver sleep service yet; bounded busy pacing it is. */
static void hub_udelay(uint32_t us) {
    while (us--) {
        for (uint32_t p = 0; p < 64u; ++p)
            __asm__ volatile("pause" ::: "memory");
    }
}

static uint16_t hub_le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

int usb_hub_parse_descriptor(const uint8_t *data, size_t length,
                             rix_usb_hub_descriptor_t *out) {
    uint8_t nports;
    if (!data || !out || length < 9u) return -1;
    if (data[0] < 9u || (size_t)data[0] > length) return -2;
    if (data[1] != USB_DT_HUB) return -3;
    nports = data[2];
    if (nports == 0u || nports > 31u) return -4;
    out->port_count = nports;
    out->characteristics = hub_le16(data + 3u);
    out->power_good_2ms = data[5];
    out->controller_current_ma = data[6];
    return 0;
}

/* Linux usb_alloc_dev route: nibble (level-1) carries the parent port
 * (ports >= 15 clamp to 15); 20-bit route covers 5 tiers. */
uint32_t usb_hub_route_string(uint32_t parent_route, uint8_t parent_level,
                              uint8_t port) {
    uint8_t p;
    uint32_t shift;
    if (parent_level == 0u || parent_level > 5u) return parent_route;
    p = port >= 15u ? 15u : port;
    shift = ((uint32_t)parent_level - 1u) * 4u;
    return parent_route + ((uint32_t)p << shift);
}

/* TT think-time code: characteristics bits 6:5 select 666/1332/1998/2664ns. */
uint8_t usb_hub_think_code(uint16_t characteristics) {
    return (uint8_t)((characteristics & USB_HUB_CHAR_TTTT_MASK) >>
                     USB_HUB_CHAR_TTTT_SHIFT);
}

/* Linux hub_port_wait_reset speed decision: HIGH wins over LOW, else FULL. */
int usb_hub_port_state(uint16_t status, uint16_t change,
                       rix_usb_hub_port_state_t *out) {
    if (!out) return -1;
    (void)change;
    out->connected = (status & USB_PORT_STAT_CONNECTION) != 0u;
    out->enabled = (status & USB_PORT_STAT_ENABLE) != 0u;
    if (status & USB_PORT_STAT_HIGH_SPEED) out->speed = 3u;
    else if (status & USB_PORT_STAT_LOW_SPEED) out->speed = 2u;
    else out->speed = 1u;
    return 0;
}

static int hub_control(size_t controller, uint8_t slot, uint8_t request_type,
                       uint8_t request, uint16_t value, uint16_t index,
                       void *data, uint16_t length, uint16_t *actual) {
    rix_usb_setup_packet_t setup;
    setup.request_type = request_type;
    setup.request = request;
    setup.value = value;
    setup.index = index;
    setup.length = length;
    return xhci_control_transfer(controller, slot, &setup, data, actual);
}

int usb_hub_get_descriptor(size_t controller, uint8_t slot, void *buffer,
                           uint16_t length, uint16_t *actual_length) {
    if (!buffer || length == 0u) return -1;
    return hub_control(controller, slot,
                       (uint8_t)(USB_DIR_IN | USB_RT_HUB),
                       USB_REQ_GET_DESCRIPTOR,
                       (uint16_t)((uint16_t)USB_DT_HUB << 8), 0, buffer,
                       length, actual_length);
}

int usb_hub_port_status(size_t controller, uint8_t slot, uint8_t port,
                        uint16_t *status, uint16_t *change) {
    static uint8_t buf[4] __attribute__((aligned(4)));
    uint16_t actual = 0;
    int rc;
    if (!status || !change || port == 0u) return -1;
    rc = hub_control(controller, slot,
                     (uint8_t)(USB_DIR_IN | USB_RT_PORT),
                     USB_REQ_GET_STATUS, 0, port, buf, sizeof(buf), &actual);
    if (rc != 0) return rc;
    if (actual < sizeof(buf)) return -2;
    *status = hub_le16(buf);
    *change = hub_le16(buf + 2u);
    return 0;
}

int usb_hub_set_port_feature(size_t controller, uint8_t slot, uint8_t port,
                             uint8_t feature) {
    if (port == 0u) return -1;
    return hub_control(controller, slot, (uint8_t)USB_RT_PORT,
                       USB_REQ_SET_FEATURE, feature, port, 0, 0, 0);
}

int usb_hub_clear_port_feature(size_t controller, uint8_t slot, uint8_t port,
                               uint8_t feature) {
    if (port == 0u) return -1;
    return hub_control(controller, slot, (uint8_t)USB_RT_PORT,
                       USB_REQ_CLEAR_FEATURE, feature, port, 0, 0, 0);
}

/* Bounded hub port reset (Linux hub_port_reset USB2 subset): SET RESET,
 * poll up to 800ms for reset-clear with connection, require ENABLE,
 * clear C_RESET, 50ms recovery. Returns 0 reset+enabled, -1 args,
 * -2 disconnected, -3 not enabled, -4 reset stuck, -5 command failure. */
int usb_hub_port_reset(size_t controller, uint8_t slot, uint8_t port) {
    uint16_t status = 0;
    uint16_t change = 0;
    uint32_t waited = 0;
    if (port == 0u) return -1;
    if (usb_hub_set_port_feature(controller, slot, port,
                                 USB_PORT_FEAT_RESET) != 0)
        return -5;
    for (waited = 0; waited < USB_HUB_RESET_TIMEOUT_MS;
         waited += USB_HUB_SHORT_RESET_MS) {
        hub_udelay(USB_HUB_SHORT_RESET_MS * 1000u);
        if (usb_hub_port_status(controller, slot, port, &status, &change) != 0)
            return -5;
        if (!(status & USB_PORT_STAT_RESET) &&
            (status & USB_PORT_STAT_CONNECTION))
            break;
    }
    if (status & USB_PORT_STAT_RESET) return -4;
    if (!(status & USB_PORT_STAT_CONNECTION)) return -2;
    (void)usb_hub_clear_port_feature(controller, slot, port,
                                     USB_PORT_FEAT_C_RESET);
    if (usb_hub_port_status(controller, slot, port, &status, &change) != 0)
        return -5;
    if (!(status & USB_PORT_STAT_ENABLE)) return -3;
    hub_udelay(USB_HUB_RECOVERY_MS * 1000u);
    return 0;
}

void usb_hub_power_all(size_t controller, uint8_t slot, uint8_t port_count,
                       uint8_t power_good_2ms) {
    uint32_t settle_ms;
    for (uint8_t p = 1; p <= port_count && p <= USB_HUB_MAX_PORTS; ++p)
        (void)usb_hub_set_port_feature(controller, slot, p,
                                       USB_PORT_FEAT_POWER);
    settle_ms = (uint32_t)power_good_2ms * 2u;
    if (settle_ms < USB_HUB_POWER_MIN_MS) settle_ms = USB_HUB_POWER_MIN_MS;
    hub_udelay(settle_ms * 1000u);
}

int usb_hub_attach_child(size_t controller, const usb_hub_parent_t *parent,
                         uint8_t hub_port, rix_xhci_device_t *out) {
    uint16_t status = 0;
    uint16_t change = 0;
    uint8_t slot_id = 0;
    uint8_t speed;
    rix_xhci_tt_info_t tt;
    if (!parent || !out || hub_port == 0u) return -1;
    out->slot_id = 0;
    out->port = 0;
    out->speed = 0;
    out->state = XHCI_DEVICE_DETACHED;
    out->parent_hub_slot = 0;
    out->route = 0;
    out->level = 0;
    if (usb_hub_port_status(controller, parent->parent_slot, hub_port,
                            &status, &change) != 0)
        return -5;
    if (!(status & USB_PORT_STAT_CONNECTION)) return -2;
    hub_udelay(USB_HUB_SHORT_RESET_MS * 1000u);
    if (usb_hub_port_status(controller, parent->parent_slot, hub_port,
                            &status, &change) != 0)
        return -5;
    if (!(status & USB_PORT_STAT_CONNECTION)) return -2;
    if (usb_hub_port_reset(controller, parent->parent_slot, hub_port) != 0)
        return -3;
    if (usb_hub_port_status(controller, parent->parent_slot, hub_port,
                            &status, &change) != 0)
        return -5;
    if (!(status & USB_PORT_STAT_CONNECTION)) return -2;
    if (status & USB_PORT_STAT_HIGH_SPEED) speed = 3u;
    else if (status & USB_PORT_STAT_LOW_SPEED) speed = 2u;
    else speed = 1u;
    if (xhci_enable_slot(controller, &slot_id) != 0) return -6;
    xhci_usb_state_transition(controller, slot_id, XHCI_USB_DEFAULT,
                              "hub-enable-slot-ok");
    tt.route = usb_hub_route_string(parent->parent_route, parent->parent_level,
                                    hub_port);
    tt.hub_slot = parent->parent_slot;
    tt.hub_port = hub_port;
    tt.think_code = parent->think_code;
    /* Route/TT info always travels: the route string is required for every
     * hub child (slot_ctx resolves TT fields itself from the speeds). */
    int addr_rc = xhci_address_device(controller, slot_id, hub_port, speed,
                                      &tt);
    if (addr_rc == -(int)XHCI_COMP_USB_TRANSACTION_ERROR) {
        /* One re-address round, mirroring root attach: drop the slot,
         * reset the hub port again, re-enable and retry once. */
        (void)xhci_disable_slot(controller, slot_id);
        if (usb_hub_port_reset(controller, parent->parent_slot, hub_port) != 0)
            return -3;
        if (usb_hub_port_status(controller, parent->parent_slot, hub_port,
                                &status, &change) != 0)
            return -5;
        if (!(status & USB_PORT_STAT_CONNECTION)) return -2;
        if (xhci_enable_slot(controller, &slot_id) != 0) return -6;
        xhci_usb_state_transition(controller, slot_id, XHCI_USB_DEFAULT,
                                  "hub-enable-slot-retry");
        addr_rc = xhci_address_device(controller, slot_id, hub_port, speed,
                                      &tt);
    }
    if (addr_rc != 0) {
        (void)xhci_disable_slot(controller, slot_id);
        return -7;
    }
    out->slot_id = slot_id;
    out->port = hub_port;
    out->speed = speed;
    out->state = XHCI_DEVICE_ADDRESSED;
    out->parent_hub_slot = parent->parent_slot;
    out->route = tt.route;
    out->level = (uint8_t)(parent->parent_level + 1u);
    return 0;
}

void usb_hub_register(size_t controller, uint8_t hub_slot, uint32_t route,
                      uint8_t level, uint8_t speed, uint8_t think_code,
                      uint8_t port_count) {
    if (hub_slot == 0u || port_count == 0u) return;
    for (unsigned i = 0; i < USB_HUB_MAX_HUBS; ++i) {
        if (hubs[i].used && hubs[i].controller == controller &&
            hubs[i].hub_slot == hub_slot) {
            hubs[i].route = route;
            hubs[i].level = level;
            hubs[i].speed = speed;
            hubs[i].think_code = think_code;
            hubs[i].port_count = port_count;
            return;
        }
    }
    for (unsigned i = 0; i < USB_HUB_MAX_HUBS; ++i) {
        if (!hubs[i].used) {
            hubs[i].used = 1;
            hubs[i].controller = controller;
            hubs[i].hub_slot = hub_slot;
            hubs[i].route = route;
            hubs[i].level = level;
            hubs[i].speed = speed;
            hubs[i].think_code = think_code;
            hubs[i].port_count = port_count;
            return;
        }
    }
    serial_write("xHCI: hub registry full\r\n");
}

void usb_hub_register_child(size_t controller, uint8_t hub_slot,
                            uint8_t hub_port, uint8_t child_slot) {
    if (hub_slot == 0u || hub_port == 0u || child_slot == 0u) return;
    for (unsigned i = 0; i < USB_HUB_MAX_CHILDREN; ++i) {
        if (!hub_children[i].used) {
            hub_children[i].used = 1;
            hub_children[i].controller = controller;
            hub_children[i].hub_slot = hub_slot;
            hub_children[i].hub_port = hub_port;
            hub_children[i].child_slot = child_slot;
            return;
        }
    }
    serial_write("xHCI: hub child registry full\r\n");
}

const usb_hub_record_t *usb_hub_find(size_t controller, uint8_t hub_slot) {
    for (unsigned i = 0; i < USB_HUB_MAX_HUBS; ++i) {
        if (hubs[i].used && hubs[i].controller == controller &&
            hubs[i].hub_slot == hub_slot)
            return &hubs[i];
    }
    return 0;
}

static void usb_hub_detach_sweep_depth(size_t controller, uint8_t hub_slot,
                                         unsigned depth) {
    unsigned i;
    unsigned p;
    if (hub_slot == 0u || depth > USB_HUB_MAX_DEPTH) return;
    for (i = 0; i < USB_HUB_MAX_CHILDREN; ++i) {
        unsigned k;
        int is_hub = 0;
        if (!hub_children[i].used ||
            hub_children[i].controller != controller ||
            hub_children[i].hub_slot != hub_slot)
            continue;
        for (k = 0; k < USB_HUB_MAX_HUBS; ++k) {
            if (hubs[k].used && hubs[k].controller == controller &&
                hubs[k].hub_slot == hub_children[i].child_slot) {
                is_hub = 1;
                break;
            }
        }
        if (is_hub) {
            /* A child that is itself a hub takes its own subtree down
             * first, so no stale grandchild keeps polling a dead slot. */
            usb_hub_detach_sweep_depth(controller,
                                       hub_children[i].child_slot,
                                       depth + 1u);
        }
        (void)xhci_device_detach(controller, hub_children[i].child_slot);
        usb_storage_detach(controller, hub_children[i].child_slot);
        hub_children[i].used = 0;
    }
    for (i = 0; i < USB_HUB_MAX_HUBS; ++i) {
        if (hubs[i].used && hubs[i].controller == controller &&
            hubs[i].hub_slot == hub_slot) {
            hubs[i].used = 0;
            for (p = 0; p <= USB_HUB_MAX_PORTS; ++p) {
                hub_attach_failed[i][p] = 0;
                hub_attach_fail_ns[i][p] = 0;
            }
        }
    }
}

void usb_hub_detach_sweep(size_t controller, uint8_t hub_slot) {
    usb_hub_detach_sweep_depth(controller, hub_slot, 0u);
}

int usb_hub_rescan_ports(size_t controller) {
    int detached = 0;
    for (unsigned h = 0; h < USB_HUB_MAX_HUBS; ++h) {
        uint8_t ports;
        uint8_t hub_slot;
        if (!hubs[h].used || hubs[h].controller != controller) continue;
        hub_slot = hubs[h].hub_slot;
        /* A detached hub is always unregistered with its sweep, but never
         * trust a record over live slot state: detaching or polling a
         * dead slot re-registers ghost devices behind stale entries. */
        if (!xhci_slot_active(controller, hub_slot)) continue;
        ports = hubs[h].port_count;
        if (ports > USB_HUB_MAX_PORTS) ports = USB_HUB_MAX_PORTS;
        for (uint8_t p = 1; p <= ports; ++p) {
            uint16_t status = 0;
            uint16_t change = 0;
            if (usb_hub_port_status(controller, hub_slot, p, &status,
                                    &change) != 0)
                continue;
            if (status & USB_PORT_STAT_CONNECTION) continue;
            for (unsigned i = 0; i < USB_HUB_MAX_CHILDREN; ++i) {
                if (hub_children[i].used &&
                    hub_children[i].controller == controller &&
                    hub_children[i].hub_slot == hub_slot &&
                    hub_children[i].hub_port == p) {
                    serial_write("xHCI: hub child gone ctl=");
                    serial_write_dec(controller);
                    serial_write(" hub=");
                    serial_write_dec(hub_slot);
                    serial_write(" port=");
                    serial_write_dec(p);
                    serial_write("\r\n");
                    /* Subtree first (a removed hub takes its own
                     * children down), then the child slot itself.
                     * Open-coding only the detach here used to leak
                     * the hub record, and a later poll re-registered
                     * a ghost hub behind the stale entry. */
                    usb_hub_detach_sweep_depth(
                        controller, hub_children[i].child_slot, 0u);
                    (void)xhci_device_detach(
                        controller, hub_children[i].child_slot);
                    usb_storage_detach(controller,
                                       hub_children[i].child_slot);
                    hub_children[i].used = 0;
                    detached++;
                }
            }
            /* A dark port re-arms its attach park, so a reinsertion
             * retries immediately instead of waiting out an old timer. */
            hub_attach_failed[h][p] = 0;
            hub_attach_fail_ns[h][p] = 0;
        }
    }
    return detached;
}

int usb_hub_poll_new_child(size_t controller, rix_xhci_device_t *out) {
    uint64_t now;
    if (!out) return -1;
    out->slot_id = 0;
    out->port = 0;
    out->speed = 0;
    out->state = XHCI_DEVICE_DETACHED;
    out->parent_hub_slot = 0;
    out->route = 0;
    out->level = 0;
    now = time_monotonic_ns();
    for (unsigned h = 0; h < USB_HUB_MAX_HUBS; ++h) {
        uint8_t ports;
        uint8_t hub_slot;
        usb_hub_parent_t parent;
        if (!hubs[h].used || hubs[h].controller != controller) continue;
        hub_slot = hubs[h].hub_slot;
        /* A detached hub is always unregistered with its sweep, but never
         * trust a record over live slot state: detaching or polling a
         * dead slot re-registers ghost devices behind stale entries. */
        if (!xhci_slot_active(controller, hub_slot)) continue;
        ports = hubs[h].port_count;
        if (ports > USB_HUB_MAX_PORTS) ports = USB_HUB_MAX_PORTS;
        parent.parent_slot = hub_slot;
        parent.parent_route = hubs[h].route;
        parent.parent_level = hubs[h].level;
        parent.think_code = hubs[h].think_code;
        for (uint8_t p = 1; p <= ports; ++p) {
            uint16_t status = 0;
            uint16_t change = 0;
            unsigned k;
            rix_xhci_device_t child;
            int arc;
            if (hub_attach_failed[h][p] &&
                now - hub_attach_fail_ns[h][p] < USB_HUB_RETRY_NS)
                continue;
            hub_attach_failed[h][p] = 0;
            if (usb_hub_port_status(controller, hub_slot, p, &status,
                                    &change) != 0)
                continue;
            if (!(status & USB_PORT_STAT_CONNECTION)) continue;
            for (k = 0; k < USB_HUB_MAX_CHILDREN; ++k) {
                if (hub_children[k].used &&
                    hub_children[k].controller == controller &&
                    hub_children[k].hub_slot == hub_slot &&
                    hub_children[k].hub_port == p)
                    break;
            }
            if (k < USB_HUB_MAX_CHILDREN) continue;
            arc = usb_hub_attach_child(controller, &parent, p, &child);
            if (arc != 0) {
                hub_attach_failed[h][p] = 1;
                hub_attach_fail_ns[h][p] = now;
                continue;
            }
            *out = child;
            return 1;
        }
    }
    return 0;
}
