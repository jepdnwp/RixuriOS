#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

static size_t length(const char *s) { size_t n = 0; while (s && s[n]) ++n; return n; }
static void out(const char *s) { (void)write(1, s, length(s)); }

void _start(void) {
    uint64_t *sp;
    __asm__ volatile("mov %%rsp,%0" : "=r"(sp));
    int argc = (int)sp[0];
    char **argv = (char **)(uintptr_t)sp[1];
    for (int i = 1; i < argc; ++i) {
        if (i > 1) out(" ");
        out(argv[i]);
    }
    out("\n");
    _exit(0);
}
