#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

static size_t text_length(const char *text) { size_t length = 0; while (text && text[length]) ++length; return length; }
static int emit(const char *text) { size_t length = text_length(text); return write(1, text, length) == (rix_ssize_t)length ? 0 : -1; }
static int streq(const char *a, const char *b) {
    size_t i = 0;
    for (;;) {
        if (a[i] != b[i]) return 0;
        if (!a[i]) return 1;
        ++i;
    }
}
static void tiny_yield(void) {
    (void)sched_yield();
}
static int emit_dec(const char *label, int value) {
    char buf[64];
    size_t n = 0;
    while (label[n] && n + 12 < sizeof(buf)) {
        buf[n] = label[n];
        ++n;
    }
    if (value == 0) {
        buf[n++] = '0';
    } else {
        char rev[12];
        int m = 0, v = value;
        if (v < 0) {
            buf[n++] = '-';
            v = -v;
        }
        while (v > 0 && m < 12) {
            rev[m++] = (char)('0' + (v % 10));
            v /= 10;
        }
        while (m > 0) buf[n++] = rev[--m];
    }
    buf[n++] = '\n';
    return write(1, buf, n) == (rix_ssize_t)n ? 0 : -1;
}

/* Spawn-exit churn: each iteration consumes a task slot and must give
 * it back. With 32 slots, 40 iterations fail unless retired slots are
 * truly reusable. */
static int churn_mode(int rounds) {
    /* volatile: fork() returns twice, the counter must survive. */
    for (volatile int i = 0; i < rounds; ++i) {
        rix_pid_t child = fork();
        if (child == (rix_pid_t)-1) return 1;
        if (child == 0) _exit(0);
        uint64_t status = 0;
        if (wait(child, &status) != child || status != 0) return 1;
    }
    return 0;
}

#define FAIR_N 200
#define FAIR_MAX_RUN 8

/* Two CPU-bound children that yield per byte must interleave on the
 * pipe; a starved or monopolized peer shows up as a long run. */
static int fair_mode(void) {
    int fds[2];
    if (pipe(fds) != 0) {
        (void)emit("fair where=pipe\n");
        return 1;
    }
    rix_pid_t a = fork();
    if (a == (rix_pid_t)-1) {
        (void)emit("fair where=fork-a\n");
        return 1;
    }
    if (a == 0) {
        (void)close(fds[0]);
        for (int i = 0; i < FAIR_N; ++i) {
            if (write(fds[1], "A", 1) != 1) _exit(2);
            tiny_yield();
        }
        (void)close(fds[1]);
        _exit(0);
    }
    rix_pid_t b = fork();
    if (b == (rix_pid_t)-1) {
        (void)emit("fair where=fork-b\n");
        return 1;
    }
    if (b == 0) {
        (void)close(fds[0]);
        for (int i = 0; i < FAIR_N; ++i) {
            if (write(fds[1], "B", 1) != 1) _exit(2);
            tiny_yield();
        }
        (void)close(fds[1]);
        _exit(0);
    }
    (void)close(fds[1]);
    int count_a = 0, count_b = 0, run = 0, max_run = 0;
    char last = 0;
    for (int i = 0; i < 2 * FAIR_N; ++i) {
        char c = 0;
        rix_ssize_t got = read(fds[0], &c, 1);
        if (got != 1) {
            (void)emit_dec("fair where=read got=", (int)got);
            (void)emit_dec("fair at i=", i);
            return 1;
        }
        if (c != 'A' && c != 'B') {
            (void)emit_dec("fair where=badchar c=", (int)c);
            return 1;
        }
        if (c == 'A') ++count_a;
        else ++count_b;
        if (c == last) ++run;
        else {
            run = 1;
            last = c;
        }
        if (run > max_run) max_run = run;
    }
    (void)close(fds[0]);
    uint64_t status = 0;
    int rw = 0;
    if (wait(a, &status) != a || status != 0) rw = 1;
    if (wait(b, &status) != b || status != 0) rw = 2;
    if (count_a != FAIR_N || count_b != FAIR_N || max_run > FAIR_MAX_RUN || rw) {
        (void)emit_dec("fair count_a=", count_a);
        (void)emit_dec("fair count_b=", count_b);
        (void)emit_dec("fair max_run=", max_run);
        (void)emit_dec("fair reap=", rw);
        return 1;
    }
    return 0;
}

int program_main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc == 3 && streq(argv[1], "churn")) {
        int rounds = 40;
        if (argv[2][0] >= '0' && argv[2][0] <= '9') {
            rounds = 0;
            for (size_t i = 0; argv[2][i]; ++i) {
                if (argv[2][i] < '0' || argv[2][i] > '9') return 2;
                rounds = rounds * 10 + (argv[2][i] - '0');
            }
        }
        if (rounds <= 0 || rounds > 200) return 2;
        if (churn_mode(rounds) != 0) return 1;
        if (emit("churn=PASS\n") != 0) return 1;
        return 0;
    }
    if (argc == 2 && streq(argv[1], "fair")) {
        if (fair_mode() != 0) return 1;
        if (emit("fair=PASS\n") != 0) return 1;
        return 0;
    }
    return 2;
}

int main(int argc, char **argv, char **envp) { return program_main(argc, argv, envp); }
