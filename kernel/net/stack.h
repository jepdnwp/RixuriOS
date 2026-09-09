#pragma once
#include "net.h"
#include "arp.h"
#include "device.h"
#include "ipv6.h"
#include <stdint.h>

#define RIX_NET_RX_QUEUE 8u

typedef struct {
    rix_net_arp_cache_t arp;
    rix_net_ipv6_neighbor_cache_t ipv6_neighbors;
    rix_net_packet_t pending;
    uint32_t pending_next_hop;
    uint8_t pending_valid;
    uint64_t pending_since_ns;
    rix_net_packet_t incoming[RIX_NET_RX_QUEUE];
    size_t incoming_head;
    size_t incoming_count;
    rix_net_packet_t incoming_ipv6[RIX_NET_RX_QUEUE];
    size_t incoming_ipv6_head;
    size_t incoming_ipv6_count;
    uint8_t initialized;
} rix_net_stack_t;

int rix_net_stack_init(rix_net_stack_t *stack);
rix_net_stack_t *rix_net_stack_default(void);
int rix_net_stack_send_ipv4(rix_net_stack_t *stack, rix_net_packet_t *packet,
                            uint32_t destination_ip);
int rix_net_stack_send_ipv6(rix_net_stack_t *stack, rix_net_packet_t *packet,
                            const uint8_t destination[16]);
int rix_net_stack_poll(rix_net_stack_t *stack, uint64_t now);
int rix_net_stack_take_ipv4(rix_net_stack_t *stack, rix_net_packet_t *packet);
int rix_net_stack_take_ipv6(rix_net_stack_t *stack, rix_net_packet_t *packet);
/* Timer-only upkeep for contexts without a socket consumer (ARP expiry and
   pending-request retransmit). Touches no DMA state. */
int rix_net_stack_timer(rix_net_stack_t *stack, uint64_t now);
