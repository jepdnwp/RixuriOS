/* Control transfers and device enumeration.
 *
 * Provenance: Linux drivers/usb/host/xhci-ring.c (queue_setup_trb,
 * queue_data_trb, queue_status_trb, transfer-event handling with residual
 * length, short-packet success) and the USB core control-message path.
 * TRB construction keeps Linux field layout (TRT, direction, IOC, chain,
 * IDT immediate setup data); completion matching accepts any TD TRB
 * address in [first,last].
 */

#include "xhc.h"

void xhc_ep0_write_link(size_t ctl, xhci_slot_runtime_t *slot) {
    volatile rix_xhci_trb_t *ring =
        (volatile rix_xhci_trb_t *)(uintptr_t)slot->ep0_ring_phys;
    (void)ctl;
    ring[XHCI_CMD_RING_TRBS - 1u].parameter_lo = (uint32_t)slot->ep0_ring_phys;
    ring[XHCI_CMD_RING_TRBS - 1u].parameter_hi =
        (uint32_t)(slot->ep0_ring_phys >> 32);
    ring[XHCI_CMD_RING_TRBS - 1u].status = 0;
    ring[XHCI_CMD_RING_TRBS - 1u].control = XHCI_TRB_TYPE(XHCI_TRB_LINK) |
        XHCI_TRB_TC | (slot->ep0_cycle ? XHCI_TRB_CYCLE : 0u);
    slot->ep0_enqueue = 0;
    slot->ep0_cycle ^= 1u;
}

uint64_t xhc_ep0_emit(size_t ctl, xhci_slot_runtime_t *slot,
                      uint64_t parameter, uint32_t status, uint32_t control) {
    volatile rix_xhci_trb_t *ring;
    uint16_t index;
    uint64_t phys;
    (void)ctl;
    if (slot->ep0_enqueue >= XHCI_CMD_RING_TRBS - 1u)
        xhc_ep0_write_link(ctl, slot);
    ring = (volatile rix_xhci_trb_t *)(uintptr_t)slot->ep0_ring_phys;
    index = slot->ep0_enqueue++;
    phys = slot->ep0_ring_phys + (uint64_t)index * sizeof(rix_xhci_trb_t);
    ring[index].parameter_lo = (uint32_t)parameter;
    ring[index].parameter_hi = (uint32_t)(parameter >> 32);
    ring[index].status = status;
    ring[index].control =
        (control & ~XHCI_TRB_CYCLE) | (slot->ep0_cycle ? XHCI_TRB_CYCLE : 0u);
    return phys;
}

/* Physical address of a linear DMA buffer. Fails closed (0) when any page
 * is unmapped, the range is not physically contiguous, or it spans a
 * 64 KiB boundary (xHCI 4.11.7.1: one data TRB cannot; Linux splits such
 * transfers, this driver aligns its buffers so the check only guards
 * against future callers regressing it). */
uint64_t xhc_dma_linear_pa(const void *buffer, uint64_t length) {
    uint64_t va;
    uint64_t pa;
    uint64_t first_base;
    uint64_t page;
    uint64_t last;
    if (!buffer || !length) return 0;
    va = (uint64_t)(uintptr_t)buffer;
    if (va > UINT64_MAX - length) return 0;
    pa = vmm_translate(va);
    if (!pa) return 0;
    first_base = pa & ~0xfffULL;
    page = va & ~0xfffULL;
    last = (va + length - 1u) & ~0xfffULL;
    for (; page <= last; page += 0x1000ULL) {
        uint64_t p = vmm_translate(page);
        if (!p) return 0;
        if ((p & ~0xfffULL) != first_base + (page - (va & ~0xfffULL)))
            return 0;
        if (page == last) break;
    }
    if (xhc_pa_crosses_64k(pa, length)) return 0;
    return pa;
}

/* Wait for a transfer event on this TD (Linux handle_tx_event residual
 * accounting). Matches the event's TRB pointer against any TRB of the TD,
 * plus slot and DCI. Short Packet counts as success for IN transfers.
 * Returns 0 on success, -completion_code on error, -100 on timeout. */
