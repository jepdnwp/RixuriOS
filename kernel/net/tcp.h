#pragma once
#include "net.h"
#include <stddef.h>
#include <stdint.h>

#define RIX_NET_TCP_HEADER_LENGTH 20u
#define RIX_NET_TCP_FLAG_FIN 0x0001u
#define RIX_NET_TCP_FLAG_SYN 0x0002u
#define RIX_NET_TCP_FLAG_RST 0x0004u
#define RIX_NET_TCP_FLAG_PSH 0x0008u
#define RIX_NET_TCP_FLAG_ACK 0x0010u
#define RIX_NET_TCP_FLAG_URG 0x0020u

typedef enum {
    RIX_TCP_CLOSED = 0,
    RIX_TCP_LISTEN,
    RIX_TCP_SYN_SENT,
    RIX_TCP_SYN_RECEIVED,
    RIX_TCP_ESTABLISHED,
    RIX_TCP_FIN_WAIT_1,
    RIX_TCP_FIN_WAIT_2,
    RIX_TCP_CLOSE_WAIT,
    RIX_TCP_LAST_ACK,
    RIX_TCP_TIME_WAIT
} rix_tcp_state_t;

typedef enum {
    RIX_TCP_EVENT_PASSIVE_OPEN = 1,
    RIX_TCP_EVENT_ACTIVE_OPEN,
    RIX_TCP_EVENT_SYN,
    RIX_TCP_EVENT_SYN_ACK,
    RIX_TCP_EVENT_ACK,
    RIX_TCP_EVENT_FIN,
    RIX_TCP_EVENT_CLOSE
} rix_tcp_event_t;

typedef struct {
    rix_tcp_state_t state;
    uint32_t sequence;
    uint32_t acknowledgment;
} rix_tcp_control_t;

typedef struct {
    uint16_t source_port;
    uint16_t destination_port;
    uint32_t sequence;
    uint32_t acknowledgment;
    uint16_t flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} rix_net_tcp_header_t;

void rix_tcp_init(rix_tcp_control_t *control);
int rix_tcp_transition(rix_tcp_control_t *control, rix_tcp_event_t event);
int rix_tcp_is_connected(const rix_tcp_control_t *control);

int rix_net_tcp_push(rix_net_packet_t *packet, uint32_t source_ip,
                     uint32_t destination_ip, uint16_t source_port,
                     uint16_t destination_port, uint32_t sequence,
                     uint32_t acknowledgment, uint16_t flags, uint16_t window,
                     const void *payload, size_t payload_length);
int rix_net_tcp_pull(rix_net_packet_t *packet, uint32_t source_ip,
                     uint32_t destination_ip, rix_net_tcp_header_t *header);
