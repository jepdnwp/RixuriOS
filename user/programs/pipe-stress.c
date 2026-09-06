#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

static size_t length(const char *s) {
    size_t n = 0;
    while (s && s[n]) ++n;
    return n;
}

static void mark(const char *s) {
    (void)write(1, s, length(s));
}

enum { STRESS_ROUNDS = 8, STRESS_PAYLOAD = 512 };
static uint8_t payload[STRESS_PAYLOAD];
static uint8_t received[STRESS_PAYLOAD];

static int fill_payload(uint8_t *payload, size_t size, uint8_t seed) {
    if (!payload) return -1;
    for (size_t i = 0; i < size; ++i) payload[i] = (uint8_t)(seed + (uint8_t)i);
    return 0;
}

int program_main(int argc, char **argv, char **envp) {
    (void)argc;
    (void)argv;
    (void)envp;
    int passed = 1;

    mark("pipe-stress:begin\n");
    for (unsigned round = 0; round < STRESS_ROUNDS; ++round) {
        int fds[2] = {-1, -1};
        if (fill_payload(payload, sizeof(payload), (uint8_t)(round * 17u)) != 0 ||
            pipe(fds) != 0) {
            passed = 0;
            break;
        }
        rix_pid_t writer = fork();
        if (writer == (rix_pid_t)-1) {
            passed = 0;
            (void)close(fds[0]);
            (void)close(fds[1]);
            break;
        }
        if (writer == 0) {
            (void)close(fds[0]);
            (void)write(fds[1], payload, sizeof(payload));
            (void)close(fds[1]);
            _exit(0);
        }
        (void)close(fds[1]);
        size_t total = 0;
        while (total < sizeof(received)) {
            rix_ssize_t got = read(fds[0], received + total, sizeof(received) - total);
            if (got <= 0) {
                passed = 0;
                break;
            }
            total += (size_t)got;
        }
        (void)close(fds[0]);
        uint64_t writer_status = 0;
        if (waitpid(writer, &writer_status, 0) != writer || writer_status != 0 ||
            total != sizeof(received)) {
            passed = 0;
        }
        for (size_t i = 0; i < sizeof(received); ++i) {
            if (received[i] != payload[i]) {
                passed = 0;
                break;
            }
        }
        rix_pid_t child = fork();
        if (child == (rix_pid_t)-1) {
            passed = 0;
            break;
        }
        if (child == 0) _exit(7);
        uint64_t child_status = 0;
        rix_pid_t probe = waitpid(child, &child_status, 1u);
        if (probe == 0) probe = waitpid(child, &child_status, 0);
        if (probe != child || child_status != 7u) {
            passed = 0;
        }
    }
    mark(passed ? "pipe-stress:blocked-reader-pass\n"
                : "pipe-stress:blocked-reader-fail\n");
    mark(passed ? "pipe-stress:fork-reap-pass\n" : "pipe-stress:fork-reap-fail\n");
    mark(passed ? "pipe-stress:PASS\n" : "pipe-stress:FAIL\n");
    return passed ? 0 : 1;
}
