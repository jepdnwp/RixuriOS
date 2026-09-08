#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

static size_t length(const char *s) { size_t n = 0; while (s && s[n]) ++n; return n; }
static void out(const char *s) { (void)write(1, s, length(s)); }
static void err(const char *s) { (void)write(2, s, length(s)); }
static void emit_number(uint32_t value) {
    char buf[10]; size_t n = 0;
    if (value == 0) { out("0"); return; }
    while (value != 0) { buf[n++] = (char)('0' + (value % 10u)); value /= 10u; }
    while (n != 0) { char c = buf[--n]; (void)write(1, &c, 1); }
}
static void emit_groups(void) {
    uint32_t groups[8];
    size_t count = 0;
    if (getgroups(8u, groups) == 0) {
        out(" groups=");
        for (size_t i = 0; i < count; ++i) {
            if (i) out(",");
            emit_number(groups[i]);
        }
    }
}

int program_main(int argc, char **argv, char **envp) {
    (void)argv;
    (void)envp;
    if (argc != 1) {
        err("id: arguments unsupported\n");
        return 2;
    }
    out("uid=");
    emit_number(getuid());
    out(" gid=");
    emit_number(getgid());
    emit_groups();
    out("\n");
    return 0;
}
