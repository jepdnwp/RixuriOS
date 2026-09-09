#include "ipv6.h"

static uint16_t be16(uint16_t value) { return (uint16_t)((value << 8) | (value >> 8)); }
static uint32_t be32(uint32_t value) {
    return ((value & 0xff000000u) >> 24) | ((value & 0x00ff0000u) >> 8) |
           ((value & 0x0000ff00u) << 8) | ((value & 0x000000ffu) << 24);
}
static void copy16(uint8_t destination[16], const uint8_t source[16]) {
    for (size_t i = 0; i < 16; ++i) destination[i] = source[i];
}
static uint32_t sum_bytes(uint32_t sum, const uint8_t *data, size_t length) {
    while (length >= 2) {
        sum += ((uint32_t)data[0] << 8) | data[1];
        data += 2; length -= 2;
    }
    if (length) sum += (uint32_t)data[0] << 8;
    return sum;
}
static uint16_t finish_sum(uint32_t sum) {
    while (sum >> 16) sum = (sum & 0xffffu) + (sum >> 16);
    return (uint16_t)~sum;
}
static uint16_t icmpv6_checksum(const uint8_t source[16], const uint8_t destination[16],
                                const uint8_t *message, size_t length) {
    uint8_t pseudo[8] = {0, 0, 0, 0, 0, 0, 0, RIX_NET_IP_PROTO_ICMPV6};
    uint32_t sum = 0;
    sum = sum_bytes(sum, source, 16);
    sum = sum_bytes(sum, destination, 16);
    uint32_t network_length = be32((uint32_t)length);
    sum = sum_bytes(sum, (const uint8_t *)&network_length, 4);
    sum = sum_bytes(sum, pseudo, sizeof(pseudo));
    return finish_sum(sum_bytes(sum, message, length));
}

int rix_net_ipv6_push(rix_net_packet_t *packet,
                      const uint8_t source[16], const uint8_t destination[16],
                      uint8_t next_header, uint8_t hop_limit,
                      uint8_t traffic_class, uint32_t flow_label) {
    if (!packet || !source || !destination || !hop_limit ||
        flow_label > 0xfffffu || rix_net_packet_length(packet) > 65535u) return -1;
    uint8_t *wire = 0;
    if (rix_net_packet_push(packet, RIX_NET_IPV6_HEADER_SIZE, (void **)&wire) != 0) return -1;
    uint32_t version_flow = (6u << 28) | ((uint32_t)traffic_class << 20) | flow_label;
    version_flow = be32(version_flow);
    for (size_t i = 0; i < 4; ++i) wire[i] = ((uint8_t *)&version_flow)[i];
    uint16_t payload_length = be16((uint16_t)(rix_net_packet_length(packet) - RIX_NET_IPV6_HEADER_SIZE));
    wire[4] = ((uint8_t *)&payload_length)[0]; wire[5] = ((uint8_t *)&payload_length)[1];
    wire[6] = next_header; wire[7] = hop_limit;
    copy16(wire + 8, source); copy16(wire + 24, destination);
    return 0;
}

int rix_net_ipv6_pull(rix_net_packet_t *packet, rix_net_ipv6_header_t *header) {
    if (!packet || !header || !rix_net_packet_data(packet) ||
        rix_net_packet_length(packet) < RIX_NET_IPV6_HEADER_SIZE) return -1;
    const uint8_t *wire = rix_net_packet_data(packet);
    uint32_t version_flow = ((uint32_t)wire[0] << 24) | ((uint32_t)wire[1] << 16) |
                             ((uint32_t)wire[2] << 8) | wire[3];
    if ((version_flow >> 28) != 6u) return -2;
    uint16_t payload_length = (uint16_t)(((uint16_t)wire[4] << 8) | wire[5]);
    if (payload_length > rix_net_packet_length(packet) - RIX_NET_IPV6_HEADER_SIZE) return -3;
    if (rix_net_packet_pull(packet, RIX_NET_IPV6_HEADER_SIZE, 0) != 0) return -1;
    packet->length = payload_length;
    header->traffic_class = (uint8_t)((version_flow >> 20) & 0xffu);
    header->flow_label = version_flow & 0xfffffu;
    header->payload_length = payload_length;
    header->next_header = wire[6]; header->hop_limit = wire[7];
    copy16(header->source, wire + 8); copy16(header->destination, wire + 24);
    return 0;
}

