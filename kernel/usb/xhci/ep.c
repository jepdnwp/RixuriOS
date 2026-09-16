/* Non-control endpoints: Configure Endpoint and ring transfers.
 *
 * Provenance: Linux drivers/usb/host/xhci-mem.c
 * (xhci_setup_endpoint / endpoint context init per xHCI 6.2.1.2) and
 * xhci-ring.c (queue_bulk_trb / queue_intr_trb, single-TRB TDs with IOC).
 * DCI mapping: ep_id = 2*ep_num + dir, EP0 reserved.
 */

#include "xhc.h"

static uint8_t xhc_ep_type(uint8_t endpoint_address, uint8_t attributes) {
    uint8_t type = attributes & 0x03u;
    int dir = (endpoint_address & 0x80u) != 0u;
    if (type == 3u) return dir ? XHCI_EP_INTERRUPT_IN : XHCI_EP_INTERRUPT_OUT;
    if (type == 2u) return dir ? XHCI_EP_BULK_IN : XHCI_EP_BULK_OUT;
    return 0;
}

/* Direct port of the Linux xhci-mem.c interval helpers
 * (xhci_microframes_to_exponent, xhci_parse_microframe_interval,
 * xhci_parse_exponent_interval, xhci_parse_frame_interval,
 * xhci_get_endpoint_interval): same arithmetic and clamp order, only the
 * fls/clamp_val primitives are spelled out. The Endpoint Context Interval
 * field is log2 of the service period in microframes; programming a raw
 * bInterval (e.g. FS 10) would schedule every 2^10 microframes = 128ms
 * instead of ~8ms. */
static unsigned xhci_fls(unsigned x) {
    unsigned r = 0u;
    while (x != 0u) {
        x >>= 1;
        r++;
    }
    return r;
}

