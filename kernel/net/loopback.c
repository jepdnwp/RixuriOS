#include "loopback.h"

static void copy_packet(rix_net_packet_t *destination, const rix_net_packet_t *source) {
    destination->start = source->start;
    destination->length = source->length;
    for (size_t i = source->start; i < source->start + source->length; ++i)
        destination->bytes[i] = source->bytes[i];
}

void rix_net_loopback_init(rix_net_loopback_t *loopback) {
    if (!loopback) return;
    loopback->head = 0;
    loopback->count = 0;
}

int rix_net_loopback_transmit(rix_net_loopback_t *loopback, const rix_net_packet_t *packet) {
    if (!loopback || !packet || packet->start > RIX_NET_FRAME_CAPACITY ||
        packet->length > RIX_NET_FRAME_CAPACITY - packet->start ||
        loopback->count >= RIX_NET_LOOPBACK_QUEUE) return -1;
    size_t tail = (loopback->head + loopback->count) % RIX_NET_LOOPBACK_QUEUE;
    copy_packet(&loopback->queue[tail], packet);
    ++loopback->count;
    return 0;
}

int rix_net_loopback_receive(rix_net_loopback_t *loopback, rix_net_packet_t *packet) {
    if (!loopback || !packet || !loopback->count) return -1;
    copy_packet(packet, &loopback->queue[loopback->head]);
    loopback->head = (loopback->head + 1) % RIX_NET_LOOPBACK_QUEUE;
    --loopback->count;
    return 0;
}

size_t rix_net_loopback_pending(const rix_net_loopback_t *loopback) {
    return loopback ? loopback->count : 0;
}
