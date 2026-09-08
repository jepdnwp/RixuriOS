#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

static uint64_t line_number = 1;

static int is_match(const char *a, const char *b) {
    size_t i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i] && a[i] == b[i]) ++i;
    return a[i] == 0 && b[i] == 0;
}
static void err(const char *s) {
    size_t n = 0;
    while (s && s[n]) ++n;
    (void)write(2, s, n);
}
static void emit_number(uint64_t value) {
    char buf[21]; size_t n = 0;
    char pad[7];
    size_t width = 0;
    if (value == 0) buf[n++] = '0';
    while (value != 0 && n < sizeof(buf)) { buf[n++] = (char)('0' + (value % 10u)); value /= 10u; }
    width = n < 6u ? 6u - n : 0u;
    for (size_t i = 0; i < width; ++i) pad[i] = ' ';
    pad[width] = 0;
    (void)write(1, pad, width);
    while (n != 0) { char c = buf[--n]; (void)write(1, &c, 1); }
    (void)write(1, "\t", 1);
}
static int copy_fd(int input, int number_lines, int *at_line_start) {
    uint8_t buffer[256];
    for (;;) {
        rix_ssize_t n = read(input, buffer, sizeof(buffer));
        if (n < 0) return 1;
        if (n == 0) return 0;
        for (rix_ssize_t i = 0; i < n; ++i) {
            if (number_lines && *at_line_start) {
                emit_number(line_number++);
                *at_line_start = 0;
            }
            if (write(1, &buffer[i], 1) != 1) return 1;
            if (buffer[i] == '\n') *at_line_start = 1;
        }
    }
}

int program_main(int argc, char **argv) {
    int number_lines = 0, status = 0, saw_file = 0, at_line_start = 1;
    for (int i = 1; i < argc; ++i) {
        if (is_match(argv[i], "-n")) { number_lines = 1; continue; }
        if (argv[i][0] == '-') {
            static const char usage[] = "cat: usage: cat [-n] [file...]\n";
            (void)write(2, usage, sizeof(usage) - 1u);
            return 2;
        }
        saw_file = 1;
        {
            int fd = openat(-100, argv[i], 0u, 0u);
            if (fd < 0) {
                err("cat: cannot open ");
                err(argv[i]);
                err("\n");
                status = 1;
                continue;
            }
            if (copy_fd(fd, number_lines, &at_line_start) != 0) status = 1;
            (void)close(fd);
        }
    }
    if (!saw_file && copy_fd(0, number_lines, &at_line_start) != 0) status = 1;
    return status;
}
