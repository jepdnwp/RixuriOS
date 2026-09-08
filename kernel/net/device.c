#include "device.h"
#include "e1000.h"

static rix_net_device_info_t device;

int rix_net_device_init(void) {
    rix_e1000_t *controller = rix_e1000_default();
    device = (rix_net_device_info_t){0};
    if (!controller) return -1;
    device.present = 1;
    device.link_up = (uint8_t)rix_e1000_link_up(controller);
    for (size_t i = 0; i < 6; ++i) device.mac[i] = controller->mac[i];
    device.address = RIX_NET_DEVICE_IP;
    device.netmask = RIX_NET_DEVICE_NETMASK;
    device.gateway = RIX_NET_DEVICE_GATEWAY;
    device.dns = RIX_NET_DEVICE_DNS;
    return 0;
}

const rix_net_device_info_t *rix_net_device_info(void) {
    return device.present ? &device : 0;
}

int rix_net_device_transmit(const rix_net_packet_t *packet) {
    rix_e1000_t *controller = rix_e1000_default();
    if (!controller || !packet || !rix_net_packet_length(packet)) return -1;
    return rix_e1000_transmit(controller,
                              rix_net_packet_data(packet), rix_net_packet_length(packet));
}

int rix_net_device_receive(rix_net_packet_t *packet) {
    rix_e1000_t *controller = rix_e1000_default();
    if (!controller || !packet) return -1;
    rix_net_packet_init(packet);
    size_t length = 0;
    int result = rix_e1000_receive(controller,
                                   packet->bytes + packet->start,
                                   RIX_NET_FRAME_CAPACITY - packet->start,
                                   &length);
    if (result <= 0) return result;
    packet->length = length;
    return result;
}
