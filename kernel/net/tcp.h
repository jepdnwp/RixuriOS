#pragma once
#include <stdint.h>

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

void rix_tcp_init(rix_tcp_control_t *control);
int rix_tcp_transition(rix_tcp_control_t *control, rix_tcp_event_t event);
int rix_tcp_is_connected(const rix_tcp_control_t *control);
