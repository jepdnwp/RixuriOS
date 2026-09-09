#include "dhcp.h"
#include "ethernet.h"
#include "ipv4.h"
#include "udp.h"
#include "device.h"

#define DHCP_OPT_PAD 0u
#define DHCP_OPT_END 255u
#define DHCP_OPT_MSGTYPE 53u
#define DHCP_OPT_MASK 1u
#define DHCP_OPT_ROUTER 3u
#define DHCP_OPT_DNS 6u
#define DHCP_OPT_REQIP 50u
#define DHCP_OPT_LEASE 51u
#define DHCP_OPT_SERVER 54u
#define DHCP_OPT_PRL 55u
#define DHCP_OPT_CLIENT_ID 61u

static void put_be32(uint8_t *destination, uint32_t value) {
    destination[0] = (uint8_t)(value >> 24);
    destination[1] = (uint8_t)(value >> 16);
    destination[2] = (uint8_t)(value >> 8);
    destination[3] = (uint8_t)value;
}

static uint32_t get_be32(const uint8_t *source) {
    return ((uint32_t)source[0] << 24) | ((uint32_t)source[1] << 16) |
           ((uint32_t)source[2] << 8) | source[3];
}

static int dhcp_header(rix_net_packet_t *packet, uint8_t op, uint32_t xid,
                       const uint8_t mac[6]) {
    uint8_t *body = 0;
    if (!packet || !mac) return -1;
    if (rix_net_packet_put(packet, 240, (void **)&body) != 0) return -1;
    for (size_t i = 0; i < 240; ++i) body[i] = 0;
    body[0] = op;
    body[1] = 1;
    body[2] = 6;
    put_be32(body + 4, xid);
    body[10] = 0x80;
    for (size_t i = 0; i < 6; ++i) body[28 + i] = mac[i];
    body[236] = 99;
    body[237] = 130;
    body[238] = 83;
    body[239] = 99;
    return 0;
}

static int dhcp_option(rix_net_packet_t *packet, uint8_t code,
                       const uint8_t *data, uint8_t length) {
    uint8_t *body = 0;
    if (rix_net_packet_put(packet, (size_t)2 + length, (void **)&body) != 0)
        return -1;
    body[0] = code;
    body[1] = length;
    for (uint8_t i = 0; i < length; ++i) body[2 + i] = data[i];
    return 0;
}

static int dhcp_pad(rix_net_packet_t *packet) {
    size_t have = rix_net_packet_length(packet);
    uint8_t end = DHCP_OPT_END, *body = 0;
    if (have > RIX_DHCP_MIN_SIZE) return -1;
    if (rix_net_packet_put(packet, 1, (void **)&body) != 0) return -1;
    body[0] = end;
    have = rix_net_packet_length(packet);
    if (rix_net_packet_put(packet, RIX_DHCP_MIN_SIZE - have, (void **)&body) != 0)
        return -1;
    for (size_t i = 0; i < RIX_DHCP_MIN_SIZE - have; ++i) body[i] = 0;
    return 0;
}

int rix_net_dhcp_build_discover(rix_net_packet_t *packet, uint32_t xid,
                                const uint8_t mac[6]) {
    uint8_t type = RIX_DHCP_MSG_DISCOVER;
    uint8_t prl[3] = {1, 3, 6};
    if (!packet || !mac || !xid) return -1;
    if (dhcp_header(packet, RIX_DHCP_OP_REQUEST, xid, mac) != 0) return -1;
    if (dhcp_option(packet, DHCP_OPT_MSGTYPE, &type, 1) != 0) return -1;
    if (dhcp_option(packet, DHCP_OPT_PRL, prl, 3) != 0) return -1;
    return dhcp_pad(packet);
}

