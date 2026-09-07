#include "ethernet.h"

static uint16_t be16(uint16_t value) {
    return (uint16_t)((value << 8) | (value >> 8));
}

static void copy_mac(uint8_t destination[6], const uint8_t source[6]) {
    for (size_t i = 0; i < 6; ++i) destination[i] = source[i];
}

int rix_net_eth_push(rix_net_packet_t *packet, const uint8_t destination[6],
                     const uint8_t source[6], uint16_t ethertype) {
    if (!packet || !destination || !source || !rix_net_ethertype_supported(ethertype)) return -1;
    rix_net_eth_header_t *header = 0;
    if (rix_net_packet_push(packet, sizeof(*header), (void **)&header) != 0) return -1;
    copy_mac(header->destination, destination);
    copy_mac(header->source, source);
    header->ethertype = be16(ethertype);
    return 0;
}

int rix_net_eth_pull(rix_net_packet_t *packet, rix_net_eth_header_t *header) {
    if (!packet || !header || rix_net_packet_length(packet) < sizeof(*header)) return -1;
    const rix_net_eth_header_t *wire = 0;
    if (rix_net_packet_pull(packet, sizeof(*wire), (void **)&wire) != 0) return -1;
    copy_mac(header->destination, wire->destination);
    copy_mac(header->source, wire->source);
    header->ethertype = be16(wire->ethertype);
    return rix_net_ethertype_supported(header->ethertype) ? 0 : -2;
}

int rix_net_ethertype_supported(uint16_t ethertype) {
    return ethertype == RIX_NET_ETHERTYPE_IPV4 || ethertype == RIX_NET_ETHERTYPE_ARP;
}
