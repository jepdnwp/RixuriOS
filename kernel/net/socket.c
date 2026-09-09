#include "socket.h"
#include "ipv4.h"
#include "udp.h"
#include "stack.h"
#include "kernel.h"

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

static int connect_tcp_external(rix_net_socket_t *socket, rix_net_endpoint_t endpoint);

int rix_net_socket_connect(rix_net_socket_table_t *table, int descriptor,
                           rix_net_endpoint_t endpoint) {
    if (!valid_descriptor(table, descriptor) || endpoint.port == 0) return -1;
    rix_net_socket_t *socket = &table->sockets[descriptor];
    if (endpoint.address == RIX_NET_SOCKET_LOOPBACK) {
        if (socket->type == RIX_NET_SOCKET_RAW_ICMP) return -1;
        if (socket->type == RIX_NET_SOCKET_TCP && tcp_loopback_handshake(socket, endpoint) != 0)
            return -4;
        socket->peer = endpoint;
        socket->connected = 1;
        return 0;
    }
    if (socket->type != RIX_NET_SOCKET_TCP) return -1;
    return connect_tcp_external(socket, endpoint);
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

/* External ICMP echo request. The caller builds the 8+ byte echo message
   (identifier/sequence/checksum); it is validated here, wrapped in IPv4
   from the configured interface address, and handed to the stack (ARP
   pending surfaces as -2, like TCP). No per-peer state is kept. */
static int send_raw_icmp_external(rix_net_socket_t *sender, const void *data,
                                  size_t length, rix_net_endpoint_t destination) {
    const rix_net_device_info_t *info = rix_net_device_info();
    rix_net_stack_t *stack = rix_net_stack_default();
    rix_net_packet_t packet;
    const uint8_t *bytes = (const uint8_t *)data;
    if (!sender || !data || !length || length > RIX_NET_MTU || !info || !stack ||
        !destination.address || destination.address == RIX_NET_SOCKET_LOOPBACK)
        return -1;
    if (length < 8 || bytes[0] != RIX_NET_ICMP_ECHO_REQUEST || bytes[1] != 0 ||
        rix_net_checksum(bytes, length) != 0) return -1;
    rix_net_packet_init(&packet);
    if (packet_copy(&packet, data, length) != 0 ||
        rix_net_ipv4_push(&packet, info->address, destination.address,
                          RIX_NET_IP_PROTO_ICMP, 64, 0, RIX_NET_IP_FLAG_DF) != 0)
        return -1;
    {
        int sent = rix_net_stack_send_ipv4(stack, &packet, destination.address);
        return sent < 0 && sent != -2 ? -1 : (int)length;
    }
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

static uint32_t tcp_next_iss = 1;

/* External TCP connect step. Returns 0 when ESTABLISHED, -2 while the
   handshake is in progress (caller paces retransmits), -1 on failure.
   Retransmission is stateless: the same SYN is rebuilt every call. */
static int connect_tcp_external(rix_net_socket_t *socket, rix_net_endpoint_t endpoint) {
    const rix_net_device_info_t *info = rix_net_device_info();
    rix_net_stack_t *stack = rix_net_stack_default();
    if (!socket || !info || !stack) return -1;
    if (socket->tcp.state == RIX_TCP_ESTABLISHED &&
        socket->peer.address == endpoint.address && socket->peer.port == endpoint.port)
        return 0;
    if (socket->tcp.state == RIX_TCP_CLOSED) {
        socket->peer = endpoint;
        socket->connected = 0;
        socket->tcp.sequence = tcp_next_iss;
        tcp_next_iss += 0x10000u;
        if (!socket->tcp.sequence) socket->tcp.sequence = 1;
        socket->tcp.acknowledgment = 0;
        if (rix_tcp_transition(&socket->tcp, RIX_TCP_EVENT_ACTIVE_OPEN) != 0) return -1;
    } else if (socket->tcp.state != RIX_TCP_SYN_SENT ||
               socket->peer.address != endpoint.address ||
               socket->peer.port != endpoint.port) {
        return -1;
    }
    rix_net_packet_t packet;
    rix_net_packet_init(&packet);
    if (rix_net_tcp_push(&packet, info->address, endpoint.address,
                         socket->local.port, endpoint.port,
                         socket->tcp.sequence, 0,
                         RIX_NET_TCP_FLAG_SYN, 4096, 0, 0) != 0 ||
        rix_net_ipv4_push(&packet, info->address, endpoint.address,
                          RIX_NET_IP_PROTO_TCP, 64, 0, RIX_NET_IP_FLAG_DF) != 0)
        return -1;
    int sent = rix_net_stack_send_ipv4(stack, &packet, endpoint.address);
    return sent < 0 && sent != -2 ? -1 : -2;
}

static int send_tcp_external(rix_net_socket_t *sender, const void *data, size_t length,
                             rix_net_endpoint_t destination) {
    const rix_net_device_info_t *info = rix_net_device_info();
    rix_net_stack_t *stack = rix_net_stack_default();
    if (!sender || !data || !length || !info || !stack) return -1;
    if (!sender->connected || !rix_tcp_is_connected(&sender->tcp) ||
        !endpoint_equal(sender->peer, destination)) return -1;
    /* Single-outstanding-segment policy (v1): the sequence is not advanced,
       so a caller-driven retransmit resends an identical segment. */
    rix_net_packet_t packet;
    rix_net_packet_init(&packet);
    if (rix_net_tcp_push(&packet, info->address, destination.address,
                         sender->local.port, destination.port,
                         sender->tcp.sequence, sender->tcp.acknowledgment,
                         RIX_NET_TCP_FLAG_ACK | RIX_NET_TCP_FLAG_PSH, 4096,
                         data, length) != 0 ||
        rix_net_ipv4_push(&packet, info->address, destination.address,
                          RIX_NET_IP_PROTO_TCP, 64, 0, RIX_NET_IP_FLAG_DF) != 0)
        return -1;
    int sent = rix_net_stack_send_ipv4(stack, &packet, destination.address);
    return sent < 0 && sent != -2 ? -1 : (int)length;
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
    if (sender->type == RIX_NET_SOCKET_RAW_ICMP)
        return destination.address == RIX_NET_SOCKET_LOOPBACK
                   ? send_raw_icmp(table, sender, data, length, destination)
                   : send_raw_icmp_external(sender, data, length, destination);
    if (destination.port == 0 ||
        (destination.address != RIX_NET_SOCKET_LOOPBACK &&
         !((sender->type == RIX_NET_SOCKET_UDP ||
            sender->type == RIX_NET_SOCKET_TCP) && rix_net_stack_default()))) return -2;
    if (sender->type == RIX_NET_SOCKET_UDP)
        return destination.address == RIX_NET_SOCKET_LOOPBACK
                   ? send_udp(table, sender, data, length, destination)
                   : send_udp_external(sender, data, length, destination);
    if (sender->type == RIX_NET_SOCKET_TCP)
        return destination.address == RIX_NET_SOCKET_LOOPBACK
                   ? send_tcp(sender, data, length, destination)
                   : send_tcp_external(sender, data, length, destination);
    return -1;
}

static int tcp_send_ack(rix_net_stack_t *stack, rix_net_socket_t *socket,
                          uint32_t peer_ip) {
    const rix_net_device_info_t *info = rix_net_device_info();
    rix_net_packet_t ack;
    if (!stack || !socket || !info || !peer_ip) return -1;
    rix_net_packet_init(&ack);
    if (rix_net_tcp_push(&ack, info->address, peer_ip,
                         socket->local.port, socket->peer.port,
                         socket->tcp.sequence, socket->tcp.acknowledgment,
                         RIX_NET_TCP_FLAG_ACK, 4096, 0, 0) != 0 ||
        rix_net_ipv4_push(&ack, info->address, peer_ip,
                          RIX_NET_IP_PROTO_TCP, 64, 0, RIX_NET_IP_FLAG_DF) != 0)
        return -1;
    return rix_net_stack_send_ipv4(stack, &ack, peer_ip) < 0 ? -1 : 0;
}

/* Wire TCP input. Matches by local port + peer; out-of-order segments are
   dropped (no reordering support); RST aborts; FIN moves to CLOSE_WAIT so a
   drained socket reads EOF. Returns 11 on match+progress, 12 on no match,
   13 on wrong-state ignore, 14 on sequence ignore, 0 on pure-ACK ignore,
   -1 on invalid frame. (Distinct from UDP's 1.) */
static int dispatch_external_tcp(rix_net_socket_table_t *table, rix_net_stack_t *stack,
                                 rix_net_packet_t *packet, uint32_t source_ip,
                                 uint32_t destination_ip) {
    rix_net_tcp_header_t header;
    if (!table || !stack || !packet || !source_ip || !destination_ip) return -1;
    if (rix_net_tcp_pull(packet, source_ip, destination_ip, &header) != 0) return -1;
#ifndef RIX_HOST_TEST
    {static unsigned tcpd_n = 0; static unsigned tcpd_seq = 0;
    if (tcpd_n < 8) {++tcpd_n;
    kernel_log("DEBUG: TCPD #"); kernel_log_dec(tcpd_seq);
    kernel_log(" sport="); kernel_log_dec(header.source_port);
    kernel_log(" flags="); kernel_log_hex(header.flags);
    kernel_log(" seq="); kernel_log_hex(header.sequence);
    kernel_log(" ack="); kernel_log_hex(header.acknowledgment);
    kernel_log(" len="); kernel_log_dec(rix_net_packet_length(packet));
    kernel_log("\r\n");} ++tcpd_seq;}
#endif
    for (size_t i = 0; i < RIX_NET_SOCKET_MAX; ++i) {
        rix_net_socket_t *socket = &table->sockets[i];
        if (!socket->used || socket->type != RIX_NET_SOCKET_TCP ||
            socket->local.port != header.destination_port ||
            socket->peer.address != source_ip || socket->peer.port != header.source_port)
            continue;
        if (header.flags & RIX_NET_TCP_FLAG_RST) {
            (void)rix_tcp_transition(&socket->tcp, RIX_TCP_EVENT_CLOSE);
            socket->connected = 0;
            return 11;
        }
        if ((header.flags & RIX_NET_TCP_FLAG_SYN) &&
            socket->tcp.state == RIX_TCP_SYN_SENT &&
            header.acknowledgment == socket->tcp.sequence + 1u) {
            socket->tcp.sequence += 1u;
            socket->tcp.acknowledgment = header.sequence + 1u;
            if (rix_tcp_transition(&socket->tcp, RIX_TCP_EVENT_SYN_ACK) != 0) return -1;
            socket->connected = 1;
            (void)tcp_send_ack(stack, socket, source_ip);
#ifndef RIX_HOST_TEST
            kernel_log("DEBUG: TCPD accepted state="); kernel_log_dec(socket->tcp.state);
            kernel_log("\r\n");
#endif
            return 11;
        }
        if (socket->tcp.state != RIX_TCP_ESTABLISHED &&
            socket->tcp.state != RIX_TCP_CLOSE_WAIT)
            return 13;
        if (header.sequence != socket->tcp.acknowledgment)
            return 14;
        size_t payload = rix_net_packet_length(packet);
        uint8_t fin = (header.flags & RIX_NET_TCP_FLAG_FIN) != 0u;
        if (payload &&
            queue_packet(socket, rix_net_packet_data(packet), payload,
                         (rix_net_endpoint_t){source_ip, header.source_port}) != 0)
            return -1;
#ifndef RIX_HOST_TEST
        kernel_log("DEBUG: TCPD queued="); kernel_log_dec(payload);
        kernel_log(" ack="); kernel_log_hex(header.sequence + (uint32_t)payload + (fin ? 1u : 0u));
        kernel_log("\r\n");
#endif
        socket->tcp.acknowledgment = header.sequence + (uint32_t)payload + (fin ? 1u : 0u);
        if (fin && rix_tcp_transition(&socket->tcp, RIX_TCP_EVENT_FIN) != 0) return -1;
        if (payload || fin) {
            (void)tcp_send_ack(stack, socket, source_ip);
            return 11;
        }
        return 0;
    }
    return 2;
}

static int dispatch_external_ipv4(rix_net_socket_table_t *table) {
    rix_net_stack_t *stack = rix_net_stack_default();
    rix_net_packet_t packet;
    rix_net_ipv4_header_t ip;
    rix_net_udp_header_t udp;
    if (!table || !stack) return 0;
    (void)rix_net_stack_poll(stack, 0);
    if (rix_net_stack_take_ipv4(stack, &packet) != 1 ||
        rix_net_ipv4_pull(&packet, &ip) != 0) return 0;
    if (ip.protocol == RIX_NET_IP_PROTO_TCP) {
        return dispatch_external_tcp(table, stack, &packet, ip.source, ip.destination);
    }
    if (ip.protocol == RIX_NET_IP_PROTO_ICMP) {
        /* Wire ICMP echo reply. The full message is queued so the receiver
           can match identifier/sequence itself; delivery is best-effort to
           every used raw socket (v1, documented). */
        const uint8_t *bytes;
        size_t icmp_length = rix_net_packet_length(&packet);
        int matched = 0;
        if (icmp_length < 8) return 0;
        bytes = rix_net_packet_data(&packet);
        if (!bytes || bytes[0] != RIX_NET_ICMP_ECHO_REPLY || bytes[1] != 0 ||
            rix_net_checksum(bytes, icmp_length) != 0) return 0;
        for (size_t i = 0; i < RIX_NET_SOCKET_MAX; ++i) {
            rix_net_socket_t *receiver = &table->sockets[i];
            if (!receiver->used || receiver->type != RIX_NET_SOCKET_RAW_ICMP)
                continue;
            if (queue_packet(receiver, bytes, icmp_length,
                             (rix_net_endpoint_t){ip.source, 0}) == 0)
                matched = 1;
        }
        return matched ? 1 : 0;
    }
    if (ip.protocol != RIX_NET_IP_PROTO_UDP ||
        rix_net_udp_pull(&packet, ip.source, ip.destination, &udp) != 0) return 0;
    rix_net_endpoint_t source = {ip.source, udp.source_port};
    for (size_t i = 0; i < RIX_NET_SOCKET_MAX; ++i) {
        rix_net_socket_t *receiver = &table->sockets[i];
        if (!receiver->used || receiver->type != RIX_NET_SOCKET_UDP ||
            receiver->local.port != udp.destination_port) continue;
        (void)queue_packet(receiver, rix_net_packet_data(&packet),
                           rix_net_packet_length(&packet), source);
        return 1;
    }
    return 0;
}

int rix_net_socket_poll(rix_net_socket_table_t *table) {
    if (!table || !rix_net_stack_default()) return -1;
    return dispatch_external_ipv4(table);
}

int rix_net_socket_receive(rix_net_socket_table_t *table, int descriptor,
                           void *data, size_t capacity,
                           rix_net_endpoint_t *source) {
    if (!valid_descriptor(table, descriptor) || !data || !capacity) return -1;
    rix_net_socket_t *socket = &table->sockets[descriptor];
    if (!socket->receive.count && (socket->type == RIX_NET_SOCKET_UDP ||
                                    socket->type == RIX_NET_SOCKET_TCP ||
                                    socket->type == RIX_NET_SOCKET_RAW_ICMP))
        dispatch_external_ipv4(table);
    if (!socket->receive.count) {
        if (socket->type == RIX_NET_SOCKET_TCP &&
            socket->tcp.state == RIX_TCP_CLOSE_WAIT)
            return 0;
        return (socket->flags & RIX_NET_SOCKET_NONBLOCK) ? -2 : -3;
    }
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


static int socket6_valid(const rix_net_socket6_udp_table_t *table, int descriptor) {
    return table && descriptor >= 0 && descriptor < (int)RIX_NET_SOCKET6_MAX &&
           table->sockets[descriptor].used;
}
void rix_net_socket6_udp_init(rix_net_socket6_udp_table_t *table) {
    if (!table) return;
    for (size_t i = 0; i < RIX_NET_SOCKET6_MAX; ++i) {
        table->sockets[i].used = 0; table->sockets[i].head = 0; table->sockets[i].count = 0;
    }
}
int rix_net_socket6_udp_open(rix_net_socket6_udp_table_t *table) {
    if (!table) return -1;
    for (size_t i = 0; i < RIX_NET_SOCKET6_MAX; ++i) if (!table->sockets[i].used) {
        table->sockets[i].used = 1; table->sockets[i].local_port = (uint16_t)(49152u + i);
        for (size_t j = 0; j < 16; ++j) table->sockets[i].local_address[j] = 0;
        table->sockets[i].local_address[15] = 1; table->sockets[i].head = 0; table->sockets[i].count = 0;
        return (int)i;
    }
    return -1;
}
int rix_net_socket6_udp_bind(rix_net_socket6_udp_table_t *table, int descriptor,
                             const rix_net_endpoint6_t *endpoint) {
    if (!socket6_valid(table, descriptor) || !endpoint || !endpoint->port) return -1;
    table->sockets[descriptor].local_port = endpoint->port;
    for (size_t i = 0; i < 16; ++i) table->sockets[descriptor].local_address[i] = endpoint->address[i];
    return 0;
}
int rix_net_socket6_udp_send(rix_net_socket6_udp_table_t *table, int descriptor,
                             const void *data, size_t length,
                             const rix_net_endpoint6_t *destination) {
    rix_net_stack_t *stack = rix_net_stack_default();
    rix_net_packet_t packet;
    if (!socket6_valid(table, descriptor) || !data || !length || !destination ||
        !destination->port || !stack) return -1;
    rix_net_packet_init(&packet);
    if (rix_net_udp6_push(&packet, table->sockets[descriptor].local_address, destination->address,
                          table->sockets[descriptor].local_port, destination->port, data, length) != 0 ||
        rix_net_ipv6_push(&packet, table->sockets[descriptor].local_address, destination->address,
                          17, 64, 0, 0) != 0) return -1;
    int result = rix_net_stack_send_ipv6(stack, &packet, destination->address);
    return result == 0 ? (int)length : result;
}
int rix_net_socket6_udp_poll(rix_net_socket6_udp_table_t *table) {
    rix_net_stack_t *stack = rix_net_stack_default();
    rix_net_packet_t packet; rix_net_ipv6_header_t ip; rix_net_udp_header_t udp;
    if (!table || !stack) return -1;
    (void)rix_net_stack_poll(stack, 0);
    if (rix_net_stack_take_ipv6(stack, &packet) != 1 || rix_net_ipv6_pull(&packet, &ip) != 0 ||
        ip.next_header != 17 || rix_net_udp6_pull(&packet, ip.source, ip.destination, &udp) != 0) return 0;
    for (size_t i = 0; i < RIX_NET_SOCKET6_MAX; ++i) {
        if (!table->sockets[i].used || table->sockets[i].local_port != udp.destination_port) continue;
        int address_match = 1;
        for (size_t j = 0; j < 16; ++j) if (table->sockets[i].local_address[j] != ip.destination[j]) address_match = 0;
        if (!address_match && table->sockets[i].local_address[0] != 0) continue;
        if (table->sockets[i].count >= RIX_NET_SOCKET_QUEUE) return -3;
        size_t slot = (table->sockets[i].head + table->sockets[i].count) % RIX_NET_SOCKET_QUEUE;
        table->sockets[i].receive[slot] = packet;
        for (size_t j = 0; j < 16; ++j) table->sockets[i].peers[slot].address[j] = ip.source[j];
        table->sockets[i].peers[slot].port = udp.source_port; ++table->sockets[i].count;
        return (int)rix_net_packet_length(&packet);
    }
    return -4;
}
int rix_net_socket6_udp_receive(rix_net_socket6_udp_table_t *table, int descriptor,
                                void *data, size_t capacity, rix_net_endpoint6_t *source) {
    if (!socket6_valid(table, descriptor) || !data || !capacity) return -1;
    if (!table->sockets[descriptor].count) return -3;
    rix_net_packet_t *packet = &table->sockets[descriptor].receive[table->sockets[descriptor].head];
    size_t length = rix_net_packet_length(packet);
    if (length > capacity) return -2;
    const uint8_t *bytes = rix_net_packet_data(packet);
    for (size_t i = 0; i < length; ++i) ((uint8_t *)data)[i] = bytes[i];
    if (source) *source = table->sockets[descriptor].peers[table->sockets[descriptor].head];
    table->sockets[descriptor].head = (table->sockets[descriptor].head + 1u) % RIX_NET_SOCKET_QUEUE;
    --table->sockets[descriptor].count;
    return (int)length;
}
