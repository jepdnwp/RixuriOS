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

int program_main(int argc, char **argv, char **envp) {
    (void)envp;
    int recursive = 0;
    int arg_index = 1;

    while (arg_index < argc && argv[arg_index][0] == '-') {
        if (is_match(argv[arg_index], "-r") || is_match(argv[arg_index], "-rf")) {
            recursive = 1;
        } else if (is_match(argv[arg_index], "-f")) {
        } else {
            out("rm: invalid option\n");
            return 2;
        }
        ++arg_index;
    }

    if (arg_index == argc) {
        out("rm: expected path\n");
        return 2;
    }

    int status = 0;
    for (; arg_index < argc; ++arg_index) {
        const char *path = argv[arg_index];
        if (unlink(path) == 0) continue;
        if (recursive && rmdir(path) == 0) continue;
        out("rm: cannot remove '");
        out(path);
        out("'\n");
        status = 1;
    }
    return status;
}
