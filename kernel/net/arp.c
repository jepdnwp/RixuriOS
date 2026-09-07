#include "arp.h"

static void copy_mac(uint8_t destination[6], const uint8_t source[6]) {
    for (size_t i = 0; i < 6; ++i) destination[i] = source[i];
}

void rix_net_arp_init(rix_net_arp_cache_t *cache) {
    if (!cache) return;
    for (size_t i = 0; i < RIX_NET_ARP_CACHE_MAX; ++i) cache->entries[i].valid = 0;
}

int rix_net_arp_learn(rix_net_arp_cache_t *cache, uint32_t ip,
                      const uint8_t mac[6], uint64_t now, uint64_t lifetime) {
    if (!cache || !ip || !mac || !lifetime) return -1;
    size_t slot = RIX_NET_ARP_CACHE_MAX;
    size_t oldest = 0;
    uint64_t oldest_expiry = UINT64_MAX;
    for (size_t i = 0; i < RIX_NET_ARP_CACHE_MAX; ++i) {
        if (cache->entries[i].valid && cache->entries[i].ip == ip) { slot = i; break; }
        if (!cache->entries[i].valid || cache->entries[i].expires_at <= now) { slot = i; break; }
        if (cache->entries[i].expires_at < oldest_expiry) {
            oldest_expiry = cache->entries[i].expires_at;
            oldest = i;
        }
    }
    if (slot == RIX_NET_ARP_CACHE_MAX) slot = oldest;
    cache->entries[slot].ip = ip;
    copy_mac(cache->entries[slot].mac, mac);
    cache->entries[slot].expires_at = now > UINT64_MAX - lifetime ? UINT64_MAX : now + lifetime;
    cache->entries[slot].valid = 1;
    return 0;
}

int rix_net_arp_lookup(const rix_net_arp_cache_t *cache, uint32_t ip,
                       uint64_t now, uint8_t mac[6]) {
    if (!cache || !ip || !mac) return -1;
    for (size_t i = 0; i < RIX_NET_ARP_CACHE_MAX; ++i) {
        const rix_net_arp_entry_t *entry = &cache->entries[i];
        if (entry->valid && entry->ip == ip && entry->expires_at > now) {
            copy_mac(mac, entry->mac);
            return 0;
        }
    }
    return -1;
}

int rix_net_arp_expire(rix_net_arp_cache_t *cache, uint64_t now) {
    if (!cache) return -1;
    int expired = 0;
    for (size_t i = 0; i < RIX_NET_ARP_CACHE_MAX; ++i) {
        if (cache->entries[i].valid && cache->entries[i].expires_at <= now) {
            cache->entries[i].valid = 0;
            ++expired;
        }
    }
    return expired;
}
