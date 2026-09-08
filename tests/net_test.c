#include "kernel/net/net.h"
#include "kernel/net/ethernet.h"
#include "kernel/net/arp.h"
#include "kernel/net/ipv4.h"
#include "kernel/net/loopback.h"
#include "kernel/net/socket.h"
#include "kernel/net/tcp.h"
#include "kernel/net/udp.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

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
    assert(rix_net_ethertype_supported(RIX_NET_ETHERTYPE_IPV6) == 0);

    rix_net_arp_cache_t cache;
    uint8_t learned[6];
    rix_net_arp_init(&cache);
    assert(rix_net_arp_learn(&cache, 0xc0a80101u, source, 10, 20) == 0);
    assert(rix_net_arp_lookup(&cache, 0xc0a80101u, 29, learned) == 0);
    assert(memcmp(learned, source, 6) == 0);
    assert(rix_net_arp_lookup(&cache, 0xc0a80101u, 30, learned) != 0);
    assert(rix_net_arp_expire(&cache, 30) == 1);

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
    assert(rix_net_socket_close(&sockets, udp_client) == 0);
    assert(rix_net_socket_close(&sockets, udp_server) == 0);
    assert(rix_net_socket_close(&sockets, client) == 0);
    assert(rix_net_socket_close(&sockets, server) == 0);
    return 0;
}
