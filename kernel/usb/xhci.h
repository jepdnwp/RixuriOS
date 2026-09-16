#pragma once
#include <stddef.h>
#include <stdint.h>
#include "usb.h"
#include "xhci/caps.h"
#include "xhci/profile.h"

typedef struct {
    uint8_t bus, device, function;
    /* Phase H6: PCI identity, kept so the hotplug/reset paths can look the
     * controller up in the hardware profile without re-walking PCI. */
    uint16_t vendor_id, device_id;
    uint64_t bar0;
    uint64_t mmio_va;
    uint8_t cap_length, max_slots, max_intrs, max_ports;
    uint32_t hci_version, hcc_params1, usbcmd, usbsts;
    uint64_t dcbaa_phys, cmd_ring_phys, event_ring_phys, erst_phys;
    uint8_t running;
    /* Phase H4: Supported-Protocol port map (USB major 2/3 per range,
     * 1-based ports) + quirk flags. Unknown when the walk finds
     * nothing (QEMU): reset path keeps the legacy heuristic. */
    uint8_t proto_major[XHCI_SPC_MAX_RANGES];
    uint8_t proto_start[XHCI_SPC_MAX_RANGES];
    uint8_t proto_count[XHCI_SPC_MAX_RANGES];
    uint8_t proto_ranges;
    uint32_t quirks;
} rix_xhci_controller_t;
/* Quirk flags and the per-ID evidence that sets them live in
 * xhci/profile.h (XHCI_PROFILE_QUIRK_*): behavioral quirks are added only
 * with per-ID evidence, never preemptively. XHCI_QUIRK_NONE is the neutral
 * value for a controller with no known part. */
#define XHCI_QUIRK_NONE XHCI_PROFILE_QUIRK_NONE
/* USB protocol major for a port: 2, 3, or 0 when the map is unknown. */
int xhci_port_protocol(size_t controller, uint8_t port);

typedef struct { uint8_t connected, enabled, speed, reset_complete; } rix_xhci_port_status_t;
typedef struct {
    uint8_t slot_id;
    uint8_t port;
    uint8_t speed;
    uint8_t state;
    /* Hub-attached devices: parent_hub_slot is the hub's slot (0 when the
     * device sits on a root port and port is a root port number);
     * otherwise port is hub-relative. route/level follow the USB 2.0
     * route-string rules (route 0 at the root, level 1 per tier). */
    uint8_t parent_hub_slot;
    uint32_t route;
    uint8_t level;
} rix_xhci_device_t;
typedef struct { uint8_t request_type, request; uint16_t value, index, length; } rix_usb_setup_packet_t;
typedef struct {
    uint8_t endpoint_address;
    uint8_t attributes;
    uint16_t max_packet_size;
    uint8_t interval;
    uint8_t max_burst;
    /* Phase H3: SuperSpeed ESIT payload (0 = derive as MPS*(burst+1)
     * for SS periodic endpoints, ignored for USB2). */
    uint16_t esit_payload;
} rix_xhci_endpoint_config_t;

#define XHCI_DEVICE_DETACHED 0u
#define XHCI_DEVICE_ADDRESSED 2u
#define XHCI_DEVICE_ERROR 0xffu

int xhci_init(void);
size_t xhci_controller_count(void);
const rix_xhci_controller_t *xhci_controller(size_t index);
int xhci_port_status(size_t controller, uint8_t port, rix_xhci_port_status_t *out);
int xhci_reset_port(size_t controller, uint8_t port);
void xhci_dump_ports(void);
/* Returns 1 when a port-status-change event was consumed, 0 when none is ready. */
int xhci_poll_port_status_change(size_t controller, uint8_t *port, uint8_t *connected);
/* Host/state-machine diagnostic: returns -2 once after a full pending-event
 * queue, requesting the caller's bounded full-port rescan path. */
