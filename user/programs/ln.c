#include "unistd.h"
#include <stddef.h>

static size_t length(const char *s) { size_t n = 0; while (s && s[n]) ++n; return n; }
static void out(const char *s) { (void)write(2, s, length(s)); }

static int is_directory(const char *path) {
    rix_stat_t st;
    if (stat(path, &st) != 0) return 0;
    return st.type == 1u;
}

static int join_path(const char *dir, const char *name, char *out_path, size_t capacity) {
    size_t used = 0;
    if (!dir || !name || !out_path || capacity < 2u) return -1;
    while (dir[used] && used + 1u < capacity) { out_path[used] = dir[used]; ++used; }
    if (dir[used]) return -1;
    if (used == 0u || out_path[used - 1u] != '/') {
        if (used + 1u >= capacity) return -1;
        out_path[used++] = '/';
    }
    for (size_t i = 0; name[i]; ++i) {
        if (used + 1u >= capacity) return -1;
        out_path[used++] = name[i];
    }
    out_path[used] = 0;
    return 0;
}

int program_main(int argc, char **argv, char **envp) {
    (void)envp;
    int arg_index = 1;
    int symbolic = 0;

    while (arg_index < argc && argv[arg_index][0] == '-') {
        if (argv[arg_index][1] == 's' && argv[arg_index][2] == 0) { symbolic = 1; ++arg_index; continue; }
        out("ln: invalid option\n");
        return 2;
    }

    if (arg_index + 1 >= argc) {
        out("ln: expected source and destination\n");
        return 2;
    }

    const char *source = argv[arg_index];
    const char *dest = argv[arg_index + 1];
    int is_multi = arg_index + 2 < argc;

    if (is_multi && !is_directory(dest)) {
        out("ln: target is not a directory\n");
        return 1;
    }

    int status = 0;
    for (; arg_index + 1 < argc; ++arg_index) {
        source = argv[arg_index];
        dest = argv[argc - 1];
        char full_dest[512];
        if (is_multi) {
            const char *name = source;
            size_t n = length(source);
            while (n > 0 && source[n - 1] != '/') --n;
            name = source + n;
            if (join_path(dest, name, full_dest, sizeof(full_dest)) != 0) {
                out("ln: path too long\n");
                status = 1;
                continue;
            }
            dest = full_dest;
        }
        if ((symbolic ? symlink(source, dest) : link(source, dest)) != 0) {
            out("ln: cannot link '");
            out(source);
            out("' to '");
            out(dest);
            out("'\n");
            status = 1;
        }
    }
    return status;
}
