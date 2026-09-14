#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>

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

static uint64_t rdtsc_now(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi) :: "memory");
    return ((uint64_t)hi << 32) | lo;
}

/* timespec subtraction with borrow handling (nsec fields are not
 * ordered across seconds). */
static uint64_t timespec_delta_ns(const struct timespec *from, const struct timespec *to) {
    int64_t ds = (int64_t)(to->tv_sec - from->tv_sec);
    int64_t dn = (int64_t)to->tv_nsec - (int64_t)from->tv_nsec;
    if (dn < 0) {
        ds -= 1;
        dn += 1000000000;
    }
    if (ds < 0) return 0;
    return (uint64_t)ds * 1000000000ull + (uint64_t)dn;
}

/* Hog test: an infinite-quantum spinner must not starve a yielding
 * peer. The hog spins ~8 TSC-s (calibrated live); the checker emits
 * 200 patterned bytes in 20 turns. The verdict is ORDER-INDEPENDENT
 * and TIMING-FREE by construction: after all bytes arrive with the
 * exact pattern, the hog is PROVEN still alive via waitpid(WNOHANG).
 * Without involuntary preemption the checker can only finish after
 * the hog's death (whichever runs first), so the probe always fails.
 * first_ms/total_ms are diagnostics only — this VM's TSC rate swings
 * ~2-4x between runs, so no wall-clock threshold is load-bearing. */
#define HOG_SPIN_NS 8000000000ull
#define HOG_TURNS 20
#define HOG_BURST 10
#define HOG_TOTAL (HOG_TURNS * HOG_BURST)
#define RIX_WNOHANG 1u

static int hog_mode(void) {
    struct timespec ta, tb;
    struct timespec cal = {0, 200000000};
    if (clock_gettime(CLOCK_MONOTONIC, &ta) != 0) return 1;
    uint64_t ra = rdtsc_now();
    if (nanosleep(&cal, NULL) != 0) return 1;
    if (clock_gettime(CLOCK_MONOTONIC, &tb) != 0) return 1;
    uint64_t rb = rdtsc_now();
    uint64_t dns = timespec_delta_ns(&ta, &tb);
    if (dns == 0 || rb <= ra) {
        (void)emit("hog where=calibrate\n");
        return 1;
    }
    uint64_t hog_ticks = (rb - ra) / dns * HOG_SPIN_NS;
    uint64_t hog_until = rdtsc_now() + hog_ticks;
    int fds[2];
    if (pipe(fds) != 0) {
        (void)emit("hog where=pipe\n");
        return 1;
    }
    rix_pid_t hog = fork();
    if (hog == (rix_pid_t)-1) {
        (void)emit("hog where=fork-hog\n");
        return 1;
    }
    if (hog == 0) {
        while (rdtsc_now() < hog_until) {
        }
        _exit(0);
    }
    rix_pid_t checker = fork();
    if (checker == (rix_pid_t)-1) {
        (void)emit("hog where=fork-checker\n");
        return 1;
    }
    if (checker == 0) {
        static const char burst[HOG_BURST + 1] = "0123456789";
        (void)close(fds[0]);
        for (int t = 0; t < HOG_TURNS; ++t) {
            if (write(fds[1], burst, HOG_BURST) != HOG_BURST) _exit(2);
            tiny_yield();
        }
        (void)close(fds[1]);
        _exit(0);
    }
    (void)close(fds[1]);
    struct timespec t0, t_first;
    int have_first = 0;
    if (clock_gettime(CLOCK_MONOTONIC, &t0) != 0) return 1;
    for (int i = 0; i < HOG_TOTAL; ++i) {
        char c = 0;
        char want = (char)('0' + (i % HOG_BURST));
        if (read(fds[0], &c, 1) != 1 || c != want) {
            (void)emit_dec("hog where=read i=", i);
            return 1;
        }
        if (!have_first) {
            if (clock_gettime(CLOCK_MONOTONIC, &t_first) != 0) return 1;
            have_first = 1;
        }
    }
    (void)close(fds[0]);
    /* Liveness proof: every byte arrived while the hog still runs.
     * Kernel-time evidence: t_end is sampled BEFORE the probe, so even
     * a hog-dead verdict reports how much kernel time covered the hog's
     * life (~0 means PIT ticks never fired during the spin). */
    struct timespec t_end;
    if (clock_gettime(CLOCK_MONOTONIC, &t_end) != 0) return 1;
    (void)emit_dec("hog total_ms=", (int)(timespec_delta_ns(&t0, &t_end) / 1000000ull));
    uint64_t status = 0;
    rix_pid_t probe = waitpid(hog, &status, RIX_WNOHANG);
    if (probe != 0) {
        (void)emit_dec("hog where=hog-dead probe=", (int)probe);
        (void)wait(checker, &status);
        (void)wait(hog, &status);
        return 1;
    }
    if (wait(checker, &status) != checker || status != 0) {
        (void)emit("hog where=reap-checker\n");
        (void)wait(hog, &status);
        return 1;
    }
    if (wait(hog, &status) != hog || status != 0) {
        (void)emit("hog where=reap-hog\n");
        return 1;
    }
    if (!have_first) return 1;
    uint64_t first_ns = timespec_delta_ns(&t0, &t_first);
    (void)emit_dec("hog first_ms=", (int)(first_ns / 1000000ull));
    return 0;
}

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
    if (argc == 2 && streq(argv[1], "hog")) {
        if (hog_mode() != 0) return 1;
        if (emit("hog=PASS\n") != 0) return 1;
        return 0;
    }
    return 2;
}

int main(int argc, char **argv, char **envp) { return program_main(argc, argv, envp); }
