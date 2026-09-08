#include "socket.h"
#include "ipv4.h"
#include "udp.h"
#include "stack.h"

static int packet_copy(rix_net_packet_t *destination, const void *data, size_t length) {
    if (!destination || (length && !data) || length > RIX_NET_FRAME_CAPACITY) return -1;
    rix_net_packet_init(destination);
    void *out = 0;
    if (rix_net_packet_put(destination, length, &out) != 0) return -1;
    for (size_t i = 0; i < length; ++i) ((uint8_t *)out)[i] = ((const uint8_t *)data)[i];
    return 0;
}

static int valid_descriptor(const rix_net_socket_table_t *table, int descriptor) {
    return table && descriptor >= 0 && descriptor < (int)RIX_NET_SOCKET_MAX &&
           table->sockets[descriptor].used;
}

static int queue_packet(rix_net_socket_t *receiver, const void *data, size_t length,
                        rix_net_endpoint_t peer) {
    if (!receiver || !data || !length || length > RIX_NET_FRAME_CAPACITY ||
        receiver->receive.count >= RIX_NET_SOCKET_QUEUE) return -1;
    size_t tail = (receiver->receive.head + receiver->receive.count) % RIX_NET_SOCKET_QUEUE;
    if (packet_copy(&receiver->receive.packets[tail], data, length) != 0) return -1;
    receiver->receive.peers[tail] = peer;
    ++receiver->receive.count;
    return 0;
}

static int endpoint_equal(rix_net_endpoint_t left, rix_net_endpoint_t right) {
    return left.address == right.address && left.port == right.port;
}

static int has_prefix(const uint8_t *data, size_t length, const char *prefix) {
    size_t prefix_length = 0;
    while (prefix[prefix_length]) ++prefix_length;
    if (length < prefix_length) return 0;
    for (size_t i = 0; i < prefix_length; ++i)
        if (data[i] != (uint8_t)prefix[i]) return 0;
    return 1;
}

static int tcp_loopback_handshake(rix_net_socket_t *socket, rix_net_endpoint_t peer) {
    if (!socket || peer.address != RIX_NET_SOCKET_LOOPBACK || peer.port != 80) return -1;
    rix_tcp_control_t service;
    rix_net_packet_t packet;
    rix_net_tcp_header_t header;
    rix_tcp_init(&service);
    if (rix_tcp_transition(&service, RIX_TCP_EVENT_PASSIVE_OPEN) != 0) return -1;
    socket->tcp.sequence = 1;
    socket->tcp.acknowledgment = 0;
    if (rix_tcp_transition(&socket->tcp, RIX_TCP_EVENT_ACTIVE_OPEN) != 0) return -1;
    rix_net_packet_init(&packet);
    if (rix_net_tcp_push(&packet, RIX_NET_SOCKET_LOOPBACK, peer.address,
                         socket->local.port, peer.port, socket->tcp.sequence, 0,
                         RIX_NET_TCP_FLAG_SYN, 4096, 0, 0) != 0 ||
        rix_net_tcp_pull(&packet, RIX_NET_SOCKET_LOOPBACK, peer.address, &header) != 0 ||
        header.flags != RIX_NET_TCP_FLAG_SYN ||
        rix_tcp_transition(&service, RIX_TCP_EVENT_SYN) != 0) return -1;
    service.sequence = 100;
    service.acknowledgment = socket->tcp.sequence + 1;
    rix_net_packet_init(&packet);
    if (rix_net_tcp_push(&packet, peer.address, RIX_NET_SOCKET_LOOPBACK,
                         peer.port, socket->local.port, service.sequence,
                         service.acknowledgment, RIX_NET_TCP_FLAG_SYN | RIX_NET_TCP_FLAG_ACK,
                         4096, 0, 0) != 0 ||
        rix_net_tcp_pull(&packet, peer.address, RIX_NET_SOCKET_LOOPBACK, &header) != 0 ||
        header.flags != (RIX_NET_TCP_FLAG_SYN | RIX_NET_TCP_FLAG_ACK) ||
        header.acknowledgment != socket->tcp.sequence + 1 ||
        rix_tcp_transition(&socket->tcp, RIX_TCP_EVENT_SYN_ACK) != 0) return -1;
    socket->tcp.sequence += 1;
    socket->tcp.acknowledgment = service.sequence + 1;
    rix_net_packet_init(&packet);
    if (rix_net_tcp_push(&packet, RIX_NET_SOCKET_LOOPBACK, peer.address,
                         socket->local.port, peer.port, socket->tcp.sequence,
                         socket->tcp.acknowledgment, RIX_NET_TCP_FLAG_ACK, 4096, 0, 0) != 0 ||
        rix_net_tcp_pull(&packet, RIX_NET_SOCKET_LOOPBACK, peer.address, &header) != 0 ||
        header.flags != RIX_NET_TCP_FLAG_ACK ||
        header.acknowledgment != service.sequence + 1 ||
        rix_tcp_transition(&service, RIX_TCP_EVENT_ACK) != 0 ||
        !rix_tcp_is_connected(&socket->tcp) || !rix_tcp_is_connected(&service)) return -1;
    return 0;
}

