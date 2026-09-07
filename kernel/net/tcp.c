#include "tcp.h"

void rix_tcp_init(rix_tcp_control_t *control) {
    if (!control) return;
    control->state = RIX_TCP_CLOSED;
    control->sequence = 0;
    control->acknowledgment = 0;
}

int rix_tcp_transition(rix_tcp_control_t *control, rix_tcp_event_t event) {
    if (!control) return -1;
    rix_tcp_state_t next = control->state;
    switch (control->state) {
    case RIX_TCP_CLOSED:
        if (event == RIX_TCP_EVENT_PASSIVE_OPEN) next = RIX_TCP_LISTEN;
        else if (event == RIX_TCP_EVENT_ACTIVE_OPEN) next = RIX_TCP_SYN_SENT;
        else return -1;
        break;
    case RIX_TCP_LISTEN:
        if (event == RIX_TCP_EVENT_SYN) next = RIX_TCP_SYN_RECEIVED;
        else if (event == RIX_TCP_EVENT_CLOSE) next = RIX_TCP_CLOSED;
        else return -1;
        break;
    case RIX_TCP_SYN_SENT:
        if (event == RIX_TCP_EVENT_SYN_ACK) next = RIX_TCP_ESTABLISHED;
        else if (event == RIX_TCP_EVENT_CLOSE) next = RIX_TCP_CLOSED;
        else return -1;
        break;
    case RIX_TCP_SYN_RECEIVED:
        if (event == RIX_TCP_EVENT_ACK) next = RIX_TCP_ESTABLISHED;
        else if (event == RIX_TCP_EVENT_CLOSE) next = RIX_TCP_FIN_WAIT_1;
        else return -1;
        break;
    case RIX_TCP_ESTABLISHED:
        if (event == RIX_TCP_EVENT_CLOSE) next = RIX_TCP_FIN_WAIT_1;
        else if (event == RIX_TCP_EVENT_FIN) next = RIX_TCP_CLOSE_WAIT;
        else if (event != RIX_TCP_EVENT_ACK) return -1;
        break;
    case RIX_TCP_FIN_WAIT_1:
        if (event == RIX_TCP_EVENT_ACK) next = RIX_TCP_FIN_WAIT_2;
        else if (event == RIX_TCP_EVENT_FIN) next = RIX_TCP_TIME_WAIT;
        else return -1;
        break;
    case RIX_TCP_FIN_WAIT_2:
        if (event == RIX_TCP_EVENT_FIN) next = RIX_TCP_TIME_WAIT;
        else return -1;
        break;
    case RIX_TCP_CLOSE_WAIT:
        if (event == RIX_TCP_EVENT_CLOSE) next = RIX_TCP_LAST_ACK;
        else return -1;
        break;
    case RIX_TCP_LAST_ACK:
        if (event == RIX_TCP_EVENT_ACK) next = RIX_TCP_CLOSED;
        else return -1;
        break;
    case RIX_TCP_TIME_WAIT:
        if (event == RIX_TCP_EVENT_CLOSE) next = RIX_TCP_CLOSED;
        else return -1;
        break;
    default: return -1;
    }
    control->state = next;
    return 0;
}

int rix_tcp_is_connected(const rix_tcp_control_t *control) {
    return control && control->state == RIX_TCP_ESTABLISHED;
}
