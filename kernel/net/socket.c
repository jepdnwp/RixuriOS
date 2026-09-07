#include "socket.h"

static void packet_copy(rix_net_packet_t *destination, const void *data, size_t length) {
    rix_net_packet_init(destination);
    void *out = 0;
    (void)rix_net_packet_put(destination, length, &out);
    for (size_t i = 0; i < length; ++i) ((uint8_t *)out)[i] = ((const uint8_t *)data)[i];
}

static int valid_descriptor(const rix_net_socket_table_t *table, int descriptor) {
    return table && descriptor >= 0 && descriptor < (int)RIX_NET_SOCKET_MAX &&
           table->sockets[descriptor].used;
}

void rix_net_socket_table_init(rix_net_socket_table_t *table) {
    if (!table) return;
    for (size_t i = 0; i < RIX_NET_SOCKET_MAX; ++i) table->sockets[i].used = 0;
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
            socket->local = (rix_net_endpoint_t){RIX_NET_SOCKET_LOOPBACK, 0};
            socket->peer = (rix_net_endpoint_t){0, 0};
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
            table->sockets[i].local.address == endpoint.address &&
            table->sockets[i].local.port == endpoint.port) return -2;
    table->sockets[descriptor].local = endpoint;
    return 0;
}

int rix_net_socket_connect(rix_net_socket_table_t *table, int descriptor,
                           rix_net_endpoint_t endpoint) {
    if (!valid_descriptor(table, descriptor) || endpoint.address != RIX_NET_SOCKET_LOOPBACK ||
        endpoint.port == 0) return -1;
    table->sockets[descriptor].peer = endpoint;
    table->sockets[descriptor].connected = 1;
    return 0;
}

int rix_net_socket_send(rix_net_socket_table_t *table, int descriptor,
                        const void *data, size_t length,
                        rix_net_endpoint_t destination) {
    if (!valid_descriptor(table, descriptor) || !data || !length || length > RIX_NET_MTU) return -1;
    rix_net_socket_t *sender = &table->sockets[descriptor];
    if (sender->connected) destination = sender->peer;
    if (destination.address != RIX_NET_SOCKET_LOOPBACK || destination.port == 0) return -2;
    if (sender->type == RIX_NET_SOCKET_TCP && destination.port == 80 && length >= 4 &&
        ((const uint8_t *)data)[0] == 'G' && ((const uint8_t *)data)[1] == 'E' &&
        ((const uint8_t *)data)[2] == 'T' && ((const uint8_t *)data)[3] == ' ') {
        static const uint8_t response[] =
            "HTTP/1.0 200 OK\r\nContent-Length: 15\r\n\r\nHello RixuriOS\n";
        if (sender->receive.count >= RIX_NET_SOCKET_QUEUE) return -3;
        size_t tail = (sender->receive.head + sender->receive.count) % RIX_NET_SOCKET_QUEUE;
        packet_copy(&sender->receive.packets[tail], response, sizeof(response) - 1);
        sender->receive.peers[tail] = destination;
        ++sender->receive.count;
        return (int)length;
    }
    for (size_t i = 0; i < RIX_NET_SOCKET_MAX; ++i) {
        rix_net_socket_t *receiver = &table->sockets[i];
        if (!receiver->used || receiver->type != sender->type ||
            receiver->local.address != destination.address || receiver->local.port != destination.port) continue;
        if (receiver->receive.count >= RIX_NET_SOCKET_QUEUE) return -3;
        size_t tail = (receiver->receive.head + receiver->receive.count) % RIX_NET_SOCKET_QUEUE;
        packet_copy(&receiver->receive.packets[tail], data, length);
        if (sender->type == RIX_NET_SOCKET_RAW_ICMP && length >= 8 &&
            ((const uint8_t *)data)[0] == 8 && ((const uint8_t *)data)[1] == 0) {
            uint8_t *reply = (uint8_t *)rix_net_packet_data(&receiver->receive.packets[tail]);
            reply[0] = 0;
            reply[2] = 0;
            reply[3] = 0;
            uint16_t checksum = rix_net_checksum(reply, length);
            reply[2] = (uint8_t)(checksum >> 8);
            reply[3] = (uint8_t)checksum;
        }
        receiver->receive.peers[tail] = sender->local;
        ++receiver->receive.count;
        return (int)length;
    }
    return -4;
}

int rix_net_socket_receive(rix_net_socket_table_t *table, int descriptor,
                           void *data, size_t capacity,
                           rix_net_endpoint_t *source) {
    if (!valid_descriptor(table, descriptor) || !data || !capacity) return -1;
    rix_net_socket_t *socket = &table->sockets[descriptor];
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
