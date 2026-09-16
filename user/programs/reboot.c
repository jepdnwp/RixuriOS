#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

static size_t length(const char *s) { size_t n = 0; while (s && s[n]) ++n; return n; }
static void err(const char *s) { (void)write(2, s, length(s)); }

int program_main(int argc, char **argv, char **envp) {
    (void)argv; (void)envp;
    if (argc != 1) { err("reboot: arguments unsupported\n"); return 2; }
    /* On success the machine resets and this never returns. UID 0 only;
     * the kernel rejects everyone else with EACCES. */
    if (reboot() != 0) { err("reboot: failed\n"); return 1; }
    return 0;
}

int main(int argc, char **argv, char **envp) { return program_main(argc, argv, envp); }
