#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

#define CUT_RANGE_LIMIT 64u
#define CUT_LINE_SIZE 4096u
#define CUT_OUTPUT_SIZE 256u

typedef struct {
    uint64_t first;
    uint64_t last;
} cut_range_t;

typedef struct {
    cut_range_t ranges[CUT_RANGE_LIMIT];
    size_t count;
} cut_selection_t;

typedef struct {
    uint8_t buffer[CUT_OUTPUT_SIZE];
    size_t used;
    int failed;
} cut_output_t;

static size_t text_length(const char *text) {
    size_t length = 0;
    while (text && text[length]) ++length;
    return length;
}

static int write_all(int fd, const void *buffer, size_t count) {
    const uint8_t *bytes = (const uint8_t *)buffer;
    size_t done = 0;
    while (done < count) {
        rix_ssize_t written = write(fd, bytes + done, count - done);
        if (written <= 0 || (size_t)written > count - done) return 1;
        done += (size_t)written;
    }
    return 0;
}

static void write_error(const char *text) {
    (void)write_all(2, text, text_length(text));
}

static int output_flush(cut_output_t *output) {
    if (output->failed) return 1;
    if (output->used != 0 && write_all(1, output->buffer, output->used) != 0) {
        output->failed = 1;
        return 1;
    }
    output->used = 0;
    return 0;
}

static int output_put(cut_output_t *output, uint8_t value) {
    if (output->failed) return 1;
    if (output->used == sizeof(output->buffer) && output_flush(output) != 0) return 1;
    output->buffer[output->used++] = value;
    return 0;
}

static int output_write(cut_output_t *output, const uint8_t *buffer, size_t count) {
    size_t index = 0;
    while (index < count) {
        if (output_put(output, buffer[index]) != 0) return 1;
        ++index;
    }
    return 0;
}

static int is_digit(char value) {
    return value >= '0' && value <= '9';
}

static int parse_number(const char *text, size_t *index, uint64_t *number) {
    uint64_t value = 0;
    uint64_t maximum = (uint64_t)-1;
    size_t start = *index;

    while (is_digit(text[*index])) {
        uint64_t digit = (uint64_t)(text[*index] - '0');
        if (value > (maximum - digit) / 10u) return 1;
        value = value * 10u + digit;
        ++*index;
    }
    if (*index == start || value == 0) return 1;
    *number = value;
    return 0;
}

static int parse_selection(const char *text, cut_selection_t *selection) {
    size_t index = 0;

    if (!text || text[0] == '\0') return 1;
    for (;;) {
        uint64_t first = 0;
        uint64_t last = 0;
        int has_first = 0;

        if (text[index] != '-') {
            if (parse_number(text, &index, &first) != 0) return 1;
            has_first = 1;
        }
        if (text[index] == '-') {
            ++index;
            if (text[index] != '\0' && text[index] != ',' &&
                parse_number(text, &index, &last) != 0) {
                return 1;
            }
            if (!has_first) first = 1;
        } else if (has_first) {
            last = first;
        } else {
            return 1;
        }
        if (first == 0 || (last != 0 && first > last)) return 1;
        if (selection->count == CUT_RANGE_LIMIT) return 1;
        selection->ranges[selection->count].first = first;
        selection->ranges[selection->count].last = last;
        ++selection->count;

        if (text[index] == '\0') return 0;
        if (text[index] != ',') return 1;
        ++index;
        if (text[index] == '\0') return 1;
    }
}

static int selection_contains(const cut_selection_t *selection, uint64_t position) {
    size_t index;
    for (index = 0; index < selection->count; ++index) {
        const cut_range_t *range = &selection->ranges[index];
        if (position >= range->first &&
            (range->last == 0 || position <= range->last)) {
            return 1;
        }
    }
    return 0;
}

static int cut_bytes(int fd, const cut_selection_t *selection,
                     cut_output_t *output) {
    uint8_t buffer[256];
    uint64_t position = 1;

    for (;;) {
        rix_ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count < 0) return 1;
        if (count == 0) return 0;
        for (size_t index = 0; index < (size_t)count; ++index) {
            uint8_t value = buffer[index];
            if (value == (uint8_t)'\n') {
                if (output_put(output, value) != 0) return 1;
                position = 1;
            } else {
                if (selection_contains(selection, position) &&
                    output_put(output, value) != 0) {
                    return 1;
                }
                if (position != (uint64_t)-1) ++position;
            }
        }
    }
}

static int cut_field_line(const uint8_t *line, size_t length, int newline,
                          uint8_t delimiter, int suppress,
                          const cut_selection_t *selection,
                          cut_output_t *output) {
    size_t index;
    int has_delimiter = 0;

    for (index = 0; index < length; ++index) {
        if (line[index] == delimiter) {
            has_delimiter = 1;
            break;
        }
    }
    if (!has_delimiter) {
        if (!suppress && output_write(output, line, length) != 0) return 1;
    } else {
        size_t start = 0;
        uint64_t field = 1;
        int emitted = 0;

        for (index = 0; index <= length; ++index) {
            if (index == length || line[index] == delimiter) {
                if (selection_contains(selection, field)) {
                    if (emitted && output_put(output, delimiter) != 0) return 1;
                    if (output_write(output, line + start, index - start) != 0) {
                        return 1;
                    }
                    emitted = 1;
                }
                start = index + 1;
                if (field != (uint64_t)-1) ++field;
            }
        }
    }
    if (newline && output_put(output, (uint8_t)'\n') != 0) return 1;
    return 0;
}

