#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

/* Phase F4: deterministic syscall fuzz. The parent forks windowed blast
 * children; each child fires N random syscalls with wild args, then
 * exits. A child may die (F2 user-kill on wild pointers) — any reaped
 * status is fine. The parent only requires: every window reaps (no
 * hang, no lost child) and the machine stays up. Deliberately SKIPPED
 * numbers (blast radius control, documented): 57 fork (bomb), 59
 * execve (image replace), 62 kill (cross-process), 82-87+90-91
 * filesystem mutation, 121+124+125 session surgery, 128-131 shm
 * exhaustion, 134 spawn (process-table flood), 137 cap delegation.
 * Everything else — incl. wild pointers into copy paths (F1 fixup +
 * EFAULT) and unknown numbers (ENOSYS) — must return, never freeze. */
#define FUZZ_WINDOWS 40
#define FUZZ_CALLS 150
#define FUZZ_NR_MAX 260

static uint32_t fuzz_state = 0xC0FFEEu;
static uint32_t fuzz_next(void) {
    fuzz_state = fuzz_state * 1664525u + 1013904223u;
    return (fuzz_state >> 16) & 0x7FFFu;
}

static long raw4(long n, long a, long b, long c, long d) {
    long r;
    register long r10 __asm__("r10") = d;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10) : "rcx", "r11", "memory");
    return r;
}

static long wild_arg(void) {
    static const uint64_t pool[] = {
        0, 1, 2, 3, 4, 7, 8, 32, 64, 128, 256, 4096, 4097,
        0x1000ULL, 0x2000ULL, 0x7ffffffff000ULL, 0x7fffffff0000ULL,
        0x8000000000ULL, 0x100000000000ULL, 0x7fff00000000ULL,
        0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFEULL, 0xFFFFFFFF00000000ULL,
        0xFFFF800000000000ULL, 0x00007FFFFFFFFFFFULL, 0x0000800000000000ULL,
    };
    return (long)pool[fuzz_next() % (sizeof(pool) / sizeof(pool[0]))];
}

static int skipped_nr(long n) {
    if (n == 57 || n == 59 || n == 62) return 1;
    if ((n >= 82 && n <= 87) || (n >= 90 && n <= 91)) return 1;
    if (n == 121 || n == 124 || n == 125) return 1;
    if (n >= 128 && n <= 131) return 1;
    if (n == 134 || n == 137) return 1;
    return 0;
}

static size_t length(const char *s) { size_t n = 0; while (s && s[n]) ++n; return n; }
static void out(const char *s) { (void)write(1, s, length(s)); }

int program_main(int argc, char **argv, char **envp) {
    /* volatile: fork() returns twice, the counter must survive. */
    volatile int w;
    (void)argc; (void)argv; (void)envp;
    for (w = 0; w < FUZZ_WINDOWS; ++w) {
        rix_pid_t child = fork();
        uint64_t status = 0;
        int i;
        if (child == (rix_pid_t)-1) { out("fuzztest: fork failed\n"); return 1; }
        if (child == 0) {
            for (i = 0; i < FUZZ_CALLS; ++i) {
                long n = (long)(fuzz_next() % FUZZ_NR_MAX);
                if (skipped_nr(n)) continue;
                (void)raw4(n, wild_arg(), wild_arg(), wild_arg(), wild_arg());
            }
            _exit(0);
        }
        if (waitpid(child, &status, 0) != child) { out("fuzztest: reap failed\n"); return 1; }
    }
    out("fuzz=PASS\n");
    return 0;
}
