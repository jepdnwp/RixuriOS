#pragma once
#include "net.h"
#include <stddef.h>
#include <stdint.h>

#define RIX_NET_IP_PROTO_ICMP 1u
#define RIX_NET_IP_PROTO_TCP 6u
#define RIX_NET_IP_PROTO_UDP 17u
#define RIX_NET_IP_FLAG_DF 0x4000u

typedef struct {
    uint32_t source;
    uint32_t destination;
    uint8_t protocol;
    uint8_t ttl;
    uint16_t identification;
    uint16_t flags_fragment;
    uint16_t total_length;
} rix_net_ipv4_header_t;

int rix_net_ipv4_push(rix_net_packet_t *packet, uint32_t source,
                      uint32_t destination, uint8_t protocol, uint8_t ttl,
                      uint16_t identification, uint16_t flags_fragment);
int rix_net_ipv4_pull(rix_net_packet_t *packet, rix_net_ipv4_header_t *header);

#define RIX_NET_ICMP_ECHO_REQUEST 8u
#define RIX_NET_ICMP_ECHO_REPLY 0u

typedef struct {
    uint8_t type;
    uint8_t code;
    uint16_t identifier;
    uint16_t sequence;
} rix_net_icmp_echo_t;

int rix_net_icmp_echo_push(rix_net_packet_t *packet, uint8_t type,
                           uint16_t identifier, uint16_t sequence,
                           const void *payload, size_t payload_length);
int rix_net_icmp_echo_pull(rix_net_packet_t *packet, rix_net_icmp_echo_t *echo);
