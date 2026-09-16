#pragma once
#include <stddef.h>
#include <stdint.h>
#include "usb_ch9.h"

#define RIX_USB_DESC_DEVICE 1u
#define RIX_USB_DESC_CONFIGURATION 2u
#define RIX_USB_DESC_INTERFACE 4u
#define RIX_USB_DESC_ENDPOINT 5u
#define RIX_USB_DESC_HID 0x21u
#define RIX_USB_DESC_HID_REPORT 0x22u
/* Phase H3: SuperSpeed Endpoint Companion (6 bytes, follows its
 * endpoint descriptor). */
#define RIX_USB_DESC_ENDPOINT_COMPANION 0x30u
#define RIX_USB_EP_COMPANION_SIZE 6u
#define RIX_USB_EP_TRANSFER_MASK 0x03u
#define RIX_USB_EP_CONTROL 0u
#define RIX_USB_EP_ISOCHRONOUS 1u
#define RIX_USB_EP_BULK 2u
#define RIX_USB_EP_INTERRUPT 3u

/* Native names above, Linux chapter-9 values below: any drift between
 * the two tables breaks descriptor parsing silently, so bind them here
 * at compile time (checked in both kernel and host builds). */
_Static_assert(RIX_USB_DESC_DEVICE == USB_DT_DEVICE, "usb ch9 device");
_Static_assert(RIX_USB_DESC_CONFIGURATION == USB_DT_CONFIG, "usb ch9 config");
_Static_assert(RIX_USB_DESC_INTERFACE == USB_DT_INTERFACE, "usb ch9 iface");
_Static_assert(RIX_USB_DESC_ENDPOINT == USB_DT_ENDPOINT, "usb ch9 ep");
_Static_assert(RIX_USB_DESC_HID == HID_DT_HID, "usb ch9 hid");
_Static_assert(RIX_USB_DESC_HID_REPORT == HID_DT_REPORT, "usb ch9 report");
_Static_assert(RIX_USB_DESC_ENDPOINT_COMPANION == USB_DT_SS_ENDPOINT_COMP,
               "usb ch9 ss companion");
_Static_assert(RIX_USB_EP_COMPANION_SIZE == USB_DT_SS_EP_COMP_SIZE,
               "usb ch9 ss companion size");
_Static_assert(RIX_USB_EP_TRANSFER_MASK == USB_ENDPOINT_XFERTYPE_MASK,
               "usb ch9 xfer mask");
_Static_assert(RIX_USB_EP_CONTROL == USB_ENDPOINT_XFER_CONTROL, "usb ch9 ctrl");
_Static_assert(RIX_USB_EP_ISOCHRONOUS == USB_ENDPOINT_XFER_ISOC, "usb ch9 isoc");
_Static_assert(RIX_USB_EP_BULK == USB_ENDPOINT_XFER_BULK, "usb ch9 bulk");
_Static_assert(RIX_USB_EP_INTERRUPT == USB_ENDPOINT_XFER_INT, "usb ch9 intr");

#define RIX_USB_MAX_INTERFACES 32u
#define RIX_USB_MAX_ENDPOINTS 64u

typedef struct {
    uint16_t usb_version;
    uint8_t device_class;
    uint8_t device_subclass;
    uint8_t device_protocol;
    uint8_t max_packet_size0;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t device_version;
    uint8_t configuration_count;
} rix_usb_device_descriptor_t;

typedef struct {
    uint16_t total_length;
    uint8_t configuration_value;
    uint8_t attributes;
    uint8_t max_power_2ma;
    uint8_t interface_count;
    uint8_t endpoint_count;
} rix_usb_configuration_info_t;

typedef struct {
    uint8_t number;
    uint8_t alternate_setting;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t protocol;
    uint8_t endpoint_count;
    uint8_t hid_descriptor_present;
    uint8_t hid_country_code;
    uint16_t hid_version;
    uint16_t hid_report_descriptor_length;
} rix_usb_interface_info_t;

typedef struct {
    uint8_t address;
    uint8_t attributes;
    uint16_t max_packet_size;
    uint8_t interval;
    /* Phase H3: from the companion descriptor when present (SS only);
     * zero for USB2 endpoints (unchanged behavior there). */
    uint8_t max_burst;
    uint8_t mult;
    uint16_t esit_payload;
} rix_usb_endpoint_info_t;

int usb_parse_device_descriptor(const uint8_t *data, size_t length,
                                rix_usb_device_descriptor_t *out);
int usb_parse_configuration_descriptor(const uint8_t *data, size_t length,
                                       rix_usb_configuration_info_t *out,
                                       rix_usb_interface_info_t *interfaces,
                                       size_t interface_capacity,
                                       rix_usb_endpoint_info_t *endpoints,
                                       size_t endpoint_capacity,
                                       size_t *interface_count,
                                       size_t *endpoint_count);
