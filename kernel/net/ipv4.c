#include "ipv4.h"

static uint16_t be16(uint16_t value) { return (uint16_t)((value << 8) | (value >> 8)); }
static uint32_t be32(uint32_t value) {
    return ((value & 0xff000000u) >> 24) | ((value & 0x00ff0000u) >> 8) |
           ((value & 0x0000ff00u) << 8) | ((value & 0x000000ffu) << 24);
}

struct ipv4_wire {
    uint8_t version_ihl;
    uint8_t dscp_ecn;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    uint32_t source;
    uint32_t destination;
};

int rix_net_ipv4_push(rix_net_packet_t *packet, uint32_t source,
                      uint32_t destination, uint8_t protocol, uint8_t ttl,
                      uint16_t identification, uint16_t flags_fragment) {
    if (!packet || !ttl || rix_net_packet_length(packet) > 65515u) return -1;
    struct ipv4_wire *wire = 0;
    if (rix_net_packet_push(packet, sizeof(*wire), (void **)&wire) != 0) return -1;
    wire->version_ihl = 0x45;
    wire->dscp_ecn = 0;
    wire->total_length = be16((uint16_t)rix_net_packet_length(packet));
    wire->identification = be16(identification);
    wire->flags_fragment = be16(flags_fragment);
    wire->ttl = ttl;
    wire->protocol = protocol;
    wire->checksum = 0;
    wire->source = be32(source);
    wire->destination = be32(destination);
    wire->checksum = be16(rix_net_checksum(wire, sizeof(*wire)));
    return 0;
}

int rix_net_ipv4_pull(rix_net_packet_t *packet, rix_net_ipv4_header_t *header) {
    if (!packet || !header || rix_net_packet_length(packet) < sizeof(struct ipv4_wire)) return -1;
    const struct ipv4_wire *wire = 0;
    if (rix_net_packet_data(packet)[0] >> 4 != 4 || (rix_net_packet_data(packet)[0] & 0xfu) != 5) return -1;
    if (rix_net_checksum(rix_net_packet_data(packet), sizeof(*wire)) != 0) return -2;
    uint16_t total = be16(*(const uint16_t *)(rix_net_packet_data(packet) + 2));
    if (total < sizeof(*wire) || total > rix_net_packet_length(packet)) return -3;
    if (rix_net_packet_pull(packet, sizeof(*wire), (void **)&wire) != 0) return -1;
    header->identification = be16(wire->identification);
    header->flags_fragment = be16(wire->flags_fragment);
    header->source = be32(wire->source);
    header->destination = be32(wire->destination);
    header->protocol = wire->protocol;
    header->ttl = wire->ttl;
    return 0;
}

struct icmp_wire { uint8_t type; uint8_t code; uint16_t checksum; uint16_t identifier; uint16_t sequence; };

int rix_net_icmp_echo_push(rix_net_packet_t *packet, uint8_t type,
                           uint16_t identifier, uint16_t sequence,
                           const void *payload, size_t payload_length) {
    if (!packet || (payload_length && !payload) || payload_length > 65527u) return -1;
    uint8_t *body = 0;
    if (rix_net_packet_put(packet, payload_length, (void **)&body) != 0) return -1;
    for (size_t i = 0; i < payload_length; ++i) body[i] = ((const uint8_t *)payload)[i];
    struct icmp_wire *wire = 0;
    if (rix_net_packet_push(packet, sizeof(*wire), (void **)&wire) != 0) return -1;
    wire->type = type;
    wire->code = 0;
    wire->checksum = 0;
    wire->identifier = be16(identifier);
    wire->sequence = be16(sequence);
    wire->checksum = be16(rix_net_checksum(rix_net_packet_data(packet), rix_net_packet_length(packet)));
    return 0;
}

int rix_net_icmp_echo_pull(rix_net_packet_t *packet, rix_net_icmp_echo_t *echo) {
    if (!packet || !echo || rix_net_packet_length(packet) < sizeof(struct icmp_wire)) return -1;
    if (rix_net_checksum(rix_net_packet_data(packet), rix_net_packet_length(packet)) != 0) return -2;
    const struct icmp_wire *wire = (const struct icmp_wire *)rix_net_packet_data(packet);
    echo->type = wire->type;
    echo->code = wire->code;
    echo->identifier = be16(wire->identifier);
    echo->sequence = be16(wire->sequence);
    return rix_net_packet_pull(packet, sizeof(*wire), 0);
}
