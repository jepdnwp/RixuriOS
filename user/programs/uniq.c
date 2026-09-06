#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

#define UNIQ_LINE_CAPACITY 4096u
#define UNIQ_READ_CAPACITY 256u

typedef struct {
    int fd;
    uint8_t buffer[UNIQ_READ_CAPACITY];
    size_t offset;
    size_t count;
    int eof;
    int error;
} uniq_reader_t;

static size_t text_length(const char *text) {
    size_t length = 0;
    while (text && text[length] != '\0') ++length;
    return length;
}

static int write_all(int fd, const void *buffer, size_t count) {
    const uint8_t *bytes = (const uint8_t *)buffer;
    size_t written = 0;
    while (written < count) {
        rix_ssize_t result = write(fd, bytes + written, count - written);
        if (result <= 0) return 1;
        written += (size_t)result;
    }
    return 0;
}

static void write_error(const char *text) {
    (void)write_all(2, text, text_length(text));
}

static int reader_next(uniq_reader_t *reader, uint8_t *value) {
    if (reader->offset == reader->count) {
        if (reader->eof) return 0;
        rix_ssize_t count = read(reader->fd, reader->buffer,
                                 sizeof(reader->buffer));
        if (count < 0) {
            reader->error = 1;
            return -1;
        }
        if (count == 0) {
            reader->eof = 1;
            return 0;
        }
        reader->offset = 0;
        reader->count = (size_t)count;
    }
    *value = reader->buffer[reader->offset++];
    return 1;
}

static int read_line(uniq_reader_t *reader, uint8_t *line, size_t *length) {
    size_t used = 0;
    for (;;) {
        uint8_t value;
        int result = reader_next(reader, &value);
        if (result < 0) return -1;
        if (result == 0) {
            if (used == 0) return 0;
            *length = used;
            return 1;
        }
        if (used >= UNIQ_LINE_CAPACITY) return -2;
        line[used++] = value;
        if (value == (uint8_t)'\n') {
            *length = used;
            return 1;
        }
    }
}

static int same_line(const uint8_t *left, size_t left_length,
                     const uint8_t *right, size_t right_length) {
    if (left_length != right_length) return 0;
    for (size_t index = 0; index < left_length; ++index) {
        if (left[index] != right[index]) return 0;
    }
    return 1;
}

int program_main(int argc, char **argv) {
    int fd = 0;
    int close_fd = 0;
    int failure = 0;
    uint8_t first[UNIQ_LINE_CAPACITY];
    uint8_t second[UNIQ_LINE_CAPACITY];
    uint8_t *previous = first;
    uint8_t *current = second;
    size_t previous_length = 0;
    int has_previous = 0;
    uniq_reader_t reader;

    if (argc < 1 || argc > 2) {
        write_error("uniq: expected zero or one path\n");
        return 2;
    }
    if (argc == 2 && !(argv[1][0] == '-' && argv[1][1] == '\0')) {
        fd = openat(-100, argv[1], 0u, 0u);
        if (fd < 0) {
            write_error("uniq: failed to open path\n");
            return 1;
        }
        close_fd = 1;
    }

    reader.fd = fd;
    reader.offset = 0;
    reader.count = 0;
    reader.eof = 0;
    reader.error = 0;
    for (;;) {
        size_t current_length = 0;
        int line_status = read_line(&reader, current, &current_length);
        if (line_status == 0) break;
        if (line_status == -1) {
            failure = 1;
            break;
        }
        if (line_status == -2) {
            failure = 2;
            break;
        }
        if (!has_previous ||
            !same_line(previous, previous_length, current, current_length)) {
            if (write_all(1, current, current_length) != 0) {
                failure = 1;
                break;
            }
            previous_length = current_length;
            {
                uint8_t *swap = previous;
                previous = current;
                current = swap;
            }
            has_previous = 1;
        }
    }

    if (close_fd && close(fd) != 0 && failure == 0) failure = 1;
    if (failure == 2) {
        write_error("uniq: input line exceeds bounded memory\n");
    } else if (failure != 0) {
        write_error("uniq: read or write failed\n");
    }
    return failure;
}
