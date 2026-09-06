#include "unistd.h"
#include "copy_metadata.h"
#include <stddef.h>
#include <stdint.h>

#define RIX_VFS_AT_FDCWD (-100)
#define RIX_VFS_O_WRONLY 1u
#define RIX_VFS_O_CREAT 4u
#define RIX_VFS_O_TRUNC 8u

static size_t length(const char *s) {
    size_t n = 0;
    while (s && s[n]) ++n;
    return n;
}

static void out(const char *s) {
    (void)write(2, s, length(s));
}

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
            out("cp: read failed\n");
            return 1;
        }
        if (n == 0) return 0;
        size_t done = 0;
        while (done < (size_t)n) {
            rix_ssize_t w = write(output, buffer + done, (size_t)n - done);
            if (w <= 0) {
                out("cp: write failed\n");
                return 1;
            }
            done += (size_t)w;
        }
    }
}

int program_main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc != 3) {
        out("cp: expected source and destination\n");
        return 2;
    }
    if (same_text(argv[1], argv[2])) return 0;
    int input = openat(RIX_VFS_AT_FDCWD, argv[1], 0u, 0u);
    if (input < 0) {
        out("cp: source open failed\n");
        return 1;
    }
    int output = openat(RIX_VFS_AT_FDCWD, argv[2],
                        RIX_VFS_O_WRONLY | RIX_VFS_O_CREAT | RIX_VFS_O_TRUNC,
                        0644u);
    if (output < 0) {
        (void)close(input);
        out("cp: destination open failed\n");
        return 1;
    }
    int status = copy_fd(input, output);
    if (close(output) != 0) status = 1;
    if (close(input) != 0) status = 1;
    if (status == 0 && copy_metadata("cp", argv[1], argv[2]) != 0) status = 1;
    return status;
}

int main(int argc, char **argv, char **envp) {
    return program_main(argc, argv, envp);
}
