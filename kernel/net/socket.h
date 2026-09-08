#pragma once
#include "net.h"
#include "tcp.h"
#include <stddef.h>
#include <stdint.h>

#define RIX_NET_SOCKET_MAX 16u
#define RIX_NET_SOCKET_QUEUE 8u
#define RIX_NET_SOCKET_ANY 0u
#define RIX_NET_SOCKET_LOOPBACK 0x7f000001u
#define RIX_NET_SOCKET_NONBLOCK 0x1u

typedef enum {
    RIX_NET_SOCKET_UDP = 1,
    RIX_NET_SOCKET_RAW_ICMP = 2,
    RIX_NET_SOCKET_TCP = 3
} rix_net_socket_type_t;

typedef struct {
    uint32_t address;
    uint16_t port;
} rix_net_endpoint_t;

typedef struct {
    rix_net_packet_t packets[RIX_NET_SOCKET_QUEUE];
    rix_net_endpoint_t peers[RIX_NET_SOCKET_QUEUE];
    size_t head;
    size_t count;
} rix_net_socket_queue_t;

typedef struct {
    uint8_t used;
    uint8_t connected;
    uint8_t flags;
    rix_net_socket_type_t type;
    rix_net_endpoint_t local;
    rix_net_endpoint_t peer;
    rix_tcp_control_t tcp;
    rix_net_socket_queue_t receive;
} rix_net_socket_t;

typedef struct {
    rix_net_socket_t sockets[RIX_NET_SOCKET_MAX];
} rix_net_socket_table_t;

void rix_net_socket_table_init(rix_net_socket_table_t *table);
int rix_net_socket_open(rix_net_socket_table_t *table, rix_net_socket_type_t type);
int rix_net_socket_close(rix_net_socket_table_t *table, int descriptor);
int rix_net_socket_bind(rix_net_socket_table_t *table, int descriptor,
                        rix_net_endpoint_t endpoint);
int rix_net_socket_connect(rix_net_socket_table_t *table, int descriptor,
                           rix_net_endpoint_t endpoint);
int rix_net_socket_send(rix_net_socket_table_t *table, int descriptor,
                        const void *data, size_t length,
                        rix_net_endpoint_t destination);
int rix_net_socket_receive(rix_net_socket_table_t *table, int descriptor,
                           void *data, size_t capacity,
                           rix_net_endpoint_t *source);
