#include "kernel/net/net.h"
#include "kernel/net/ethernet.h"
#include "kernel/net/arp.h"
#include "kernel/net/ipv4.h"
#include "kernel/net/ipv6.h"
#include "kernel/net/loopback.h"
#include "kernel/net/socket.h"
#include "kernel/net/tcp.h"
#include "kernel/net/udp.h"
#include "kernel/net/dhcp.h"
#include "kernel/net/device.h"
#include "kernel/net/stack.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

const rix_net_device_info_t *rix_net_device_info(void) { return 0; }
static rix_net_stack_t host_stack;
rix_net_stack_t *rix_net_stack_default(void) { return &host_stack; }
int rix_net_stack_send_ipv4(rix_net_stack_t *stack, rix_net_packet_t *packet,
                            uint32_t destination_ip) {
    (void)stack; (void)packet; (void)destination_ip; return -1;
}
int rix_net_stack_poll(rix_net_stack_t *stack, uint64_t now) {
    (void)stack; (void)now; return -1;
}
static rix_net_packet_t stub_frame;
static int stub_frame_valid = 0;
int rix_net_stack_take_ipv4(rix_net_stack_t *stack, rix_net_packet_t *packet) {
    (void)stack;
    if (!packet || !stub_frame_valid) return 0;
    *packet = stub_frame;
    stub_frame_valid = 0;
    return 1;
}

static int contains_bytes(const uint8_t *data, size_t length, const char *needle) {
    size_t needle_length = strlen(needle);
    if (needle_length > length) return 0;
    for (size_t offset = 0; offset <= length - needle_length; ++offset)
        if (memcmp(data + offset, needle, needle_length) == 0) return 1;
    return 0;
}