void rix_net_socket_table_init(rix_net_socket_table_t *table) {
    if (!table) return;
    for (size_t i = 0; i < RIX_NET_SOCKET_MAX; ++i) {
        table->sockets[i].used = 0;
        rix_tcp_init(&table->sockets[i].tcp);
    }
}

int rix_net_socket_open(rix_net_socket_table_t *table, rix_net_socket_type_t type) {
    if (!table || (type != RIX_NET_SOCKET_UDP && type != RIX_NET_SOCKET_RAW_ICMP &&
                   type != RIX_NET_SOCKET_TCP)) return -1;
    for (size_t i = 0; i < RIX_NET_SOCKET_MAX; ++i) {
        if (!table->sockets[i].used) {
            rix_net_socket_t *socket = &table->sockets[i];
            socket->used = 1;
            socket->connected = 0;
            socket->flags = 0;
            socket->type = type;
            socket->local = (rix_net_endpoint_t){RIX_NET_SOCKET_LOOPBACK,
                                                 (uint16_t)(49152u + i)};
            socket->peer = (rix_net_endpoint_t){0, 0};
            rix_tcp_init(&socket->tcp);
            socket->receive.head = 0;
            socket->receive.count = 0;
            return (int)i;
        }
    }
    return -1;
}

int rix_net_socket_close(rix_net_socket_table_t *table, int descriptor) {
    if (!valid_descriptor(table, descriptor)) return -1;
    table->sockets[descriptor].used = 0;
    return 0;
}

int rix_net_socket_bind(rix_net_socket_table_t *table, int descriptor,
                        rix_net_endpoint_t endpoint) {
    if (!valid_descriptor(table, descriptor) || endpoint.address != RIX_NET_SOCKET_LOOPBACK ||
        endpoint.port == 0) return -1;
    for (size_t i = 0; i < RIX_NET_SOCKET_MAX; ++i)
        if ((int)i != descriptor && table->sockets[i].used &&
            endpoint_equal(table->sockets[i].local, endpoint)) return -2;
    table->sockets[descriptor].local = endpoint;
    return 0;
}

int rix_net_socket_connect(rix_net_socket_table_t *table, int descriptor,
                           rix_net_endpoint_t endpoint) {
    if (!valid_descriptor(table, descriptor) || endpoint.address != RIX_NET_SOCKET_LOOPBACK ||
        endpoint.port == 0) return -1;
    rix_net_socket_t *socket = &table->sockets[descriptor];
    if (socket->type == RIX_NET_SOCKET_RAW_ICMP) return -1;
    if (socket->type == RIX_NET_SOCKET_TCP && tcp_loopback_handshake(socket, endpoint) != 0)
        return -4;
    socket->peer = endpoint;
    socket->connected = 1;
    return 0;
}

static int send_raw_icmp(rix_net_socket_table_t *table, rix_net_socket_t *sender,
                         const void *data, size_t length, rix_net_endpoint_t destination) {
    rix_net_packet_t packet;
    if (packet_copy(&packet, data, length) != 0) return -1;
    const uint8_t *bytes = rix_net_packet_data(&packet);
    if (length >= 8 && bytes[0] == RIX_NET_ICMP_ECHO_REQUEST && bytes[1] == 0 &&
        rix_net_checksum(bytes, length) == 0) {
        rix_net_icmp_echo_t echo;
        if (rix_net_icmp_echo_pull(&packet, &echo) != 0) return -1;
        size_t payload_length = rix_net_packet_length(&packet);
        const uint8_t *payload = rix_net_packet_data(&packet);
        rix_net_packet_t reply;
        rix_net_packet_init(&reply);
        if (rix_net_icmp_echo_push(&reply, RIX_NET_ICMP_ECHO_REPLY, echo.identifier,
                                    echo.sequence, payload, payload_length) != 0) return -1;
        for (size_t i = 0; i < RIX_NET_SOCKET_MAX; ++i) {
            rix_net_socket_t *receiver = &table->sockets[i];
            if (!receiver->used || receiver->type != RIX_NET_SOCKET_RAW_ICMP ||
                !endpoint_equal(receiver->local, destination)) continue;
            return queue_packet(receiver, rix_net_packet_data(&reply),
                                rix_net_packet_length(&reply), sender->local) == 0
                       ? (int)length : -3;
        }
        return -4;
    }
    for (size_t i = 0; i < RIX_NET_SOCKET_MAX; ++i) {
        rix_net_socket_t *receiver = &table->sockets[i];
        if (!receiver->used || receiver->type != RIX_NET_SOCKET_RAW_ICMP ||
            !endpoint_equal(receiver->local, destination)) continue;
        return queue_packet(receiver, data, length, sender->local) == 0 ? (int)length : -3;
    }
    return -4;
}

