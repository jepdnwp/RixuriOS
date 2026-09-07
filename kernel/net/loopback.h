#pragma once
#include "net.h"
#include <stddef.h>
#include <stdint.h>

#define RIX_NET_LOOPBACK_IP 0x7f000001u
#define RIX_NET_LOOPBACK_QUEUE 8u

typedef struct {
    rix_net_packet_t queue[RIX_NET_LOOPBACK_QUEUE];
    size_t head;
    size_t count;
} rix_net_loopback_t;

void rix_net_loopback_init(rix_net_loopback_t *loopback);
int rix_net_loopback_transmit(rix_net_loopback_t *loopback, const rix_net_packet_t *packet);
int rix_net_loopback_receive(rix_net_loopback_t *loopback, rix_net_packet_t *packet);
size_t rix_net_loopback_pending(const rix_net_loopback_t *loopback);
