#pragma once
#include "net.h"
#include <stddef.h>
#include <stdint.h>

#define RIX_NET_UDP_HEADER_LENGTH 8u

typedef struct {
    uint16_t source_port;
    uint16_t destination_port;
    uint16_t length;
    uint16_t checksum;
} rix_net_udp_header_t;

int rix_net_udp_push(rix_net_packet_t *packet, uint32_t source_ip,
                     uint32_t destination_ip, uint16_t source_port,
                     uint16_t destination_port, const void *payload,
                     size_t payload_length);
int rix_net_udp_pull(rix_net_packet_t *packet, uint32_t source_ip,
                     uint32_t destination_ip, rix_net_udp_header_t *header);
