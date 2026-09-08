#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

static size_t length(const char *text) {
    size_t count = 0;
    while (text && text[count]) ++count;
    return count;
}

static int write_all(int fd, const void *buffer, size_t count) {
    const uint8_t *bytes = (const uint8_t *)buffer;
    size_t done = 0;
    while (done < count) {
        rix_ssize_t written = write(fd, bytes + done, count - done);
        if (written <= 0) return 1;
        done += (size_t)written;
    }
    return 0;
}

static int write_text(int fd, const char *text) {
    return write_all(fd, text, length(text));
}

static int is_space(uint8_t value) {
    return value == (uint8_t)' ' || value == (uint8_t)'\t' ||
           value == (uint8_t)'\n' || value == (uint8_t)'\v' ||
           value == (uint8_t)'\f' || value == (uint8_t)'\r';
}

static int count_fd(int input, uint64_t *lines, uint64_t *words,
                    uint64_t *bytes) {
    uint8_t buffer[256];
    int in_word = 0;

    for (;;) {
        rix_ssize_t count = read(input, buffer, sizeof(buffer));
        if (count < 0) return 1;
        if (count == 0) return 0;
        *bytes += (uint64_t)count;
        for (size_t index = 0; index < (size_t)count; ++index) {
            uint8_t value = buffer[index];
            if (value == (uint8_t)'\n') ++*lines;
            if (is_space(value)) {
                in_word = 0;
            } else if (!in_word) {
                ++*words;
                in_word = 1;
            }
        }
    }
}

static int write_number(uint64_t value) {
    char digits[20];
    size_t count = 0;
    do {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0);
    while (count) {
        if (write_all(1, digits + --count, 1) != 0) return 1;
    }
    return 0;
}

static int write_counts(uint64_t lines, uint64_t words, uint64_t bytes,
                        int show_lines, int show_words, int show_bytes,
                        const char *path) {
    int first = 1;
    if (show_lines) {
        if (!first) write_all(1, " ", 1);
        if (write_number(lines) != 0) return 1;
        first = 0;
    }
    if (show_words) {
        if (!first) write_all(1, " ", 1);
        if (write_number(words) != 0) return 1;
        first = 0;
    }
    if (show_bytes) {
        if (!first) write_all(1, " ", 1);
        if (write_number(bytes) != 0) return 1;
        first = 0;
    }
    if (path) {
        if (!first) write_all(1, " ", 1);
        if (write_text(1, path) != 0) return 1;
    }
    return write_text(1, "\n");
}

static int is_match(const char *a, const char *b) {
    size_t i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i] && a[i] == b[i]) ++i;
    return a[i] == 0 && b[i] == 0;
}

int program_main(int argc, char **argv) {
    int show_lines = 0, show_words = 0, show_bytes = 0;
    int arg_index = 1;

    if (argc < 2) {
        show_lines = show_words = show_bytes = 1;
    } else {
        while (arg_index < argc && argv[arg_index][0] == '-') {
            if (is_match(argv[arg_index], "--")) {
                ++arg_index;
                break;
            }
            for (size_t i = 1; argv[arg_index][i]; ++i) {
                if (argv[arg_index][i] == 'l') show_lines = 1;
                else if (argv[arg_index][i] == 'w') show_words = 1;
                else if (argv[arg_index][i] == 'c') show_bytes = 1;
                else {
                    write_text(2, "wc: invalid option\n");
                    return 2;
                }
            }
            ++arg_index;
        }
        if (!show_lines && !show_words && !show_bytes) {
            show_lines = show_words = show_bytes = 1;
        }
    }

    if (arg_index == argc) {
        uint64_t lines = 0, words = 0, bytes = 0;
        if (count_fd(0, &lines, &words, &bytes) != 0) {
            write_text(2, "wc: read failed\n");
            return 1;
        }
        return write_counts(lines, words, bytes, show_lines, show_words, show_bytes, 0);
    }

    int status = 0;
    uint64_t total_lines = 0, total_words = 0, total_bytes = 0;
    int has_paths = 0;
    for (; arg_index < argc; ++arg_index) {
        const char *path = argv[arg_index];
        int fd = openat(-100, path, 0u, 0u);
        if (fd < 0) {
            write_text(2, "wc: cannot open '");
            write_text(2, path);
            write_text(2, "'\n");
            status = 1;
            continue;
        }
        uint64_t lines = 0, words = 0, bytes = 0;
        if (count_fd(fd, &lines, &words, &bytes) != 0) {
            write_text(2, "wc: read failed on '");
            write_text(2, path);
            write_text(2, "'\n");
            status = 1;
        } else {
            write_counts(lines, words, bytes, show_lines, show_words, show_bytes, path);
            total_lines += lines;
            total_words += words;
            total_bytes += bytes;
            has_paths = 1;
        }
        (void)close(fd);
    }
    if (has_paths) {
        write_counts(total_lines, total_words, total_bytes, show_lines, show_words, show_bytes, "total");
    }
    return status;
}
