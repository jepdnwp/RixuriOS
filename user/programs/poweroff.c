#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

static size_t length(const char *s) { size_t n = 0; while (s && s[n]) ++n; return n; }
static void err(const char *s) { (void)write(2, s, length(s)); }

int program_main(int argc, char **argv, char **envp) {
    (void)argv; (void)envp;
    if (argc != 1) { err("poweroff: arguments unsupported\n"); return 2; }
    /* On success the machine powers off via ACPI S5 and this never returns.
     * UID 0 only; without validated FADT power data the kernel fails closed
     * instead of pretending success. */
    if (poweroff() != 0) { err("poweroff: failed\n"); return 1; }
    return 0;
}

int main(int argc, char **argv, char **envp) { return program_main(argc, argv, envp); }
