#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

static size_t length(const char *text) {
    size_t count = 0;
    while (text && text[count]) ++count;
    return count;
}

static int is_match(const char *a, const char *b) {
    size_t i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i] && a[i] == b[i]) ++i;
    return a[i] == 0 && b[i] == 0;
}

static int case_equal(char a, char b) {
    if (a >= 'A' && a <= 'Z') a = (char)(a + 'a' - 'A');
    if (b >= 'A' && b <= 'Z') b = (char)(b + 'a' - 'A');
    return a == b;
}

static int contains(const char *line, size_t line_length, const char *pattern,
                    size_t pattern_length, int case_insensitive) {
    if (pattern_length == 0) return 1;
    if (pattern_length > line_length) return 0;
    for (size_t start = 0; start + pattern_length <= line_length; ++start) {
        size_t index = 0;
        while (index < pattern_length) {
            char lc = line[start + index];
            char pc = pattern[index];
            if (case_insensitive) {
                if (!case_equal(lc, pc)) break;
            } else {
                if (lc != pc) break;
            }
            ++index;
        }
        if (index == pattern_length) return 1;
    }
    return 0;
}

static int grep_fd(int fd, const char *pattern, size_t pattern_length,
                   int case_insensitive, int invert, int list_files,
                   const char *path, int *status) {
    char line[256];
    size_t used = 0;
    int matched = 0;
    for (;;) {
        char buffer[128];
        rix_ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count < 0) { *status = 1; return 1; }
        if (count == 0) {
            if (used && contains(line, used, pattern, pattern_length, case_insensitive) ^ invert) {
                if (list_files) {
                    if (!matched) {
                        (void)write(1, path, length(path));
                        (void)write(1, "\n", 1);
                        matched = 1;
                    }
                } else {
                    if (write(1, line, used) != (rix_ssize_t)used || write(1, "\n", 1) != 1) {
                        *status = 1; return 1;
                    }
                }
            }
            return 0;
        }
        for (size_t i = 0; i < (size_t)count; ++i) {
            if (buffer[i] == '\n') {
                if (contains(line, used, pattern, pattern_length, case_insensitive) ^ invert) {
                    if (list_files) {
                        if (!matched) {
                            (void)write(1, path, length(path));
                            (void)write(1, "\n", 1);
                            matched = 1;
                        }
                    } else {
                        if (write(1, line, used) != (rix_ssize_t)used || write(1, "\n", 1) != 1) {
                            *status = 1; return 1;
                        }
                    }
                }
                used = 0;
            } else if (used + 1u < sizeof(line)) {
                line[used++] = buffer[i];
            } else {
                *status = 1; return 1;
            }
        }
    }
}

int program_main(int argc, char **argv) {
    int case_insensitive = 0, invert = 0, list_files = 0;
    int arg_index = 1;

    while (arg_index < argc && argv[arg_index][0] == '-') {
        if (is_match(argv[arg_index], "--")) {
            ++arg_index;
            break;
        }
        for (size_t i = 1; argv[arg_index][i]; ++i) {
            if (argv[arg_index][i] == 'i') case_insensitive = 1;
            else if (argv[arg_index][i] == 'v') invert = 1;
            else if (argv[arg_index][i] == 'l') list_files = 1;
            else {
                write(2, "grep: invalid option\n", 22);
                return 2;
            }
        }
        ++arg_index;
    }

    if (arg_index >= argc) {
        write(2, "grep: expected pattern\n", 24);
        return 2;
    }

    const char *pattern = argv[arg_index++];
    size_t pattern_length = length(pattern);

    if (arg_index == argc) {
        int status = 0;
        grep_fd(0, pattern, pattern_length, case_insensitive, invert, list_files, "(standard input)", &status);
        return status;
    }

    int status = 0;
    for (; arg_index < argc; ++arg_index) {
        int fd = openat(-100, argv[arg_index], 0u, 0u);
        if (fd < 0) {
            write(2, "grep: cannot open '", 20);
            write(2, argv[arg_index], length(argv[arg_index]));
            write(2, "'\n", 2);
            status = 1;
            continue;
        }
        grep_fd(fd, pattern, pattern_length, case_insensitive, invert, list_files, argv[arg_index], &status);
        (void)close(fd);
    }
    return status;
}
