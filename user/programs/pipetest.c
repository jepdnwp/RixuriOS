#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

static size_t text_length(const char *text) { size_t length = 0; while (text && text[length]) ++length; return length; }
static int emit(const char *text) { size_t length = text_length(text); return write(1, text, length) == (rix_ssize_t)length ? 0 : -1; }

/* Deterministic close/EOF semantics for true blocking readers (no
 * timing assertions — any hang fails via the harness prompt timeout):
 *  1. writer closes an empty pipe unread: reader must see EOF (0)
 *     promptly instead of hanging;
 *  2. writer writes one byte then closes: reader must see the byte,
 *     then EOF on the next read. */
static int close_eof(void) {
    int fds[2];
    if (pipe(fds) != 0) return 1;
    rix_pid_t c = fork();
    if (c == (rix_pid_t)-1) return 1;
    if (c == 0) {
        (void)close(fds[0]);
        (void)close(fds[1]);
        _exit(0);
    }
    (void)close(fds[1]);
    char b = 0;
    if (read(fds[0], &b, 1) != 0) return 1;
    (void)close(fds[0]);
    uint64_t status = 0;
    if (wait(c, &status) != c || status != 0) return 1;
    return 0;
}

static int write_then_close(void) {
    int fds[2];
    if (pipe(fds) != 0) return 1;
    rix_pid_t c = fork();
    if (c == (rix_pid_t)-1) return 1;
    if (c == 0) {
        (void)close(fds[0]);
        if (write(fds[1], "Z", 1) != 1) _exit(2);
        (void)close(fds[1]);
        _exit(0);
    }
    (void)close(fds[1]);
    char b = 0;
    if (read(fds[0], &b, 1) != 1 || b != 'Z') return 1;
    if (read(fds[0], &b, 1) != 0) return 1;
    (void)close(fds[0]);
    uint64_t status = 0;
    if (wait(c, &status) != c || status != 0) return 1;
    return 0;
}

int program_main(int argc, char **argv, char **envp) {
    (void)argc;
    (void)argv;
    (void)envp;
    if (close_eof() != 0) return 1;
    if (emit("pipe-close-eof=PASS\n") != 0) return 1;
    if (write_then_close() != 0) return 1;
    if (emit("pipe-write-close=PASS\n") != 0) return 1;
    return 0;
}

int main(int argc, char **argv, char **envp) { return program_main(argc, argv, envp); }
