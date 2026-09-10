#include "unistd.h"
#include "hosts.h"
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

/* Send one echo request per round and poll for its reply: at most four
   wire requests, ~8s worst case when the target stays silent. Returns 0 on
   a validated reply, -1 on send failure, -2 on timeout. */
static int echo_rounds(int fd, rix_net_endpoint_t destination,
                       const uint8_t *request, size_t length,
                       uint16_t identifier, uint8_t sequence) {
    static uint8_t reply[64];
    struct timespec pause = {0, 10000000};
    for (unsigned round = 0; round < 4; ++round) {
        if (socket_send(fd, request, length, destination) != (int)length)
            return -1;
        for (unsigned attempt = 0; attempt < 200; ++attempt) {
            int received = socket_receive(fd, reply, sizeof(reply), 0);
            if (received >= 8 && reply[0] == 0 && reply[1] == 0 &&
                reply[4] == (uint8_t)(identifier >> 8) &&
                reply[5] == (uint8_t)identifier && reply[7] == sequence &&
                checksum(reply, (size_t)received) == 0)
                return 0;
            (void)nanosleep(&pause, NULL);
        }
    }
    return -2;
}

int program_main(int argc, char **argv, char **envp) {
    const char *target = "127.0.0.1";
    uint32_t address = RIX_NET_SOCKET_LOOPBACK;
    (void)envp;
    if (argc > 1 && argv && argv[1] && argv[1][0]) {
        target = argv[1];
        if (rix_parse_ipv4(argv[1], &address) != 0) {
            address = 0;
            if (rix_dns_valid_name(argv[1]) != 0 ||
                (rix_hosts_lookup(argv[1], &address) != 0 &&
                 rix_dns_query(argv[1], rix_resolv_server(RIX_NET_DEVICE_DNS),
                               &address) != 0)) {
                say("ping: DNS/network path unavailable\n");
                return 2;
            }
        }
    }
    {
        uint16_t identifier = (uint16_t)(getpid() & 0xffffu);
        uint8_t request[8] = {8, 0, 0, 0, 0, 0, 0, 1};
        uint16_t sum;
        int fd;
        if (!identifier) identifier = 1;
        request[4] = (uint8_t)(identifier >> 8);
        request[5] = (uint8_t)identifier;
        sum = checksum(request, sizeof(request));
        request[2] = (uint8_t)(sum >> 8);
        request[3] = (uint8_t)sum;
        fd = socket_open(RIX_NET_SOCKET_RAW_ICMP);
        if (fd < 0) {
            say("ping: socket failed\n");
            return 1;
        }
        if (address == RIX_NET_SOCKET_LOOPBACK) {
            static const rix_net_endpoint_t loopback = {RIX_NET_SOCKET_LOOPBACK, 1};
            uint8_t reply[8] = {0};
            if (socket_bind(fd, loopback) != 0) {
                say("ping: socket failed\n");
                return 1;
            }
            if (socket_send(fd, request, sizeof(request), loopback) !=
                (int)sizeof(request)) {
                say("ping: send failed\n");
                return 1;
            }
            if (socket_receive(fd, reply, sizeof(reply), 0) != (int)sizeof(reply) ||
                reply[0] != 0 || reply[1] != 0 || reply[4] != request[4] ||
                reply[5] != request[5] || checksum(reply, sizeof(reply)) != 0) {
                say("ping: invalid echo reply\n");
                return 1;
            }
        } else {
            rix_net_endpoint_t destination = {address, 1};
            int echo = echo_rounds(fd, destination, request, sizeof(request),
                                   identifier, 1);
            if (echo < 0) {
                say(echo == -2 ? "ping: no echo reply\n" : "ping: send failed\n");
                return 1;
            }
        }
    }
    say("ping: ");
    say(target);
    say(": PASS\n");
    return 0;
}
