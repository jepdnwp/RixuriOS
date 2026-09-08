#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

#define PS_MAX_PROCS 128u

static rix_process_info_t table[PS_MAX_PROCS];

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
    if (state == RIX_PROC_RUNNING) return 'R';
    if (state == RIX_PROC_SLEEPING) return 'S';
    if (state == RIX_PROC_ZOMBIE) return 'Z';
    return '?';
}

int program_main(int argc, char **argv, char **envp) {
    size_t count = 0;
    (void)argv;
    (void)envp;
    if (argc != 1) { out("ps: arguments unsupported\n"); return 2; }
    if (list_processes(table, PS_MAX_PROCS, &count) != 0) { out("ps: failed\n"); return 1; }
    out("PID PPID UID STAT NAME\n");
    for (size_t i = 0; i < count; ++i) {
        padded_number(table[i].pid, 3);
        out(" ");
        padded_number(table[i].parent, 4);
        out(" ");
        padded_number(table[i].uid, 3);
        out(" ");
        {
            char state[2] = {state_letter(table[i].state), 0};
            out(state);
        }
        out("    ");
        for (size_t k = 0; k < RIX_PROCESS_NAME_MAX && table[i].name[k]; ++k) {
            char c = table[i].name[k];
            if (c < 32 || c > 126) c = '?';
            (void)write(1, &c, 1);
        }
        out("\n");
    }
    number(count);
    out(" process(es)\n");
    return 0;
}
