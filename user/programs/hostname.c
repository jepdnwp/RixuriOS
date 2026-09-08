#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

static void out(const char *s) {
    size_t n = 0;
    while (s && s[n]) ++n;
    (void)write(1, s, n);
}

int program_main(int argc, char **argv, char **envp) {
    static char name[128];
    int fd;
    rix_ssize_t n;
    size_t used = 0;
    (void)argv;
    (void)envp;
    if (argc != 1) { out("hostname: arguments unsupported\n"); return 2; }
    fd = openat(-100, "/etc/hostname", 0u, 0u);
    if (fd < 0) { out("hostname: cannot open /etc/hostname\n"); return 1; }
    for (;;) {
        if (used + 1u >= sizeof(name)) { (void)close(fd); out("hostname: name too long\n"); return 1; }
        n = read(fd, name + used, sizeof(name) - used - 1u);
        if (n < 0) { (void)close(fd); out("hostname: read failed\n"); return 1; }
        if (n == 0) break;
        used += (size_t)n;
    }
    (void)close(fd);
    name[used] = 0;
    while (used && (name[used - 1u] == '\n' || name[used - 1u] == '\r')) name[--used] = 0;
    if (!used) { out("hostname: empty name\n"); return 1; }
    out(name);
    out("\n");
    return 0;
}