int xhc_wait_transfer_limit(size_t ctl, xhci_runtime_t *rt, uint64_t first_phys,
                            uint64_t last_phys, uint8_t slot_id, uint8_t ep_id,
                            uint16_t requested, uint16_t *actual,
                            uint32_t poll_limit) {
    const rix_xhci_controller_t *c = &xhc_controllers[ctl];
    volatile rix_xhci_trb_t *events =
        (volatile rix_xhci_trb_t *)(uintptr_t)c->event_ring_phys;
    if (poll_limit == 0u || poll_limit > XHCI_POLL_LIMIT)
        poll_limit = XHCI_POLL_LIMIT;
    for (uint32_t i = 0; i < poll_limit; ++i) {
        volatile rix_xhci_trb_t *event = &events[rt->event_dequeue];
        uint32_t control = event->control;
        uint32_t status;
        uint32_t type;
        uint8_t event_slot;
        uint8_t ep;
        uint8_t cc;
        uint32_t residual;
        uint64_t parameter;
        if ((control & XHCI_TRB_CYCLE) != (rt->event_cycle ? XHCI_TRB_CYCLE : 0u)) {
            if ((i & 0xffu) == 0u) xhc_udelay(50u);
            continue;
        }
        status = event->status;
        type = XHCI_TRB_FIELD_TO_TYPE(control);
        event_slot = (uint8_t)XHCI_TRB_TO_SLOT_ID(control);
        ep = (uint8_t)XHCI_TRB_TO_EP_ID(control);
        cc = (uint8_t)XHCI_GET_COMP_CODE(status);
        residual = XHCI_EVENT_TRB_LEN(status);
        parameter = ((uint64_t)event->parameter_hi << 32) | event->parameter_lo;
        if (type == XHCI_TRB_PORT_STATUS_CHANGE) {
            uint8_t p = (uint8_t)(event->parameter_lo >> 24);
            xhc_pending_port_push(ctl, p, 1);
            xhc_acknowledge_event(ctl, rt);
            XHCI_MMIO_WRITE32(xhc_op_base(ctl), XHCI_USBSTS,
                           XHCI_STS_PCD | XHCI_STS_EINT);
            continue;
        }
        xhc_acknowledge_event(ctl, rt);
        xhc_record_event(ctl, (uint8_t)type, status, event_slot, ep,
                         parameter);
        if (type != XHCI_TRB_TRANSFER_EVENT) continue;
        if (parameter < first_phys || parameter > last_phys) continue;
        if ((parameter - first_phys) % sizeof(rix_xhci_trb_t) != 0u) continue;
        if (event_slot != slot_id || ep != ep_id) continue;
        if (actual)
            *actual = residual >= requested ? 0u : (uint16_t)(requested - residual);
        if (cc == XHCI_COMP_SUCCESS) return 0;
        if (cc == XHCI_COMP_SHORT_PACKET) return 0;
#if XHCI_CC4_SNAPSHOT
        if (cc == XHCI_COMP_USB_TRANSACTION_ERROR)
            xhc_cc4_snapshot(ctl, rt, slot_id, ep_id, first_phys, last_phys);
#endif
        return cc != 0u ? -(int)cc : -90;
    }
    return -100;
}

int xhc_wait_transfer(size_t ctl, xhci_runtime_t *rt, uint64_t first_phys,
                      uint64_t last_phys, uint8_t slot_id, uint8_t ep_id,
                      uint16_t requested, uint16_t *actual) {
    return xhc_wait_transfer_limit(ctl, rt, first_phys, last_phys, slot_id,
                                   ep_id, requested, actual, XHCI_POLL_LIMIT);
}

