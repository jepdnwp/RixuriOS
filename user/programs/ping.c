#include "unistd.h"
#include <stdint.h>

static size_t length(const char *text) {
    size_t n = 0;
    while (text && text[n]) ++n;
    return n;
}

static void say(const char *text) { (void)write(1, text, length(text)); }

static uint16_t checksum(const uint8_t *data, size_t length_bytes) {
    uint32_t sum = 0;
    while (length_bytes >= 2) {
        sum += ((uint32_t)data[0] << 8) | data[1];
        data += 2;
        length_bytes -= 2;
    }
    if (length_bytes) sum += (uint32_t)data[0] << 8;
    while (sum >> 16) sum = (sum & 0xffffu) + (sum >> 16);
    return (uint16_t)~sum;
}

int program_main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;
    uint8_t request[8] = {8, 0, 0, 0, 0x12, 0x34, 0, 1};
    uint16_t sum = checksum(request, sizeof(request));
    request[2] = (uint8_t)(sum >> 8);
    request[3] = (uint8_t)sum;
    int fd = socket_open(RIX_NET_SOCKET_RAW_ICMP);
    if (fd < 0 || socket_bind(fd, (rix_net_endpoint_t){RIX_NET_SOCKET_LOOPBACK, 1}) != 0) {
        say("ping: socket failed\n");
        return 1;
    }
    rix_net_endpoint_t destination = {RIX_NET_SOCKET_LOOPBACK, 1};
    if (socket_send(fd, request, sizeof(request), destination) != (int)sizeof(request)) {
        say("ping: send failed\n");
        return 1;
    }
    uint8_t reply[8] = {0};
    if (socket_receive(fd, reply, sizeof(reply), 0) != (int)sizeof(reply) ||
        reply[0] != 0 || reply[1] != 0 || reply[4] != request[4] ||
        reply[5] != request[5] || checksum(reply, sizeof(reply)) != 0) {
        say("ping: invalid echo reply\n");
        return 1;
    }
    say("ping: 127.0.0.1: PASS\n");
    return 0;
}