int main(void) {
    rix_net_packet_t packet;
    void *ptr = 0;
    uint8_t header[4] = {0xaa, 0xbb, 0xcc, 0xdd};
    rix_net_packet_init(&packet);
    assert(rix_net_packet_length(&packet) == 0);
    assert(rix_net_packet_put(&packet, sizeof(header), &ptr) == 0);
    memcpy(ptr, header, sizeof(header));
    assert(rix_net_packet_length(&packet) == sizeof(header));
    assert(memcmp(rix_net_packet_data(&packet), header, sizeof(header)) == 0);
    assert(rix_net_packet_push(&packet, 2, &ptr) == 0);
    memset(ptr, 0x11, 2);
    assert(rix_net_packet_length(&packet) == 6);
    assert(rix_net_packet_pull(&packet, 2, &ptr) == 0);
    assert(((uint8_t *)ptr)[0] == 0x11 && ((uint8_t *)ptr)[1] == 0x11);
    assert(rix_net_packet_length(&packet) == 4);
    assert(rix_net_checksum(header, sizeof(header)) == 0x8866);
    assert(rix_net_packet_push(&packet, RIX_NET_HEADROOM + 1, &ptr) != 0);
    assert(rix_net_packet_put(&packet, RIX_NET_FRAME_CAPACITY, &ptr) != 0);
    assert(rix_net_packet_pull(&packet, 5, &ptr) != 0);

    const uint8_t destination[6] = {0, 1, 2, 3, 4, 5};
    const uint8_t source[6] = {6, 7, 8, 9, 10, 11};
    rix_net_eth_header_t ethernet;
    rix_net_packet_init(&packet);
    assert(rix_net_eth_push(&packet, destination, source, RIX_NET_ETHERTYPE_ARP) == 0);
    assert(rix_net_eth_pull(&packet, &ethernet) == 0);
    assert(memcmp(ethernet.destination, destination, 6) == 0);
    assert(memcmp(ethernet.source, source, 6) == 0);
    assert(ethernet.ethertype == RIX_NET_ETHERTYPE_ARP);
    assert(rix_net_ethertype_supported(RIX_NET_ETHERTYPE_IPV6) != 0);

    rix_net_arp_cache_t cache;
    uint8_t learned[6];
    rix_net_arp_init(&cache);
    assert(rix_net_arp_learn(&cache, 0xc0a80101u, source, 10, 20) == 0);
    assert(rix_net_arp_lookup(&cache, 0xc0a80101u, 29, learned) == 0);
    assert(memcmp(learned, source, 6) == 0);
    assert(rix_net_arp_lookup(&cache, 0xc0a80101u, 29, learned) == 0);
    assert(rix_net_arp_expire(&cache, 30) == 1);

    rix_net_arp_packet_t arp;
    rix_net_packet_init(&packet);
    assert(rix_net_arp_push(&packet, RIX_NET_ARP_REQUEST, source, 0xc0a80102u,
                            destination, 0xc0a80101u) == 0);
    assert(rix_net_arp_pull(&packet, &arp) == 0);
    assert(arp.operation == RIX_NET_ARP_REQUEST && arp.sender_ip == 0xc0a80102u &&
           arp.target_ip == 0xc0a80101u && memcmp(arp.sender_mac, source, 6) == 0);
    assert(rix_net_packet_length(&packet) == 0);

    const uint8_t payload[] = {1, 2, 3, 4};
    rix_net_ipv4_header_t ip;
    rix_net_icmp_echo_t echo;
    rix_net_packet_init(&packet);
    assert(rix_net_icmp_echo_push(&packet, RIX_NET_ICMP_ECHO_REQUEST, 7, 9,
                                  payload, sizeof(payload)) == 0);
    assert(rix_net_ipv4_push(&packet, 0xc0a80102u, 0xc0a80101u,
                             RIX_NET_IP_PROTO_ICMP, 64, 1, RIX_NET_IP_FLAG_DF) == 0);
    assert(rix_net_ipv4_pull(&packet, &ip) == 0);
    assert(ip.source == 0xc0a80102u && ip.destination == 0xc0a80101u);
    assert(ip.protocol == RIX_NET_IP_PROTO_ICMP && ip.ttl == 64);
    assert(rix_net_icmp_echo_pull(&packet, &echo) == 0);
    assert(echo.type == RIX_NET_ICMP_ECHO_REQUEST && echo.identifier == 7 && echo.sequence == 9);
    assert(rix_net_packet_length(&packet) == sizeof(payload));

    const uint8_t ipv6_source[16] = {0x20,0x01,0x0d,0xb8,0,0,0,0,0,0,0,0,0,0,0,1};
    const uint8_t ipv6_destination[16] = {0x20,0x01,0x0d,0xb8,0,0,0,0,0,0,0,0,0,0,0,2};
    const uint8_t ipv6_payload[] = {'v','6'};
    rix_net_ipv6_header_t ipv6;
    rix_net_icmpv6_echo_t echo6;
    rix_net_packet_init(&packet);
    assert(rix_net_icmpv6_echo_push(&packet, RIX_NET_ICMPV6_ECHO_REQUEST, 12, 3,
                                    ipv6_source, ipv6_destination,
                                    ipv6_payload, sizeof(ipv6_payload)) == 0);
    assert(rix_net_ipv6_push(&packet, ipv6_source, ipv6_destination,
                             RIX_NET_IP_PROTO_ICMPV6, 64, 0x2a, 0x12345) == 0);
    assert(rix_net_ipv6_pull(&packet, &ipv6) == 0);
    assert(ipv6.next_header == RIX_NET_IP_PROTO_ICMPV6 && ipv6.hop_limit == 64 &&
           ipv6.traffic_class == 0x2a && ipv6.flow_label == 0x12345);
    assert(rix_net_icmpv6_echo_pull(&packet, ipv6.source, ipv6.destination, &echo6) == 0);
    assert(echo6.type == RIX_NET_ICMPV6_ECHO_REQUEST && echo6.identifier == 12 &&
           echo6.sequence == 3 && rix_net_packet_length(&packet) == sizeof(ipv6_payload));

    rix_net_packet_init(&packet);
    assert(rix_net_icmpv6_neighbor_solicit_push(&packet, ipv6_source, ipv6_destination,
                                                 ipv6_destination) == 0);
    uint8_t nd_type = 0, nd_target[16] = {0}; uint32_t nd_flags = 0;
    assert(rix_net_icmpv6_neighbor_pull(&packet, ipv6_source, ipv6_destination,
                                        &nd_type, &nd_flags, nd_target) == 0);
    assert(nd_type == RIX_NET_ICMPV6_NEIGHBOR_SOLICIT && nd_flags == 0 &&
           memcmp(nd_target, ipv6_destination, 16) == 0);
    rix_net_packet_init(&packet);
    assert(rix_net_icmpv6_neighbor_advert_push(&packet, ipv6_destination, ipv6_source,
                                                0x60000000u, ipv6_destination) == 0);
    assert(rix_net_icmpv6_neighbor_pull(&packet, ipv6_destination, ipv6_source,
                                        &nd_type, &nd_flags, nd_target) == 0);
    assert(nd_type == RIX_NET_ICMPV6_NEIGHBOR_ADVERT && nd_flags == 0x60000000u);
    ((uint8_t *)rix_net_packet_data(&packet))[1] ^= 1u;
    assert(rix_net_icmpv6_neighbor_pull(&packet, ipv6_destination, ipv6_source,
                                        &nd_type, &nd_flags, nd_target) != 0);

    rix_net_packet_init(&packet);
    assert(rix_net_icmpv6_router_solicit_push(&packet, ipv6_source, ipv6_destination) == 0);
    assert(rix_net_packet_length(&packet) == 8);
    uint8_t ra_prefix[16] = {0x20,0x01,0x0d,0xb8,0x12,0x34,0,0,0,0,0,0,0,0,0,0};
    uint8_t ra_got_prefix[16] = {0}; uint8_t ra_hop = 0, ra_flags = 0, ra_prefix_len = 0;
    uint16_t ra_lifetime = 0;
    rix_net_packet_init(&packet);
    assert(rix_net_icmpv6_router_advert_push(&packet, ipv6_destination, ipv6_source,
                                             64, 0xc0, 1800, ra_prefix, 64) == 0);
    assert(rix_net_icmpv6_router_advert_pull(&packet, ipv6_destination, ipv6_source,
                                             &ra_hop, &ra_flags, &ra_lifetime,
                                             ra_got_prefix, &ra_prefix_len) == 0);
    assert(ra_hop == 64 && ra_flags == 0xc0 && ra_lifetime == 1800 && ra_prefix_len == 64 &&
           memcmp(ra_got_prefix, ra_prefix, 16) == 0);
    const uint8_t iid[8] = {0x02,0xaa,0xbb,0xff,0xfe,0xcc,0xdd,0xee};
    uint8_t slaac[16] = {0};
    assert(rix_net_ipv6_slaac_address(ra_prefix, 64, iid, slaac) == 0 &&
           memcmp(slaac, ra_prefix, 8) == 0 && memcmp(slaac + 8, iid, 8) == 0);
    const uint8_t ipv6_test_mac[6] = {0x00,0x11,0x22,0x33,0x44,0x55};
    uint8_t link_local[16] = {0};
    assert(rix_net_ipv6_link_local_from_mac(ipv6_test_mac, link_local) == 0 &&
           link_local[0] == 0xfe && link_local[1] == 0x80 && link_local[8] == 0x02 &&
           link_local[11] == 0xff && link_local[12] == 0xfe);
    rix_net_ipv6_neighbor_cache_t neighbors;
    uint8_t resolved_mac[6] = {0};
    rix_net_ipv6_neighbor_init(&neighbors);
    assert(rix_net_ipv6_neighbor_learn(&neighbors, ipv6_destination, ipv6_test_mac, 100) == 0);
    assert(rix_net_ipv6_neighbor_lookup(&neighbors, ipv6_destination, 99, resolved_mac) == 0 &&
           memcmp(resolved_mac, ipv6_test_mac, 6) == 0);
    assert(rix_net_ipv6_neighbor_expire(&neighbors, 100) == 1);
    assert(rix_net_ipv6_neighbor_lookup(&neighbors, ipv6_destination, 100, resolved_mac) != 0);

    rix_net_packet_init(&packet);
    assert(rix_net_udp_push(&packet, 0xc0a80102u, 0xc0a80101u, 12000, 53,
                            payload, sizeof(payload)) == 0);
    rix_net_udp_header_t udp;
    assert(rix_net_udp_pull(&packet, 0xc0a80102u, 0xc0a80101u, &udp) == 0);
    assert(udp.source_port == 12000 && udp.destination_port == 53 && udp.length == 12);
    assert(rix_net_packet_length(&packet) == sizeof(payload));
    assert(memcmp(rix_net_packet_data(&packet), payload, sizeof(payload)) == 0);
    rix_net_packet_init(&packet);
    assert(rix_net_udp_push(&packet, 0xc0a80102u, 0xc0a80101u, 12000, 53,
                            payload, sizeof(payload)) == 0);
    ((uint8_t *)rix_net_packet_data(&packet))[3] ^= 1u;
    assert(rix_net_udp_pull(&packet, 0xc0a80102u, 0xc0a80101u, &udp) != 0);
    rix_net_packet_init(&packet);
    packet.start = 0;
    assert(rix_net_udp_push(&packet, 0xc0a80102u, 0xc0a80101u, 12000, 53,
                            payload, sizeof(payload)) != 0 && packet.length == 0);

    rix_net_tcp_header_t tcp;
    rix_net_packet_init(&packet);
    assert(rix_net_tcp_push(&packet, 0xc0a80102u, 0xc0a80101u, 40000, 80,
                            10, 20, RIX_NET_TCP_FLAG_SYN, 4096, 0, 0) == 0);
    assert(rix_net_tcp_pull(&packet, 0xc0a80102u, 0xc0a80101u, &tcp) == 0);
    assert(tcp.source_port == 40000 && tcp.destination_port == 80 &&
           tcp.sequence == 10 && tcp.acknowledgment == 20 &&
           tcp.flags == RIX_NET_TCP_FLAG_SYN && rix_net_packet_length(&packet) == 0);
    rix_net_packet_init(&packet);
    assert(rix_net_tcp_push(&packet, 0xc0a80102u, 0xc0a80101u, 40000, 80,
                            11, 21, RIX_NET_TCP_FLAG_ACK | RIX_NET_TCP_FLAG_PSH,
                            4096, payload, sizeof(payload)) == 0);
    assert(rix_net_tcp_pull(&packet, 0xc0a80102u, 0xc0a80101u, &tcp) == 0);
    assert(tcp.flags == (RIX_NET_TCP_FLAG_ACK | RIX_NET_TCP_FLAG_PSH));
    assert(rix_net_packet_length(&packet) == sizeof(payload));
    rix_net_packet_init(&packet);
    packet.start = 0;
    assert(rix_net_tcp_push(&packet, 0xc0a80102u, 0xc0a80101u, 40000, 80,
                            1, 1, RIX_NET_TCP_FLAG_ACK, 4096,
                            payload, sizeof(payload)) != 0 && packet.length == 0);

    rix_net_loopback_t loopback;
    rix_net_loopback_init(&loopback);
    rix_net_packet_init(&packet);
    assert(rix_net_icmp_echo_push(&packet, RIX_NET_ICMP_ECHO_REQUEST, 11, 1,
                                  payload, sizeof(payload)) == 0);
    assert(rix_net_loopback_transmit(&loopback, &packet) == 0);
    assert(rix_net_loopback_pending(&loopback) == 1);
    rix_net_packet_t received;
    rix_net_packet_init(&received);
    assert(rix_net_loopback_receive(&loopback, &received) == 0);
    assert(rix_net_loopback_pending(&loopback) == 0);
    assert(rix_net_icmp_echo_pull(&received, &echo) == 0);
    assert(echo.identifier == 11 && echo.sequence == 1);
    assert(rix_net_loopback_receive(&loopback, &received) != 0);

    rix_net_socket_table_t sockets;
    uint8_t message[] = {'p', 'i', 'n', 'g'};
    uint8_t reply[128] = {0};
    rix_net_endpoint_t peer_endpoint;
    rix_net_socket_table_init(&sockets);
    int server = rix_net_socket_open(&sockets, RIX_NET_SOCKET_RAW_ICMP);
    int client = rix_net_socket_open(&sockets, RIX_NET_SOCKET_RAW_ICMP);
    assert(server >= 0 && client >= 0);
    assert(rix_net_socket_bind(&sockets, server, (rix_net_endpoint_t){RIX_NET_SOCKET_LOOPBACK, 7}) == 0);
    assert(rix_net_socket_connect(&sockets, client, (rix_net_endpoint_t){RIX_NET_SOCKET_LOOPBACK, 7}) != 0);
    assert(rix_net_socket_receive(&sockets, server, reply, sizeof(reply), &peer_endpoint) == -3);
    assert(rix_net_socket_send(&sockets, client, message, sizeof(message),
                               (rix_net_endpoint_t){RIX_NET_SOCKET_LOOPBACK, 7}) == 4);
    assert(rix_net_socket_receive(&sockets, server, reply, sizeof(reply), &peer_endpoint) == 4);
    assert(memcmp(reply, message, sizeof(message)) == 0);
    assert(peer_endpoint.address == RIX_NET_SOCKET_LOOPBACK);

    int udp_server = rix_net_socket_open(&sockets, RIX_NET_SOCKET_UDP);
    int udp_client = rix_net_socket_open(&sockets, RIX_NET_SOCKET_UDP);
    assert(udp_server >= 0 && udp_client >= 0);
    assert(rix_net_socket_bind(&sockets, udp_server,
                               (rix_net_endpoint_t){RIX_NET_SOCKET_LOOPBACK, 5353}) == 0);
    assert(rix_net_socket_send(&sockets, udp_client, message, sizeof(message),
                               (rix_net_endpoint_t){RIX_NET_SOCKET_LOOPBACK, 5353}) == 4);
    assert(rix_net_socket_receive(&sockets, udp_server, reply, sizeof(reply), &peer_endpoint) == 4);
    assert(memcmp(reply, message, sizeof(message)) == 0);

    int http = rix_net_socket_open(&sockets, RIX_NET_SOCKET_TCP);
    assert(http >= 0);
    assert(rix_net_socket_bind(&sockets, http,
                               (rix_net_endpoint_t){RIX_NET_SOCKET_LOOPBACK, 40000}) == 0);
    assert(rix_net_socket_connect(&sockets, http,
                                  (rix_net_endpoint_t){RIX_NET_SOCKET_LOOPBACK, 80}) == 0);
    static const char request[] = "GET / HTTP/1.0\r\nHost: localhost\r\n\r\n";
    assert(rix_net_socket_send(&sockets, http, request, sizeof(request) - 1,
                               (rix_net_endpoint_t){RIX_NET_SOCKET_LOOPBACK, 80}) ==
           (int)(sizeof(request) - 1));
    int received_length = rix_net_socket_receive(&sockets, http, reply, sizeof(reply),
                                                  &peer_endpoint);
    assert(received_length > 0);
    assert(contains_bytes(reply, (size_t)received_length, "HTTP/1.0 200 OK"));
    assert(contains_bytes(reply, (size_t)received_length, "Content-Type: text/html"));
    assert(contains_bytes(reply, (size_t)received_length,
                           "<html><body><h1>Hello RixuriOS</h1></body></html>\n"));
    assert(peer_endpoint.port == 80);
    assert(rix_net_socket_close(&sockets, http) == 0);
    /* Wire TCP dispatch: SYN-ACK accept on a SYN_SENT socket. */
    int wire = rix_net_socket_open(&sockets, RIX_NET_SOCKET_TCP);
    assert(wire >= 0);
    rix_net_socket_t *wsock = &sockets.sockets[wire];
    wsock->local = (rix_net_endpoint_t){0xc0a80102u, 40000};
    wsock->peer = (rix_net_endpoint_t){0xc0a80101u, 80};
    assert(rix_tcp_transition(&wsock->tcp, RIX_TCP_EVENT_ACTIVE_OPEN) == 0);
    wsock->tcp.sequence = 7;
    wsock->tcp.acknowledgment = 0;
    rix_net_packet_init(&packet);
    assert(rix_net_tcp_push(&packet, 0xc0a80101u, 0xc0a80102u, 80, 40000,
                            100, 8,
                            RIX_NET_TCP_FLAG_SYN | RIX_NET_TCP_FLAG_ACK,
                            4096, 0, 0) == 0);
    assert(rix_net_ipv4_push(&packet, 0xc0a80101u, 0xc0a80102u,
                             RIX_NET_IP_PROTO_TCP, 64, 0, RIX_NET_IP_FLAG_DF) == 0);
    stub_frame = packet;
    stub_frame_valid = 1;
    uint8_t wireout[64] = {0};
    /* No data queued yet; the SYN-ACK carries none, so receive still waits. */
    assert(rix_net_socket_receive(&sockets, wire, wireout, sizeof(wireout), 0) == -3);
    assert(wsock->tcp.state == RIX_TCP_ESTABLISHED);
    assert(wsock->tcp.state == RIX_TCP_ESTABLISHED);
    assert(wsock->connected == 1);
    assert(wsock->tcp.sequence == 8 && wsock->tcp.acknowledgment == 101);
    /* Wire TCP dispatch: in-order data is queued and acknowledged. */
    rix_net_packet_init(&packet);
    static const uint8_t hello[] = {'h', 'i'};
    assert(rix_net_tcp_push(&packet, 0xc0a80101u, 0xc0a80102u, 80, 40000,
                            101, 8,
                            RIX_NET_TCP_FLAG_ACK | RIX_NET_TCP_FLAG_PSH,
                            4096, hello, sizeof(hello)) == 0);
    assert(rix_net_ipv4_push(&packet, 0xc0a80101u, 0xc0a80102u,
                             RIX_NET_IP_PROTO_TCP, 64, 0, RIX_NET_IP_FLAG_DF) == 0);
    stub_frame = packet;
    stub_frame_valid = 1;
    assert(rix_net_socket_receive(&sockets, wire, wireout, sizeof(wireout),
                                  &peer_endpoint) == 2);
    assert(memcmp(wireout, "hi", 2) == 0);
    assert(peer_endpoint.address == 0xc0a80101u && peer_endpoint.port == 80);
    assert(wsock->tcp.acknowledgment == 103);
    /* Out-of-order data is dropped, not queued. */
    rix_net_packet_init(&packet);
    assert(rix_net_tcp_push(&packet, 0xc0a80101u, 0xc0a80102u, 80, 40000,
                            999, 8,
                            RIX_NET_TCP_FLAG_ACK | RIX_NET_TCP_FLAG_PSH,
                            4096, hello, sizeof(hello)) == 0);
    assert(rix_net_ipv4_push(&packet, 0xc0a80101u, 0xc0a80102u,
                             RIX_NET_IP_PROTO_TCP, 64, 0, RIX_NET_IP_FLAG_DF) == 0);
    stub_frame = packet;
    stub_frame_valid = 1;
    assert(rix_net_socket_receive(&sockets, wire, wireout, sizeof(wireout), 0) == -3);
    assert(wsock->tcp.acknowledgment == 103);
    /* FIN moves to CLOSE_WAIT and a drained socket reads EOF. */
    rix_net_packet_init(&packet);
    assert(rix_net_tcp_push(&packet, 0xc0a80101u, 0xc0a80102u, 80, 40000,
                            103, 8, RIX_NET_TCP_FLAG_FIN,
                            4096, 0, 0) == 0);
    assert(rix_net_ipv4_push(&packet, 0xc0a80101u, 0xc0a80102u,
                             RIX_NET_IP_PROTO_TCP, 64, 0, RIX_NET_IP_FLAG_DF) == 0);
    stub_frame = packet;
    stub_frame_valid = 1;
    assert(rix_net_socket_receive(&sockets, wire, wireout, sizeof(wireout), 0) == 0);
    assert(wsock->tcp.state == RIX_TCP_CLOSE_WAIT);
    assert(rix_net_socket_receive(&sockets, wire, wireout, sizeof(wireout), 0) == 0);
    assert(rix_net_socket_close(&sockets, wire) == 0);
    /* Wire ICMP dispatch: an echo reply is queued to raw sockets. */
    int pinger = rix_net_socket_open(&sockets, RIX_NET_SOCKET_RAW_ICMP);
    assert(pinger >= 0);
    rix_net_packet_init(&packet);
    assert(rix_net_icmp_echo_push(&packet, RIX_NET_ICMP_ECHO_REPLY, 0x1234, 7,
                                 0, 0) == 0);
    assert(rix_net_ipv4_push(&packet, 0xc0a80101u, 0xc0a80102u,
                             RIX_NET_IP_PROTO_ICMP, 64, 0, RIX_NET_IP_FLAG_DF) == 0);
    stub_frame = packet;
    stub_frame_valid = 1;
    assert(rix_net_socket_receive(&sockets, pinger, wireout, sizeof(wireout),
                                  &peer_endpoint) == 8);
    assert(wireout[0] == 0 && wireout[1] == 0);
    assert(wireout[4] == 0x12 && wireout[5] == 0x34 && wireout[7] == 7);
    assert(peer_endpoint.address == 0xc0a80101u);
    /* A corrupted echo reply is dropped, not queued. */
    rix_net_packet_init(&packet);
    assert(rix_net_icmp_echo_push(&packet, RIX_NET_ICMP_ECHO_REPLY, 0x1234, 7,
                                 0, 0) == 0);
    assert(rix_net_ipv4_push(&packet, 0xc0a80101u, 0xc0a80102u,
                             RIX_NET_IP_PROTO_ICMP, 64, 0, RIX_NET_IP_FLAG_DF) == 0);
    ((uint8_t *)rix_net_packet_data(&packet))[23] ^= 1u;
    stub_frame = packet;
    stub_frame_valid = 1;
    assert(rix_net_socket_receive(&sockets, pinger, wireout, sizeof(wireout),
                                  0) == -3);
    assert(rix_net_socket_close(&sockets, pinger) == 0);
    assert(rix_net_socket_close(&sockets, udp_client) == 0);
    assert(rix_net_socket_close(&sockets, udp_server) == 0);
    assert(rix_net_socket_close(&sockets, client) == 0);
    assert(rix_net_socket_close(&sockets, server) == 0);
    /* DHCP: discover/request build + offer parse round-trip. */
    static const uint8_t test_mac[6] = {0x52, 0x55, 0x0a, 0x00, 0x02, 0x0f};
    rix_net_packet_init(&packet);
    assert(rix_net_dhcp_build_discover(&packet, 0x12345678u, test_mac) == 0);
    assert(rix_net_packet_length(&packet) == RIX_DHCP_MIN_SIZE);
    {
        const uint8_t *dhcp = rix_net_packet_data(&packet);
        assert(dhcp[0] == RIX_DHCP_OP_REQUEST && dhcp[1] == 1 && dhcp[2] == 6);
        assert(dhcp[4] == 0x12 && dhcp[5] == 0x34 && dhcp[6] == 0x56 && dhcp[7] == 0x78);
        assert(dhcp[10] == 0x80);
        assert(memcmp(dhcp + 28, test_mac, 6) == 0);
        assert(dhcp[236] == 99 && dhcp[237] == 130 && dhcp[238] == 83 && dhcp[239] == 99);
        assert(dhcp[240] == 53 && dhcp[241] == 1 && dhcp[242] == RIX_DHCP_MSG_DISCOVER);
    }
    assert(rix_net_dhcp_build_discover(0, 0x12345678u, test_mac) != 0);
    assert(rix_net_dhcp_build_discover(&packet, 0, test_mac) != 0);
    assert(rix_net_dhcp_build_request(0, 1, test_mac, 1) != 0);
    assert(rix_net_dhcp_build_request(&packet, 1, 0, 1) != 0);
    rix_dhcp_offer_t offer;
    rix_net_packet_init(&packet);
    assert(rix_net_dhcp_build_discover(&packet, 0x9abcdef0u, test_mac) == 0);
    {
        uint8_t *dhcp = (uint8_t *)rix_net_packet_data(&packet);
        dhcp[0] = RIX_DHCP_OP_REPLY;
        dhcp[16] = 10;
        dhcp[17] = 0;
        dhcp[18] = 2;
        dhcp[19] = 15;
        uint8_t *opt = dhcp + 240;
        opt[0] = 53;
        opt[1] = 1;
        opt[2] = RIX_DHCP_MSG_OFFER;
        opt[3] = 1;
        opt[4] = 4;
        opt[5] = 255;
        opt[6] = 255;
        opt[7] = 255;
        opt[8] = 0;
        opt[9] = 3;
        opt[10] = 4;
        opt[11] = 10;
        opt[12] = 0;
        opt[13] = 2;
        opt[14] = 2;
        opt[15] = 6;
        opt[16] = 4;
        opt[17] = 10;
        opt[18] = 0;
        opt[19] = 2;
        opt[20] = 3;
        opt[21] = 54;
        opt[22] = 4;
        opt[23] = 10;
        opt[24] = 0;
        opt[25] = 2;
        opt[26] = 2;
        opt[27] = 51;
        opt[28] = 4;
        opt[29] = 0;
        opt[30] = 0;
        opt[31] = 0x0e;
        opt[32] = 0x10;
        opt[33] = 255;
        assert(rix_net_dhcp_parse_reply(&packet, 0x9abcdef0u, RIX_DHCP_MSG_OFFER, &offer) == 0);
        assert(offer.address == 0x0a00020fu);
        assert(offer.has_netmask && offer.netmask == 0xffffff00u);
        assert(offer.has_gateway && offer.gateway == 0x0a000202u);
        assert(offer.has_dns && offer.dns == 0x0a000203u);
        assert(offer.server == 0x0a000202u);
        assert(offer.lease_seconds == 3600u);
        assert(rix_net_dhcp_parse_reply(&packet, 0x11111111u, RIX_DHCP_MSG_OFFER, &offer) != 0);
        assert(rix_net_dhcp_parse_reply(&packet, 0x9abcdef0u, RIX_DHCP_MSG_ACK, &offer) != 0);
        dhcp[237] ^= 1u;
        assert(rix_net_dhcp_parse_reply(&packet, 0x9abcdef0u, RIX_DHCP_MSG_OFFER, &offer) != 0);
        dhcp[237] ^= 1u;
        dhcp[16] = 0;
        dhcp[17] = 0;
        dhcp[18] = 0;
        dhcp[19] = 0;
        assert(rix_net_dhcp_parse_reply(&packet, 0x9abcdef0u, RIX_DHCP_MSG_OFFER, &offer) != 0);
    }
    rix_net_packet_init(&packet);
    assert(rix_net_dhcp_build_request(&packet, 0x9abcdef0u, test_mac, 0x0a00020fu) == 0);
    {
        const uint8_t *dhcp = rix_net_packet_data(&packet);
        assert(dhcp[240] == 53 && dhcp[241] == 1 && dhcp[242] == RIX_DHCP_MSG_REQUEST);
        assert(dhcp[243] == 50 && dhcp[244] == 4 && dhcp[245] == 10 &&
               dhcp[246] == 0 && dhcp[247] == 2 && dhcp[248] == 15);
        assert(rix_net_packet_length(&packet) == RIX_DHCP_MIN_SIZE);
    }
    rix_net_packet_init(&packet);
    assert(rix_net_dhcp_build_request_for_server(&packet, 0x9abcdef0u, test_mac,
                                                 0x0a00020fu, 0x0a000202u) == 0);
    {
        const uint8_t *dhcp = rix_net_packet_data(&packet);
        size_t found_server = 0, found_client = 0;
        for (size_t i = 240; i + 1 < rix_net_packet_length(&packet); ) {
            uint8_t code = dhcp[i++];
            if (code == 255) break;
            if (code == 0) continue;
            uint8_t length = dhcp[i++];
            if (i + length > rix_net_packet_length(&packet)) break;
            if (code == 54 && length == 4 && dhcp[i] == 10 && dhcp[i + 3] == 2)
                found_server = 1;
            if (code == 61 && length == 7 && dhcp[i] == 1 && dhcp[i + 6] == 0x0f)
                found_client = 1;
            i += length;
        }
        assert(found_server && found_client);
    }
    return 0;
}