int xhci_control_transfer(size_t controller, uint8_t slot_id,
                          const rix_usb_setup_packet_t *setup,
                          void *data, uint16_t *actual_length) {
    xhci_runtime_t *rt;
    xhci_slot_runtime_t *slot;
    uint16_t length;
    int data_in;
    uint64_t setup_param;
    uint64_t data_pa = 0;
    uint64_t setup_phys = 0, data_phys = 0, status_phys = 0;
    int rc = -1;
    if (actual_length) *actual_length = 0;
    if (!setup || controller >= xhc_count || slot_id == 0u ||
        slot_id > xhc_controllers[controller].max_slots)
        return -1;
    rt = &xhc_runtimes[controller];
    slot = &rt->slots[slot_id];
    length = setup->length;
    if (!xhc_controllers[controller].running || !slot->allocated ||
        !slot->addressed || !slot->ep0_ring_phys)
        return -2;
    if (length != 0u && !data) return -2;
    if (data && length != 0u &&
        (uint64_t)(uintptr_t)data > UINT64_MAX - length)
        return -3;
    if (length != 0u) {
        data_pa = xhc_dma_linear_pa(data, length);
        if (!data_pa) return -5;
    }
    data_in = (setup->request_type & USB_DIR_IN) != 0u;
    setup_param = (uint64_t)setup->request_type |
        ((uint64_t)setup->request << 8) |
        ((uint64_t)setup->value << 16) |
        ((uint64_t)setup->index << 32) |
        ((uint64_t)setup->length << 48);
    for (unsigned attempt = 1u; attempt <= 3u; ++attempt) {
        uint16_t attempt_enqueue = slot->ep0_enqueue;
        uint8_t attempt_cycle = slot->ep0_cycle;
        uint32_t setup_control;
        if (attempt > 1u) {
            slot->ep0_enqueue = attempt_enqueue;
            slot->ep0_cycle = attempt_cycle;
            if (slot->ep0_enqueue + (length ? 3u : 2u) > XHCI_CMD_RING_TRBS - 1u)
                xhc_ep0_write_link(controller, slot);
            if (xhc_reset_ep0_for_retry(controller, slot_id) != 0) return rc;
            if (xhc_set_ep0_dequeue_for_retry(controller, slot_id,
                                              slot->ep0_enqueue,
                                              slot->ep0_cycle) != 0)
                return rc;
        }
        slot->ep0_seq++;
        setup_control = XHCI_TRB_TYPE(XHCI_TRB_SETUP_STAGE) | XHCI_TRB_IDT;
        if (length != 0u)
            setup_control |= XHCI_TRB_CHAIN | ((data_in ? 3u : 2u) << 16);
        setup_phys = xhc_ep0_emit(controller, slot, setup_param, 8u,
                                  setup_control);
        if (length != 0u) {
            uint32_t data_control = XHCI_TRB_TYPE(XHCI_TRB_DATA_STAGE) |
                XHCI_TRB_CHAIN | (data_in ? XHCI_TRB_DIR : 0u);
            data_phys = xhc_ep0_emit(controller, slot, data_pa, length,
                                     data_control);
        }
        {
            /* Status Stage runs opposite to the Data Stage (Linux
             * queue_status_trb direction logic); with no data stage the
             * status is IN. */
            uint32_t status_control = XHCI_TRB_TYPE(XHCI_TRB_STATUS_STAGE) |
                XHCI_TRB_IOC;
            if (length == 0u || !data_in) status_control |= XHCI_TRB_DIR;
            status_phys = xhc_ep0_emit(controller, slot, 0, 0, status_control);
        }
#if XHCI_EP0_TRACE
        xhc_log_ep0_trb("EP0-SETUP", setup_phys);
        if (length != 0u) xhc_log_ep0_trb("EP0-DATA", data_phys);
        xhc_log_ep0_trb("EP0-STATUS", status_phys);
#endif
        __asm__ volatile("mfence" ::: "memory");
        xhc_record_doorbell(controller, slot_id, 1u, 1u);
        xhc_doorbell(controller, slot_id, XHCI_DB_TARGET(0));
        rc = xhc_wait_transfer(controller, rt, setup_phys, status_phys,
                               slot_id, 1u, length, actual_length);
        if (rc == 0) return 0;
        if (rc != -(int)XHCI_COMP_USB_TRANSACTION_ERROR &&
            rc != -(int)XHCI_COMP_STALL_ERROR)
            return rc;
        {
            rix_xhci_port_status_t ps;
            if (xhci_port_status(controller, slot->port, &ps) != 0 ||
                !ps.connected)
                return rc;
        }
        xhc_udelay(50000u);
    }
    return rc;
}

