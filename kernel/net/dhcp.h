#pragma once
#include "net.h"
#include <stddef.h>
#include <stdint.h>

#define RIX_DHCP_OP_REQUEST 1u
#define RIX_DHCP_OP_REPLY 2u
#define RIX_DHCP_MSG_DISCOVER 1u
#define RIX_DHCP_MSG_OFFER 2u
#define RIX_DHCP_MSG_REQUEST 3u
#define RIX_DHCP_MSG_ACK 5u
#define RIX_DHCP_MSG_NAK 6u
#define RIX_DHCP_MAGIC 0x63825363u
#define RIX_DHCP_MIN_SIZE 300u
#define RIX_DHCP_SERVER_PORT 67u
#define RIX_DHCP_CLIENT_PORT 68u

typedef struct {
    uint32_t address;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns;
    uint32_t server;
    uint32_t lease_seconds;
    uint8_t has_netmask;
    uint8_t has_gateway;
    uint8_t has_dns;
} rix_dhcp_offer_t;

int rix_net_dhcp_build_discover(rix_net_packet_t *packet, uint32_t xid,
                                const uint8_t mac[6]);
int rix_net_dhcp_build_request(rix_net_packet_t *packet, uint32_t xid,
                               const uint8_t mac[6], uint32_t requested_ip);
int rix_net_dhcp_parse_reply(const rix_net_packet_t *packet, uint32_t xid,
                             uint8_t expected_type, rix_dhcp_offer_t *offer);
#ifndef RIX_HOST_TEST
int rix_net_dhcp_run(void);
#endif