int rix_net_dhcp_build_request_for_server(rix_net_packet_t *packet, uint32_t xid,
                                          const uint8_t mac[6], uint32_t requested_ip,
                                          uint32_t server) {
    uint8_t type = RIX_DHCP_MSG_REQUEST;
    uint8_t requested[4];
    uint8_t prl[3] = {1, 3, 6};
    if (!packet || !mac || !xid || !requested_ip) return -1;
    if (dhcp_header(packet, RIX_DHCP_OP_REQUEST, xid, mac) != 0) return -1;
    if (dhcp_option(packet, DHCP_OPT_MSGTYPE, &type, 1) != 0) return -1;
    put_be32(requested, requested_ip);
    if (dhcp_option(packet, DHCP_OPT_REQIP, requested, 4) != 0) return -1;
    uint8_t client_id[7] = {1, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]};
    if (dhcp_option(packet, DHCP_OPT_CLIENT_ID, client_id, sizeof(client_id)) != 0) return -1;
    if (server) {
        uint8_t server_id[4];
        put_be32(server_id, server);
        if (dhcp_option(packet, DHCP_OPT_SERVER, server_id, sizeof(server_id)) != 0) return -1;
    }
    if (dhcp_option(packet, DHCP_OPT_PRL, prl, 3) != 0) return -1;
    return dhcp_pad(packet);
}

int rix_net_dhcp_build_request(rix_net_packet_t *packet, uint32_t xid,
                               const uint8_t mac[6], uint32_t requested_ip) {
    return rix_net_dhcp_build_request_for_server(packet, xid, mac, requested_ip, 0);
}

static int contiguous_mask(uint32_t mask) {
    if (!mask) return 0;
    return ((~mask & (~mask + 1u)) == 0u);
}

int rix_net_dhcp_parse_reply(const rix_net_packet_t *packet, uint32_t xid,
                             uint8_t expected_type, rix_dhcp_offer_t *offer) {
    const uint8_t *bytes;
    size_t length, offset;
    uint8_t seen_type = 0;
    if (!packet || !xid || !offer) return -1;
    length = rix_net_packet_length(packet);
    if (length < 244) return -1;
    bytes = rix_net_packet_data(packet);
    if (!bytes || bytes[0] != RIX_DHCP_OP_REPLY) return -1;
    if (get_be32(bytes + 4) != xid) return -1;
    if (bytes[236] != 99 || bytes[237] != 130 || bytes[238] != 83 ||
        bytes[239] != 99)
        return -1;
    offer->address = get_be32(bytes + 16);
    if (!offer->address) return -1;
    offer->netmask = 0;
    offer->gateway = 0;
    offer->dns = 0;
    offer->server = 0;
    offer->lease_seconds = 0;
    offer->has_netmask = 0;
    offer->has_gateway = 0;
    offer->has_dns = 0;
    offset = 240;
    while (offset < length) {
        uint8_t code = bytes[offset++];
        uint8_t option_length;
        if (code == DHCP_OPT_PAD) continue;
        if (code == DHCP_OPT_END) break;
        if (offset >= length) return -1;
        option_length = bytes[offset++];
        if ((size_t)option_length > length - offset) return -1;
        switch (code) {
        case DHCP_OPT_MSGTYPE:
            if (option_length != 1) return -1;
            seen_type = 1;
            if (bytes[offset] != expected_type) return -2;
            break;
        case DHCP_OPT_MASK:
            if (option_length == 4) {
                uint32_t mask = get_be32(bytes + offset);
                if (contiguous_mask(mask)) {
                    offer->netmask = mask;
                    offer->has_netmask = 1;
                }
            }
            break;
        case DHCP_OPT_ROUTER:
            if (option_length >= 4) {
                offer->gateway = get_be32(bytes + offset);
                offer->has_gateway = 1;
            }
            break;
        case DHCP_OPT_DNS:
            if (option_length >= 4) {
                offer->dns = get_be32(bytes + offset);
                offer->has_dns = 1;
            }
            break;
        case DHCP_OPT_SERVER:
            if (option_length == 4) offer->server = get_be32(bytes + offset);
            break;
        case DHCP_OPT_LEASE:
            if (option_length == 4) offer->lease_seconds = get_be32(bytes + offset);
            break;
        default:
            break;
        }
        offset += option_length;
    }
    if (!seen_type) return -1;
    return 0;
}