static unsigned xhci_clamp_val(unsigned v, unsigned lo, unsigned hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static unsigned xhci_microframes_to_exponent(unsigned desc_interval,
                                             unsigned min_exponent,
                                             unsigned max_exponent) {
    unsigned interval = xhci_fls(desc_interval) - 1u;
    return xhci_clamp_val(interval, min_exponent, max_exponent);
}

static unsigned xhci_parse_microframe_interval(uint8_t binterval) {
    if (binterval == 0u) return 0u;
    return xhci_microframes_to_exponent(binterval, 0u, 15u);
}

static unsigned xhci_parse_exponent_interval(uint8_t binterval) {
    return xhci_clamp_val(binterval, 1u, 16u) - 1u;
}

static unsigned xhci_parse_frame_interval(uint8_t binterval) {
    return xhci_microframes_to_exponent((unsigned)binterval * 8u, 3u, 10u);
}

unsigned xhc_ep_interval(uint8_t speed, uint8_t ep_type, uint8_t binterval) {
    if (ep_type == 3u) {
        if (speed == 1u || speed == 2u)
            return xhci_parse_frame_interval(binterval);
        if (speed == 3u || speed == 4u || speed == 5u)
            return xhci_parse_exponent_interval(binterval);
        return 0u;
    }
    if (ep_type == 2u && speed == 3u)
        return xhci_parse_microframe_interval(binterval);
    return 0u;
}

int xhci_configure_endpoint(size_t controller, uint8_t slot_id,
                            const rix_xhci_endpoint_config_t *config) {
    rix_xhci_controller_t *c;
    xhci_slot_runtime_t *slot;
    uint8_t type;
    uint8_t ep_num;
    uint8_t ep_id;
    uint32_t csz;
    volatile uint32_t *input;
    volatile uint32_t *slot_ctx;
    volatile uint32_t *ep;
    uint32_t entries;
    uint32_t esit = 0;
    uint32_t interval;
    uint16_t mps;
    uint8_t burst = 0;
    uint64_t ring_phys;
    volatile rix_xhci_trb_t *ring;
    int dir;
    if (!config) return -1;
    type = config->attributes & 0x03u;
    if (type != 3u && type != 2u) return -1;
    /* Bulk endpoints carry no interval (Linux programs 0); only
     * interrupt endpoints require a nonzero bInterval. */
    if (config->max_packet_size == 0u) return -1;
    if (type == 3u && config->interval == 0u) return -1;
    ep_num = (uint8_t)(config->endpoint_address & 0x0fu);
    if ((config->endpoint_address & 0x70u) != 0u || ep_num == 0u ||
        ep_num >= 15u)
        return -2;
    dir = (config->endpoint_address & 0x80u) != 0u;
    ep_id = (uint8_t)((uint8_t)(ep_num * 2u) + (dir ? 1u : 0u));
    if (controller >= xhc_count || slot_id == 0u ||
        slot_id > xhc_controllers[controller].max_slots)
        return -3;
    c = &xhc_controllers[controller];
    slot = &xhc_runtimes[controller].slots[slot_id];
    if (!c->running || !slot->allocated || !slot->addressed ||
        !slot->input_context_phys)
        return -3;
    ring_phys = xhc_dma_page(c);
    if (!ring_phys) return -4;
    xhc_zero_page(ring_phys);
    ring = (volatile rix_xhci_trb_t *)(uintptr_t)ring_phys;
    ring[XHCI_CMD_RING_TRBS - 1u].parameter_lo = (uint32_t)ring_phys;
    ring[XHCI_CMD_RING_TRBS - 1u].parameter_hi = (uint32_t)(ring_phys >> 32);
    ring[XHCI_CMD_RING_TRBS - 1u].status = 0;
    ring[XHCI_CMD_RING_TRBS - 1u].control =
        XHCI_TRB_TYPE(XHCI_TRB_LINK) | XHCI_TRB_TC | XHCI_TRB_CYCLE;
    csz = (c->hcc_params1 & XHCI_HCC_CSZ) != 0u ? 64u : 32u;
    input = (volatile uint32_t *)(uintptr_t)slot->input_context_phys;
    slot_ctx = input + csz / 4u;
    ep = input + csz / 4u * (uint32_t)(ep_id + 1u);
    entries = ep_id > 1u ? ep_id : 1u;
    slot_ctx[0] = (slot_ctx[0] & ~(0x1fu << XHCI_SLOT_LAST_CTX_SHIFT)) |
        (entries << XHCI_SLOT_LAST_CTX_SHIFT);
    input[0] = 0;
    input[1] = XHCI_INPUT_ADD_SLOT | (1u << ep_id);
    /* MPS is bits 10:0 of wMaxPacketSize (Linux usb_endpoint_maxp mask);
     * HS additional transactions live in bits 12:11 and become Max Burst
     * (Linux xhci_get_endpoint_max_burst); SS burst comes from the
     * companion. Raw wMaxPacketSize must never land in the MPS field. */
    mps = config->max_packet_size & (uint16_t)USB_ENDPOINT_MAXP_MASK;
    if (slot->speed == 4u || slot->speed == 5u) {
        burst = config->max_burst;
    } else if (slot->speed == 3u && type == 3u) {
        burst = (uint8_t)(((uint32_t)config->max_packet_size >> 11) & 0x03u);
    }
    if (type == 3u) {
        /* Max ESIT payload for periodic endpoints (Linux
         * usb_endpoint_max_periodic_payload); explicit SS companion
         * payload wins when present. */
        esit = (uint32_t)mps * ((uint32_t)burst + 1u);
        if ((slot->speed == 4u || slot->speed == 5u) && config->esit_payload)
            esit = config->esit_payload;
        if (esit > 0xffffu) esit = 0xffffu;
    }
    interval = xhc_ep_interval(slot->speed, type, config->interval);
    ep[0] = (esit & 0xffffu) | (interval << 16);
    ep[1] = (3u << 1) | ((uint32_t)xhc_ep_type(config->endpoint_address,
                                               config->attributes)
                         << 3) |
        ((uint32_t)burst << 8) |
        ((uint32_t)mps << 16);
    ep[2] = (uint32_t)ring_phys | XHCI_TRB_CYCLE;
    ep[3] = (uint32_t)(ring_phys >> 32);
    ep[4] = (type == 3u) ? (esit & 0xffffu) : 8u;
    {
        int rc = xhc_submit_command(controller, slot->input_context_phys,
                                    XHCI_TRB_TYPE(XHCI_TRB_CONFIGURE_ENDPOINT) |
                                        XHCI_TRB_SLOT_FOR(slot_id),
                                    0);
        if (rc != 0) {
            pmm_free_page(ring_phys);
            return rc;
        }
    }
    if (slot->endpoints[ep_id].ring_phys)
        pmm_free_page(slot->endpoints[ep_id].ring_phys);
    slot->endpoints[ep_id].ring_phys = ring_phys;
    slot->endpoints[ep_id].type = type;
    slot->endpoints[ep_id].cycle = 1;
    slot->endpoints[ep_id].enqueue = 0;
    slot->endpoints[ep_id].in_flight = 0;
    slot->endpoints[ep_id].in_flight_first = 0;
    slot->endpoints[ep_id].in_flight_last = 0;
    return 0;
}

/* Reset a halted non-control endpoint (Linux endpoint-halt recovery:
 * Reset Endpoint, then Set Transfer Ring Dequeue Pointer past the dead
 * TD). HARD reset (no TSP): toggle restarts at DATA0, matching the
 * device side after CLEAR_FEATURE(ENDPOINT_HALT). Skips the dead TD by
 * restarting at the current enqueue/cycle; the failed TD already
 * completed to its caller with an error and is never replayed. */
int xhci_reset_endpoint(size_t controller, uint8_t slot_id,
                        uint8_t endpoint_address) {
    xhci_endpoint_runtime_t *ep_rt;
    uint8_t ep_num;
    uint8_t ep_id;
    uint64_t deq;
    int rc;
    int dir;
    ep_num = (uint8_t)(endpoint_address & 0x0fu);
    dir = (endpoint_address & 0x80u) != 0u;
    ep_id = (uint8_t)((uint8_t)(ep_num * 2u) + (dir ? 1u : 0u));
    if ((endpoint_address & 0x70u) != 0u || ep_num == 0u || ep_id >= 32u)
        return -1;
    if (controller >= xhc_count || slot_id == 0u ||
        slot_id > xhc_controllers[controller].max_slots)
        return -1;
    if (!xhc_controllers[controller].running ||
        !xhc_runtimes[controller].slots[slot_id].allocated ||
        !xhc_runtimes[controller].slots[slot_id].addressed)
        return -2;
    ep_rt = &xhc_runtimes[controller].slots[slot_id].endpoints[ep_id];
    if (!ep_rt->ring_phys || ep_rt->type == 0u) return -2;
    rc = xhc_submit_command(controller, 0,
                            XHCI_TRB_TYPE(XHCI_TRB_RESET_ENDPOINT) |
                                ((uint32_t)ep_id << XHCI_TRB_EP_SHIFT) |
                                XHCI_TRB_SLOT_FOR(slot_id),
                            0);
    /* Context State means the endpoint was not halted; the ring below
     * is still exactly where the runtime thinks it is. */
    if (rc != 0 && rc != -(int)XHCI_COMP_CONTEXT_STATE_ERROR) return rc;
    if (ep_rt->enqueue >= XHCI_CMD_RING_TRBS - 1u) {
        ep_rt->enqueue = 0;
        ep_rt->cycle ^= 1u;
    }
    deq = ep_rt->ring_phys + (uint64_t)ep_rt->enqueue * sizeof(rix_xhci_trb_t);
    rc = xhc_submit_command(controller, deq | (ep_rt->cycle ? 1u : 0u),
                            XHCI_TRB_TYPE(XHCI_TRB_SET_DEQUEUE_POINTER) |
                                ((uint32_t)ep_id << XHCI_TRB_EP_SHIFT) |
                                XHCI_TRB_SLOT_FOR(slot_id),
                            0);
    if (rc != 0) return rc;
    ep_rt->in_flight = 0;
    ep_rt->in_flight_first = 0;
    ep_rt->in_flight_last = 0;
    return 0;
}

int xhc_endpoint_transfer(size_t controller, uint8_t slot_id,
                          uint8_t endpoint_address, uint8_t ep_type,
                          void *buffer, uint16_t length,
                          uint16_t *actual_length) {
    xhci_runtime_t *rt;
    xhci_slot_runtime_t *slot;
    xhci_endpoint_runtime_t *ep_rt;
    uint8_t ep_num;
    uint8_t ep_id;
    volatile rix_xhci_trb_t *ring;
    uint64_t pa;
    uint64_t trb_phys;
    uint16_t index;
    int dir;
    if (actual_length) *actual_length = 0;
    if (!buffer || length == 0u) return -1;
    ep_num = (uint8_t)(endpoint_address & 0x0fu);
    dir = (endpoint_address & 0x80u) != 0u;
    ep_id = (uint8_t)((uint8_t)(ep_num * 2u) + (dir ? 1u : 0u));
    if ((endpoint_address & 0x70u) != 0u || ep_id >= 32u) return -2;
    if (controller >= xhc_count || slot_id == 0u ||
        slot_id > xhc_controllers[controller].max_slots)
        return -2;
    rt = &xhc_runtimes[controller];
    slot = &rt->slots[slot_id];
    if (!xhc_controllers[controller].running || !slot->allocated ||
        !slot->addressed)
        return -2;
    ep_rt = &slot->endpoints[ep_id];
    if (!ep_rt->ring_phys || ep_rt->type != ep_type) return -2;
    if (ep_rt->in_flight) {
        /* A previous TD is still owned by the controller (e.g. a NAK-
         * waiting interrupt-IN that outlived its wait). Never orphan
         * another TD on top of it: keep waiting on the live TD instead
         * of piling up zombie TDs that wrap the ring under the live
         * dequeue. Returns -100 while still pending. */
        int wrc = xhc_wait_transfer(controller, rt, ep_rt->in_flight_first,
                                    ep_rt->in_flight_last, slot_id, ep_id,
                                    length, actual_length);
        if (wrc != XHCI_XFER_TIMEOUT) ep_rt->in_flight = 0;
        return wrc;
    }
    if (ep_rt->enqueue >= XHCI_CMD_RING_TRBS - 1u) {
        ring = (volatile rix_xhci_trb_t *)(uintptr_t)ep_rt->ring_phys;
        ring[XHCI_CMD_RING_TRBS - 1u].parameter_lo =
            (uint32_t)ep_rt->ring_phys;
        ring[XHCI_CMD_RING_TRBS - 1u].parameter_hi =
            (uint32_t)(ep_rt->ring_phys >> 32);
        ring[XHCI_CMD_RING_TRBS - 1u].status = 0;
        ring[XHCI_CMD_RING_TRBS - 1u].control = XHCI_TRB_TYPE(XHCI_TRB_LINK) |
            XHCI_TRB_TC | (ep_rt->cycle ? XHCI_TRB_CYCLE : 0u);
        ep_rt->enqueue = 0;
        ep_rt->cycle ^= 1u;
    }
    pa = xhc_dma_linear_pa(buffer, length);
    if (!pa) return -3;
    ring = (volatile rix_xhci_trb_t *)(uintptr_t)ep_rt->ring_phys;
    index = ep_rt->enqueue++;
    trb_phys = ep_rt->ring_phys + (uint64_t)index * sizeof(rix_xhci_trb_t);
    ring[index].parameter_lo = (uint32_t)pa;
    ring[index].parameter_hi = (uint32_t)(pa >> 32);
    ring[index].status = length;
    ring[index].control = XHCI_TRB_TYPE(XHCI_TRB_NORMAL) | XHCI_TRB_IOC |
        (dir ? XHCI_TRB_ISP : 0u) |
        (ep_rt->cycle ? XHCI_TRB_CYCLE : 0u);
    __asm__ volatile("mfence" ::: "memory");
    xhc_record_doorbell(controller, slot_id, ep_id, ep_id);
    xhc_doorbell(controller, slot_id, ep_id);
    ep_rt->in_flight_first = trb_phys;
    ep_rt->in_flight_last = trb_phys;
    ep_rt->in_flight = 1;
    {
        int rc = xhc_wait_transfer(controller, rt, trb_phys, trb_phys,
                                   slot_id, ep_id, length, actual_length);
        if (rc != XHCI_XFER_TIMEOUT) ep_rt->in_flight = 0;
#if XHCI_HID_TRACE
        /* Payload dump on successful interrupt-IN only (bulk stays quiet).
         * One line per completed report: keypress-rate traffic, and the
         * first 8 bytes are enough to validate HID report framing. */
        if (rc == 0 && ep_type == 3u && actual_length) {
            const uint8_t *bytes = (const uint8_t *)buffer;
            serial_write("xHCI: HID-IN ctl=");
            serial_write_dec(controller);
            serial_write(" slot=");
            serial_write_dec(slot_id);
            serial_write(" ep=");
            serial_write_hex(endpoint_address);
            serial_write(" len=");
            serial_write_dec(*actual_length);
            for (uint16_t i = 0; i < *actual_length && i < 8u; ++i) {
                serial_write(" ");
                serial_write_hex(bytes[i]);
            }
            serial_write("\r\n");
        }
#endif
        return rc;
    }
}

int xhci_interrupt_transfer(size_t controller, uint8_t slot_id,
                            uint8_t endpoint_address, void *buffer,
                            uint16_t length, uint16_t *actual_length) {
    return xhc_endpoint_transfer(controller, slot_id, endpoint_address, 3u,
                                 buffer, length, actual_length);
}

int xhci_bulk_transfer(size_t controller, uint8_t slot_id,
                       uint8_t endpoint_address, void *buffer, uint16_t length,
                       uint16_t *actual_length) {
    return xhc_endpoint_transfer(controller, slot_id, endpoint_address, 2u,
                                 buffer, length, actual_length);
}
