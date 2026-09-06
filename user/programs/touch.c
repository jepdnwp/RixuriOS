#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

static size_t length(const char *s) {
    size_t n = 0;
    while (s && s[n]) ++n;
    return n;
}

static void error_path(const char *path) {
    (void)write(2, "touch: failed: ", 15u);
    (void)write(2, path, length(path));
    (void)write(2, "\n", 1u);
}

int program_main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc < 2) {
        (void)write(2, "touch: expected path\n", 21u);
        return 2;
    }
    int status = 0;
    for (int i = 1; i < argc; ++i) {
        int fd = openat(-100, argv[i], 1u | 4u, 0644u);
        if (fd < 0) {
            error_path(argv[i]);
            status = 1;
            continue;
        }
        if (close(fd) != 0) {
            error_path(argv[i]);
            status = 1;
        }
    }
    return status;
}
