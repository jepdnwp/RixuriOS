#pragma once
#include <stddef.h>
#include <stdint.h>
#include "usb.h"
#include "xhci.h"

/* USB hub class driver (USB 2.0 ch11, Linux drivers/usb/core/hub.c subset).
 * Covers what a single-user desktop needs: hub descriptor fetch/parse,
 * per-port power, bounded port reset with speed detection, TT/route
 * computation for split transactions, and a bounded device registry for
 * hub children plus hub-behind-hub depth limiting. The hub status-change
 * interrupt endpoint is intentionally never armed: port state is polled
 * with GET_STATUS on the existing cooperative schedule (Linux uses the
 * interrupt only as a wakeup hint; all state comes from control
 * transfers). Hotplug monitoring of hub ports happens in the main
 * hotplug worker every 16th round; see usb_hub_rescan_ports(). */

#define USB_HUB_MAX_PORTS 8u
#define USB_HUB_MAX_HUBS 8u
#define USB_HUB_MAX_CHILDREN 32u
#define USB_HUB_MAX_DEPTH 2u

typedef struct {
    uint8_t port_count;
    uint16_t characteristics;
    uint8_t power_good_2ms;
    uint8_t controller_current_ma;
} rix_usb_hub_descriptor_t;

typedef struct {
    uint8_t connected;
    uint8_t enabled;
    uint8_t speed;
} rix_usb_hub_port_state_t;

/* Parent description for a hub-child attach (route/TT inputs). Parent
 * speed is resolved from the addressed parent slot, not passed here. */
typedef struct {
    uint8_t parent_slot;
    uint32_t parent_route;
    uint8_t parent_level;
    uint8_t think_code;
} usb_hub_parent_t;

typedef struct {
    uint8_t used;
    size_t controller;
    uint8_t hub_slot;
    uint32_t route;
    uint8_t level;
    uint8_t speed;
    uint8_t think_code;
    uint8_t port_count;
} usb_hub_record_t;

int usb_hub_parse_descriptor(const uint8_t *data, size_t length,
                             rix_usb_hub_descriptor_t *out);
uint32_t usb_hub_route_string(uint32_t parent_route, uint8_t parent_level,
                              uint8_t port);
uint8_t usb_hub_think_code(uint16_t characteristics);
int usb_hub_port_state(uint16_t status, uint16_t change,
                       rix_usb_hub_port_state_t *out);
int usb_hub_get_descriptor(size_t controller, uint8_t slot, void *buffer,
                           uint16_t length, uint16_t *actual_length);
int usb_hub_port_status(size_t controller, uint8_t slot, uint8_t port,
                        uint16_t *status, uint16_t *change);
int usb_hub_set_port_feature(size_t controller, uint8_t slot, uint8_t port,
                             uint8_t feature);
int usb_hub_clear_port_feature(size_t controller, uint8_t slot, uint8_t port,
                               uint8_t feature);
int usb_hub_port_reset(size_t controller, uint8_t slot, uint8_t port);
void usb_hub_power_all(size_t controller, uint8_t slot, uint8_t port_count,
                       uint8_t power_good_2ms);
int usb_hub_attach_child(size_t controller, const usb_hub_parent_t *parent,
                         uint8_t hub_port, rix_xhci_device_t *out);
void usb_hub_register(size_t controller, uint8_t hub_slot, uint32_t route,
                      uint8_t level, uint8_t speed, uint8_t think_code,
                      uint8_t port_count);
void usb_hub_register_child(size_t controller, uint8_t hub_slot,
                            uint8_t hub_port, uint8_t child_slot);
const usb_hub_record_t *usb_hub_find(size_t controller, uint8_t hub_slot);
/* Detach every registered child of hub_slot, then unregister the hub
 * itself if registered. Called after the hub slot is disabled. */
void usb_hub_detach_sweep(size_t controller, uint8_t hub_slot);
/* Poll all registered hubs' ports; detach children whose port went
 * dark. Returns detached count. Called every 16th hotplug round. */
int usb_hub_rescan_ports(size_t controller);
