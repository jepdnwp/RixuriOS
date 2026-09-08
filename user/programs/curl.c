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
    char response[128] = {0};
    int received = socket_receive(fd, response, sizeof(response) - 1, 0);
    if (received < 15 || !has_prefix(response, (size_t)received, "HTTP/1.0 200 OK\r\n") ||
        !has_prefix(response + received - 15, 15, "Hello RixuriOS\n")) {
        say("curl: invalid HTTP response\n");
        return 1;
    }
    say("curl: HTTP 200 loopback PASS\n");
    return 0;
}