int xhci_pending_port_pop(size_t controller, uint8_t *port, uint8_t *connected);
/* Services one port event: attach/reset/address on connect, disable on disconnect. */
int xhci_service_hotplug(size_t controller, rix_xhci_device_t *device, uint8_t *connected);
int xhci_enable_slot(size_t controller, uint8_t *out_slot);
int xhci_disable_slot(size_t controller, uint8_t slot);
/* Transaction-translator inputs for hub-attached devices (xHCI 6.2.1.1
 * TT fields + 20-bit route string). think_code is the 2-bit TT think
 * time ((ns / 666) - 1). Pass NULL for directly attached devices; pass
 * the struct for EVERY hub child (the route string is required even
 * behind full-speed hubs without any TT — only the slot_ctx[2] split
 * fields additionally gate on a high-speed parent). */
typedef struct {
    uint32_t route;
    uint8_t hub_slot;
    uint8_t hub_port;
    uint8_t think_code;
} rix_xhci_tt_info_t;
int xhci_address_device(size_t controller, uint8_t slot, uint8_t port,
                        uint8_t speed, const rix_xhci_tt_info_t *tt);
int xhci_device_attach(size_t controller, uint8_t port, rix_xhci_device_t *out);
int xhci_device_detach(size_t controller, uint8_t slot);
/* 1 when the slot is allocated (addressed or not), 0 otherwise. Lets
 * consumers drop state bound to a dead slot instead of polling it. */
int xhci_slot_active(size_t controller, uint8_t slot_id);
void xhci_park_port(size_t controller, uint8_t port);
void xhci_usb_state_transition(size_t controller, uint8_t slot, uint8_t new_state,
                               const char *reason);
#define XHCI_USB_DETACHED 0u
#define XHCI_USB_DEFAULT 1u
#define XHCI_USB_ADDRESSED 2u
#define XHCI_USB_CONFIGURED 3u
int xhci_control_transfer(size_t controller, uint8_t slot,
                          const rix_usb_setup_packet_t *setup,
                          void *data, uint16_t *actual_length);
int xhci_get_descriptor(size_t controller, uint8_t slot, uint8_t descriptor_type,
                        uint8_t descriptor_index, uint16_t language_id,
                        void *buffer, uint16_t length, uint16_t *actual_length);
int xhci_set_configuration(size_t controller, uint8_t slot,
                           uint8_t configuration_value);
int xhci_get_hid_report_descriptor(size_t controller, uint8_t slot,
                                   uint8_t interface_number, void *buffer,
                                   uint16_t length, uint16_t *actual_length);
int xhci_hid_set_protocol(size_t controller, uint8_t slot, uint8_t interface_number,
                          uint8_t protocol);
int xhci_hid_set_idle(size_t controller, uint8_t slot, uint8_t interface_number,
                      uint8_t report_id, uint8_t duration_4ms);
int xhci_hid_get_protocol(size_t controller, uint8_t slot, uint8_t interface_number,
                          uint8_t *protocol);
int xhci_enumerate_device(size_t controller, uint8_t slot,
                          rix_usb_device_descriptor_t *device,
                          uint8_t *configuration, uint16_t configuration_capacity,
                          rix_usb_configuration_info_t *configuration_info,
                          rix_usb_interface_info_t *interfaces, size_t interface_capacity,
                          rix_usb_endpoint_info_t *endpoints, size_t endpoint_capacity,
                          size_t *interface_count, size_t *endpoint_count);
int xhci_configure_endpoint(size_t controller, uint8_t slot,
                            const rix_xhci_endpoint_config_t *config);
int xhci_reset_endpoint(size_t controller, uint8_t slot_id,
                        uint8_t endpoint_address);
int xhci_interrupt_transfer(size_t controller, uint8_t slot, uint8_t endpoint_address,
                            void *buffer, uint16_t length, uint16_t *actual_length);
int xhci_bulk_transfer(size_t controller, uint8_t slot, uint8_t endpoint_address,
                       void *buffer, uint16_t length, uint16_t *actual_length);
