/* Device slots, input contexts and slot commands.
 *
 * Provenance: Linux drivers/usb/host/xhci-mem.c
 * (xhci_alloc_virt_device, input-control/slot/endpoint context layout per
 * xHCI 6.2) and xhci-ring.c queue_slot_control / queue_address_device /
 * queue_configure_endpoint / queue_reset_ep / queue_set_tr_deq.
 * BSR is never set (always BSR=0 Address Device). Transaction-Error
 * retries follow the extracted AMD-verified sequence.
 */

#include "xhc.h"

/* Bootstrap EP0 MaxPacketSize before the first descriptor read (Linux
 * uses the port speed the same way when building the input context).
 * Full speed must start at 8 until bMaxPacketSize0 is known. */
uint16_t xhc_initial_ep0_mps(uint8_t speed) {
    if (speed == 3u) return 64u;
    if (speed >= 4u) return 512u;
    if (speed == 1u) return 8u;
    return 8u;
}

int xhc_allocate_slot_context(size_t ctl, xhci_slot_runtime_t *slot) {
    const rix_xhci_controller_t *c = &xhc_controllers[ctl];
    uint64_t device = xhc_dma_page(c);
    uint64_t input = xhc_dma_page(c);
    uint64_t ep0_ring = xhc_dma_page(c);
    if (!device || !input || !ep0_ring) {
        if (device) pmm_free_page(device);
        if (input) pmm_free_page(input);
        if (ep0_ring) pmm_free_page(ep0_ring);
        return -1;
    }
    xhc_zero_page(device);
    xhc_zero_page(input);
    xhc_zero_page(ep0_ring);
    slot->device_context_phys = device;
    slot->input_context_phys = input;
    slot->ep0_ring_phys = ep0_ring;
    return 0;
}

void xhc_release_slot_context(size_t ctl, uint8_t slot_id) {
    rix_xhci_controller_t *c = &xhc_controllers[ctl];
    xhci_slot_runtime_t *slot = &xhc_runtimes[ctl].slots[slot_id];
    volatile uint64_t *dcbaa =
        (volatile uint64_t *)(uintptr_t)c->dcbaa_phys;
    dcbaa[slot_id] = 0;
    __asm__ volatile("mfence" ::: "memory");
    if (slot->device_context_phys) pmm_free_page(slot->device_context_phys);
    if (slot->input_context_phys) pmm_free_page(slot->input_context_phys);
    if (slot->ep0_ring_phys) pmm_free_page(slot->ep0_ring_phys);
    for (unsigned i = 2u; i < 32u; ++i) {
        if (slot->endpoints[i].ring_phys) {
            pmm_free_page(slot->endpoints[i].ring_phys);
            slot->endpoints[i].ring_phys = 0;
        }
        slot->endpoints[i].type = 0;
        slot->endpoints[i].cycle = 0;
        slot->endpoints[i].enqueue = 0;
        slot->endpoints[i].in_flight = 0;
        slot->endpoints[i].in_flight_first = 0;
        slot->endpoints[i].in_flight_last = 0;
    }
    slot->device_context_phys = 0;
    slot->input_context_phys = 0;
    slot->ep0_ring_phys = 0;
    slot->allocated = 0;
    slot->addressed = 0;
    slot->port = 0;
    slot->speed = 0;
    slot->route_string = 0;
    slot->ep0_enqueue = 0;
    slot->ep0_cycle = 0;
    slot->ep0_seq = 0;
    slot->addr_done_ns = 0;
    slot->usb_state = XHCI_USB_DETACHED;
}

/* Build the Address Device input context (Linux xhci_setup_addressable_virt_dev
 * context preparation): Add Slot + Add EP0, slot context with
 * Speed/Route/Context-Entries=1/RH-Port, EP0 context with Control type,
 * CERR and the transfer-ring dequeue pointer with DCS. */
