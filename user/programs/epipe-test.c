#include "unistd.h"
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

static size_t length(const char *s) { size_t n = 0; while (s && s[n]) ++n; return n; }
static void out(const char *s) { (void)write(1, s, length(s)); }

int program_main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;
    int fds[2] = {-1, -1};
    char byte = 'x';
    if (pipe(fds) != 0) { out("epipe: pipe failed\n"); return 1; }
    /* No readers: the write must fail closed with EPIPE (no SIGPIPE
     * delivery exists yet, so the error return is the contract). */
    if (close(fds[0]) != 0) { out("epipe: close failed\n"); return 1; }
    errno = 0;
    if (write(fds[1], &byte, 1) != -1 || errno != RIX_EPIPE) {
        out("epipe: expected EPIPE\n");
        return 1;
    }
    out("epipe-broken=PASS\n");
    /* Closed write end stays EINVAL-class, not EPIPE. */
    if (close(fds[1]) != 0) { out("epipe: close2 failed\n"); return 1; }
    errno = 0;
    if (write(fds[1], &byte, 1) != -1 || errno == RIX_EPIPE) {
        out("epipe: closed-fd class wrong\n");
        return 1;
    }
    out("epipe-closed=PASS\n");
    out("epipe=PASS\n");
    return 0;
}

int main(int argc, char **argv, char **envp) { return program_main(argc, argv, envp); }
