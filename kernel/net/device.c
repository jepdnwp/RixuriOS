#include "device.h"
#include "e1000.h"
#include "rtl8125.h"

static rix_net_device_info_t device;
static int use_rtl8125;

static void snapshot_e1000(const rix_e1000_t *controller) {
    device.present = 1;
    device.link_up = (uint8_t)rix_e1000_link_up(controller);
    for (size_t i = 0; i < 6; ++i) device.mac[i] = controller->mac[i];
}

static void snapshot_rtl8125(const rix_rtl8125_t *controller) {
    device.present = 1;
    device.link_up = controller->link_up ? 1u : 0u;
    for (size_t i = 0; i < 6; ++i) device.mac[i] = controller->mac[i];
}

int rix_net_device_init(void) {
    rix_e1000_t *e1000 = rix_e1000_default();
    device = (rix_net_device_info_t){0};
    use_rtl8125 = 0;
    if (e1000) {
        snapshot_e1000(e1000);
    } else {
        rix_rtl8125_t *rtl = rix_rtl8125_default();
        if (!rtl) return -1;
        use_rtl8125 = 1;
        snapshot_rtl8125(rtl);
    }
    device.address = RIX_NET_DEVICE_IP;
    device.netmask = RIX_NET_DEVICE_NETMASK;
    device.gateway = RIX_NET_DEVICE_GATEWAY;
    device.dns = RIX_NET_DEVICE_DNS;
    return 0;
}

const char *rix_net_device_backend(void) {
    if (!device.present) return "none";
    return use_rtl8125 ? "rtl8125" : "e1000";
}

const rix_net_device_info_t *rix_net_device_info(void) {
    return device.present ? &device : 0;
}

int rix_net_device_configure(uint32_t address, uint32_t netmask,
                             uint32_t gateway, uint32_t dns) {
    if (!device.present || !address) return -1;
    device.address = address;
    if (netmask) device.netmask = netmask;
    if (gateway) device.gateway = gateway;
    if (dns) device.dns = dns;
    return 0;
}

int rix_net_device_transmit(const rix_net_packet_t *packet) {
    if (!device.present || !packet || !rix_net_packet_length(packet)) return -1;
    if (use_rtl8125) {
        rix_rtl8125_t *controller = rix_rtl8125_default();
        if (!controller) return -1;
        return rix_rtl8125_transmit(controller,
                                    rix_net_packet_data(packet), rix_net_packet_length(packet));
    }
    rix_e1000_t *controller = rix_e1000_default();
    if (!controller) return -1;
    return rix_e1000_transmit(controller,
                              rix_net_packet_data(packet), rix_net_packet_length(packet));
}

int rix_net_device_receive(rix_net_packet_t *packet) {
    size_t length = 0;
    int result;
    if (!device.present || !packet) return -1;
    rix_net_packet_init(packet);
    if (use_rtl8125) {
        rix_rtl8125_t *controller = rix_rtl8125_default();
        if (!controller) return -1;
        result = rix_rtl8125_receive(controller,
                                     packet->bytes + packet->start,
                                     RIX_NET_FRAME_CAPACITY - packet->start,
                                     &length);
    } else {
        rix_e1000_t *controller = rix_e1000_default();
        if (!controller) return -1;
        result = rix_e1000_receive(controller,
                                   packet->bytes + packet->start,
                                   RIX_NET_FRAME_CAPACITY - packet->start,
                                   &length);
    }
    if (result <= 0) return result;
    packet->length = length;
    return result;
}
