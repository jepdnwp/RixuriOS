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
    uint64_t ring_phys;
    volatile rix_xhci_trb_t *ring;
    int dir;
    if (!config) return -1;
    type = config->attributes & 0x03u;
    if (type != 3u && type != 2u) return -1;
    if (config->max_packet_size == 0u || config->interval == 0u) return -1;
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
    if (slot->speed >= 4u && type == 3u) {
        esit = config->esit_payload
            ? config->esit_payload
            : (uint32_t)config->max_packet_size *
                ((uint32_t)config->max_burst + 1u);
        if (esit > 0xffffu) esit = 0xffffu;
    }
    ep[0] = (esit & 0xffffu) | ((uint32_t)config->interval << 16);
    ep[1] = (3u << 1) | ((uint32_t)xhc_ep_type(config->endpoint_address,
                                               config->attributes)
                         << 3) |
        ((uint32_t)config->max_burst << 8) |
        ((uint32_t)config->max_packet_size << 16);
    ep[2] = (uint32_t)ring_phys | XHCI_TRB_CYCLE;
    ep[3] = (uint32_t)(ring_phys >> 32);
    ep[4] = 8u;
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
        (ep_rt->cycle ? XHCI_TRB_CYCLE : 0u);
    __asm__ volatile("mfence" ::: "memory");
    xhc_record_doorbell(controller, slot_id, ep_id, ep_id);
    xhc_doorbell(controller, slot_id, ep_id);
    {
        int rc = xhc_wait_transfer(controller, rt, trb_phys, trb_phys,
                                   slot_id, ep_id, length, actual_length);
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
