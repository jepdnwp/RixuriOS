#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

static size_t length(const char *s) { size_t n = 0; while (s && s[n]) ++n; return n; }
static void out(const char *s) { (void)write(1, s, length(s)); }
static int is_match(const char *a, const char *b) {
    size_t i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i] && a[i] == b[i]) ++i;
    return a[i] == 0 && b[i] == 0;
}

int program_main(int argc, char **argv) {
    int start = 1, trailing = 1;
    if (argc > 1 && is_match(argv[1], "-n")) { start = 2; trailing = 0; }
    for (int i = start; i < argc; ++i) {
        if (i > start) out(" ");
        out(argv[i]);
    }
    if (trailing) out("\n");
    return 0;
}
