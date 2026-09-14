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

/* Blocked-writer backpressure (closes the Phase D open item: transfers
 * larger than the 4096-byte channel used to fail). The writer pushes 8KB
 * while the parent drains concurrently; the writer must block (not fail)
 * when the channel fills, and the reader must observe every byte exact. */
#define BACKPRESSURE_BYTES 8192u
static uint8_t backpressure_data[BACKPRESSURE_BYTES];
static int blocked_writer(void) {
    for (uint32_t i = 0; i < BACKPRESSURE_BYTES; ++i)
        backpressure_data[i] = (uint8_t)((i * 7u + 1u) & 0xFFu);
    int fds[2];
    if (pipe(fds) != 0) return 1;
    rix_pid_t c = fork();
    if (c == (rix_pid_t)-1) return 1;
    if (c == 0) {
        (void)close(fds[0]);
        /* NOTE: a single write() is capped at RIX_MAX_IO (4096) by the
         * syscall layer, so push the 8KB as 32x256B chunks; the total
         * still exceeds channel capacity and forces writer blocking. */
        uint32_t off = 0;
        while (off < BACKPRESSURE_BYTES) {
            rix_ssize_t wrote = write(fds[1], backpressure_data + off, 256);
            if (wrote <= 0) _exit(2);
            off += (uint32_t)wrote;
        }
        (void)close(fds[1]);
        _exit(off == BACKPRESSURE_BYTES ? 0 : 2);
    }
    (void)close(fds[1]);
    uint8_t chunk[256];
    uint32_t received = 0;
    for (;;) {
        rix_ssize_t got = read(fds[0], chunk, sizeof(chunk));
        if (got == 0) break;
        if (got < 0) return 1;
        for (rix_ssize_t k = 0; k < got; ++k) {
            if (received >= BACKPRESSURE_BYTES) return 1;
            if (chunk[k] != (uint8_t)((received * 7u + 1u) & 0xFFu)) return 1;
            ++received;
        }
    }
    (void)close(fds[0]);
    uint64_t status = 0;
    if (received != BACKPRESSURE_BYTES) return 1;
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
    if (blocked_writer() != 0) return 1;
    if (emit("pipe-backpressure=PASS\n") != 0) return 1;
    return 0;
}

int main(int argc, char **argv, char **envp) { return program_main(argc, argv, envp); }
