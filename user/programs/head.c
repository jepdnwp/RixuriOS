#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

static size_t length(const char *s) { size_t n = 0; while (s && s[n]) ++n; return n; }
static void err(const char *s) { (void)write(2, s, length(s)); }

int program_main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc > 2) { err("head: expected path\n"); return 2; }
    int fd = 0;
    if (argc == 2) { fd = openat(-100, argv[1], 0u, 0u); if (fd < 0) { err("head: failed\n"); return 1; } }
    uint8_t buffer[256]; size_t lines = 0; int status = 0;
    for (;;) {
        rix_ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n < 0) { if (lines != 0) break; status = 1; break; }
        if (n == 0 || lines >= 10) break;
        size_t emit = (size_t)n;
        for (size_t i = 0; i < emit; ++i) {
            if (buffer[i] == '\n') {
                ++lines;
                if (lines == 10) { emit = i + 1; break; }
            }
        }
        size_t done = 0;
        while (done < emit) {
            rix_ssize_t w = write(1, buffer + done, emit - done);
            if (w <= 0) { status = 1; break; }
            done += (size_t)w;
        }
        if (status != 0 || emit < (size_t)n) break;
    }
    if (argc == 2) (void)close(fd);
    return status;
}
