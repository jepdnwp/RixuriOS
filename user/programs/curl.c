#include "unistd.h"
#include <stddef.h>

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

int program_main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc > 1 && argv && argv[1] && argv[1][0] != 'l') {
        say("curl: DNS/network path unavailable\n");
        return 2;
    }
    int fd = socket_open(RIX_NET_SOCKET_TCP);
    if (fd < 0 || socket_connect(fd, (rix_net_endpoint_t){RIX_NET_SOCKET_LOOPBACK, 80}) != 0) {
        say("curl: connect failed\n");
        return 1;
    }
    static const char request[] = "GET / HTTP/1.0\r\nHost: localhost\r\n\r\n";
    if (socket_send(fd, request, sizeof(request) - 1,
                    (rix_net_endpoint_t){RIX_NET_SOCKET_LOOPBACK, 80}) < 0) {
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