static int cut_fields(int fd, uint8_t delimiter, int suppress,
                      const cut_selection_t *selection, cut_output_t *output) {
    uint8_t line[CUT_LINE_SIZE];
    size_t used = 0;

    for (;;) {
        uint8_t buffer[256];
        rix_ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count < 0) return 1;
        if (count == 0) break;
        for (size_t index = 0; index < (size_t)count; ++index) {
            uint8_t value = buffer[index];
            if (value == (uint8_t)'\n') {
                if (cut_field_line(line, used, 1, delimiter, suppress,
                                   selection, output) != 0) {
                    return 1;
                }
                used = 0;
            } else {
                if (used == sizeof(line)) return 1;
                line[used++] = value;
            }
        }
    }
    if (used != 0 && cut_field_line(line, used, 0, delimiter, suppress,
                                     selection, output) != 0) {
        return 1;
    }
    return 0;
}

static int option_value(const char *argument, size_t *offset, int argc,
                        char **argv, int *argument_index,
                        const char **value) {
    if (argument[*offset] != '\0') {
        *value = argument + *offset;
        while (argument[*offset] != '\0') ++*offset;
        return 0;
    }
    ++*argument_index;
    if (*argument_index >= argc) return 1;
    *value = argv[*argument_index];
    return 0;
}

static int delimiter_value(const char *text, uint8_t *delimiter) {
    if (!text || text[0] == '\0' || text[1] != '\0' || text[0] == '\n') {
        return 1;
    }
    *delimiter = (uint8_t)text[0];
    return 0;
}

int program_main(int argc, char **argv) {
    cut_selection_t selection;
    const char *path = 0;
    uint8_t delimiter = (uint8_t)'\t';
    int mode = 0;
    int suppress = 0;
    int options_done = 0;
    int argument_index;
    int fd = 0;
    int close_fd = 0;
    cut_output_t output;
    int status;

    selection.count = 0;
    output.used = 0;
    output.failed = 0;
    if (argc < 1) {
        write_error("cut: invalid arguments\n");
        return 2;
    }
    for (argument_index = 1; argument_index < argc; ++argument_index) {
        const char *argument = argv[argument_index];
        if (!options_done && argument[0] == '-' && argument[1] != '\0') {
            size_t offset = 1;
            if (argument[1] == '-' && argument[2] == '\0') {
                options_done = 1;
                continue;
            }
            while (argument[offset] != '\0') {
                char option = argument[offset++];
                const char *value;
                int requested_mode;

                if (option == 's') {
                    suppress = 1;
                    continue;
                }
                if (option == 'f') requested_mode = 1;
                else if (option == 'b' || option == 'c') requested_mode = 2;
                else if (option == 'd') requested_mode = 3;
                else {
                    write_error("cut: invalid option\n");
                    return 2;
                }
                if (option_value(argument, &offset, argc, argv,
                                 &argument_index, &value) != 0) {
                    write_error("cut: option requires an argument\n");
                    return 2;
                }
                if (requested_mode == 1 || requested_mode == 2) {
                    if (mode != 0 && mode != requested_mode) {
                        write_error("cut: selection modes cannot be combined\n");
                        return 2;
                    }
                    if (mode == requested_mode || selection.count != 0) {
                        write_error("cut: selection specified more than once\n");
                        return 2;
                    }
                    mode = requested_mode;
                    if (parse_selection(value, &selection) != 0) {
                        write_error("cut: invalid selection list\n");
                        return 2;
                    }
                } else {
                    if (delimiter_value(value, &delimiter) != 0) {
                        write_error("cut: delimiter must be one byte\n");
                        return 2;
                    }
                }
            }
        } else {
            if (path != 0) {
                write_error("cut: expected at most one path\n");
                return 2;
            }
            path = argument;
        }
    }
    if (mode == 0 || (suppress && mode != 1) || (mode != 1 && delimiter != (uint8_t)'\t')) {
        write_error("cut: usage: cut -b list | -c list | -f list [-d delim] [-s] [path]\n");
        return 2;
    }
    if (path && !(path[0] == '-' && path[1] == '\0')) {
        fd = openat(-100, path, 0u, 0u);
        if (fd < 0) {
            write_error("cut: failed to open path\n");
            return 1;
        }
        close_fd = 1;
    }

    if (mode == 1) {
        status = cut_fields(fd, delimiter, suppress, &selection, &output);
    } else {
        status = cut_bytes(fd, &selection, &output);
    }
    if (close_fd && close(fd) != 0) status = 1;
    if (status == 0 && output_flush(&output) != 0) status = 1;
    if (status != 0) write_error("cut: read or write failed\n");
    return status;
}
