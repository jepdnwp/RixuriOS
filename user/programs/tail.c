#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

static size_t length(const char *s) { size_t n = 0; while (s && s[n]) ++n; return n; }
static void out(const char *s) { (void)write(2, s, length(s)); }

static int parse_number(const char *text, size_t *value) {
    size_t result = 0;
    if (!text || !text[0]) return -1;
    for (size_t i = 0; text[i]; ++i) {
        if (text[i] < '0' || text[i] > '9') return -1;
        size_t digit = (size_t)(text[i] - '0');
        if (result > (SIZE_MAX - digit) / 10u) return -1;
        result = result * 10u + digit;
    }
    *value = result;
    return 0;
}

static int copy_fd(int input, size_t max_lines) {
    uint8_t buffer[256];
    size_t lines = 0;
    int status = 0;
    for (;;) {
        rix_ssize_t n = read(input, buffer, sizeof(buffer));
        if (n < 0) { if (lines != 0) break; status = 1; break; }
        if (n == 0 || lines >= max_lines) break;
        size_t emit = (size_t)n;
        for (size_t i = 0; i < emit; ++i) {
            if (buffer[i] == '\n') {
                ++lines;
                if (lines == max_lines) { emit = i + 1; break; }
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
    return status;
}

int program_main(int argc, char **argv, char **envp) {
    (void)envp;
    size_t max_lines = 10;
    int arg_index = 1;

    while (arg_index < argc && argv[arg_index][0] == '-') {
        if (argv[arg_index][1] == 'n') {
            if (parse_number(argv[arg_index] + 2, &max_lines) != 0) {
                out("tail: invalid line count\n");
                return 2;
            }
        } else {
            out("tail: invalid option\n");
            return 2;
        }
        ++arg_index;
    }

    if (arg_index == argc) {
        return copy_fd(0, max_lines);
    }

    int status = 0;
    for (; arg_index < argc; ++arg_index) {
        const char *path = argv[arg_index];
        int fd = 0;
        int close_fd = 0;
        if (path[0] == '-' && path[1] == '\0') {
            fd = 0;
        } else {
            fd = openat(-100, path, 0u, 0u);
            if (fd < 0) {
                out("tail: cannot open '");
                out(path);
                out("'\n");
                status = 1;
                continue;
            }
            close_fd = 1;
        }
        if (copy_fd(fd, max_lines) != 0) status = 1;
        if (close_fd) (void)close(fd);
    }
    return status;
}
