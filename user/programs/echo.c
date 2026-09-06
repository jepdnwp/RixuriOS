#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

static size_t length(const char *s) { size_t n = 0; while (s && s[n]) ++n; return n; }
static void out(const char *s) { (void)write(1, s, length(s)); }

int program_main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        if (i > 1) out(" ");
        out(argv[i]);
    }
    out("\n");
    return 0;
}
