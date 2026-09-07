#include "kernel/net/net.h"
#include "kernel/net/ethernet.h"
#include "kernel/net/arp.h"
#include "kernel/net/ipv4.h"
#include "kernel/net/loopback.h"
#include "kernel/net/socket.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

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
    uint8_t reply[8] = {0};
    rix_net_endpoint_t peer_endpoint;
    rix_net_socket_table_init(&sockets);
    int server = rix_net_socket_open(&sockets, RIX_NET_SOCKET_RAW_ICMP);
    int client = rix_net_socket_open(&sockets, RIX_NET_SOCKET_RAW_ICMP);
    assert(server >= 0 && client >= 0);
    assert(rix_net_socket_bind(&sockets, server, (rix_net_endpoint_t){RIX_NET_SOCKET_LOOPBACK, 7}) == 0);
    assert(rix_net_socket_connect(&sockets, client, (rix_net_endpoint_t){RIX_NET_SOCKET_LOOPBACK, 7}) == 0);
    assert(rix_net_socket_receive(&sockets, server, reply, sizeof(reply), &peer_endpoint) == -3);
    assert(rix_net_socket_send(&sockets, client, message, sizeof(message), (rix_net_endpoint_t){0, 0}) == 4);
    assert(rix_net_socket_receive(&sockets, server, reply, sizeof(reply), &peer_endpoint) == 4);
    assert(memcmp(reply, message, sizeof(message)) == 0);
    assert(peer_endpoint.address == RIX_NET_SOCKET_LOOPBACK);
    assert(rix_net_socket_close(&sockets, client) == 0);
    assert(rix_net_socket_close(&sockets, server) == 0);

    return 0;
}