int xhci_get_descriptor(size_t controller, uint8_t slot_id,
                        uint8_t descriptor_type, uint8_t descriptor_index,
                        uint16_t language_id, void *buffer, uint16_t length,
                        uint16_t *actual_length) {
    rix_usb_setup_packet_t setup;
    if (descriptor_type == 0u || (length != 0u && !buffer)) return -1;
    setup.request_type = (uint8_t)(USB_DIR_IN | USB_TYPE_STANDARD |
                                   USB_RECIP_DEVICE);
    setup.request = USB_REQ_GET_DESCRIPTOR;
    setup.value = (uint16_t)(((uint16_t)descriptor_type << 8) | descriptor_index);
    setup.index = language_id;
    setup.length = length;
    return xhci_control_transfer(controller, slot_id, &setup, buffer,
                                 actual_length);
}

int xhci_set_configuration(size_t controller, uint8_t slot_id,
                           uint8_t configuration_value) {
    rix_usb_setup_packet_t setup;
    setup.request_type = (uint8_t)(USB_DIR_OUT | USB_TYPE_STANDARD |
                                   USB_RECIP_DEVICE);
    setup.request = USB_REQ_SET_CONFIGURATION;
    setup.value = configuration_value;
    setup.index = 0;
    setup.length = 0;
    return xhci_control_transfer(controller, slot_id, &setup, 0, 0);
}

int xhci_get_hid_report_descriptor(size_t controller, uint8_t slot_id,
                                   uint8_t interface_number, void *buffer,
                                   uint16_t length, uint16_t *actual_length) {
    /* Pinned: 0x81. A past cleanup "fixed" this to CLASS type and real
     * firmware stalled the fetch (QEMU failed=8). */
    _Static_assert((USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE) ==
                   0x81u, "hid report bmRequestType");
    rix_usb_setup_packet_t setup;
    if (interface_number >= 32u || length == 0u || !buffer) return -1;
    /* GET_DESCRIPTOR is a STANDARD request even for class-defined
     * descriptor types (USB 2.0 §9.4.3, HID 1.11 §7.1.1): only wValue
     * carries the class type. CLASS here would stall real firmware. */
    setup.request_type = (uint8_t)(USB_DIR_IN | USB_TYPE_STANDARD |
                                   USB_RECIP_INTERFACE);
    setup.request = USB_REQ_GET_DESCRIPTOR;
    setup.value = (uint16_t)((uint16_t)HID_DT_REPORT << 8);
    setup.index = interface_number;
    setup.length = length;
    return xhci_control_transfer(controller, slot_id, &setup, buffer,
                                 actual_length);
}

int xhci_hid_set_protocol(size_t controller, uint8_t slot_id,
                          uint8_t interface_number, uint8_t protocol) {
    rix_usb_setup_packet_t setup;
    if (interface_number >= 32u || protocol > 1u) return -1;
    setup.request_type = (uint8_t)(USB_DIR_OUT | USB_TYPE_CLASS |
                                   USB_RECIP_INTERFACE);
    setup.request = HID_REQ_SET_PROTOCOL;
    setup.value = protocol;
    setup.index = interface_number;
    setup.length = 0;
    return xhci_control_transfer(controller, slot_id, &setup, 0, 0);
}

int xhci_hid_set_idle(size_t controller, uint8_t slot_id,
                      uint8_t interface_number, uint8_t report_id,
                      uint8_t duration_4ms) {
    rix_usb_setup_packet_t setup;
    if (interface_number >= 32u) return -1;
    setup.request_type = (uint8_t)(USB_DIR_OUT | USB_TYPE_CLASS |
                                   USB_RECIP_INTERFACE);
    setup.request = HID_REQ_SET_IDLE;
    setup.value = (uint16_t)(((uint16_t)duration_4ms << 8) | report_id);
    setup.index = interface_number;
    setup.length = 0;
    return xhci_control_transfer(controller, slot_id, &setup, 0, 0);
}

