#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

static size_t length(const char *s) { size_t n = 0; while (s && s[n]) ++n; return n; }
static void out(const char *s) { (void)write(2, s, length(s)); }
static int is_match(const char *a, const char *b) {
    size_t i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i] && a[i] == b[i]) ++i;
    return a[i] == 0 && b[i] == 0;
}
static int make_parents(char *path) {
    if (!path || !path[0]) return -1;
    for (size_t i = 1; path[i]; ++i) {
        if (path[i] != '/') continue;
        path[i] = 0;
        if (!((path[0] == '/' && path[1] == 0) || path[0] == 0)) {
            int rc = mkdir(path, 0755u);
            if (rc != 0) {
                int probe = openat(-100, path, 0u, 0u);
                if (probe < 0) { path[i] = '/'; return -1; }
                (void)close(probe);
            }
        }
        path[i] = '/';
    }
    return 0;
}

int program_main(int argc, char **argv, char **envp) {
    int make_parents_flag = 0;
    const char *target = 0;
    (void)envp;
    for (int i = 1; i < argc; ++i) {
        if (is_match(argv[i], "-p")) { make_parents_flag = 1; continue; }
        if (argv[i][0] == '-' || target) { out("mkdir: usage: mkdir [-p] <dir>\n"); return 2; }
        target = argv[i];
    }
    if (!target) { out("mkdir: expected path\n"); return 2; }
    if (make_parents_flag) {
        char path[256];
        size_t n = length(target);
        if (n == 0u || n + 1u > sizeof(path)) { out("mkdir: failed\n"); return 1; }
        for (size_t i = 0; i <= n; ++i) path[i] = target[i];
        if (make_parents(path) != 0) { out("mkdir: failed\n"); return 1; }
    }
    if (mkdir(target, 0755u) != 0) {
        if (!make_parents_flag) { out("mkdir: failed\n"); return 1; }
        {
            int probe = openat(-100, target, 0u, 0u);
            if (probe < 0) { out("mkdir: failed\n"); return 1; }
            (void)close(probe);
        }
    }
    return 0;
}
