#pragma once
#include <stddef.h>
#include <stdint.h>

#define RIX_NET_MTU 1500u
#define RIX_NET_HEADROOM 64u
#define RIX_NET_TAILROOM 16u
#define RIX_NET_FRAME_CAPACITY (RIX_NET_HEADROOM + RIX_NET_MTU + RIX_NET_TAILROOM)

typedef struct {
    uint8_t bytes[RIX_NET_FRAME_CAPACITY];
    size_t start;
    size_t length;
} rix_net_packet_t;

void rix_net_packet_init(rix_net_packet_t *packet);
int rix_net_packet_push(rix_net_packet_t *packet, size_t length, void **out);
int rix_net_packet_put(rix_net_packet_t *packet, size_t length, void **out);
int rix_net_packet_pull(rix_net_packet_t *packet, size_t length, void **out);
const uint8_t *rix_net_packet_data(const rix_net_packet_t *packet);
size_t rix_net_packet_length(const rix_net_packet_t *packet);

uint16_t rix_net_checksum(const void *data, size_t length);
uint16_t rix_net_checksum_add(uint32_t sum);
