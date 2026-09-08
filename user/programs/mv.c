#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

static size_t length(const char *s) { size_t n = 0; while (s && s[n]) ++n; return n; }
static void out(const char *s) { (void)write(2, s, length(s)); }
static int same_text(const char *left, const char *right) {
    size_t i = 0;
    if (!left || !right) return 0;
    while (left[i] && right[i] && left[i] == right[i]) ++i;
    return left[i] == 0 && right[i] == 0;
}

static int copy_fd(int input, int output) {
    uint8_t buffer[256];
    for (;;) {
        rix_ssize_t n = read(input, buffer, sizeof(buffer));
        if (n < 0) {
            out("mv: read failed\n");
            return 1;
        }
        if (n == 0) return 0;
        size_t done = 0;
        while (done < (size_t)n) {
            rix_ssize_t w = write(output, buffer + done, (size_t)n - done);
            if (w <= 0) {
                out("mv: write failed\n");
                return 1;
            }
            done += (size_t)w;
        }
    }
}

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

    while (arg_index < argc && argv[arg_index][0] == '-') {
        out("mv: invalid option\n");
        return 2;
    }

    if (arg_index + 1 >= argc) {
        out("mv: expected source and destination\n");
        return 2;
    }

    const char *source = argv[arg_index];
    const char *dest = argv[arg_index + 1];
    int is_multi = arg_index + 2 < argc;

    if (is_multi && !is_directory(dest)) {
        out("mv: target is not a directory\n");
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
                out("mv: path too long\n");
                status = 1;
                continue;
            }
            dest = full_dest;
        }
        if (same_text(source, dest)) continue;
        int rename_status = rename(source, dest);
        if (rename_status == 0) continue;
        int input = openat(-100, source, 0u, 0u);
        if (input < 0) {
            out("mv: cannot open '");
            out(source);
            out("'\n");
            status = 1;
            continue;
        }
        int output = openat(-100, dest, 1u | 4u | 8u, 0644u);
        if (output < 0) {
            (void)close(input);
            out("mv: cannot create '");
            out(dest);
            out("'\n");
            status = 1;
            continue;
        }
        int copy_status = copy_fd(input, output);
        if (close(output) != 0) copy_status = 1;
        (void)close(input);
        if (copy_status != 0) {
            status = 1;
            continue;
        }
        if (unlink(source) != 0) {
            out("mv: cannot remove source '");
            out(source);
            out("'\n");
            status = 1;
        }
    }
    return status;
}