struct icmpv6_wire { uint8_t type; uint8_t code; uint16_t checksum; };
static int icmpv6_put(rix_net_packet_t *packet, uint8_t type, uint8_t code,
                      const uint8_t source[16], const uint8_t destination[16]) {
    struct icmpv6_wire *wire = 0;
    if (rix_net_packet_push(packet, sizeof(*wire), (void **)&wire) != 0) return -1;
    wire->type = type; wire->code = code; wire->checksum = 0;
    wire->checksum = be16(icmpv6_checksum(source, destination,
                                          rix_net_packet_data(packet),
                                          rix_net_packet_length(packet)));
    return 0;
}

int rix_net_icmpv6_echo_push(rix_net_packet_t *packet, uint8_t type,
                             uint16_t identifier, uint16_t sequence,
                             const uint8_t source[16], const uint8_t destination[16],
                             const void *payload, size_t payload_length) {
    if (!packet || !source || !destination ||
        (payload_length && !payload) || payload_length > 65527u ||
        (type != RIX_NET_ICMPV6_ECHO_REQUEST && type != RIX_NET_ICMPV6_ECHO_REPLY)) return -1;
    uint8_t *body = 0;
    if (rix_net_packet_put(packet, payload_length, (void **)&body) != 0) return -1;
    for (size_t i = 0; i < payload_length; ++i) body[i] = ((const uint8_t *)payload)[i];
    uint8_t *fields = 0;
    if (rix_net_packet_push(packet, 4, (void **)&fields) != 0) return -1;
    uint16_t id = be16(identifier), seq = be16(sequence);
    fields[0] = ((uint8_t *)&id)[0]; fields[1] = ((uint8_t *)&id)[1];
    fields[2] = ((uint8_t *)&seq)[0]; fields[3] = ((uint8_t *)&seq)[1];
    return icmpv6_put(packet, type, 0, source, destination);
}

int rix_net_icmpv6_echo_pull(rix_net_packet_t *packet,
                             const uint8_t source[16], const uint8_t destination[16],
                             rix_net_icmpv6_echo_t *echo) {
    if (!packet || !source || !destination || !echo || rix_net_packet_length(packet) < 8) return -1;
    const uint8_t *data = rix_net_packet_data(packet);
    if ((data[0] != RIX_NET_ICMPV6_ECHO_REQUEST && data[0] != RIX_NET_ICMPV6_ECHO_REPLY) ||
        data[1] != 0 || icmpv6_checksum(source, destination, data, rix_net_packet_length(packet)) != 0) return -2;
    echo->type = data[0]; echo->code = data[1];
    echo->identifier = (uint16_t)(((uint16_t)data[4] << 8) | data[5]);
    echo->sequence = (uint16_t)(((uint16_t)data[6] << 8) | data[7]);
    return rix_net_packet_pull(packet, 8, 0);
}

int rix_net_icmpv6_neighbor_solicit_push(rix_net_packet_t *packet,
                                         const uint8_t source[16], const uint8_t destination[16],
                                         const uint8_t target[16]) {
    if (!packet || !source || !destination || !target) return -1;
    uint8_t *body = 0;
    if (rix_net_packet_put(packet, 20, (void **)&body) != 0) return -1;
    for (size_t i = 0; i < 4; ++i) body[i] = 0;
    copy16(body + 4, target);
    return icmpv6_put(packet, RIX_NET_ICMPV6_NEIGHBOR_SOLICIT, 0, source, destination);
}

