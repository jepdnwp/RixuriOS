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

    while (count != 0) {
        --count;
        if (write_all(1, digits + count, 1) != 0) return 1;
    }
    return 0;
}

static int write_counts(uint64_t lines, uint64_t words, uint64_t bytes,
                        const char *path) {
    if (write_number(lines) != 0 || write_text(1, " ") != 0 ||
        write_number(words) != 0 || write_text(1, " ") != 0 ||
        write_number(bytes) != 0) {
        return 1;
    }
    if (path && (write_text(1, " ") != 0 || write_text(1, path) != 0)) {
        return 1;
    }
    return write_text(1, "\n");
}

int program_main(int argc, char **argv) {
    if (argc < 1 || argc > 2) {
        (void)write_text(2, "wc: expected zero or one path\n");
        return 2;
    }

    int input = 0;
    const char *path = 0;
    if (argc == 2) {
        path = argv[1];
        input = openat(-100, path, 0u, 0u);
        if (input < 0) {
            (void)write_text(2, "wc: failed to open path\n");
            return 1;
        }
    }

    uint64_t lines = 0;
    uint64_t words = 0;
    uint64_t bytes = 0;
    int status = count_fd(input, &lines, &words, &bytes);
    if (argc == 2) (void)close(input);
    if (status != 0) {
        (void)write_text(2, "wc: read failed\n");
        return 1;
    }
    return write_counts(lines, words, bytes, path);
}