int xhc_prepare_address_context(size_t ctl, uint8_t slot_id, uint8_t port,
                                uint8_t speed) {
    rix_xhci_controller_t *c = &xhc_controllers[ctl];
    xhci_slot_runtime_t *slot = &xhc_runtimes[ctl].slots[slot_id];
    uint32_t context_size;
    volatile uint32_t *input;
    volatile uint32_t *slot_ctx;
    volatile uint32_t *ep0_ctx;
    uint16_t mps;
    uint32_t cerr;
    if (xhc_allocate_slot_context(ctl, slot) != 0) return -1;
    ((volatile uint64_t *)(uintptr_t)c->dcbaa_phys)[slot_id] =
        slot->device_context_phys;
    __asm__ volatile("mfence" ::: "memory");
    {
        volatile rix_xhci_trb_t *ring =
            (volatile rix_xhci_trb_t *)(uintptr_t)slot->ep0_ring_phys;
        ring[XHCI_CMD_RING_TRBS - 1u].parameter_lo = (uint32_t)slot->ep0_ring_phys;
        ring[XHCI_CMD_RING_TRBS - 1u].parameter_hi =
            (uint32_t)(slot->ep0_ring_phys >> 32);
        ring[XHCI_CMD_RING_TRBS - 1u].status = 0;
        ring[XHCI_CMD_RING_TRBS - 1u].control =
            XHCI_TRB_TYPE(XHCI_TRB_LINK) | XHCI_TRB_TC | XHCI_TRB_CYCLE;
    }
    context_size = (c->hcc_params1 & XHCI_HCC_CSZ) != 0u ? 64u : 32u;
    input = (volatile uint32_t *)(uintptr_t)slot->input_context_phys;
    slot_ctx = input + context_size / 4u;
    ep0_ctx = input + 2u * context_size / 4u;
    input[0] = 0;
    input[1] = XHCI_INPUT_ADD_SLOT | XHCI_INPUT_ADD_EP0;
    slot_ctx[0] = (((uint32_t)speed & 0xfu) << XHCI_SLOT_SPEED_SHIFT) |
        (1u << XHCI_SLOT_LAST_CTX_SHIFT);
    slot_ctx[1] = (uint32_t)port << 16;
    slot_ctx[2] = 0;
    slot_ctx[3] = 0;
    mps = xhc_initial_ep0_mps(speed);
    cerr = (speed >= 4u) ? 0u : 3u;
    ep0_ctx[0] = 0;
    ep0_ctx[1] = (cerr << 1) | (XHCI_EP_CONTROL << 3) | ((uint32_t)mps << 16);
    ep0_ctx[2] = (uint32_t)slot->ep0_ring_phys | 1u;
    ep0_ctx[3] = (uint32_t)(slot->ep0_ring_phys >> 32);
    ep0_ctx[4] = 8u;
    ep0_ctx[5] = 0;
    ep0_ctx[6] = 0;
    ep0_ctx[7] = 0;
    slot->ep0_enqueue = 0;
    slot->ep0_cycle = 1;
    slot->port = port;
    slot->speed = speed;
    return 0;
}

int xhci_enable_slot(size_t controller, uint8_t *out_slot) {
    uint8_t slot_id = 0;
    int rc;
    if (controller >= xhc_count || !out_slot) return -1;
    rc = xhc_submit_command(controller, 0, XHCI_TRB_TYPE(XHCI_TRB_ENABLE_SLOT),
                            &slot_id);
    if (rc != 0) return rc;
    if (slot_id == 0u || slot_id > xhc_controllers[controller].max_slots)
        return -3;
    if (xhc_runtimes[controller].slots[slot_id].allocated) return -4;
    xhc_runtimes[controller].slots[slot_id].allocated = 1;
    *out_slot = slot_id;
    return 0;
}

int xhci_disable_slot(size_t controller, uint8_t slot_id) {
    int rc;
    if (controller >= xhc_count || slot_id == 0u ||
        slot_id > xhc_controllers[controller].max_slots)
        return -1;
    if (!xhc_runtimes[controller].slots[slot_id].allocated) return -2;
    rc = xhc_submit_command(controller, 0,
                            XHCI_TRB_TYPE(XHCI_TRB_DISABLE_SLOT) |
                                XHCI_TRB_SLOT_FOR(slot_id),
                            0);
    xhc_release_slot_context(controller, slot_id);
    return rc;
}

/* Address Device with BSR=0 (Linux queue_address_device setup_addr=0).
 * Only Transaction-Error (CC4) is retried, up to 3 attempts with a paced
 * delay; the same input context is legal to re-issue in Default state.
 * On success the zero-address guard rejects a no-op completion. */
int xhci_address_device(size_t controller, uint8_t slot_id, uint8_t port,
                        uint8_t speed) {
    rix_xhci_controller_t *c;
    xhci_slot_runtime_t *slot;
    int rc = -1;
    if (controller >= xhc_count || slot_id == 0u ||
        slot_id > xhc_controllers[controller].max_slots)
        return -1;
    c = &xhc_controllers[controller];
    (void)c;
    slot = &xhc_runtimes[controller].slots[slot_id];
    if (!slot->allocated || slot->addressed) return -2;
    if (xhc_prepare_address_context(controller, slot_id, port, speed) != 0)
        return -3;
#if XHCI_ADDR_TRACE
    xhc_log_address_device_begin(controller, slot_id, port, speed);
#endif
    for (unsigned attempt = 1u; attempt <= 3u; ++attempt) {
        rc = xhc_submit_command(controller, slot->input_context_phys,
                                XHCI_TRB_TYPE(XHCI_TRB_ADDRESS_DEVICE) |
                                    XHCI_TRB_SLOT_FOR(slot_id),
                                0);
        if (rc == 0 || rc != -(int)XHCI_COMP_USB_TRANSACTION_ERROR) break;
        {
            rix_xhci_port_status_t ps;
            if (xhci_port_status(controller, port, &ps) != 0 || !ps.connected)
                break;
        }
        xhc_udelay(50000u);
    }
    if (rc == 0) {
        volatile uint32_t *devctx =
            (volatile uint32_t *)(uintptr_t)slot->device_context_phys;
        if ((devctx[3] & 0xffu) == 0u) rc = -(int)XHCI_COMP_USB_TRANSACTION_ERROR;
    }
    if (rc != 0) return rc;
    slot->addressed = 1;
    slot->addr_done_ns = time_monotonic_ns();
    xhci_usb_state_transition(controller, slot_id, XHCI_USB_ADDRESSED,
                              "address-device-ok");
    return 0;
}

