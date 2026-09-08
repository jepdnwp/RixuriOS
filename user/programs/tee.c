#include "unistd.h"
#include <stddef.h>

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
    int append = 0;
    int arg_index = 1;

    while (arg_index < argc && argv[arg_index][0] == '-') {
        if (is_match(argv[arg_index], "-a")) {
            append = 1;
        } else {
            out("tee: invalid option\n");
            return 2;
        }
        ++arg_index;
    }

    if (arg_index >= argc) {
        out("tee: expected file\n");
        return 2;
    }

    int fds[8];
    int count = 0;
    for (; arg_index < argc && count < 8; ++arg_index) {
        int flags = 1u | 4u;
        if (!append) flags |= 8u;
        fds[count] = openat(-100, argv[arg_index], (uint32_t)flags, 0644u);
        if (fds[count] < 0) {
            out("tee: cannot open '");
            out(argv[arg_index]);
            out("'\n");
            for (int i = 0; i < count; ++i) (void)close(fds[i]);
            return 1;
        }
        ++count;
    }

    char b[256];
    int status = 0;
    for (;;) {
        rix_ssize_t n = read(0, b, sizeof(b));
        if (n < 0) { status = 1; break; }
        if (n == 0) break;
        size_t done = 0;
        while (done < (size_t)n) {
            rix_ssize_t w = write(1, b + done, (size_t)n - done);
            if (w <= 0) { status = 1; break; }
            done += (size_t)w;
        }
        for (int i = 0; i < count; ++i) {
            done = 0;
            while (done < (size_t)n) {
                rix_ssize_t w = write(fds[i], b + done, (size_t)n - done);
                if (w <= 0) { status = 1; break; }
                done += (size_t)w;
            }
        }
        if (status) break;
    }

    for (int i = 0; i < count; ++i) {
        if (close(fds[i]) != 0) status = 1;
    }
    return status;
}
