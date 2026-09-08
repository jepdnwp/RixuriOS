#include "unistd.h"
#include "hosts.h"
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
static int dns_resolve_name(const char *name, uint32_t *address) {
    if (!name || !address || rix_dns_valid_name(name) != 0) return -1;
    if (rix_hosts_lookup(name, address) == 0) return 0;
    return rix_dns_query(name, rix_resolv_server(RIX_NET_DEVICE_DNS), address);
}

static int parse_endpoint(const char *text, uint32_t *ip, uint16_t *port,
                            char *host, size_t *hostlen) {
    /* Strict A.B.C.D:P form used for deterministic testing without DNS. */
    uint32_t parts[4] = {0, 0, 0, 0};
    size_t i = 0;
    int part = 0, digits = 0;
    if (!text || !ip || !port || !host || !hostlen) return -1;
    while (text[i] && text[i] != ':') {
        if (text[i] == '.') {
            if (!digits || part >= 3) return -1;
            ++part;
            digits = 0;
        } else if (text[i] >= '0' && text[i] <= '9') {
            parts[part] = parts[part] * 10u + (uint32_t)(text[i] - '0');
            if (++digits > 3 || parts[part] > 255u) return -1;
        } else {
            return -1;
        }
        ++i;
    }
    if (!digits || part != 3 || text[i] != ':') return -1;
    size_t host_end = i;
    ++i;
    uint32_t port_value = 0;
    int port_digits = 0;
    while (text[i]) {
        if (text[i] < '0' || text[i] > '9') return -1;
        port_value = port_value * 10u + (uint32_t)(text[i] - '0');
        if (++port_digits > 5 || port_value > 65535u) return -1;
        ++i;
    }
    if (!port_digits || !port_value) return -1;
    if (host_end >= 64) return -1;
    for (size_t k = 0; k < host_end; ++k) host[k] = text[k];
    host[host_end] = 0;
    *ip = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    *port = (uint16_t)port_value;
    *hostlen = host_end;
    return *ip ? 0 : -1;
}

int program_main(int argc, char **argv, char **envp) {
    (void)envp;
    uint32_t destination_ip = RIX_NET_SOCKET_LOOPBACK;
    uint16_t destination_port = 80;
    char host_buffer[256];
    size_t host_length = 9;
    const char *host_header = "localhost";
    int external = 0;
    if (argc > 1 && argv && argv[1]) {
        if (parse_endpoint(argv[1], &destination_ip, &destination_port,
                           host_buffer, &host_length) == 0) {
            external = 1;
            host_header = host_buffer;
        } else {
            size_t namelen = length(argv[1]);
            if (!namelen || namelen > 128 || rix_dns_valid_name(argv[1]) != 0) {
                say("curl: DNS/network path unavailable\n");
                return 2;
            }
            external = 1;
            if (dns_resolve_name(argv[1], &destination_ip) != 0) {
                say("curl: DNS query failed\n");
                return 2;
            }
            host_header = argv[1];
            host_length = namelen;
        }
    }
    if (!external) {
    int fd = socket_open(RIX_NET_SOCKET_TCP);
    if (fd < 0 || socket_connect(fd, (rix_net_endpoint_t){destination_ip, 80}) != 0) {
        say("curl: connect failed\n");
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
    int efd = socket_open(RIX_NET_SOCKET_TCP);
    if (efd < 0 || socket_connect(efd, (rix_net_endpoint_t){destination_ip, destination_port}) != 0) {
        say(host_header == host_buffer ? "curl: external TCP unavailable\n" : "curl: DNS resolved; external TCP unavailable\n");
        return 1;
    }
    char eget[512];
    size_t eget_length = 0;
    {
        static const char prefix[] = "GET / HTTP/1.0\r\nHost: ";
        static const char suffix[] = "\r\n\r\n";
        size_t i = 0, k;
        for (k = 0; k < sizeof(prefix) - 1 && i < sizeof(eget); ++k) eget[i++] = prefix[k];
        for (k = 0; k < host_length && i < sizeof(eget); ++k) eget[i++] = host_header[k];
        for (k = 0; k < sizeof(suffix) - 1 && i < sizeof(eget); ++k) eget[i++] = suffix[k];
        eget_length = i;
    }
    static char eresponse[4096];
    size_t total = 0;
    rix_timespec_t epause = {0, 250000000u};
    for (unsigned round = 0; round < 60; ++round) {
        if (!total) {
            if (socket_send(efd, eget, eget_length,
                            (rix_net_endpoint_t){destination_ip, destination_port}) !=
                (int)eget_length) {
                say("curl: send failed\n");
                return 1;
            }
        }
        for (unsigned attempt = 0; attempt < 200; ++attempt) {
            if (total >= sizeof(eresponse)) break;
            int got = socket_receive(efd, eresponse + total,
                                     sizeof(eresponse) - total, 0);
            if (got < 0) break;
            if (got == 0) break;
            total += (size_t)got;
            if (header_end(eresponse, total)) break;
        }
        if (header_end(eresponse, total)) break;
        (void)nanosleep(&epause, NULL);
    }
    size_t ebody = header_end(eresponse, total);
    if (total < 12 || !has_prefix(eresponse, total, "HTTP/") || !ebody) {
        say("curl: invalid HTTP response bytes=");
        {
            char number[22];
            size_t position = sizeof(number);
            uint64_t value = total;
            number[--position] = '\n';
            if (!value) number[--position] = '0';
            while (value && position) {
                number[--position] = (char)('0' + value % 10u);
                value /= 10u;
            }
            (void)write(1, number + position, sizeof(number) - position);
        }
        return 1;
    }
    size_t sp = 5;
    while (sp < total && eresponse[sp] != ' ' && eresponse[sp] != '\r' &&
           eresponse[sp] != '\n') ++sp;
    if (sp + 4 >= total || eresponse[sp] != ' ' ||
        eresponse[sp + 1] < '0' || eresponse[sp + 1] > '9' ||
        eresponse[sp + 2] < '0' || eresponse[sp + 2] > '9' ||
        eresponse[sp + 3] < '0' || eresponse[sp + 3] > '9') {
        say("curl: invalid HTTP response\n");
        return 1;
    }
    if (write(1, eresponse, total) < 0) {
        say("curl: output failed\n");
        return 1;
    }
    {
        char message[30] = "curl: HTTP 000 external PASS\n";
        message[11] = eresponse[sp + 1];
        message[12] = eresponse[sp + 2];
        message[13] = eresponse[sp + 3];
        say(message);
    }
    return 0;
}
