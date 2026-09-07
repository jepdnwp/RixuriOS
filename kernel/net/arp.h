#pragma once
#include <stddef.h>
#include <stdint.h>

#define RIX_NET_ARP_CACHE_MAX 16u
#define RIX_NET_ARP_ETHERNET 1u
#define RIX_NET_ARP_IPV4_LEN 4u

typedef struct {
    uint32_t ip;
    uint8_t mac[6];
    uint64_t expires_at;
    uint8_t valid;
} rix_net_arp_entry_t;

typedef struct {
    rix_net_arp_entry_t entries[RIX_NET_ARP_CACHE_MAX];
} rix_net_arp_cache_t;

void rix_net_arp_init(rix_net_arp_cache_t *cache);
int rix_net_arp_learn(rix_net_arp_cache_t *cache, uint32_t ip,
                      const uint8_t mac[6], uint64_t now, uint64_t lifetime);
int rix_net_arp_lookup(const rix_net_arp_cache_t *cache, uint32_t ip,
                       uint64_t now, uint8_t mac[6]);
int rix_net_arp_expire(rix_net_arp_cache_t *cache, uint64_t now);