static int send_udp(rix_net_socket_table_t *table, rix_net_socket_t *sender,
                    const void *data, size_t length, rix_net_endpoint_t destination) {
    rix_net_packet_t packet;
    rix_net_udp_header_t header;
    rix_net_packet_init(&packet);
    if (rix_net_udp_push(&packet, RIX_NET_SOCKET_LOOPBACK, destination.address,
                         sender->local.port, destination.port, data, length) != 0 ||
        rix_net_udp_pull(&packet, RIX_NET_SOCKET_LOOPBACK, destination.address, &header) != 0)
        return -1;
    for (size_t i = 0; i < RIX_NET_SOCKET_MAX; ++i) {
        rix_net_socket_t *receiver = &table->sockets[i];
        if (!receiver->used || receiver->type != RIX_NET_SOCKET_UDP ||
            !endpoint_equal(receiver->local, destination)) continue;
        return queue_packet(receiver, rix_net_packet_data(&packet),
                            rix_net_packet_length(&packet), sender->local) == 0
                   ? (int)length : -3;
    }
    return -4;
}

static int send_udp_external(rix_net_socket_t *sender, const void *data, size_t length,
                             rix_net_endpoint_t destination) {
    const rix_net_device_info_t *info = rix_net_device_info();
    rix_net_stack_t *stack = rix_net_stack_default();
    rix_net_packet_t packet;
    if (!info || !stack || !sender || !data || !length) return -1;
    rix_net_packet_init(&packet);
    if (rix_net_udp_push(&packet, info->address, destination.address,
                         sender->local.port, destination.port, data, length) != 0 ||
        rix_net_ipv4_push(&packet, info->address, destination.address,
                          RIX_NET_IP_PROTO_UDP, 64, 0, RIX_NET_IP_FLAG_DF) != 0)
        return -1;
    int result = rix_net_stack_send_ipv4(stack, &packet, destination.address);
    return result == 0 || result == -2 ? (int)length : -1;
}

static int send_tcp(rix_net_socket_t *sender, const void *data, size_t length,
                    rix_net_endpoint_t destination) {
    if (!sender->connected || !rix_tcp_is_connected(&sender->tcp) ||
        !endpoint_equal(sender->peer, destination)) return -1;
    rix_net_packet_t request;
    rix_net_tcp_header_t request_header;
    rix_net_packet_init(&request);
    if (rix_net_tcp_push(&request, RIX_NET_SOCKET_LOOPBACK, destination.address,
                         sender->local.port, destination.port, sender->tcp.sequence,
                         sender->tcp.acknowledgment,
                         RIX_NET_TCP_FLAG_ACK | RIX_NET_TCP_FLAG_PSH, 4096,
                         data, length) != 0 ||
        rix_net_tcp_pull(&request, RIX_NET_SOCKET_LOOPBACK, destination.address,
                         &request_header) != 0 ||
        request_header.flags != (RIX_NET_TCP_FLAG_ACK | RIX_NET_TCP_FLAG_PSH) ||
        request_header.sequence != sender->tcp.sequence ||
        request_header.acknowledgment != sender->tcp.acknowledgment) return -2;
    sender->tcp.sequence += (uint32_t)length;
    if (destination.port != 80 || !has_prefix(data, length, "GET ")) return (int)length;
    static const uint8_t response[] =
        "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nContent-Length: 50\r\n\r\n"
        "<html><body><h1>Hello RixuriOS</h1></body></html>\n";
    rix_net_packet_t reply;
    rix_net_tcp_header_t reply_header;
    rix_net_packet_init(&reply);
    if (rix_net_tcp_push(&reply, destination.address, RIX_NET_SOCKET_LOOPBACK,
                         destination.port, sender->local.port, 101,
                         sender->tcp.sequence, RIX_NET_TCP_FLAG_ACK | RIX_NET_TCP_FLAG_PSH,
                         4096, response, sizeof(response) - 1) != 0 ||
        rix_net_tcp_pull(&reply, destination.address, RIX_NET_SOCKET_LOOPBACK,
                         &reply_header) != 0 ||
        reply_header.flags != (RIX_NET_TCP_FLAG_ACK | RIX_NET_TCP_FLAG_PSH) ||
        reply_header.acknowledgment != sender->tcp.sequence) return -2;
    sender->tcp.acknowledgment = reply_header.sequence + (uint32_t)(sizeof(response) - 1);
    return queue_packet(sender, rix_net_packet_data(&reply), rix_net_packet_length(&reply),
                        destination) == 0
               ? (int)length : -3;
}

