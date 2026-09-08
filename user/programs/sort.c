#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

#define SORT_DATA_CAPACITY 65536u
#define SORT_LINE_CAPACITY 1024u

static uint8_t sort_data[SORT_DATA_CAPACITY];
static size_t sort_offsets[SORT_LINE_CAPACITY];
static size_t sort_lengths[SORT_LINE_CAPACITY];

static size_t length(const char *text) {
    size_t count = 0;
    while (text && text[count]) ++count;
    return count;
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
    return write_all(fd, text, length(text));
}

static int is_match(const char *a, const char *b) {
    size_t i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i] && a[i] == b[i]) ++i;
    return a[i] == 0 && b[i] == 0;
}

static int compare_lines(size_t line1, size_t line2) {
    size_t offset1 = sort_offsets[line1];
    size_t offset2 = sort_offsets[line2];
    size_t len1 = sort_lengths[line1];
    size_t len2 = sort_lengths[line2];
    size_t common = len1 < len2 ? len1 : len2;
    for (size_t index = 0; index < common; ++index) {
        uint8_t left = sort_data[offset1 + index];
        uint8_t right = sort_data[offset2 + index];
        if (left < right) return -1;
        if (left > right) return 1;
    }
    if (len1 < len2) return -1;
    if (len1 > len2) return 1;
    return 0;
}

static void swap_lines(size_t left, size_t right) {
    size_t offset = sort_offsets[left];
    size_t len = sort_lengths[left];
    sort_offsets[left] = sort_offsets[right];
    sort_lengths[left] = sort_lengths[right];
    sort_offsets[right] = offset;
    sort_lengths[right] = len;
}

static size_t partition(size_t low, size_t high) {
    size_t i = low;
    for (size_t j = low; j < high; ++j) {
        int cmp = compare_lines(j, high);
        if (cmp < 0 || (cmp == 0 && sort_lengths[j] < sort_lengths[high])) {
            swap_lines(i, j);
            ++i;
        }
    }
    swap_lines(i, high);
    return i;
}

static void quicksort(size_t low, size_t high) {
    if (low >= high) return;
    size_t pivot = partition(low, high);
    if (pivot > 0) quicksort(low, pivot - 1);
    quicksort(pivot + 1, high);
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
                if (*line_count >= SORT_LINE_CAPACITY) return 2;
                sort_offsets[*line_count] = line_start;
                sort_lengths[*line_count] = line_length;
                ++*line_count;
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

    if (line_length != 0) {
        if (*line_count >= SORT_LINE_CAPACITY) return 2;
        sort_offsets[*line_count] = line_start;
        sort_lengths[*line_count] = line_length;
        ++*line_count;
    }
    return 0;
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
    int reverse = 0;
    int arg_index = 1;

    if (argc >= 2 && is_match(argv[1], "-r")) {
        reverse = 1;
        ++arg_index;
    }

    if (arg_index > argc || arg_index + 1 < argc) {
        write_text(2, "sort: expected zero or one path\n");
        return 2;
    }

    int input = 0;
    int close_input = 0;
    size_t line_count;
    size_t data_used;
    int status;

    if (arg_index == argc - 1) {
        input = openat(-100, argv[arg_index], 0u, 0u);
        if (input < 0) {
            write_text(2, "sort: failed to open path\n");
            return 1;
        }
        close_input = 1;
    }

    status = read_lines(input, &line_count, &data_used);
    if (close_input) (void)close(input);
    if (status == 1) {
        write_text(2, "sort: read failed\n");
        return 1;
    }
    if (status == 2) {
        write_text(2, "sort: input exceeds bounded memory\n");
        return 1;
    }

    if (line_count > 1) {
        if (reverse) {
            for (size_t i = 0; i < line_count / 2; ++i) {
                swap_lines(i, line_count - i - 1);
            }
        } else {
            quicksort(0, line_count - 1);
        }
    }

    if (write_lines(line_count) != 0) {
        write_text(2, "sort: write failed\n");
        return 1;
    }
    return 0;
}
