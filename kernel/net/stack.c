#include "stack.h"
#include "ethernet.h"

static const uint8_t broadcast_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static rix_net_stack_t *default_stack;

static void copy_packet(rix_net_packet_t *destination, const rix_net_packet_t *source) {
    *destination = *source;
}

static uint32_t next_hop(const rix_net_device_info_t *info, uint32_t destination) {
    return (destination & info->netmask) == (info->address & info->netmask)
               ? destination : info->gateway;
}

static int send_arp_request(const rix_net_device_info_t *info, uint32_t target) {
    uint8_t empty_mac[6] = {0};
    rix_net_packet_t packet;
    rix_net_packet_init(&packet);
    if (rix_net_arp_push(&packet, RIX_NET_ARP_REQUEST, info->mac, info->address,
                         empty_mac, target) != 0 ||
        rix_net_eth_push(&packet, broadcast_mac, info->mac, RIX_NET_ETHERTYPE_ARP) != 0)
        return -1;
    return rix_net_device_transmit(&packet) < 0 ? -1 : 0;
}

int rix_net_stack_init(rix_net_stack_t *stack) {
    if (!stack || !rix_net_device_info()) return -1;
    rix_net_arp_init(&stack->arp);
    rix_net_packet_init(&stack->pending);
    rix_net_packet_init(&stack->incoming);
    stack->pending_next_hop = 0;
    stack->pending_valid = 0;
    stack->incoming_valid = 0;
    stack->initialized = 1;
    default_stack = stack;
    return 0;
}

rix_net_stack_t *rix_net_stack_default(void) {
    return default_stack;
}

int rix_net_stack_send_ipv4(rix_net_stack_t *stack, rix_net_packet_t *packet,
                            uint32_t destination_ip) {
    const rix_net_device_info_t *info = rix_net_device_info();
    if (!stack || !stack->initialized || !packet || !rix_net_packet_length(packet) ||
        !info || !destination_ip) return -1;
    uint32_t hop = next_hop(info, destination_ip);
    uint8_t mac[6];
    if (rix_net_arp_lookup(&stack->arp, hop, 0, mac) != 0) {
        if (stack->pending_valid) return -2;
        copy_packet(&stack->pending, packet);
        stack->pending_next_hop = hop;
        stack->pending_valid = 1;
        return send_arp_request(info, hop) == 0 ? -2 : -3;
    }
    if (rix_net_eth_push(packet, mac, info->mac, RIX_NET_ETHERTYPE_IPV4) != 0)
        return -1;
    return rix_net_device_transmit(packet) < 0 ? -1 : 0;
}

static int handle_arp(rix_net_stack_t *stack, const rix_net_arp_packet_t *arp,
                      uint64_t now) {
    const rix_net_device_info_t *info = rix_net_device_info();
    if (!stack || !arp || !info || arp->sender_ip == 0) return -1;
    if (rix_net_arp_learn(&stack->arp, arp->sender_ip, arp->sender_mac, now, 60) != 0)
        return -1;
    if (arp->operation == RIX_NET_ARP_REQUEST && arp->target_ip == info->address) {
        rix_net_packet_t reply;
        rix_net_packet_init(&reply);
        if (rix_net_arp_push(&reply, RIX_NET_ARP_REPLY, info->mac, info->address,
                             arp->sender_mac, arp->sender_ip) != 0 ||
            rix_net_eth_push(&reply, arp->sender_mac, info->mac, RIX_NET_ETHERTYPE_ARP) != 0)
            return -1;
        if (rix_net_device_transmit(&reply) < 0) return -1;
    }
    if (stack->pending_valid && arp->sender_ip == stack->pending_next_hop) {
        uint8_t mac[6];
        if (rix_net_arp_lookup(&stack->arp, stack->pending_next_hop, now, mac) == 0) {
            const rix_net_device_info_t *current = rix_net_device_info();
            if (rix_net_eth_push(&stack->pending, mac, current->mac,
                                 RIX_NET_ETHERTYPE_IPV4) != 0 ||
                rix_net_device_transmit(&stack->pending) < 0) return -1;
            stack->pending_valid = 0;
        }
    }
    return 0;
}

int rix_net_stack_poll(rix_net_stack_t *stack, uint64_t now) {
    if (!stack || !stack->initialized) return -1;
    rix_net_arp_expire(&stack->arp, now);
    rix_net_packet_t packet;
    int received = rix_net_device_receive(&packet);
    if (received <= 0) return received;
    rix_net_eth_header_t ethernet;
    if (rix_net_eth_pull(&packet, &ethernet) != 0) return -2;
    if (ethernet.ethertype == RIX_NET_ETHERTYPE_ARP) {
        rix_net_arp_packet_t arp;
        if (rix_net_arp_pull(&packet, &arp) != 0) return -3;
        return handle_arp(stack, &arp, now) == 0 ? 1 : -4;
    }
    if (ethernet.ethertype == RIX_NET_ETHERTYPE_IPV4 && !stack->incoming_valid) {
        copy_packet(&stack->incoming, &packet);
        stack->incoming_valid = 1;
    }
    return 1;
}

int rix_net_stack_take_ipv4(rix_net_stack_t *stack, rix_net_packet_t *packet) {
    if (!stack || !packet || !stack->initialized || !stack->incoming_valid) return 0;
    copy_packet(packet, &stack->incoming);
    stack->incoming_valid = 0;
    return 1;
}
