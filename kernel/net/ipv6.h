#pragma once
#include "net.h"
#include <stddef.h>
#include <stdint.h>

#define RIX_NET_IP_PROTO_ICMPV6 58u
#define RIX_NET_IPV6_HEADER_SIZE 40u
#define RIX_NET_IPV6_ADDR_SIZE 16u

#define RIX_NET_ICMPV6_ECHO_REQUEST 128u
#define RIX_NET_ICMPV6_ECHO_REPLY 129u
#define RIX_NET_ICMPV6_NEIGHBOR_SOLICIT 135u
#define RIX_NET_ICMPV6_NEIGHBOR_ADVERT 136u

typedef struct {
    uint8_t source[RIX_NET_IPV6_ADDR_SIZE];
    uint8_t destination[RIX_NET_IPV6_ADDR_SIZE];
    uint8_t traffic_class;
    uint32_t flow_label;
    uint16_t payload_length;
    uint8_t next_header;
    uint8_t hop_limit;
} rix_net_ipv6_header_t;

int rix_net_ipv6_push(rix_net_packet_t *packet,
                      const uint8_t source[RIX_NET_IPV6_ADDR_SIZE],
                      const uint8_t destination[RIX_NET_IPV6_ADDR_SIZE],
                      uint8_t next_header, uint8_t hop_limit,
                      uint8_t traffic_class, uint32_t flow_label);
int rix_net_ipv6_pull(rix_net_packet_t *packet, rix_net_ipv6_header_t *header);

typedef struct {
    uint8_t type;
    uint8_t code;
    uint16_t identifier;
    uint16_t sequence;
} rix_net_icmpv6_echo_t;

int rix_net_icmpv6_echo_push(rix_net_packet_t *packet, uint8_t type,
                             uint16_t identifier, uint16_t sequence,
                             const uint8_t source[RIX_NET_IPV6_ADDR_SIZE],
                             const uint8_t destination[RIX_NET_IPV6_ADDR_SIZE],
                             const void *payload, size_t payload_length);
int rix_net_icmpv6_echo_pull(rix_net_packet_t *packet,
                             const uint8_t source[RIX_NET_IPV6_ADDR_SIZE],
                             const uint8_t destination[RIX_NET_IPV6_ADDR_SIZE],
                             rix_net_icmpv6_echo_t *echo);

/* RFC 4861 Neighbor Solicitation/Advertisement bodies without options.
   Option parsing remains a separate bounded layer. */
int rix_net_icmpv6_neighbor_solicit_push(rix_net_packet_t *packet,
                                         const uint8_t source[RIX_NET_IPV6_ADDR_SIZE],
                                         const uint8_t destination[RIX_NET_IPV6_ADDR_SIZE],
                                         const uint8_t target[RIX_NET_IPV6_ADDR_SIZE]);
int rix_net_icmpv6_neighbor_advert_push(rix_net_packet_t *packet,
                                        const uint8_t source[RIX_NET_IPV6_ADDR_SIZE],
                                        const uint8_t destination[RIX_NET_IPV6_ADDR_SIZE],
                                        uint32_t flags,
                                        const uint8_t target[RIX_NET_IPV6_ADDR_SIZE]);
int rix_net_icmpv6_neighbor_pull(rix_net_packet_t *packet,
                                 const uint8_t source[RIX_NET_IPV6_ADDR_SIZE],
                                 const uint8_t destination[RIX_NET_IPV6_ADDR_SIZE],
                                 uint8_t *type, uint32_t *flags,
                                 uint8_t target[RIX_NET_IPV6_ADDR_SIZE]);
