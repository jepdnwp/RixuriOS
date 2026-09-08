#include "udp.h"
#include "ipv4.h"

static void put_be16(uint8_t *destination, uint16_t value) {
    destination[0] = (uint8_t)(value >> 8);
    destination[1] = (uint8_t)value;
}

static uint16_t pseudo_checksum(uint32_t source_ip, uint32_t destination_ip,
                                const uint8_t *segment, size_t length) {
    uint32_t sum = 0;
    sum += source_ip >> 16;
    sum += source_ip & 0xffffu;
    sum += destination_ip >> 16;
    sum += destination_ip & 0xffffu;
    sum += RIX_NET_IP_PROTO_UDP;
    sum += (uint32_t)length;
    while (length >= 2) {
        sum += ((uint32_t)segment[0] << 8) | segment[1];
        segment += 2;
        length -= 2;
    }
    if (length) sum += (uint32_t)segment[0] << 8;
    return rix_net_checksum_add(sum);
}

int rix_net_udp_push(rix_net_packet_t *packet, uint32_t source_ip,
                     uint32_t destination_ip, uint16_t source_port,
                     uint16_t destination_port, const void *payload,
                     size_t payload_length) {
    if (!packet || !source_ip || !destination_ip || !source_port ||
        !destination_port || (payload_length && !payload) ||
        payload_length > 65527u) return -1;
    if (packet->start > RIX_NET_FRAME_CAPACITY ||
        packet->length > RIX_NET_FRAME_CAPACITY - packet->start ||
        packet->start < RIX_NET_UDP_HEADER_LENGTH ||
        payload_length > RIX_NET_FRAME_CAPACITY - packet->start -
                         packet->length - RIX_NET_UDP_HEADER_LENGTH) return -1;
    uint8_t *body = 0;
    if (rix_net_packet_put(packet, payload_length, (void **)&body) != 0) return -1;
    for (size_t i = 0; i < payload_length; ++i) body[i] = ((const uint8_t *)payload)[i];
    uint8_t *header = 0;
    if (rix_net_packet_push(packet, RIX_NET_UDP_HEADER_LENGTH, (void **)&header) != 0) return -1;
    put_be16(header + 0, source_port);
    put_be16(header + 2, destination_port);
    put_be16(header + 4, (uint16_t)rix_net_packet_length(packet));
    put_be16(header + 6, 0);
    uint16_t checksum = pseudo_checksum(source_ip, destination_ip,
                                        rix_net_packet_data(packet),
                                        rix_net_packet_length(packet));
    put_be16(header + 6, checksum ? checksum : 0xffffu);
    return 0;
}

int rix_net_udp_pull(rix_net_packet_t *packet, uint32_t source_ip,
                     uint32_t destination_ip, rix_net_udp_header_t *header) {
    if (!packet || !header || !source_ip || !destination_ip ||
        rix_net_packet_length(packet) < RIX_NET_UDP_HEADER_LENGTH) return -1;
    const uint8_t *bytes = rix_net_packet_data(packet);
    uint16_t length = (uint16_t)(((uint16_t)bytes[4] << 8) | bytes[5]);
    if (length < RIX_NET_UDP_HEADER_LENGTH || length > rix_net_packet_length(packet)) return -2;
    if (pseudo_checksum(source_ip, destination_ip, bytes, length) != 0) return -3;
    header->source_port = (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
    header->destination_port = (uint16_t)(((uint16_t)bytes[2] << 8) | bytes[3]);
    header->length = length;
    header->checksum = (uint16_t)(((uint16_t)bytes[6] << 8) | bytes[7]);
    if (!header->source_port || !header->destination_port) return -4;
    if (rix_net_packet_pull(packet, RIX_NET_UDP_HEADER_LENGTH, 0) != 0) return -1;
    packet->length = (size_t)length - RIX_NET_UDP_HEADER_LENGTH;
    return 0;
}