int rix_net_icmpv6_neighbor_advert_push(rix_net_packet_t *packet,
                                        const uint8_t source[16], const uint8_t destination[16],
                                        uint32_t flags, const uint8_t target[16]) {
    if (!packet || !source || !destination || !target || flags & 0x00ffffffu) return -1;
    uint8_t *body = 0;
    if (rix_net_packet_put(packet, 20, (void **)&body) != 0) return -1;
    uint32_t network_flags = be32(flags);
    for (size_t i = 0; i < 4; ++i) body[i] = ((uint8_t *)&network_flags)[i];
    copy16(body + 4, target);
    return icmpv6_put(packet, RIX_NET_ICMPV6_NEIGHBOR_ADVERT, 0, source, destination);
}

int rix_net_icmpv6_neighbor_pull(rix_net_packet_t *packet,
                                 const uint8_t source[16], const uint8_t destination[16],
                                 uint8_t *type, uint32_t *flags, uint8_t target[16]) {
    if (!packet || !source || !destination || !type || !flags || !target ||
        rix_net_packet_length(packet) < 24) return -1;
    const uint8_t *data = rix_net_packet_data(packet);
    if ((data[0] != RIX_NET_ICMPV6_NEIGHBOR_SOLICIT && data[0] != RIX_NET_ICMPV6_NEIGHBOR_ADVERT) ||
        data[1] != 0 || rix_net_packet_length(packet) != 24 ||
        icmpv6_checksum(source, destination, data, rix_net_packet_length(packet)) != 0) return -2;
    *type = data[0]; *flags = ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16) |
                              ((uint32_t)data[6] << 8) | data[7];
    copy16(target, data + 8);
    return rix_net_packet_pull(packet, 24, 0);
}


int rix_net_icmpv6_router_solicit_push(rix_net_packet_t *packet,
                                       const uint8_t source[16], const uint8_t destination[16]) {
    if (!packet || !source || !destination) return -1;
    uint8_t *body = 0;
    if (rix_net_packet_put(packet, 4, (void **)&body) != 0) return -1;
    for (size_t i = 0; i < 4; ++i) body[i] = 0;
    return icmpv6_put(packet, RIX_NET_ICMPV6_ROUTER_SOLICIT, 0, source, destination);
}

int rix_net_icmpv6_router_advert_push(rix_net_packet_t *packet,
                                      const uint8_t source[16], const uint8_t destination[16],
                                      uint8_t hop_limit, uint8_t flags,
                                      uint16_t router_lifetime, const uint8_t prefix[16],
                                      uint8_t prefix_length) {
    if (!packet || !source || !destination || !prefix || prefix_length > 128u) return -1;
    uint8_t *body = 0;
    if (rix_net_packet_put(packet, 44, (void **)&body) != 0) return -1;
    body[0] = hop_limit; body[1] = flags;
    uint16_t lifetime = be16(router_lifetime);
    body[2] = ((uint8_t *)&lifetime)[0]; body[3] = ((uint8_t *)&lifetime)[1];
    for (size_t i = 4; i < 12; ++i) body[i] = 0;
    body[12] = 3; body[13] = 4; body[14] = prefix_length; body[15] = 0xc0;
    uint32_t valid = be32(0xffffffffu);
    for (size_t i = 0; i < 4; ++i) body[16 + i] = ((uint8_t *)&valid)[i];
    for (size_t i = 20; i < 28; ++i) body[i] = 0;
    for (size_t i = 0; i < 16; ++i) body[28 + i] = prefix[i];
    return icmpv6_put(packet, RIX_NET_ICMPV6_ROUTER_ADVERT, 0, source, destination);
}

