#pragma once
#include "net.h"
#include <stddef.h>
#include <stdint.h>

#define RIX_NET_ARP_CACHE_MAX 16u
#define RIX_NET_ARP_ETHERNET 1u
#define RIX_NET_ARP_IPV4 0x0800u
#define RIX_NET_ARP_IPV4_LEN 4u
#define RIX_NET_ARP_REQUEST 1u
#define RIX_NET_ARP_REPLY 2u
#define RIX_NET_ARP_HEADER_LENGTH 28u

typedef struct {
    uint32_t ip;
    uint8_t mac[6];
    uint64_t expires_at;
    uint8_t valid;
} rix_net_arp_entry_t;

typedef struct {
    rix_net_arp_entry_t entries[RIX_NET_ARP_CACHE_MAX];
} rix_net_arp_cache_t;

typedef struct {
    uint16_t hardware_type;
    uint16_t protocol_type;
    uint8_t hardware_length;
    uint8_t protocol_length;
    uint16_t operation;
    uint8_t sender_mac[6];
    uint32_t sender_ip;
    uint8_t target_mac[6];
    uint32_t target_ip;
} rix_net_arp_packet_t;

void rix_net_arp_init(rix_net_arp_cache_t *cache);
int rix_net_arp_learn(rix_net_arp_cache_t *cache, uint32_t ip,
                      const uint8_t mac[6], uint64_t now, uint64_t lifetime);
int rix_net_arp_lookup(const rix_net_arp_cache_t *cache, uint32_t ip,
                       uint64_t now, uint8_t mac[6]);
int rix_net_arp_expire(rix_net_arp_cache_t *cache, uint64_t now);
int rix_net_arp_push(rix_net_packet_t *packet, uint16_t operation,
                     const uint8_t sender_mac[6], uint32_t sender_ip,
                     const uint8_t target_mac[6], uint32_t target_ip);
int rix_net_arp_pull(rix_net_packet_t *packet, rix_net_arp_packet_t *arp);