#ifndef RIX_HOST_TEST
#include "../serial.h"

/* Bounded boot-time window. Quick rounds catch fast responders (QEMU SLIRP
   answers in ~1ms); the extended tail rides out delayed RX delivery and
   slower servers. Physical hardware without link skips DHCP before this. */
#define RIX_DHCP_ROUNDS 12u
#define RIX_DHCP_POLLS_PER_ROUND 1000000u

typedef struct {
    unsigned frames;
    unsigned eth_drop;
    unsigned ip_drop;
    unsigned udp_drop;
    unsigned port_drop;
    unsigned parse_drop;
} dhcp_stats_t;

static void dhcp_report(unsigned round, const char *phase,
                        const dhcp_stats_t *stats, unsigned accepted) {
    serial_write("DHCP: round=");
    serial_write_dec(round);
    serial_write(" ");
    serial_write(phase);
    serial_write(" frames=");
    serial_write_dec(stats->frames);
    serial_write(" eth=");
    serial_write_dec(stats->eth_drop);
    serial_write(" ip=");
    serial_write_dec(stats->ip_drop);
    serial_write(" udp=");
    serial_write_dec(stats->udp_drop);
    serial_write(" port=");
    serial_write_dec(stats->port_drop);
    serial_write(" parse=");
    serial_write_dec(stats->parse_drop);
    serial_write(" accept=");
    serial_write_dec(accepted);
    serial_write("\r\n");
}
/* UDP header for DHCP requests: source IP is 0.0.0.0, which
   rix_net_udp_push rejects, so the 8-byte header is built here with the
   checksum computed over a zero source address. Payload must already be
   in the packet (appended via rix_net_packet_put by the builders above). */
static int dhcp_udp_push(rix_net_packet_t *packet) {
    uint8_t *header = 0;
    const uint8_t *segment;
    size_t length, remaining;
    uint32_t sum = 0;
    if (!packet) return -1;
    length = rix_net_packet_length(packet) + 8;
    if (length > 65535u || length < 8) return -1;
    if (rix_net_packet_push(packet, 8, (void **)&header) != 0) return -1;
    header[0] = 0;
    header[1] = 68;
    header[2] = 0;
    header[3] = 67;
    header[4] = (uint8_t)(length >> 8);
    header[5] = (uint8_t)length;
    header[6] = 0;
    header[7] = 0;
    sum += 0xffffu + 0xffffu;
    sum += RIX_NET_IP_PROTO_UDP;
    sum += (uint32_t)length;
    segment = rix_net_packet_data(packet);
    remaining = length;
    while (remaining >= 2) {
        sum += ((uint32_t)segment[0] << 8) | segment[1];
        segment += 2;
        remaining -= 2;
    }
    if (remaining) sum += (uint32_t)segment[0] << 8;
    {
        uint16_t checksum = rix_net_checksum_add(sum);
        header[6] = (uint8_t)(checksum >> 8);
        header[7] = (uint8_t)(checksum ? checksum : 0xffffu);
    }
    return 0;
}

static int dhcp_transmit(rix_net_packet_t *packet, const uint8_t mac[6]) {
    static const uint8_t broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    if (!packet || !mac) return -1;
    if (dhcp_udp_push(packet) != 0) return -1;
    if (rix_net_ipv4_push(packet, 0, 0xffffffffu, RIX_NET_IP_PROTO_UDP,
                          64, 0, 0) != 0)
        return -1;
    if (rix_net_eth_push(packet, broadcast, mac, RIX_NET_ETHERTYPE_IPV4) != 0)
        return -1;
    return rix_net_device_transmit(packet);
}

/* Poll once for a DHCP reply matching xid/type. Returns 1 accepted,
   0 nothing usable yet, -1 on programming error (never on wire input). */
