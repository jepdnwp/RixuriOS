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

static void put_be16(uint8_t *destination, uint16_t value) {
    destination[0] = (uint8_t)(value >> 8);
    destination[1] = (uint8_t)value;
}

static void put_be32(uint8_t *destination, uint32_t value) {
    destination[0] = (uint8_t)(value >> 24);
    destination[1] = (uint8_t)(value >> 16);
    destination[2] = (uint8_t)(value >> 8);
    destination[3] = (uint8_t)value;
}

static uint16_t get_be16(const uint8_t *source) {
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static uint32_t get_be32(const uint8_t *source) {
    return ((uint32_t)source[0] << 24) | ((uint32_t)source[1] << 16) |
           ((uint32_t)source[2] << 8) | source[3];
}

int rix_net_arp_push(rix_net_packet_t *packet, uint16_t operation,
                     const uint8_t sender_mac[6], uint32_t sender_ip,
                     const uint8_t target_mac[6], uint32_t target_ip) {
    if (!packet || !sender_mac || !target_mac || !sender_ip || !target_ip ||
        (operation != RIX_NET_ARP_REQUEST && operation != RIX_NET_ARP_REPLY) ||
        packet->start > RIX_NET_FRAME_CAPACITY ||
        packet->start > RIX_NET_FRAME_CAPACITY - RIX_NET_ARP_HEADER_LENGTH ||
        packet->length > RIX_NET_FRAME_CAPACITY - packet->start - RIX_NET_ARP_HEADER_LENGTH)
        return -1;
    uint8_t *bytes = 0;
    if (rix_net_packet_put(packet, RIX_NET_ARP_HEADER_LENGTH, (void **)&bytes) != 0) return -1;
    put_be16(bytes + 0, RIX_NET_ARP_ETHERNET);
    put_be16(bytes + 2, RIX_NET_ARP_IPV4);
    bytes[4] = 6;
    bytes[5] = 4;
    put_be16(bytes + 6, operation);
    for (size_t i = 0; i < 6; ++i) bytes[8 + i] = sender_mac[i];
    put_be32(bytes + 14, sender_ip);
    for (size_t i = 0; i < 6; ++i) bytes[18 + i] = target_mac[i];
    put_be32(bytes + 24, target_ip);
    return 0;
}

int rix_net_arp_pull(rix_net_packet_t *packet, rix_net_arp_packet_t *arp) {
    if (!packet || !arp || rix_net_packet_length(packet) < RIX_NET_ARP_HEADER_LENGTH)
        return -1;
    const uint8_t *bytes = rix_net_packet_data(packet);
    if (get_be16(bytes + 0) != RIX_NET_ARP_ETHERNET ||
        get_be16(bytes + 2) != RIX_NET_ARP_IPV4 || bytes[4] != 6 || bytes[5] != 4 ||
        (get_be16(bytes + 6) != RIX_NET_ARP_REQUEST &&
         get_be16(bytes + 6) != RIX_NET_ARP_REPLY)) return -2;
    arp->hardware_type = get_be16(bytes + 0);
    arp->protocol_type = get_be16(bytes + 2);
    arp->hardware_length = bytes[4];
    arp->protocol_length = bytes[5];
    arp->operation = get_be16(bytes + 6);
    for (size_t i = 0; i < 6; ++i) {
        arp->sender_mac[i] = bytes[8 + i];
        arp->target_mac[i] = bytes[18 + i];
    }
    arp->sender_ip = get_be32(bytes + 14);
    arp->target_ip = get_be32(bytes + 24);
    if (rix_net_packet_pull(packet, RIX_NET_ARP_HEADER_LENGTH, 0) != 0) return -1;
    return 0;
}
