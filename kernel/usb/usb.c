#include "usb.h"

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static int descriptor_at(const uint8_t *data, size_t length, size_t offset,
                         const uint8_t **descriptor, size_t *descriptor_length) {
    if (!data || offset > length || length - offset < 2u) return -1;
    size_t size = data[offset];
    if (size < 2u || size > length - offset) return -2;
    *descriptor = data + offset;
    *descriptor_length = size;
    return 0;
}

int usb_parse_device_descriptor(const uint8_t *data, size_t length,
                               rix_usb_device_descriptor_t *out) {
    if (!data || !out || length < 18u || data[0] != 18u ||
        data[1] != RIX_USB_DESC_DEVICE || data[7] == 0u) return -1;
    out->usb_version = le16(data + 2u);
    out->device_class = data[4];
    out->device_subclass = data[5];
    out->device_protocol = data[6];
    out->max_packet_size0 = data[7];
    out->vendor_id = le16(data + 8u);
    out->product_id = le16(data + 10u);
    out->device_version = le16(data + 12u);
    out->configuration_count = data[17];
    return 0;
}

static int scan_configuration(const uint8_t *data, size_t total_length,
                              size_t *interfaces, size_t *endpoints) {
    size_t offset = 0;
    size_t interface_count = 0;
    size_t endpoint_count = 0;
    int have_interface = 0;
    while (offset < total_length) {
        const uint8_t *descriptor = NULL;
        size_t descriptor_length = 0;
        if (descriptor_at(data, total_length, offset, &descriptor,
                          &descriptor_length) != 0) return -1;
        if (descriptor[1] == RIX_USB_DESC_INTERFACE) {
            if (descriptor_length < 9u) return -2;
            interface_count++;
            have_interface = 1;
        } else if (descriptor[1] == RIX_USB_DESC_HID) {
            if (descriptor_length < 9u || !have_interface || descriptor[5] == 0u ||
                descriptor[6] != RIX_USB_DESC_HID_REPORT) return -3;
        } else if (descriptor[1] == RIX_USB_DESC_ENDPOINT) {
            if (descriptor_length < 7u || !have_interface ||
                (descriptor[2] & 0x70u) != 0u) return -3;
            endpoint_count++;
        }
        if (offset > total_length - descriptor_length) return -4;
        offset += descriptor_length;
    }
    *interfaces = interface_count;
    *endpoints = endpoint_count;
    return 0;
}

int usb_parse_configuration_descriptor(const uint8_t *data, size_t length,
                                       rix_usb_configuration_info_t *out,
                                       rix_usb_interface_info_t *interfaces,
                                       size_t interface_capacity,
                                       rix_usb_endpoint_info_t *endpoints,
                                       size_t endpoint_capacity,
                                       size_t *interface_count,
                                       size_t *endpoint_count) {
    if (!data || !out || !interface_count || !endpoint_count || length < 9u ||
        data[0] < 9u || data[1] != RIX_USB_DESC_CONFIGURATION) return -1;
    size_t total_length = le16(data + 2u);
    if (total_length < 9u || total_length > length) return -2;
    size_t needed_interfaces = 0;
    size_t needed_endpoints = 0;
    if (scan_configuration(data, total_length, &needed_interfaces,
                           &needed_endpoints) != 0) return -3;
    if (needed_interfaces > interface_capacity ||
        needed_endpoints > endpoint_capacity ||
        (needed_interfaces && !interfaces) || (needed_endpoints && !endpoints)) return -4;

    out->total_length = (uint16_t)total_length;
    out->interface_count = data[4];
    out->configuration_value = data[5];
    out->attributes = data[7];
    out->max_power_2ma = data[8];
    size_t offset = 0;
    size_t current_interface = (size_t)-1;
    size_t actual_interfaces = 0;
    size_t actual_endpoints = 0;
    while (offset < total_length) {
        const uint8_t *descriptor = NULL;
        size_t descriptor_length = 0;
        if (descriptor_at(data, total_length, offset, &descriptor,
                          &descriptor_length) != 0) return -5;
        if (descriptor[1] == RIX_USB_DESC_INTERFACE) {
            rix_usb_interface_info_t *interface = &interfaces[actual_interfaces];
            interface->number = descriptor[2];
            interface->alternate_setting = descriptor[3];
            interface->endpoint_count = 0;
            interface->class_code = descriptor[5];
            interface->subclass = descriptor[6];
            interface->protocol = descriptor[7];
            interface->hid_descriptor_present = 0;
            interface->hid_country_code = 0;
            interface->hid_version = 0;
            interface->hid_report_descriptor_length = 0;
            current_interface = actual_interfaces++;
        } else if (descriptor[1] == RIX_USB_DESC_HID) {
            rix_usb_interface_info_t *interface = &interfaces[current_interface];
            if (!interface->hid_descriptor_present) {
                interface->hid_descriptor_present = 1;
                interface->hid_version = le16(descriptor + 2u);
                interface->hid_country_code = descriptor[4];
                interface->hid_report_descriptor_length = le16(descriptor + 7u);
            }
        } else if (descriptor[1] == RIX_USB_DESC_ENDPOINT) {
            rix_usb_endpoint_info_t *endpoint = &endpoints[actual_endpoints++];
            endpoint->address = descriptor[2];
            endpoint->attributes = descriptor[3];
            endpoint->max_packet_size = le16(descriptor + 4u);
            endpoint->interval = descriptor[6];
            interfaces[current_interface].endpoint_count++;
        }
        offset += descriptor_length;
    }
    *interface_count = actual_interfaces;
    *endpoint_count = actual_endpoints;
    return 0;
}
