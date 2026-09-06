#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

#define TR_SET_SIZE 256u
#define TR_BUFFER_SIZE 256u

typedef struct {
    uint8_t values[TR_SET_SIZE];
    uint8_t present[TR_SET_SIZE];
    size_t length;
} tr_set_t;

static size_t text_length(const char *text) {
    size_t length = 0;
    while (text && text[length] != '\0') ++length;
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

static void set_init(tr_set_t *set) {
    size_t index;
    set->length = 0;
    for (index = 0; index < TR_SET_SIZE; ++index) set->present[index] = 0;
}

static int set_append(tr_set_t *set, uint8_t value) {
    if (set->length >= TR_SET_SIZE) return 1;
    set->values[set->length++] = value;
    set->present[value] = 1;
    return 0;
}

static int set_append_range(tr_set_t *set, uint8_t first, uint8_t last) {
    uint16_t value = first;
    uint16_t end = last;
    if (first > last) return 1;
    while (value <= end) {
        if (set_append(set, (uint8_t)value) != 0) return 1;
        if (value == end) break;
        ++value;
    }
    return 0;
}

static int is_hex_digit(char value) {
    return (value >= '0' && value <= '9') ||
           (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

static uint8_t hex_value(char value) {
    if (value >= '0' && value <= '9') return (uint8_t)(value - '0');
    if (value >= 'a' && value <= 'f') return (uint8_t)(value - 'a' + 10);
    return (uint8_t)(value - 'A' + 10);
}

static int is_octal_digit(char value) {
    return value >= '0' && value <= '7';
}

static int parse_escape(const char *text, size_t *position, uint8_t *value) {
    size_t index = *position;
    char escaped;
    uint16_t number;
    size_t digits;
    if (text[index] != '\\' || text[index + 1] == '\0') return 1;
    ++index;
    escaped = text[index++];
    if (escaped == 'a') *value = 7u;
    else if (escaped == 'b') *value = 8u;
    else if (escaped == 'f') *value = 12u;
    else if (escaped == 'n') *value = 10u;
    else if (escaped == 'r') *value = 13u;
    else if (escaped == 't') *value = 9u;
    else if (escaped == 'v') *value = 11u;
    else if (escaped == 'x') {
        if (!is_hex_digit(text[index])) return 1;
        number = hex_value(text[index++]);
        if (is_hex_digit(text[index])) {
            number = (uint16_t)(number * 16u + hex_value(text[index++]));
        }
        *value = (uint8_t)number;
    } else if (is_octal_digit(escaped)) {
        number = (uint16_t)(escaped - '0');
        digits = 1;
        while (digits < 3u && is_octal_digit(text[index])) {
            number = (uint16_t)(number * 8u + (uint16_t)(text[index++] - '0'));
            ++digits;
        }
        *value = (uint8_t)number;
    } else {
        *value = (uint8_t)escaped;
    }
    *position = index;
    return 0;
}

static int text_is(const char *text, size_t start, size_t length,
                   const char *expected) {
    size_t index = 0;
    while (index < length) {
        if (expected[index] == '\0' || text[start + index] != expected[index]) {
            return 0;
        }
        ++index;
    }
    return expected[length] == '\0';
}

static int class_kind(const char *text, size_t start, size_t length) {
    if (text_is(text, start, length, "alnum")) return 1;
    if (text_is(text, start, length, "alpha")) return 2;
    if (text_is(text, start, length, "ascii")) return 3;
    if (text_is(text, start, length, "blank")) return 4;
    if (text_is(text, start, length, "cntrl")) return 5;
    if (text_is(text, start, length, "digit")) return 6;
    if (text_is(text, start, length, "graph")) return 7;
    if (text_is(text, start, length, "lower")) return 8;
    if (text_is(text, start, length, "print")) return 9;
    if (text_is(text, start, length, "punct")) return 10;
    if (text_is(text, start, length, "space")) return 11;
    if (text_is(text, start, length, "upper")) return 12;
    if (text_is(text, start, length, "xdigit")) return 13;
    return 0;
}

static int append_class(tr_set_t *set, int kind) {
    if (kind == 1) {
        if (set_append_range(set, (uint8_t)'0', (uint8_t)'9') != 0 ||
            set_append_range(set, (uint8_t)'A', (uint8_t)'Z') != 0 ||
            set_append_range(set, (uint8_t)'a', (uint8_t)'z') != 0) return 1;
    } else if (kind == 2) {
        if (set_append_range(set, (uint8_t)'A', (uint8_t)'Z') != 0 ||
            set_append_range(set, (uint8_t)'a', (uint8_t)'z') != 0) return 1;
    } else if (kind == 3) {
        if (set_append_range(set, 0u, 127u) != 0) return 1;
    } else if (kind == 4) {
        if (set_append(set, (uint8_t)' ') != 0 ||
            set_append(set, (uint8_t)'\t') != 0) return 1;
    } else if (kind == 5) {
        if (set_append_range(set, 0u, 31u) != 0 ||
            set_append(set, 127u) != 0) return 1;
    } else if (kind == 6) {
        if (set_append_range(set, (uint8_t)'0', (uint8_t)'9') != 0) return 1;
    } else if (kind == 7) {
        if (set_append_range(set, 33u, 126u) != 0) return 1;
    } else if (kind == 8) {
        if (set_append_range(set, (uint8_t)'a', (uint8_t)'z') != 0) return 1;
    } else if (kind == 9) {
        if (set_append_range(set, 32u, 126u) != 0) return 1;
    } else if (kind == 10) {
        if (set_append_range(set, 33u, 47u) != 0 ||
            set_append_range(set, 58u, 64u) != 0 ||
            set_append_range(set, 91u, 96u) != 0 ||
            set_append_range(set, 123u, 126u) != 0) return 1;
    } else if (kind == 11) {
        if (set_append(set, (uint8_t)'\t') != 0 ||
            set_append(set, (uint8_t)'\n') != 0 ||
            set_append(set, (uint8_t)'\v') != 0 ||
            set_append(set, (uint8_t)'\f') != 0 ||
            set_append(set, (uint8_t)'\r') != 0) return 1;
    } else if (kind == 12) {
        if (set_append_range(set, (uint8_t)'A', (uint8_t)'Z') != 0) return 1;
    } else if (kind == 13) {
        if (set_append_range(set, (uint8_t)'0', (uint8_t)'9') != 0 ||
            set_append_range(set, (uint8_t)'A', (uint8_t)'F') != 0 ||
            set_append_range(set, (uint8_t)'a', (uint8_t)'f') != 0) return 1;
    } else {
        return 1;
    }
    return 0;
}

/* Return 1 for a class, 0 for an ordinary character, and -1 for bad syntax. */
static int parse_class(const char *text, size_t *position, tr_set_t *set) {
    size_t index;
    size_t name_start;
    size_t name_length;
    size_t end_offset;
    int kind;
    if (text[*position] != '[' || text[*position + 1] != ':') {
        if (text[*position] != '[' || text[*position + 1] != '[' ||
            text[*position + 2] != ':') return 0;
        name_start = *position + 3u;
        end_offset = 2u;
    } else {
        name_start = *position + 2u;
        end_offset = 1u;
    }
    index = name_start;
    while (text[index] != '\0' && text[index] != ':') ++index;
    if (text[index] == '\0' || text[index + 1] != ']' ||
        (end_offset == 2u && text[index + 2] != ']')) return -1;
    name_length = index - name_start;
    kind = class_kind(text, name_start, name_length);
    if (kind == 0 || append_class(set, kind) != 0) return -1;
    *position = index + end_offset + 1u;
    return 1;
}

static int parse_literal(const char *text, size_t *position, uint8_t *value) {
    if (text[*position] == '\\') {
        return parse_escape(text, position, value);
    }
    *value = (uint8_t)text[*position];
    ++*position;
    return 0;
}

static int parse_set(const char *text, tr_set_t *set) {
    size_t position = 0;
    size_t endpoint_position;
    uint8_t first;
    uint8_t last;
    int class_result;
    set_init(set);
    while (text[position] != '\0') {
        class_result = parse_class(text, &position, set);
        if (class_result < 0) return 1;
        if (class_result > 0) continue;
        if (parse_literal(text, &position, &first) != 0) return 1;
        if (text[position] == '-' && text[position + 1] != '\0') {
            endpoint_position = position + 1u;
            if (!(text[endpoint_position] == '[' &&
                  text[endpoint_position + 1] == ':') &&
                !(text[endpoint_position] == '[' &&
                  text[endpoint_position + 1] == '[' &&
                  text[endpoint_position + 2] == ':') &&
                parse_literal(text, &endpoint_position, &last) == 0) {
                if (set_append_range(set, first, last) != 0) return 1;
                position = endpoint_position;
                continue;
            }
        }
        if (set_append(set, first) != 0) return 1;
    }
    return 0;
}

static void initialize_identity(uint8_t *map) {
    size_t value;
    for (value = 0; value < TR_SET_SIZE; ++value) map[value] = (uint8_t)value;
}

static void choose_source(const tr_set_t *set1, int complement,
                          uint8_t *selected) {
    size_t value;
    for (value = 0; value < TR_SET_SIZE; ++value) {
        selected[value] = (uint8_t)(complement ? !set1->present[value] :
                                    set1->present[value]);
    }
}

static size_t selected_count(const uint8_t *selected) {
    size_t value;
    size_t count = 0;
    for (value = 0; value < TR_SET_SIZE; ++value) {
        if (selected[value]) ++count;
    }
    return count;
}

static void build_translation(const tr_set_t *set1, const tr_set_t *set2,
                              const uint8_t *selected, int complement,
                              uint8_t *map) {
    size_t value;
    size_t index;
    size_t target = 0;
    initialize_identity(map);
    if (!complement) {
        for (index = 0; index < set1->length; ++index) {
            value = set1->values[index];
            map[value] = set2->values[index < set2->length ? index :
                                       set2->length - 1u];
        }
    } else {
        for (value = 0; value < TR_SET_SIZE; ++value) {
            if (selected[value]) {
                map[value] = set2->values[target < set2->length ? target :
                                          set2->length - 1u];
                ++target;
            }
        }
    }
}

static int output_put(uint8_t *output, size_t *used, uint8_t value) {
    if (*used == TR_BUFFER_SIZE) {
        if (write_all(1, output, *used) != 0) return 1;
        *used = 0;
    }
    output[(*used)++] = value;
    return 0;
}

static int translate_fd(int fd, const uint8_t *selected, const uint8_t *map,
                        int translate, int delete_chars, int squeeze,
                        const uint8_t *squeeze_set) {
    uint8_t input[TR_BUFFER_SIZE];
    uint8_t output[TR_BUFFER_SIZE];
    size_t output_used = 0;
    uint8_t previous = 0;
    int have_previous = 0;
    for (;;) {
        rix_ssize_t count = read(fd, input, sizeof(input));
        size_t index;
        if (count < 0) return 1;
        if (count == 0) break;
        for (index = 0; index < (size_t)count; ++index) {
            uint8_t value = input[index];
            if (delete_chars && selected[value]) continue;
            if (translate && selected[value]) value = map[value];
            if (squeeze && squeeze_set[value] && have_previous &&
                previous == value) continue;
            if (output_put(output, &output_used, value) != 0) return 1;
            if (squeeze && squeeze_set[value]) {
                previous = value;
                have_previous = 1;
            } else {
                have_previous = 0;
            }
        }
    }
    if (output_used != 0 && write_all(1, output, output_used) != 0) return 1;
    return 0;
}

static int parse_options(int argc, char **argv, int *complement,
                         int *delete_chars, int *squeeze, const char **operands,
                         size_t *operand_count) {
    int argument_index;
    int options_done = 0;
    *complement = 0;
    *delete_chars = 0;
    *squeeze = 0;
    *operand_count = 0;
    for (argument_index = 1; argument_index < argc; ++argument_index) {
        const char *argument = argv[argument_index];
        size_t offset;
        if (!options_done && argument[0] == '-' && argument[1] != '\0') {
            if (argument[1] == '-' && argument[2] == '\0') {
                options_done = 1;
                continue;
            }
            offset = 1;
            while (argument[offset] != '\0') {
                char option = argument[offset++];
                if (option == 'c' || option == 'C') *complement = 1;
                else if (option == 'd') *delete_chars = 1;
                else if (option == 's') *squeeze = 1;
                else return 1;
            }
        } else {
            if (*operand_count >= 3u) return 1;
            operands[(*operand_count)++] = argument;
        }
    }
    return 0;
}

int program_main(int argc, char **argv) {
    const char *operands[3];
    size_t operand_count;
    size_t set_count;
    const char *path = 0;
    tr_set_t set1;
    tr_set_t set2;
    uint8_t selected[TR_SET_SIZE];
    uint8_t map[TR_SET_SIZE];
    uint8_t squeeze_set[TR_SET_SIZE];
    int complement;
    int delete_chars;
    int squeeze;
    int translate;
    int input = 0;
    int close_input = 0;
    int status;
    size_t value;

    if (argc < 1 || parse_options(argc, argv, &complement, &delete_chars,
                                  &squeeze, operands, &operand_count) != 0) {
        write_error("tr: invalid arguments\n");
        return 2;
    }
    if (operand_count == 0u) {
        write_error("tr: usage: tr [-cC] [-ds] string1 [string2] [path]\n");
        return 2;
    }
    if (delete_chars && squeeze) {
        if (operand_count < 2u || operand_count > 3u) {
            write_error("tr: -d and -s require two strings\n");
            return 2;
        }
        set_count = 2u;
    } else if (delete_chars) {
        if (operand_count > 2u) {
            write_error("tr: expected one string and an optional path\n");
            return 2;
        }
        set_count = 1u;
    } else if (squeeze) {
        if (operand_count > 3u) {
            write_error("tr: expected at most two strings and one path\n");
            return 2;
        }
        set_count = operand_count == 3u ? 2u : operand_count;
    } else {
        if (operand_count < 2u || operand_count > 3u) {
            write_error("tr: expected two strings and an optional path\n");
            return 2;
        }
        set_count = 2u;
    }
    if (set_count == 0u || set_count > operand_count) {
        write_error("tr: invalid strings\n");
        return 2;
    }
    if (operand_count > set_count) path = operands[set_count];
    if (parse_set(operands[0], &set1) != 0 ||
        (set_count == 2u && parse_set(operands[1], &set2) != 0)) {
        write_error("tr: invalid character set\n");
        return 2;
    }

    choose_source(&set1, complement, selected);
    translate = !delete_chars && set_count == 2u;
    if (translate && selected_count(selected) != 0u && set2.length == 0u) {
        write_error("tr: string2 must not be empty\n");
        return 2;
    }
    if (delete_chars && !squeeze) translate = 0;
    initialize_identity(map);
    if (translate) {
        build_translation(&set1, &set2, selected, complement, map);
    }

    for (value = 0; value < TR_SET_SIZE; ++value) squeeze_set[value] = 0;
    if (squeeze) {
        if (set_count == 2u && set2.length != 0u) {
            for (value = 0; value < TR_SET_SIZE; ++value) {
                squeeze_set[value] = set2.present[value];
            }
        } else {
            for (value = 0; value < TR_SET_SIZE; ++value) {
                squeeze_set[value] = selected[value];
            }
        }
    }

    if (path && !(path[0] == '-' && path[1] == '\0')) {
        input = openat(-100, path, 0u, 0u);
        if (input < 0) {
            write_error("tr: failed to open path\n");
            return 1;
        }
        close_input = 1;
    }
    status = translate_fd(input, selected, map, translate, delete_chars,
                          squeeze, squeeze_set);
    if (close_input && close(input) != 0) status = 1;
    if (status != 0) write_error("tr: read or write failed\n");
    return status;
}
