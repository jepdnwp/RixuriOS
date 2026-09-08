#include "stack.h"
#include "ethernet.h"
#include "../serial.h"
#include "../time/time.h"

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
    for (size_t i = 0; i < RIX_NET_RX_QUEUE; ++i)
        rix_net_packet_init(&stack->incoming[i]);
    stack->incoming_head = 0;
    stack->incoming_count = 0;
    stack->pending_next_hop = 0;
    stack->pending_valid = 0;
    stack->pending_since_ns = 0;
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
        stack->pending_since_ns = time_monotonic_ns();
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
    {static unsigned hdb = 0;
    if (!hdb) {hdb = 1;
     serial_write("ARP op="); serial_write_dec(arp->operation);
     serial_write(" sender="); serial_write_hex(arp->sender_ip);
     serial_write(" pend="); serial_write_dec(stack->pending_valid);
     serial_write("\r\n");}}
    int learn_rc = rix_net_arp_learn(&stack->arp, arp->sender_ip, arp->sender_mac, now, 60);
    {static unsigned ldb = 0;
    if (!ldb) {ldb = 1;
     serial_write("ARP learn rc="); if (learn_rc < 0) serial_write("-");
     serial_write_dec((uint64_t)(learn_rc < 0 ? -learn_rc : learn_rc)); serial_write("\r\n");}}
    if (learn_rc != 0)
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
        int lookup_rc = rix_net_arp_lookup(&stack->arp, stack->pending_next_hop, now, mac);
        {static unsigned fdb = 0;
        if (!fdb) {fdb = 1;
         serial_write("ARP flush lookup rc="); if (lookup_rc < 0) serial_write("-");
         serial_write_dec((uint64_t)(lookup_rc < 0 ? -lookup_rc : lookup_rc)); serial_write("\r\n");}}
        if (lookup_rc == 0) {
            const rix_net_device_info_t *current = rix_net_device_info();
            int push_rc = rix_net_eth_push(&stack->pending, mac, current->mac,
                                 RIX_NET_ETHERTYPE_IPV4);
            int tx_rc = push_rc == 0 ? rix_net_device_transmit(&stack->pending) : -9;
            {static unsigned tdb = 0;
            if (!tdb) {tdb = 1;
             serial_write("ARP flush push="); if (push_rc < 0) serial_write("-");
             serial_write_dec((uint64_t)(push_rc < 0 ? -push_rc : push_rc));
             serial_write(" tx="); if (tx_rc < 0) serial_write("-");
             serial_write_dec((uint64_t)(tx_rc < 0 ? -tx_rc : tx_rc)); serial_write("\r\n");}}
            if (push_rc != 0 || tx_rc < 0) return -1;
            stack->pending_valid = 0;
        }
    }
    return 0;
}

int rix_net_stack_timer(rix_net_stack_t *stack, uint64_t now) {
    if (!stack || !stack->initialized) return -1;
    rix_net_arp_expire(&stack->arp, now);
    /* ARP pending timeout: a lost request/reply must not wedge the hop
       forever. Retransmit the request if it is older than one second. */
    if (stack->pending_valid) {
        uint64_t age_now = time_monotonic_ns();
        uint64_t since = stack->pending_since_ns;
        if (age_now >= since && age_now - since >= 1000000000ULL) {
            stack->pending_since_ns = age_now;
            const rix_net_device_info_t *retry_info = rix_net_device_info();
            if (retry_info)
                (void)send_arp_request(retry_info, stack->pending_next_hop);
        }
    }
    return 0;
}

static unsigned stack_stores;
static unsigned stack_takes;

int rix_net_stack_poll(rix_net_stack_t *stack, uint64_t now) {
    if (rix_net_stack_timer(stack, now) != 0) return -1;
    rix_net_packet_t packet;
    int received = rix_net_device_receive(&packet);
    {static unsigned spdb = 0;
    if (!spdb && received != 0) {spdb = 1;
     serial_write("STACK first rx result="); if (received < 0) serial_write("-");
     serial_write_dec((uint64_t)(received < 0 ? -received : received)); serial_write("\r\n");}}
    if (received <= 0) return received;
    rix_net_eth_header_t ethernet;
    if (rix_net_eth_pull(&packet, &ethernet) != 0) return -2;
    if (ethernet.ethertype == RIX_NET_ETHERTYPE_ARP) {
        rix_net_arp_packet_t arp;
        if (rix_net_arp_pull(&packet, &arp) != 0) return -3;
        return handle_arp(stack, &arp, now) == 0 ? 1 : -4;
    }
    if (ethernet.ethertype == RIX_NET_ETHERTYPE_IPV4) {
        if (stack->incoming_count >= RIX_NET_RX_QUEUE) {
            /* Drop-oldest: under retransmit floods a stale head would
               starve fresh data forever; the peer retransmits anything
               still unacknowledged. The diagnostic is capped so async
               serial output cannot slice a shell prompt apart. */
            {static unsigned dropseq = 0;
            if (dropseq < 8) {
            serial_write("STACK IPv4 drop-oldest takes=");
            serial_write_dec(stack_takes);
            serial_write(" stores=");
            serial_write_dec(stack_stores);
            serial_write(" drop#");
            serial_write_dec(dropseq++);
            serial_write(" tns=");
            serial_write_dec(time_monotonic_ns());
            serial_write("\r\n");
            }}
            stack->incoming_head = (stack->incoming_head + 1u) % RIX_NET_RX_QUEUE;
            --stack->incoming_count;
        }
        {
            size_t tail = (stack->incoming_head + stack->incoming_count) % RIX_NET_RX_QUEUE;
            copy_packet(&stack->incoming[tail], &packet);
            ++stack->incoming_count;
            ++stack_stores;
        }
    }
    return 1;
}

int rix_net_stack_take_ipv4(rix_net_stack_t *stack, rix_net_packet_t *packet) {
    if (!stack || !packet || !stack->initialized || !stack->incoming_count) return 0;
    copy_packet(packet, &stack->incoming[stack->incoming_head]);
    stack->incoming_head = (stack->incoming_head + 1u) % RIX_NET_RX_QUEUE;
    --stack->incoming_count;
    ++stack_takes;
    return 1;
}
