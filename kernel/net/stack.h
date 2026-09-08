#pragma once
#include "net.h"
#include "arp.h"
#include "device.h"
#include <stdint.h>

typedef struct {
    rix_net_arp_cache_t arp;
    rix_net_packet_t pending;
    uint32_t pending_next_hop;
    uint8_t pending_valid;
    rix_net_packet_t incoming;
    uint8_t incoming_valid;
    uint8_t initialized;
} rix_net_stack_t;

int rix_net_stack_init(rix_net_stack_t *stack);
rix_net_stack_t *rix_net_stack_default(void);
int rix_net_stack_send_ipv4(rix_net_stack_t *stack, rix_net_packet_t *packet,
                            uint32_t destination_ip);
int rix_net_stack_poll(rix_net_stack_t *stack, uint64_t now);
int rix_net_stack_take_ipv4(rix_net_stack_t *stack, rix_net_packet_t *packet);
