#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

static size_t length(const char *text) {
    size_t count = 0;
    while (text && text[count]) ++count;
    return count;
}

static int contains(const char *line, size_t line_length, const char *pattern,
                   size_t pattern_length) {
    if (pattern_length == 0) return 1;
    if (pattern_length > line_length) return 0;
    for (size_t start = 0; start + pattern_length <= line_length; ++start) {
        size_t index = 0;
        while (index < pattern_length && line[start + index] == pattern[index]) ++index;
        if (index == pattern_length) return 1;
    }
    return 0;
}

static int grep_fd(int fd, const char *pattern, size_t pattern_length) {
    char line[256];
    size_t used = 0;
    for (;;) {
        char buffer[128];
        rix_ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count < 0) return 1;
        if (count == 0) {
            if (used && contains(line, used, pattern, pattern_length)) {
                if (write(1, line, used) != (rix_ssize_t)used || write(1, "\n", 1) != 1) return 1;
            }
            return 0;
        }
        for (size_t i = 0; i < (size_t)count; ++i) {
            if (buffer[i] == '\n') {
                if (contains(line, used, pattern, pattern_length)) {
                    if (write(1, line, used) != (rix_ssize_t)used || write(1, "\n", 1) != 1) return 1;
                }
                used = 0;
            } else if (used + 1u < sizeof(line)) {
                line[used++] = buffer[i];
            } else {
                return 1;
            }
        }
    }
}

int program_main(int argc, char **argv) {
    int status = 0;
    if (argc < 2) {
        return 2;
    }
    size_t pattern_length = length(argv[1]);
    if (argc == 2) {
        status = grep_fd(0, argv[1], pattern_length);
    } else {
        for (int index = 2; index < argc; ++index) {
            int fd = openat(-100, argv[index], 0u, 0u);
            if (fd < 0 || grep_fd(fd, argv[1], pattern_length) != 0) status = 1;
            if (fd >= 0) (void)close(fd);
        }
    }
    return status;
}
