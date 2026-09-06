#include "unistd.h"
#include <stddef.h>

static size_t length(const char *s) { size_t n = 0; while (s && s[n]) ++n; return n; }
static void out(const char *s) { (void)write(2, s, length(s)); }

int program_main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc != 3) { out("ln: expected source and destination\n"); return 2; }
    if (link(argv[1], argv[2]) != 0) { out("ln: failed\n"); return 1; }
    return 0;
}
