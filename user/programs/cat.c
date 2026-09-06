#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

static int copy_fd(int input) {
    uint8_t buffer[256];
    for (;;) {
        rix_ssize_t n = read(input, buffer, sizeof(buffer));
        if (n < 0) return 1;
        if (n == 0) return 0;
        size_t done = 0;
        while (done < (size_t)n) {
            rix_ssize_t w = write(1, buffer + done, (size_t)n - done);
            if (w <= 0) return 1;
            done += (size_t)w;
        }
    }
}

int program_main(int argc, char **argv) {
    int status = 0;
    if (argc <= 1) {
        status = copy_fd(0);
    } else {
        for (int i = 1; i < argc; ++i) {
            int fd = openat(-100, argv[i], 0u, 0u);
            if (fd < 0) { status = 1; continue; }
            if (copy_fd(fd) != 0) status = 1;
            (void)close(fd);
        }
    }
    return status;
}
