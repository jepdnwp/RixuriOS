#include "../kernel/usb/usb.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

static void test_device(void) {
    static const uint8_t descriptor[] = {
        18, RIX_USB_DESC_DEVICE, 0x00, 0x02, 0, 0, 0, 64,
        0x34, 0x12, 0x78, 0x56, 0x00, 0x01, 1, 2, 3, 1
    };
    rix_usb_device_descriptor_t out;
    assert(usb_parse_device_descriptor(descriptor, sizeof(descriptor), &out) == 0);
    assert(out.usb_version == 0x0200 && out.vendor_id == 0x1234);
    assert(out.product_id == 0x5678 && out.max_packet_size0 == 64);
}

static void test_configuration(void) {
    static const uint8_t descriptor[] = {
        9, RIX_USB_DESC_CONFIGURATION, 41, 0, 1, 1, 0, 0x80, 50,
        9, RIX_USB_DESC_INTERFACE, 0, 0, 1, 3, 1, 1, 0,
        9, RIX_USB_DESC_HID, 0x11, 0x01, 0, 1, RIX_USB_DESC_HID_REPORT, 63, 0,
        7, RIX_USB_DESC_ENDPOINT, 0x81, RIX_USB_EP_INTERRUPT, 8, 0, 10,
        7, RIX_USB_DESC_ENDPOINT, 0x02, RIX_USB_EP_BULK, 64, 0, 0
    };
    rix_usb_configuration_info_t config;
    rix_usb_interface_info_t interfaces[2];
    rix_usb_endpoint_info_t endpoints[2];
    size_t interface_count = 0, endpoint_count = 0;
    assert(usb_parse_configuration_descriptor(
        descriptor, sizeof(descriptor), &config, interfaces, 2, endpoints, 2,
        &interface_count, &endpoint_count) == 0);
    assert(config.total_length == sizeof(descriptor));
    assert(interface_count == 1 && endpoint_count == 2);
    assert(interfaces[0].class_code == 3 && interfaces[0].endpoint_count == 2);
    assert(interfaces[0].hid_descriptor_present &&
           interfaces[0].hid_version == 0x0111 &&
           interfaces[0].hid_report_descriptor_length == 63);
    assert(endpoints[0].address == 0x81 && endpoints[0].interval == 10);
    assert(endpoints[1].max_packet_size == 64);
}

static void test_rejections(void) {
    static const uint8_t zero_length[] = {0, RIX_USB_DESC_CONFIGURATION};
    static const uint8_t endpoint_first[] = {
        9, RIX_USB_DESC_CONFIGURATION, 16, 0, 0, 1, 0, 0x80, 1,
        7, RIX_USB_DESC_ENDPOINT, 0x81, RIX_USB_EP_INTERRUPT, 8, 0, 10
    };
    static const uint8_t malformed_hid[] = {
        9, RIX_USB_DESC_CONFIGURATION, 27, 0, 1, 1, 0, 0x80, 1,
        9, RIX_USB_DESC_INTERFACE, 0, 0, 0, 3, 1, 1, 0,
        9, RIX_USB_DESC_HID, 0x11, 0x01, 0, 1, RIX_USB_DESC_ENDPOINT, 63, 0
    };
    static const uint8_t truncated[] = {9, RIX_USB_DESC_CONFIGURATION, 9, 0};
    rix_usb_configuration_info_t config;
    size_t interfaces = 0, endpoints = 0;
    assert(usb_parse_configuration_descriptor(zero_length, sizeof(zero_length),
                                               &config, NULL, 0, NULL, 0,
                                               &interfaces, &endpoints) != 0);
    assert(usb_parse_configuration_descriptor(endpoint_first, sizeof(endpoint_first),
                                               &config, NULL, 0, NULL, 0,
                                               &interfaces, &endpoints) != 0);
    assert(usb_parse_configuration_descriptor(malformed_hid, sizeof(malformed_hid),
                                               &config, NULL, 0, NULL, 0,
                                               &interfaces, &endpoints) != 0);
    assert(usb_parse_configuration_descriptor(truncated, sizeof(truncated),
                                               &config, NULL, 0, NULL, 0,
                                               &interfaces, &endpoints) != 0);
}

static void test_companion(void) {
    /* Phase H3: SuperSpeed companion attaches to its endpoint only. */
    static const uint8_t descriptor[] = {
        9, RIX_USB_DESC_CONFIGURATION, 38, 0, 1, 1, 0, 0x80, 50,
        9, RIX_USB_DESC_INTERFACE, 0, 0, 2, 0xff, 0, 0, 0,
        7, RIX_USB_DESC_ENDPOINT, 0x81, RIX_USB_EP_INTERRUPT, 0x00, 0x04, 4,
        6, RIX_USB_DESC_ENDPOINT_COMPANION, 3, 1, 0x00, 0x08,
        7, RIX_USB_DESC_ENDPOINT, 0x02, RIX_USB_EP_BULK, 0x00, 0x02, 0
    };
    rix_usb_configuration_info_t config;
    rix_usb_interface_info_t interfaces[1];
    rix_usb_endpoint_info_t endpoints[2];
    size_t interface_count = 0, endpoint_count = 0;
    assert(usb_parse_configuration_descriptor(
        descriptor, sizeof(descriptor), &config, interfaces, 1, endpoints, 2,
        &interface_count, &endpoint_count) == 0);
    assert(interface_count == 1 && endpoint_count == 2);
    assert(endpoints[0].max_packet_size == 1024);
    assert(endpoints[0].max_burst == 3 && endpoints[0].mult == 1);
    assert(endpoints[0].esit_payload == 0x0800);
    assert(endpoints[1].max_burst == 0 && endpoints[1].mult == 0);
    assert(endpoints[1].esit_payload == 0);
    /* Malformed companion (wrong size) is ignored, endpoint intact. */
    static const uint8_t ragged[] = {
        9, RIX_USB_DESC_CONFIGURATION, 30, 0, 1, 1, 0, 0x80, 50,
        9, RIX_USB_DESC_INTERFACE, 0, 0, 1, 0xff, 0, 0, 0,
        7, RIX_USB_DESC_ENDPOINT, 0x81, RIX_USB_EP_INTERRUPT, 0x00, 0x04, 4,
        5, RIX_USB_DESC_ENDPOINT_COMPANION, 3, 1, 0
    };
    interface_count = 0;
    endpoint_count = 0;
    assert(usb_parse_configuration_descriptor(
        ragged, sizeof(ragged), &config, interfaces, 1, endpoints, 2,
        &interface_count, &endpoint_count) == 0);
    assert(endpoint_count == 1);
    assert(endpoints[0].max_burst == 0 && endpoints[0].esit_payload == 0);
}

int main(void) {
    test_device();
    test_configuration();
    test_companion();
    test_rejections();
    return 0;
}
