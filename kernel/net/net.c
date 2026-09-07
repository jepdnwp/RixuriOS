#include "net.h"

static uint32_t fold_sum(uint32_t sum) {
    while (sum >> 16) sum = (sum & 0xffffu) + (sum >> 16);
    return sum;
}

void rix_net_packet_init(rix_net_packet_t *packet) {
    if (!packet) return;
    packet->start = RIX_NET_HEADROOM;
    packet->length = 0;
}

int rix_net_packet_push(rix_net_packet_t *packet, size_t length, void **out) {
    if (!packet || length > packet->start) return -1;
    packet->start -= length;
    packet->length += length;
    if (out) *out = packet->bytes + packet->start;
    return 0;
}

int rix_net_packet_put(rix_net_packet_t *packet, size_t length, void **out) {
    if (!packet || packet->start > RIX_NET_FRAME_CAPACITY ||
        length > RIX_NET_FRAME_CAPACITY - packet->start - packet->length) return -1;
    if (out) *out = packet->bytes + packet->start + packet->length;
    packet->length += length;
    return 0;
}

int rix_net_packet_pull(rix_net_packet_t *packet, size_t length, void **out) {
    if (!packet || length > packet->length) return -1;
    if (out) *out = packet->bytes + packet->start;
    packet->start += length;
    packet->length -= length;
    return 0;
}

const uint8_t *rix_net_packet_data(const rix_net_packet_t *packet) {
    if (!packet || packet->start > RIX_NET_FRAME_CAPACITY ||
        packet->length > RIX_NET_FRAME_CAPACITY - packet->start) return 0;
    return packet->bytes + packet->start;
}

size_t rix_net_packet_length(const rix_net_packet_t *packet) {
    return packet ? packet->length : 0;
}

uint16_t rix_net_checksum_add(uint32_t sum) {
    return (uint16_t)~fold_sum(sum);
}

uint16_t rix_net_checksum(const void *data, size_t length) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t sum = 0;
    while (length >= 2) {
        sum += ((uint32_t)bytes[0] << 8) | bytes[1];
        bytes += 2;
        length -= 2;
    }
    if (length) sum += (uint32_t)bytes[0] << 8;
    return rix_net_checksum_add(sum);
}
