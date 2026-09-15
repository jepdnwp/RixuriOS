#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

#define THREADS_MAX 64u

static rix_thread_info_t table[THREADS_MAX];

static size_t length(const char *s) { size_t n = 0; while (s && s[n]) ++n; return n; }
static void out(const char *s) { (void)write(1, s, length(s)); }
static void number(uint64_t value) {
    char buf[21]; size_t n = 0;
    if (value == 0) { out("0"); return; }
    while (value != 0 && n < sizeof(buf)) { buf[n++] = (char)('0' + (value % 10u)); value /= 10u; }
    while (n != 0) { char c = buf[--n]; (void)write(1, &c, 1); }
}
static void spaces(size_t count) { for (size_t i = 0; i < count; ++i) out(" "); }
static void padded_number(uint64_t value, size_t width) {
    char buf[21]; size_t n = 0;
    if (value == 0) buf[n++] = '0';
    while (value != 0 && n < sizeof(buf)) { buf[n++] = (char)('0' + (value % 10u)); value /= 10u; }
    if (n < width) spaces(width - n);
    while (n != 0) { char c = buf[--n]; (void)write(1, &c, 1); }
}
static char state_letter(uint32_t state) {
    if (state == RIX_THREAD_ACTIVE) return 'A';
    if (state == RIX_THREAD_DETACHED) return 'D';
    return '?';
}

int program_main(int argc, char **argv, char **envp) {
    size_t count = 0;
    (void)argv;
    (void)envp;
    if (argc != 1) { out("threads: arguments unsupported\n"); return 2; }
    if (list_threads(table, THREADS_MAX, &count) < 0) { out("threads: failed\n"); return 1; }
    out("TID OWNER STAT\n");
    for (size_t i = 0; i < count; ++i) {
        padded_number(table[i].tid, 3);
        out(" ");
        padded_number(table[i].owner_pid, 5);
        out(" ");
        {
            char state[2] = {state_letter(table[i].state), 0};
            out(state);
        }
        out("\n");
    }
    number(count);
    out(" thread(s)\n");
    return 0;
}
