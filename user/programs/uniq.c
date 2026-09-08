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

static int write_number_word(uint64_t value, const char *space) {
    return write_number(value) == 0 && write_all(1, space, 1) == 0 ? 0 : 1;
}

int program_main(int argc, char **argv) {
    int fd = 0;
    int close_fd = 0;
    int failure = 0;
    int count_mode = 0;
    int dup_only = 0;
    int uniq_only = 0;
    uint8_t first[UNIQ_LINE_CAPACITY];
    uint8_t second[UNIQ_LINE_CAPACITY];
    uint8_t *previous = first;
    uint8_t *current = second;
    size_t previous_length = 0;
    int has_previous = 0;
    uint64_t repeat_count = 0;
    uniq_reader_t reader;
    int arg_index = 1;

    while (arg_index < argc && argv[arg_index][0] == '-') {
        for (size_t i = 1; argv[arg_index][i]; ++i) {
            if (argv[arg_index][i] == 'c') count_mode = 1;
            else if (argv[arg_index][i] == 'd') dup_only = 1;
            else if (argv[arg_index][i] == 'u') uniq_only = 1;
            else {
                write_error("uniq: invalid option\n");
                return 2;
            }
        }
        ++arg_index;
    }

    if (dup_only && uniq_only) {
        write_error("uniq: -d and -u cannot be combined\n");
        return 2;
    }

    if (arg_index < argc && !(argv[arg_index][0] == '-' && argv[arg_index][1] == '\0')) {
        fd = openat(-100, argv[arg_index], 0u, 0u);
        if (fd < 0) {
            write_error("uniq: failed to open path\n");
            return 1;
        }
        close_fd = 1;
        ++arg_index;
    }

    if (arg_index < argc) {
        write_error("uniq: expected at most one path\n");
        return 2;
    }

    reader.fd = fd;
    reader.offset = 0;
    reader.count = 0;
    reader.eof = 0;
    reader.error = 0;
    for (;;) {
        size_t current_length = 0;
        int line_status = read_line(&reader, current, &current_length);
        if (line_status == 0) {
            if (count_mode && has_previous) {
                write_number_word(repeat_count, " ");
                write_all(1, previous, previous_length);
            }
            break;
        }
        if (line_status == -1) {
            failure = 1;
            break;
        }
        if (line_status == -2) {
            failure = 2;
            break;
        }

        if (!has_previous) {
            repeat_count = 1;
            has_previous = 1;
        } else if (same_line(previous, previous_length, current, current_length)) {
            ++repeat_count;
        } else {
            int emit = 0;
            if (count_mode) {
                if (dup_only && repeat_count > 1) emit = 1;
                else if (uniq_only && repeat_count == 1) emit = 1;
                else if (!dup_only && !uniq_only) emit = 1;
            } else {
                if (dup_only && repeat_count > 1) emit = 1;
                else if (uniq_only && repeat_count == 1) emit = 1;
                else if (!dup_only && !uniq_only) emit = 1;
            }
            if (emit) {
                if (count_mode) {
                    if (write_number_word(repeat_count, " ") != 0) {
                        failure = 1;
                        break;
                    }
                }
                if (write_all(1, previous, previous_length) != 0) {
                    failure = 1;
                    break;
                }
            }
            repeat_count = 1;
        }

        previous_length = current_length;
        {
            uint8_t *swap = previous;
            previous = current;
            current = swap;
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