static int dhcp_poll_once(uint32_t xid, uint8_t expected_type,
                          rix_dhcp_offer_t *offer, dhcp_stats_t *stats) {
    rix_net_packet_t frame;
    rix_net_eth_header_t ethernet;
    rix_net_ipv4_header_t ip;
    rix_net_udp_header_t udp;
    int received;
    if (!xid || !offer || !stats) return -1;
    rix_net_packet_init(&frame);
    received = rix_net_device_receive(&frame);
    if (received < 0) return -1;
    if (received == 0) return 0;
    stats->frames++;
    if (rix_net_eth_pull(&frame, &ethernet) != 0) {
        stats->eth_drop++;
        return 0;
    }
    if (ethernet.ethertype != RIX_NET_ETHERTYPE_IPV4) {
        stats->eth_drop++;
        return 0;
    }
    if (rix_net_ipv4_pull(&frame, &ip) != 0) {
        stats->ip_drop++;
        return 0;
    }
    if (ip.protocol != RIX_NET_IP_PROTO_UDP) {
        stats->ip_drop++;
        return 0;
    }
    if (rix_net_udp_pull(&frame, ip.source, ip.destination, &udp) != 0) {
        stats->udp_drop++;
        return 0;
    }
    if (udp.source_port != RIX_DHCP_SERVER_PORT ||
        udp.destination_port != RIX_DHCP_CLIENT_PORT) {
        stats->port_drop++;
        return 0;
    }
    if (rix_net_dhcp_parse_reply(&frame, xid, expected_type, offer) != 0) {
        stats->parse_drop++;
        return 0;
    }
    return 1;
}

int rix_net_dhcp_run(void) {
    const rix_net_device_info_t *info = rix_net_device_info();
    static uint32_t xid = 0x12345678u;
    rix_dhcp_offer_t offer;
    uint32_t current;
    unsigned round, poll;
    if (!info) return -1;
    current = xid++;
    if (!current) current = xid++;
    offer.address = 0;
    offer.has_netmask = 0;
    for (round = 0; round < RIX_DHCP_ROUNDS; ++round) {
        rix_net_packet_t discover;
        dhcp_stats_t stats = {0, 0, 0, 0, 0, 0};
        unsigned accepted = 0;
        rix_net_packet_init(&discover);
        if (rix_net_dhcp_build_discover(&discover, current, info->mac) != 0) return -1;
        if (dhcp_transmit(&discover, info->mac) < 0) return -1;
        for (poll = 0; poll < RIX_DHCP_POLLS_PER_ROUND; ++poll) {
            int found = dhcp_poll_once(current, RIX_DHCP_MSG_OFFER, &offer, &stats);
            if (found < 0) return -1;
            if (found) {
                accepted = 1;
                break;
            }
        }
        dhcp_report(round, "discover", &stats, accepted);
        if (offer.address && offer.has_netmask) break;
        offer.address = 0;
    }
    if (!offer.address) return -1;
    for (round = 0; round < RIX_DHCP_ROUNDS; ++round) {
        rix_net_packet_t request;
        dhcp_stats_t stats = {0, 0, 0, 0, 0, 0};
        unsigned accepted = 0;
        rix_net_packet_init(&request);
        if (rix_net_dhcp_build_request_for_server(&request, current, info->mac,
                                                  offer.address, offer.server) != 0)
            return -1;
        if (dhcp_transmit(&request, info->mac) < 0) return -1;
        for (poll = 0; poll < RIX_DHCP_POLLS_PER_ROUND; ++poll) {
            rix_dhcp_offer_t ack;
            int found = dhcp_poll_once(current, RIX_DHCP_MSG_ACK, &ack, &stats);
            if (found < 0) return -1;
            if (found) {
                uint32_t netmask, gateway, dns;
                if (ack.address != offer.address) continue;
                accepted = 1;
                dhcp_report(round, "request", &stats, accepted);
                netmask = ack.has_netmask ? ack.netmask : offer.netmask;
                gateway = ack.has_gateway ? ack.gateway : offer.gateway;
                dns = ack.has_dns ? ack.dns : offer.dns;
                if (rix_net_device_configure(ack.address, netmask,
                                             gateway, dns) != 0)
                    return -1;
                return 0;
            }
        }
        dhcp_report(round, "request", &stats, accepted);
    }
    return -1;
}
#endif