int xhci_hid_get_protocol(size_t controller, uint8_t slot_id,
                          uint8_t interface_number, uint8_t *protocol) {
    rix_usb_setup_packet_t setup;
    uint8_t value = 0xffu;
    uint16_t actual = 0;
    int rc;
    if (interface_number >= 32u || !protocol) return -1;
    setup.request_type = (uint8_t)(USB_DIR_IN | USB_TYPE_CLASS |
                                   USB_RECIP_INTERFACE);
    setup.request = HID_REQ_GET_PROTOCOL;
    setup.value = 0;
    setup.index = interface_number;
    setup.length = 1;
    rc = xhci_control_transfer(controller, slot_id, &setup, &value, &actual);
    if (rc != 0) return rc;
    if (actual != 1u || value > 1u) return -2;
    *protocol = value;
    return 0;
}

/* Two-stage enumeration (Linux usb_get_device_descriptor + config fetch):
 * short device read for MPS0, full device descriptor, config header for
 * wTotalLength, full configuration through the bounds-checked parser. */
int xhci_enumerate_device(size_t controller, uint8_t slot_id,
                          rix_usb_device_descriptor_t *device,
                          uint8_t *configuration, uint16_t configuration_capacity,
                          rix_usb_configuration_info_t *configuration_info,
                          rix_usb_interface_info_t *interfaces,
                          size_t interface_capacity,
                          rix_usb_endpoint_info_t *endpoints,
                          size_t endpoint_capacity, size_t *interface_count,
                          size_t *endpoint_count) {
    /* Static: enumerate runs to completion synchronously (worker and
     * pre-scheduler settle never overlap), and a 64-byte stack buffer
     * could straddle 64 KiB while this aligned one cannot. */
    static uint8_t buf[64] __attribute__((aligned(64)));
    uint16_t actual = 0;
    int rc;
    uint8_t mps0;
    uint16_t total;
    if (!device || !configuration || !configuration_info ||
        configuration_capacity < 9u || !interface_count || !endpoint_count)
        return -1;
    for (unsigned i = 0; i < sizeof(buf); ++i) buf[i] = 0;
    rc = xhci_get_descriptor(controller, slot_id, RIX_USB_DESC_DEVICE, 0, 0,
                             buf, 8u, &actual);
    if (rc != 0 || actual < 8u) return -2;
    mps0 = buf[7];
    if (mps0 == 8u || mps0 == 16u || mps0 == 32u || mps0 == 64u || mps0 == 9u) {
        /* Log-only on failure: enumeration continues with the bootstrap MPS. */
        (void)xhc_evaluate_ep0_mps(controller, slot_id, mps0);
    }
    rc = xhci_get_descriptor(controller, slot_id, RIX_USB_DESC_DEVICE, 0, 0,
                             buf, 18u, &actual);
    if (rc != 0 || actual < 18u) return -2;
    if (usb_parse_device_descriptor(buf, 18u, device) != 0) return -2;
    rc = xhci_get_descriptor(controller, slot_id, RIX_USB_DESC_CONFIGURATION,
                             0, 0, configuration, 9u, &actual);
    if (rc != 0 || actual < 9u ||
        configuration[1] != RIX_USB_DESC_CONFIGURATION)
        return -3;
    total = (uint16_t)configuration[2] | ((uint16_t)configuration[3] << 8);
    if (total < 9u || total > configuration_capacity) return -4;
    rc = xhci_get_descriptor(controller, slot_id, RIX_USB_DESC_CONFIGURATION,
                             0, 0, configuration, total, &actual);
    if (rc != 0 || actual < total) return -5;
    if (usb_parse_configuration_descriptor(configuration, total,
                                            configuration_info, interfaces,
                                            interface_capacity, endpoints,
                                            endpoint_capacity, interface_count,
                                            endpoint_count) != 0)
        return -6;
    xhci_usb_state_transition(controller, slot_id, XHCI_USB_ADDRESSED,
                              "enumerate-ok");
    return 0;
}
