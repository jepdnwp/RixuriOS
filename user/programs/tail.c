#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

static size_t length(const char *s) { size_t n = 0; while (s && s[n]) ++n; return n; }
static void err(const char *s) { (void)write(2, s, length(s)); }

int program_main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc > 2) { err("tail: expected path\n"); return 2; }
    int fd = 0;
    if (argc == 2) { fd = openat(-100, argv[1], 0u, 0u); if (fd < 0) { err("tail: failed\n"); return 1; } }
    uint8_t buffer[8192]; size_t used = 0; int status = 0;
    for (;;) {
        if (used == sizeof(buffer)) { status = 1; break; }
        size_t request = sizeof(buffer) - used;
        if (request > 256u) request = 256u;
        rix_ssize_t n = read(fd, buffer + used, request);
        if (n < 0) { if (used != 0) break; status = 1; break; }
        if (n == 0) break;
        used += (size_t)n;
    }
    if (argc == 2) (void)close(fd);
    if (status != 0) { err("tail: read failed\n"); return status; }
    size_t lines = 1;
    for (size_t i = 0; i < used; ++i) if (buffer[i] == '\n' && i + 1 < used) ++lines;
    size_t skip = lines > 10 ? lines - 10 : 0; size_t first = 0;
    for (size_t i = 0; i < used && skip != 0; ++i)
        if (buffer[i] == '\n' && i + 1 < used) { first = i + 1; --skip; }
    size_t done = 0;
    while (first + done < used) {
        rix_ssize_t n = write(1, buffer + first + done, used - first - done);
        if (n <= 0) return 1;
        done += (size_t)n;
    }
    return 0;
}
