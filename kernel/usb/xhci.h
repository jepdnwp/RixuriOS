#pragma once
#include <stddef.h>
#include <stdint.h>
#include "usb.h"

typedef struct {
    uint8_t bus, device, function;
    uint64_t bar0;
    uint8_t cap_length, max_slots, max_intrs, max_ports;
    uint32_t hci_version, hcc_params1, usbcmd, usbsts;
    uint64_t dcbaa_phys, cmd_ring_phys, event_ring_phys, erst_phys;
    uint8_t running;
} rix_xhci_controller_t;

typedef struct { uint8_t connected, enabled, speed, reset_complete; } rix_xhci_port_status_t;
typedef struct { uint8_t slot_id, port, speed, state; } rix_xhci_device_t;
typedef struct { uint8_t request_type, request; uint16_t value, index, length; } rix_usb_setup_packet_t;
typedef struct {
    uint8_t endpoint_address;
    uint8_t attributes;
    uint16_t max_packet_size;
    uint8_t interval;
    uint8_t max_burst;
} rix_xhci_endpoint_config_t;

#define RIX_XHCI_DEVICE_ADDRESSED 2u

int xhci_init(void);
size_t xhci_controller_count(void);
const rix_xhci_controller_t *xhci_controller(size_t index);
int xhci_port_status(size_t controller, uint8_t port, rix_xhci_port_status_t *out);
int xhci_reset_port(size_t controller, uint8_t port);
/* Returns 1 when a port-status-change event was consumed, 0 when none is ready. */
int xhci_poll_port_status_change(size_t controller, uint8_t *port, uint8_t *connected);
/* Services one port event: attach/reset/address on connect, disable on disconnect. */
int xhci_service_hotplug(size_t controller, rix_xhci_device_t *device, uint8_t *connected);
int xhci_enable_slot(size_t controller, uint8_t *out_slot);
int xhci_disable_slot(size_t controller, uint8_t slot);
int xhci_address_device(size_t controller, uint8_t slot, uint8_t port, uint8_t speed);
int xhci_device_attach(size_t controller, uint8_t port, rix_xhci_device_t *out);
int xhci_device_detach(size_t controller, uint8_t slot);
int xhci_control_transfer(size_t controller, uint8_t slot,
                          const rix_usb_setup_packet_t *setup,
                          void *data, uint16_t *actual_length);
int xhci_get_descriptor(size_t controller, uint8_t slot, uint8_t descriptor_type,
                        uint8_t descriptor_index, uint16_t language_id,
                        void *buffer, uint16_t length, uint16_t *actual_length);
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
int xhci_interrupt_transfer(size_t controller, uint8_t slot, uint8_t endpoint_address,
                            void *buffer, uint16_t length, uint16_t *actual_length);
int xhci_bulk_transfer(size_t controller, uint8_t slot, uint8_t endpoint_address,
                       void *buffer, uint16_t length, uint16_t *actual_length);
