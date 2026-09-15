#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

/* Phase F2: user-fault kill proof. Forks a child that deliberately
 * faults (NULL store or UD2); the kernel must terminate just that
 * process (exit 139) instead of freezing. The parent reaps and reports.
 * Survival past the fault (or any other status) fails. */
static size_t length(const char *s) { size_t n = 0; while (s && s[n]) ++n; return n; }
static void out(const char *s) { (void)write(1, s, length(s)); }

static void do_null(void) {
    *(volatile uint8_t *)(uintptr_t)0 = 0;
}

static void do_ud(void) {
    __asm__ volatile("ud2" ::: "memory");
}

int program_main(int argc, char **argv, char **envp) {
    int mode_ud;
    rix_pid_t child;
    uint64_t status = 0;
    (void)envp;
    if (argc != 2) { out("crashtest: need null|ud\n"); return 2; }
    mode_ud = argv[1][0] == 'u' && argv[1][1] == 'd' && argv[1][2] == 0;
    if (!mode_ud && !(argv[1][0] == 'n' && argv[1][1] == 'u' &&
                      argv[1][2] == 'l' && argv[1][3] == 'l' &&
                      argv[1][4] == 0)) {
        out("crashtest: need null|ud\n");
        return 2;
    }
    child = fork();
    if (child == (rix_pid_t)-1) { out("crashtest: fork failed\n"); return 1; }
    if (child == 0) {
        if (mode_ud) do_ud();
        else do_null();
        _exit(99);
    }
    if (waitpid(child, &status, 0) != child) { out("crashtest: reap failed\n"); return 1; }
    if (status != 139) { out("crashtest: wrong status\n"); return 1; }
    out("crash-reap=PASS\n");
    return 0;
}
