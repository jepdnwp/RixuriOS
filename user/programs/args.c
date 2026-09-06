#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

static size_t length(const char *text) {
    size_t count = 0;
    while (text && text[count]) ++count;
    return count;
}

static void out(const char *text) {
    (void)write(1, text, length(text));
}

static void out_u64(uint64_t value) {
    char digits[21];
    size_t count = 0;
    if (value == 0) {
        out("0");
        return;
    }
    while (value && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (count) {
        char digit[1] = {digits[--count]};
        (void)write(1, digit, 1);
    }
}

int program_main(int argc, char **argv, char **envp) {

    out("argc=");
    out_u64((uint64_t)(argc < 0 ? 0 : argc));
    out("\n");
    for (int index = 0; index < argc; ++index) {
        out("argv[");
        out_u64((uint64_t)index);
        out("]=");
        out(argv[index]);
        out("\n");
    }
    for (size_t index = 0; envp && envp[index]; ++index) {
        out("envp=");
        out(envp[index]);
        out("\n");
    }
    return 0;
}
