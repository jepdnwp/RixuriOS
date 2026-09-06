#include "unistd.h"
#include <stddef.h>

static size_t length(const char *s) { size_t n = 0; while (s && s[n]) ++n; return n; }
int program_main(int argc, char **argv, char **envp) {
    (void)argv;
    if (argc != 1) { (void)write(2, "env: arguments unsupported\n", 27); return 2; }
    for (size_t i = 0; envp && envp[i]; ++i) {
        const char *s = envp[i];
        if (write(1, s, length(s)) <= 0 || write(1, "\n", 1) != 1) return 1;
    }
    return 0;
}