int rix_net_icmpv6_router_advert_pull(rix_net_packet_t *packet,
                                      const uint8_t source[16], const uint8_t destination[16],
                                      uint8_t *hop_limit, uint8_t *flags,
                                      uint16_t *router_lifetime, uint8_t prefix[16],
                                      uint8_t *prefix_length) {
    if (!packet || !source || !destination || !hop_limit || !flags ||
        !router_lifetime || !prefix || !prefix_length || rix_net_packet_length(packet) != 48) return -1;
    const uint8_t *data = rix_net_packet_data(packet);
    if (data[0] != RIX_NET_ICMPV6_ROUTER_ADVERT || data[1] != 0 ||
        data[16] != 3 || data[17] != 4 || data[19] != 0xc0 || data[18] > 128u ||
        icmpv6_checksum(source, destination, data, rix_net_packet_length(packet)) != 0) return -2;
    *hop_limit = data[4]; *flags = data[5];
    *router_lifetime = (uint16_t)(((uint16_t)data[6] << 8) | data[7]);
    *prefix_length = data[18];
    copy16(prefix, data + 32);
    return rix_net_packet_pull(packet, 48, 0);
}

int rix_net_ipv6_slaac_address(const uint8_t prefix[16], uint8_t prefix_length,
                               const uint8_t interface_id[8], uint8_t address[16]) {
    if (!prefix || !interface_id || !address || prefix_length > 128u) return -1;
    copy16(address, prefix);
    for (size_t bit = prefix_length; bit < 128u; ++bit) address[bit / 8] &= (uint8_t)~(1u << (7u - (bit % 8u)));
    if (prefix_length != 64u) return -2;
    for (size_t i = 0; i < 8; ++i) address[8 + i] = interface_id[i];
    return 0;
}

int rix_net_ipv6_link_local_from_mac(const uint8_t mac[6], uint8_t address[16]) {
    if (!mac || !address) return -1;
    for (size_t i = 0; i < 16; ++i) address[i] = 0;
    address[0] = 0xfe; address[1] = 0x80;
    address[8] = (uint8_t)(mac[0] ^ 0x02u); address[9] = mac[1]; address[10] = mac[2];
    address[11] = 0xff; address[12] = 0xfe; address[13] = mac[3];
    address[14] = mac[4]; address[15] = mac[5];
    return 0;
}

static int ipv6_address_equal(const uint8_t left[16], const uint8_t right[16]) {
    uint8_t different = 0;
    for (size_t i = 0; i < 16; ++i) different |= (uint8_t)(left[i] ^ right[i]);
    return different == 0;
}
static void copy_mac6(uint8_t destination[6], const uint8_t source[6]) {
    for (size_t i = 0; i < 6; ++i) destination[i] = source[i];
}
void rix_net_ipv6_neighbor_init(rix_net_ipv6_neighbor_cache_t *cache) {
    if (!cache) return;
    for (size_t i = 0; i < RIX_NET_IPV6_NEIGHBOR_CACHE_SIZE; ++i) cache->entries[i].used = 0;
}
int rix_net_ipv6_neighbor_learn(rix_net_ipv6_neighbor_cache_t *cache,
                                const uint8_t address[16], const uint8_t mac[6], uint64_t expires) {
    if (!cache || !address || !mac) return -1;
    size_t slot = RIX_NET_IPV6_NEIGHBOR_CACHE_SIZE;
    for (size_t i = 0; i < RIX_NET_IPV6_NEIGHBOR_CACHE_SIZE; ++i)
        if (cache->entries[i].used && ipv6_address_equal(cache->entries[i].address, address)) { slot = i; break; }
    if (slot == RIX_NET_IPV6_NEIGHBOR_CACHE_SIZE)
        for (size_t i = 0; i < RIX_NET_IPV6_NEIGHBOR_CACHE_SIZE; ++i)
            if (!cache->entries[i].used) { slot = i; break; }
    if (slot == RIX_NET_IPV6_NEIGHBOR_CACHE_SIZE) return -2;
    cache->entries[slot].used = 1; copy16(cache->entries[slot].address, address);
    copy_mac6(cache->entries[slot].mac, mac); cache->entries[slot].expires = expires;
    return 0;
}
int rix_net_ipv6_neighbor_lookup(rix_net_ipv6_neighbor_cache_t *cache,
                                 const uint8_t address[16], uint64_t now, uint8_t mac[6]) {
    if (!cache || !address || !mac) return -1;
    for (size_t i = 0; i < RIX_NET_IPV6_NEIGHBOR_CACHE_SIZE; ++i) {
        rix_net_ipv6_neighbor_t *entry = &cache->entries[i];
        if (entry->used && entry->expires > now && ipv6_address_equal(entry->address, address)) {
            copy_mac6(mac, entry->mac); return 0;
        }
    }
    return -2;
}
int rix_net_ipv6_neighbor_expire(rix_net_ipv6_neighbor_cache_t *cache, uint64_t now) {
    if (!cache) return -1;
    int expired = 0;
    for (size_t i = 0; i < RIX_NET_IPV6_NEIGHBOR_CACHE_SIZE; ++i)
        if (cache->entries[i].used && cache->entries[i].expires <= now) { cache->entries[i].used = 0; ++expired; }
    return expired;
}


