#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

static size_t length(const char *text) {
    size_t n = 0;
    while (text && text[n]) ++n;
    return n;
}

static void out(const char *text) {
    (void)write(2, text, length(text));
}

static int parse_seconds(const char *text, uint64_t *value) {
    uint64_t result = 0;
    if (!text || !text[0] || !value) return -1;
    for (size_t i = 0; text[i]; ++i) {
        if (text[i] < '0' || text[i] > '9') return -1;
        uint64_t digit = (uint64_t)(text[i] - '0');
        if (result > (UINT64_MAX - digit) / 10u) return -1;
        result = result * 10u + digit;
    }
    *value = result;
    return 0;
}

int program_main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc != 2) {
        out("sleep: expected seconds\n");
        return 2;
    }
    uint64_t seconds;
    if (parse_seconds(argv[1], &seconds) != 0) {
        out("sleep: invalid duration\n");
        return 2;
    }
    rix_timespec_t request = {seconds, 0};
    return nanosleep(&request, NULL) == 0 ? 0 : 1;
}