int rix_net_socket_send(rix_net_socket_table_t *table, int descriptor,
                        const void *data, size_t length,
                        rix_net_endpoint_t destination) {
    if (!valid_descriptor(table, descriptor) || !data || !length || length > RIX_NET_MTU)
        return -1;
    rix_net_socket_t *sender = &table->sockets[descriptor];
    if (sender->connected) destination = sender->peer;
    if (destination.port == 0 ||
        (destination.address != RIX_NET_SOCKET_LOOPBACK &&
         !(sender->type == RIX_NET_SOCKET_UDP && rix_net_stack_default()))) return -2;
    if (sender->type == RIX_NET_SOCKET_RAW_ICMP)
        return send_raw_icmp(table, sender, data, length, destination);
    if (sender->type == RIX_NET_SOCKET_UDP)
        return destination.address == RIX_NET_SOCKET_LOOPBACK
                   ? send_udp(table, sender, data, length, destination)
                   : send_udp_external(sender, data, length, destination);
    if (sender->type == RIX_NET_SOCKET_TCP)
        return send_tcp(sender, data, length, destination);
    return -1;
}

static void dispatch_external_ipv4(rix_net_socket_table_t *table) {
    rix_net_stack_t *stack = rix_net_stack_default();
    rix_net_packet_t packet;
    rix_net_ipv4_header_t ip;
    rix_net_udp_header_t udp;
    if (!table || !stack) return;
    (void)rix_net_stack_poll(stack, 0);
    if (rix_net_stack_take_ipv4(stack, &packet) != 1 ||
        rix_net_ipv4_pull(&packet, &ip) != 0 || ip.protocol != RIX_NET_IP_PROTO_UDP ||
        rix_net_udp_pull(&packet, ip.source, ip.destination, &udp) != 0) return;
    rix_net_endpoint_t source = {ip.source, udp.source_port};
    for (size_t i = 0; i < RIX_NET_SOCKET_MAX; ++i) {
        rix_net_socket_t *receiver = &table->sockets[i];
        if (!receiver->used || receiver->type != RIX_NET_SOCKET_UDP ||
            receiver->local.port != udp.destination_port) continue;
        (void)queue_packet(receiver, rix_net_packet_data(&packet),
                           rix_net_packet_length(&packet), source);
        return;
    }
}

int rix_net_socket_receive(rix_net_socket_table_t *table, int descriptor,
                           void *data, size_t capacity,
                           rix_net_endpoint_t *source) {
    if (!valid_descriptor(table, descriptor) || !data || !capacity) return -1;
    rix_net_socket_t *socket = &table->sockets[descriptor];
    if (!socket->receive.count && socket->type == RIX_NET_SOCKET_UDP)
        dispatch_external_ipv4(table);
    if (!socket->receive.count) return (socket->flags & RIX_NET_SOCKET_NONBLOCK) ? -2 : -3;
    size_t head = socket->receive.head;
    size_t length = rix_net_packet_length(&socket->receive.packets[head]);
    if (length > capacity) return -4;
    const uint8_t *bytes = rix_net_packet_data(&socket->receive.packets[head]);
    for (size_t i = 0; i < length; ++i) ((uint8_t *)data)[i] = bytes[i];
    if (source) *source = socket->receive.peers[head];
    socket->receive.head = (head + 1) % RIX_NET_SOCKET_QUEUE;
    --socket->receive.count;
    return (int)length;
}