static uint16_t transport6_checksum(const uint8_t source[16], const uint8_t destination[16],
                                    uint8_t protocol, const uint8_t *message, size_t length) {
    uint8_t pseudo[8] = {0, 0, 0, 0, 0, 0, 0, protocol};
    uint32_t sum = 0;
    uint32_t network_length = be32((uint32_t)length);
    sum = sum_bytes(sum, source, 16); sum = sum_bytes(sum, destination, 16);
    sum = sum_bytes(sum, (const uint8_t *)&network_length, 4);
    sum = sum_bytes(sum, pseudo, sizeof(pseudo));
    return finish_sum(sum_bytes(sum, message, length));
}

int rix_net_udp6_push(rix_net_packet_t *packet, const uint8_t source[16],
                      const uint8_t destination[16], uint16_t source_port,
                      uint16_t destination_port, const void *payload, size_t payload_length) {
    if (!packet || !source || !destination || (payload_length && !payload) ||
        payload_length > 65527u) return -1;
    uint8_t *body = 0;
    if (rix_net_packet_put(packet, payload_length, (void **)&body) != 0) return -1;
    for (size_t i = 0; i < payload_length; ++i) body[i] = ((const uint8_t *)payload)[i];
    uint8_t *wire = 0;
    if (rix_net_packet_push(packet, 8, (void **)&wire) != 0) return -1;
    uint16_t value = be16(source_port); wire[0] = ((uint8_t *)&value)[0]; wire[1] = ((uint8_t *)&value)[1];
    value = be16(destination_port); wire[2] = ((uint8_t *)&value)[0]; wire[3] = ((uint8_t *)&value)[1];
    value = be16((uint16_t)(payload_length + 8u)); wire[4] = ((uint8_t *)&value)[0]; wire[5] = ((uint8_t *)&value)[1];
    wire[6] = wire[7] = 0;
    value = be16(transport6_checksum(source, destination, 17, rix_net_packet_data(packet),
                                     rix_net_packet_length(packet)));
    wire[6] = ((uint8_t *)&value)[0]; wire[7] = ((uint8_t *)&value)[1];
    return 0;
}

int rix_net_udp6_pull(rix_net_packet_t *packet, const uint8_t source[16],
                      const uint8_t destination[16], rix_net_udp_header_t *header) {
    if (!packet || !source || !destination || !header || rix_net_packet_length(packet) < 8) return -1;
    const uint8_t *wire = rix_net_packet_data(packet);
    uint16_t length = (uint16_t)(((uint16_t)wire[4] << 8) | wire[5]);
    if (length < 8 || length != rix_net_packet_length(packet) ||
        transport6_checksum(source, destination, 17, wire, length) != 0) return -2;
    header->source_port = (uint16_t)(((uint16_t)wire[0] << 8) | wire[1]);
    header->destination_port = (uint16_t)(((uint16_t)wire[2] << 8) | wire[3]);
    header->length = length; header->checksum = (uint16_t)(((uint16_t)wire[6] << 8) | wire[7]);
    if (rix_net_packet_pull(packet, 8, 0) != 0) return -1;
    packet->length = length - 8u;
    return 0;
}

