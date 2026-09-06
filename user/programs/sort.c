#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

#define SORT_DATA_CAPACITY 65536u
#define SORT_LINE_CAPACITY 1024u

static uint8_t sort_data[SORT_DATA_CAPACITY];
static size_t sort_offsets[SORT_LINE_CAPACITY];
static size_t sort_lengths[SORT_LINE_CAPACITY];

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

static int write_text(int fd, const char *text) {
    return write_all(fd, text, text_length(text));
}

static int compare_line_values(size_t line, size_t offset, size_t length) {
    size_t line_length = sort_lengths[line];
    size_t common_length = line_length < length ? line_length : length;

    for (size_t index = 0; index < common_length; ++index) {
        uint8_t left_byte = sort_data[sort_offsets[line] + index];
        uint8_t right_byte = sort_data[offset + index];
        if (left_byte < right_byte) return -1;
        if (left_byte > right_byte) return 1;
    }
    if (line_length < length) return -1;
    if (line_length > length) return 1;
    return 0;
}

static int add_line(size_t offset, size_t length, size_t *line_count) {
    if (*line_count >= SORT_LINE_CAPACITY) return 1;
    sort_offsets[*line_count] = offset;
    sort_lengths[*line_count] = length;
    ++*line_count;
    return 0;
}

static int read_lines(int fd, size_t *line_count, size_t *data_used) {
    uint8_t buffer[256];
    size_t line_start = 0;
    size_t line_length = 0;

    *line_count = 0;
    *data_used = 0;
    for (;;) {
        rix_ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count < 0) return 1;
        if (count == 0) break;

        for (size_t index = 0; index < (size_t)count; ++index) {
            if (buffer[index] == (uint8_t)'\n') {
                if (add_line(line_start, line_length, line_count) != 0) return 2;
                line_start = *data_used;
                line_length = 0;
            } else {
                if (*data_used >= SORT_DATA_CAPACITY) return 2;
                sort_data[*data_used] = buffer[index];
                ++*data_used;
                ++line_length;
            }
        }
    }

    if (line_length != 0 && add_line(line_start, line_length, line_count) != 0) {
        return 2;
    }
    return 0;
}

static void sort_lines(size_t line_count) {
    for (size_t index = 1; index < line_count; ++index) {
        size_t offset = sort_offsets[index];
        size_t length = sort_lengths[index];
        size_t position = index;

        while (position != 0 &&
               compare_line_values(position - 1u, offset, length) > 0) {
            sort_offsets[position] = sort_offsets[position - 1u];
            sort_lengths[position] = sort_lengths[position - 1u];
            --position;
        }
        sort_offsets[position] = offset;
        sort_lengths[position] = length;
    }
}

static int write_lines(size_t line_count) {
    for (size_t index = 0; index < line_count; ++index) {
        if (write_all(1, sort_data + sort_offsets[index], sort_lengths[index]) != 0 ||
            write_all(1, "\n", 1) != 0) {
            return 1;
        }
    }
    return 0;
}

int program_main(int argc, char **argv) {
    int input = 0;
    int close_input = 0;
    size_t line_count;
    size_t data_used;
    int status;

    if (argc < 1 || argc > 2) {
        (void)write_text(2, "sort: expected zero or one path\n");
        return 2;
    }
    if (argc == 2) {
        input = openat(-100, argv[1], 0u, 0u);
        if (input < 0) {
            (void)write_text(2, "sort: failed to open path\n");
            return 1;
        }
        close_input = 1;
    }

    status = read_lines(input, &line_count, &data_used);
    if (close_input) (void)close(input);
    if (status == 1) {
        (void)write_text(2, "sort: read failed\n");
        return 1;
    }
    if (status == 2) {
        (void)write_text(2, "sort: input exceeds bounded memory\n");
        return 1;
    }

    (void)data_used;
    sort_lines(line_count);
    if (write_lines(line_count) != 0) {
        (void)write_text(2, "sort: write failed\n");
        return 1;
    }
    return 0;
}
