#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

static size_t length(const char *text) { size_t n = 0; while (text && text[n]) ++n; return n; }
static void say(const char *text) { (void)write(1, text, length(text)); }
static int has_prefix(const char *text, size_t size, const char *prefix) {
    size_t n = length(prefix);
    if (size < n) return 0;
    for (size_t i = 0; i < n; ++i) if (text[i] != prefix[i]) return 0;
    return 1;
}
static size_t header_end(const char *response, size_t size) {
    if (!response || size < 4) return 0;
    for (size_t i = 0; i + 3 < size; ++i)
        if (response[i] == '\r' && response[i + 1] == '\n' &&
            response[i + 2] == '\r' && response[i + 3] == '\n') return i + 4;
    return 0;
}
static uint16_t get_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}
static uint32_t get_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}
static int same_text(const char *left, const char *right) {
    size_t i = 0;
    while (left && right && left[i] && right[i] && left[i] == right[i]) ++i;
    return left && right && left[i] == 0 && right[i] == 0;
}

static int dns_skip_name(const uint8_t *packet, size_t length, size_t *offset) {
    size_t cursor = *offset;
    while (cursor < length) {
        uint8_t label = packet[cursor++];
        if (!label) { *offset = cursor; return 0; }
        if ((label & 0xc0u) == 0xc0u) {
            if (cursor >= length) return -1;
            *offset = cursor + 1;
            return 0;
        }
        if (label > 63u || label > length - cursor) return -1;
        cursor += label;
    }
    return -1;
}

static int dns_resolve_google(uint32_t *address) {
    static const uint8_t query[] = {
        0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 6, 'g', 'o', 'o', 'g', 'l', 'e',
        3, 'c', 'o', 'm', 0, 0, 1, 0, 1
    };
    uint8_t response[512] = {0};
    int fd = socket_open(RIX_NET_SOCKET_UDP);
    if (fd < 0 || socket_send(fd, query, sizeof(query),
                              (rix_net_endpoint_t){0x0a000203u, 53}) != (int)sizeof(query))
        return -1;
    for (unsigned attempt = 0; attempt < 10000; ++attempt) {
        int received = socket_receive(fd, response, sizeof(response), 0);
        if (received >= 12 && get_be16(response) == 0x1234u) {
            uint16_t flags = get_be16(response + 2);
            uint16_t answers = get_be16(response + 6);
            if ((flags & 0x8000u) && !(flags & 0x000fu) && answers) {
                size_t offset = 12;
                if (dns_skip_name(response, (size_t)received, &offset) == 0 &&
                    offset + 4 <= (size_t)received) {
                    offset += 4;
                    for (uint16_t answer = 0; answer < answers; ++answer) {
                        if (dns_skip_name(response, (size_t)received, &offset) != 0 ||
                            offset + 10 > (size_t)received) break;
                        uint16_t type = get_be16(response + offset);
                        uint16_t class_code = get_be16(response + offset + 2);
                        uint16_t data_length = get_be16(response + offset + 8);
                        offset += 10;
                        if (offset + data_length > (size_t)received) break;
                        if (type == 1 && class_code == 1 && data_length == 4) {
                            *address = get_be32(response + offset);
                            return 0;
                        }
                        offset += data_length;
                    }
                }
            }
        }
    }
    return -1;
}

int program_main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc > 1 && argv && argv[1] && !same_text(argv[1], "google.com")) {
        say("curl: DNS/network path unavailable\n");
        return 2;
    }
    uint32_t destination_ip = RIX_NET_SOCKET_LOOPBACK;
    if (argc > 1 && dns_resolve_google(&destination_ip) != 0) {
        say("curl: DNS query failed\n");
        return 2;
    }
    int fd = socket_open(RIX_NET_SOCKET_TCP);
    if (fd < 0 || socket_connect(fd, (rix_net_endpoint_t){destination_ip, 80}) != 0) {
        say(argc > 1 ? "curl: DNS resolved; external TCP unavailable\n" : "curl: connect failed\n");
        return 1;
    }
    static const char request[] = "GET / HTTP/1.0\r\nHost: localhost\r\n\r\n";
    if (socket_send(fd, request, sizeof(request) - 1,
                    (rix_net_endpoint_t){destination_ip, 80}) < 0) {
        say("curl: send failed\n");
        return 1;
    }
    char response[256] = {0};
    int received = socket_receive(fd, response, sizeof(response) - 1, 0);
    size_t body = received > 0 ? header_end(response, (size_t)received) : 0;
    static const char html[] = "<html><body><h1>Hello RixuriOS</h1></body></html>\n";
    if (received <= 0 || !has_prefix(response, (size_t)received, "HTTP/1.0 200 OK\r\n") ||
        !body || (size_t)received - body != sizeof(html) - 1 ||
        !has_prefix(response + body, (size_t)received - body, html)) {
        say("curl: invalid HTTP response\n");
        return 1;
    }
    if (write(1, response + body, (size_t)received - body) < 0) {
        say("curl: output failed\n");
        return 1;
    }
    say("curl: HTTP 200 loopback PASS\n");
    return 0;
}
