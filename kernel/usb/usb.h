#pragma once
#include <stddef.h>
#include <stdint.h>

#define RIX_USB_DESC_DEVICE 1u
#define RIX_USB_DESC_CONFIGURATION 2u
#define RIX_USB_DESC_INTERFACE 4u
#define RIX_USB_DESC_ENDPOINT 5u
#define RIX_USB_EP_TRANSFER_MASK 0x03u
#define RIX_USB_EP_CONTROL 0u
#define RIX_USB_EP_ISOCHRONOUS 1u
#define RIX_USB_EP_BULK 2u
#define RIX_USB_EP_INTERRUPT 3u

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
} rix_usb_interface_info_t;

typedef struct {
    uint8_t address;
    uint8_t attributes;
    uint16_t max_packet_size;
    uint8_t interval;
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