/* Reset Endpoint with Transfer-State-Preserve for EP0 retry (Linux
 * queue_reset_ep EP_HARD_RESET on DCI 1). CC 0 or Context-State means the
 * endpoint was not halted and the ring can simply be re-rung. */
int xhc_reset_ep0_for_retry(size_t ctl, uint8_t slot_id) {
    int rc = xhc_submit_command(ctl, 0,
                                XHCI_TRB_TYPE(XHCI_TRB_RESET_ENDPOINT) |
                                    XHCI_TRB_TSP | ((uint32_t)1u << XHCI_TRB_EP_SHIFT) |
                                    XHCI_TRB_SLOT_FOR(slot_id),
                                0);
    if (rc == 0 || rc == -(int)XHCI_COMP_CONTEXT_STATE_ERROR) return 0;
    return rc;
}

/* Reprogram the EP0 dequeue pointer after a retry reset (Linux
 * queue_set_tr_deq with the saved enqueue/cycle). */
int xhc_set_ep0_dequeue_for_retry(size_t ctl, uint8_t slot_id,
                                  uint16_t enqueue, uint8_t cycle) {
    xhci_slot_runtime_t *slot;
    uint64_t deq;
    if (ctl >= xhc_count || slot_id == 0u ||
        slot_id > xhc_controllers[ctl].max_slots || enqueue >= XHCI_CMD_RING_TRBS - 1u)
        return -1;
    slot = &xhc_runtimes[ctl].slots[slot_id];
    deq = slot->ep0_ring_phys + (uint64_t)enqueue * sizeof(rix_xhci_trb_t);
    return xhc_submit_command(ctl, deq | (cycle ? 1u : 0u),
                              XHCI_TRB_TYPE(XHCI_TRB_SET_DEQUEUE_POINTER) |
                                  ((uint32_t)1u << XHCI_TRB_EP_SHIFT) |
                                  XHCI_TRB_SLOT_FOR(slot_id),
                              0);
}

/* Evaluate Context updating only EP0 MaxPacketSize from the real device
 * descriptor (Linux xhci_evaluate_context_for_ep0 / Evaluate Context with
 * Add EP0). Copy is device->input so unrelated fields cannot be clobbered. */
int xhc_evaluate_ep0_mps(size_t ctl, uint8_t slot_id, uint8_t mps) {
    rix_xhci_controller_t *c;
    xhci_slot_runtime_t *slot;
    uint32_t csz;
    volatile uint32_t *dev;
    volatile uint32_t *ep0in;
    volatile uint32_t *input;
    if (mps == 0u) return -1;
    if (ctl >= xhc_count || slot_id == 0u ||
        slot_id > xhc_controllers[ctl].max_slots)
        return -2;
    c = &xhc_controllers[ctl];
    slot = &xhc_runtimes[ctl].slots[slot_id];
    if (!slot->allocated || !slot->addressed || !slot->input_context_phys ||
        !slot->device_context_phys)
        return -2;
    csz = (c->hcc_params1 & XHCI_HCC_CSZ) != 0u ? 64u : 32u;
    input = (volatile uint32_t *)(uintptr_t)slot->input_context_phys;
    dev = (volatile uint32_t *)(uintptr_t)slot->device_context_phys;
    ep0in = input + 2u * csz / 4u;
    for (uint32_t i = 0; i < csz / 4u; ++i) ep0in[i] = dev[csz / 4u + i];
    ep0in[0] &= ~0x7u;
    ep0in[1] = (ep0in[1] & ~0xffff0000u) | ((uint32_t)mps << 16);
    input[0] = 0;
    input[1] = XHCI_INPUT_ADD_EP0;
    __asm__ volatile("mfence" ::: "memory");
    return xhc_submit_command(ctl, slot->input_context_phys,
                              XHCI_TRB_TYPE(XHCI_TRB_EVALUATE_CONTEXT) |
                                  XHCI_TRB_SLOT_FOR(slot_id),
                              0);
}
