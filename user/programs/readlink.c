#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

static size_t length(const char *s) { size_t n = 0; while (s && s[n]) ++n; return n; }
static void out(const char *s) { (void)write(1, s, length(s)); }
static void err(const char *s) { (void)write(2, s, length(s)); }

int program_main(int argc, char **argv, char **envp) {
    (void)envp;
    static char target[1024];
    if (argc != 2) { err("readlink: expected path\n"); return 2; }
    rix_ssize_t n = readlink(argv[1], target, sizeof(target) - 1u);
    if (n < 0) { err("readlink: failed\n"); return 1; }
    target[n] = 0;
    out(target);
    out("\n");
    return 0;
}

int main(int argc, char **argv, char **envp) { return program_main(argc, argv, envp); }
