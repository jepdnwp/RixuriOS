#include "tcp.h"
#include "ipv4.h"

static void put_be16(uint8_t *destination, uint16_t value) {
    destination[0] = (uint8_t)(value >> 8);
    destination[1] = (uint8_t)value;
}

static void put_be32(uint8_t *destination, uint32_t value) {
    destination[0] = (uint8_t)(value >> 24);
    destination[1] = (uint8_t)(value >> 16);
    destination[2] = (uint8_t)(value >> 8);
    destination[3] = (uint8_t)value;
}

static uint16_t get_be16(const uint8_t *source) {
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static uint32_t get_be32(const uint8_t *source) {
    return ((uint32_t)source[0] << 24) | ((uint32_t)source[1] << 16) |
           ((uint32_t)source[2] << 8) | source[3];
}

static uint16_t pseudo_checksum(uint32_t source_ip, uint32_t destination_ip,
                                const uint8_t *segment, size_t length) {
    uint32_t sum = 0;
    sum += source_ip >> 16;
    sum += source_ip & 0xffffu;
    sum += destination_ip >> 16;
    sum += destination_ip & 0xffffu;
    sum += RIX_NET_IP_PROTO_TCP;
    sum += (uint32_t)length;
    while (length >= 2) {
        sum += ((uint32_t)segment[0] << 8) | segment[1];
        segment += 2;
        length -= 2;
    }
    if (length) sum += (uint32_t)segment[0] << 8;
    return rix_net_checksum_add(sum);
}

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

int rix_net_tcp_push(rix_net_packet_t *packet, uint32_t source_ip,
                     uint32_t destination_ip, uint16_t source_port,
                     uint16_t destination_port, uint32_t sequence,
                     uint32_t acknowledgment, uint16_t flags, uint16_t window,
                     const void *payload, size_t payload_length) {
    if (!packet || !source_ip || !destination_ip || !source_port ||
        !destination_port || (payload_length && !payload) ||
        payload_length > 65515u || (flags & ~0x01ffu)) return -1;
    if (packet->start > RIX_NET_FRAME_CAPACITY ||
        packet->length > RIX_NET_FRAME_CAPACITY - packet->start ||
        packet->start < RIX_NET_TCP_HEADER_LENGTH ||
        payload_length > RIX_NET_FRAME_CAPACITY - packet->start -
                         packet->length - RIX_NET_TCP_HEADER_LENGTH) return -1;
    uint8_t *body = 0;
    if (rix_net_packet_put(packet, payload_length, (void **)&body) != 0) return -1;
    for (size_t i = 0; i < payload_length; ++i) body[i] = ((const uint8_t *)payload)[i];
    uint8_t *header = 0;
    if (rix_net_packet_push(packet, RIX_NET_TCP_HEADER_LENGTH, (void **)&header) != 0) return -1;
    put_be16(header + 0, source_port);
    put_be16(header + 2, destination_port);
    put_be32(header + 4, sequence);
    put_be32(header + 8, acknowledgment);
    header[12] = 5u << 4;
    header[13] = (uint8_t)flags;
    put_be16(header + 14, window);
    put_be16(header + 16, 0);
    put_be16(header + 18, 0);
    uint16_t checksum = pseudo_checksum(source_ip, destination_ip,
                                        rix_net_packet_data(packet),
                                        rix_net_packet_length(packet));
    put_be16(header + 16, checksum);
    return 0;
}

int rix_net_tcp_pull(rix_net_packet_t *packet, uint32_t source_ip,
                     uint32_t destination_ip, rix_net_tcp_header_t *header) {
    if (!packet || !header || !source_ip || !destination_ip ||
        rix_net_packet_length(packet) < RIX_NET_TCP_HEADER_LENGTH) return -1;
    const uint8_t *bytes = rix_net_packet_data(packet);
    size_t header_length = (size_t)(bytes[12] >> 4) * 4u;
    if (header_length < RIX_NET_TCP_HEADER_LENGTH || header_length > rix_net_packet_length(packet) ||
        (bytes[12] & 0x0fu) != 0) return -2;
    if (pseudo_checksum(source_ip, destination_ip, bytes, rix_net_packet_length(packet)) != 0)
        return -3;
    header->source_port = get_be16(bytes + 0);
    header->destination_port = get_be16(bytes + 2);
    header->sequence = get_be32(bytes + 4);
    header->acknowledgment = get_be32(bytes + 8);
    header->flags = bytes[13];
    header->window = get_be16(bytes + 14);
    header->checksum = get_be16(bytes + 16);
    header->urgent = get_be16(bytes + 18);
    if (!header->source_port || !header->destination_port) return -4;
    if (rix_net_packet_pull(packet, header_length, 0) != 0) return -1;
    return 0;
}
