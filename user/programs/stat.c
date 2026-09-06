#include "unistd.h"
#include <stddef.h>

static size_t length(const char *s) { size_t n = 0; while (s && s[n]) ++n; return n; }
static void out(const char *s) { (void)write(1, s, length(s)); }
static void number(uint64_t value) {
    char buf[32]; size_t n = 0;
    if (value == 0) { out("0"); return; }
    while (value != 0) { buf[n++] = (char)('0' + (value % 10)); value /= 10; }
    while (n != 0) { char c[2] = { buf[--n], '\0' }; out(c); }
}

int program_main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc != 2) { out("stat: expected path\n"); return 2; }
    rix_stat_t st;
    if (stat(argv[1], &st) != 0) { out("stat: failed\n"); return 1; }
    out("inode "); number(st.inode);
    out("\ntype "); number(st.type);
    out("\nmode "); number(st.mode);
    out("\nsize "); number(st.size);
    out("\n");
    return 0;
}
