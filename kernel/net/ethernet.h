#pragma once
#include "net.h"
#include <stddef.h>
#include <stdint.h>

#define RIX_NET_ETH_ADDR_LEN 6u
#define RIX_NET_ETHERTYPE_IPV4 0x0800u
#define RIX_NET_ETHERTYPE_ARP 0x0806u
#define RIX_NET_ETHERTYPE_IPV6 0x86ddu

typedef struct {
    uint8_t destination[RIX_NET_ETH_ADDR_LEN];
    uint8_t source[RIX_NET_ETH_ADDR_LEN];
    uint16_t ethertype;
} rix_net_eth_header_t;

int rix_net_eth_push(rix_net_packet_t *packet, const uint8_t destination[6],
                     const uint8_t source[6], uint16_t ethertype);
int rix_net_eth_pull(rix_net_packet_t *packet, rix_net_eth_header_t *header);
int rix_net_ethertype_supported(uint16_t ethertype);
