#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

static size_t length(const char *s) { size_t n = 0; while (s && s[n]) ++n; return n; }
static void err(const char *s) { (void)write(2, s, length(s)); }

static int parse_id(const char *s, uint32_t *out) {
    uint32_t value = 0;
    size_t digits = 0;
    if (!s || !*s || !out) return -1;
    while (*s) {
        if (*s < '0' || *s > '9' || digits >= 10u) return -1;
        uint32_t next = value * 10u + (uint32_t)(*s - '0');
        if (next < value) return -1;
        value = next;
        ++digits;
        ++s;
    }
    *out = value;
    return 0;
}

int program_main(int argc, char **argv, char **envp) {
    (void)envp;
    uint32_t uid = 0, gid = 0;
    const char *sep = NULL;
    if (argc != 3) { err("chown: expected uid[:gid] and path\n"); return 2; }
    for (const char *p = argv[1]; *p; ++p) {
        if (*p == ':') { sep = p; break; }
    }
    if (!sep) {
        if (parse_id(argv[1], &uid) != 0) { err("chown: bad uid\n"); return 2; }
        gid = uid;
        {
            rix_stat_t st;
            if (stat(argv[2], &st) == 0) gid = st.gid;
        }
    } else {
        char left[16];
        size_t n = (size_t)(sep - argv[1]);
        if (n == 0 || n >= sizeof(left)) { err("chown: bad uid\n"); return 2; }
        for (size_t i = 0; i < n; ++i) left[i] = argv[1][i];
        left[n] = 0;
        if (parse_id(left, &uid) != 0 || parse_id(sep + 1, &gid) != 0) {
            err("chown: bad uid:gid\n");
            return 2;
        }
    }
    if (chown(argv[2], uid, gid) != 0) { err("chown: failed\n"); return 1; }
    return 0;
}

int main(int argc, char **argv, char **envp) { return program_main(argc, argv, envp); }