int rix_net_tcp6_push(rix_net_packet_t *packet, const uint8_t source[16],
                      const uint8_t destination[16], uint16_t source_port,
                      uint16_t destination_port, uint32_t sequence,
                      uint32_t acknowledgment, uint16_t flags, uint16_t window,
                      const void *payload, size_t payload_length) {
    if (!packet || !source || !destination || (payload_length && !payload) ||
        payload_length > RIX_NET_MTU - 20u) return -1;
    uint8_t *body = 0;
    if (rix_net_packet_put(packet, payload_length, (void **)&body) != 0) return -1;
    for (size_t i = 0; i < payload_length; ++i) body[i] = ((const uint8_t *)payload)[i];
    uint8_t *wire = 0;
    if (rix_net_packet_push(packet, 20, (void **)&wire) != 0) return -1;
    uint16_t v16 = be16(source_port); wire[0] = ((uint8_t *)&v16)[0]; wire[1] = ((uint8_t *)&v16)[1];
    v16 = be16(destination_port); wire[2] = ((uint8_t *)&v16)[0]; wire[3] = ((uint8_t *)&v16)[1];
    uint32_t v32 = be32(sequence); for (size_t i = 0; i < 4; ++i) wire[4 + i] = ((uint8_t *)&v32)[i];
    v32 = be32(acknowledgment); for (size_t i = 0; i < 4; ++i) wire[8 + i] = ((uint8_t *)&v32)[i];
    wire[12] = 0x50; v16 = be16(flags); wire[13] = ((uint8_t *)&v16)[1];
    v16 = be16(window); wire[14] = ((uint8_t *)&v16)[0]; wire[15] = ((uint8_t *)&v16)[1];
    wire[16] = wire[17] = 0; wire[18] = wire[19] = 0;
    v16 = be16(transport6_checksum(source, destination, 6, rix_net_packet_data(packet),
                                   rix_net_packet_length(packet)));
    wire[16] = ((uint8_t *)&v16)[0]; wire[17] = ((uint8_t *)&v16)[1];
    return 0;
}

int rix_net_tcp6_pull(rix_net_packet_t *packet, const uint8_t source[16],
                      const uint8_t destination[16], rix_net_tcp_header_t *header) {
    if (!packet || !source || !destination || !header || rix_net_packet_length(packet) < 20) return -1;
    const uint8_t *wire = rix_net_packet_data(packet);
    size_t header_length = (size_t)(wire[12] >> 4) * 4u;
    if (header_length < 20 || header_length > rix_net_packet_length(packet) ||
        transport6_checksum(source, destination, 6, wire, rix_net_packet_length(packet)) != 0) return -2;
    header->source_port = (uint16_t)(((uint16_t)wire[0] << 8) | wire[1]);
    header->destination_port = (uint16_t)(((uint16_t)wire[2] << 8) | wire[3]);
    header->sequence = ((uint32_t)wire[4] << 24) | ((uint32_t)wire[5] << 16) | ((uint32_t)wire[6] << 8) | wire[7];
    header->acknowledgment = ((uint32_t)wire[8] << 24) | ((uint32_t)wire[9] << 16) | ((uint32_t)wire[10] << 8) | wire[11];
    header->flags = (uint16_t)(((uint16_t)wire[12] & 0x0fu) << 8 | wire[13]);
    header->window = (uint16_t)(((uint16_t)wire[14] << 8) | wire[15]);
    header->checksum = (uint16_t)(((uint16_t)wire[16] << 8) | wire[17]);
    header->urgent = (uint16_t)(((uint16_t)wire[18] << 8) | wire[19]);
    if (rix_net_packet_pull(packet, header_length, 0) != 0) return -1;
    packet->length = rix_net_packet_length(packet);
    return 0;
}
