#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

static size_t length(const char *s) { size_t n = 0; while (s && s[n]) ++n; return n; }
static void err(const char *s) { (void)write(2, s, length(s)); }

static int parse_octal(const char *s, uint32_t *out) {
    uint32_t value = 0;
    size_t digits = 0;
    if (!s || !*s || !out) return -1;
    while (*s) {
        if (*s < '0' || *s > '7' || digits >= 4u) return -1;
        value = value * 8u + (uint32_t)(*s - '0');
        ++digits;
        ++s;
    }
    if (value > 07777u) return -1;
    *out = value;
    return 0;
}

int program_main(int argc, char **argv, char **envp) {
    (void)envp;
    uint32_t mode = 0;
    if (argc != 3 || parse_octal(argv[1], &mode) != 0) {
        err("chmod: expected octal mode and path\n");
        return 2;
    }
    if (chmod(argv[2], mode) != 0) { err("chmod: failed\n"); return 1; }
    return 0;
}

int main(int argc, char **argv, char **envp) { return program_main(argc, argv, envp); }
