#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

static size_t length(const char *s) { size_t n = 0; while (s && s[n]) ++n; return n; }
static void out(const char *s) { (void)write(1, s, length(s)); }
static int is_match(const char *a, const char *b) {
    size_t i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i] && a[i] == b[i]) ++i;
    return a[i] == 0 && b[i] == 0;
}
static void number(uint64_t value) {
    char buf[21]; size_t n = 0;
    if (value == 0) { out("0"); return; }
    while (value != 0 && n < sizeof(buf)) { buf[n++] = (char)('0' + (value % 10u)); value /= 10u; }
    while (n != 0) { char c = buf[--n]; (void)write(1, &c, 1); }
}
static void mode_string(uint32_t mode, int is_dir, char *text) {
    text[0] = is_dir ? 'd' : '-';
    text[1] = (mode & 0400u) ? 'r' : '-';
    text[2] = (mode & 0200u) ? 'w' : '-';
    text[3] = (mode & 0100u) ? 'x' : '-';
    text[4] = (mode & 0040u) ? 'r' : '-';
    text[5] = (mode & 0020u) ? 'w' : '-';
    text[6] = (mode & 0010u) ? 'x' : '-';
    text[7] = (mode & 0004u) ? 'r' : '-';
    text[8] = (mode & 0002u) ? 'w' : '-';
    text[9] = (mode & 0001u) ? 'x' : '-';
    text[10] = 0;
}
static int join_path(const char *dir, const char *name, char *out_path, size_t capacity) {
    size_t used = 0;
    if (!dir || !name || !out_path || capacity < 2u) return -1;
    while (dir[used] && used + 1u < capacity) { out_path[used] = dir[used]; ++used; }
    if (dir[used]) return -1;
    if (used == 0u || out_path[used - 1u] != '/') {
        if (used + 1u >= capacity) return -1;
        out_path[used++] = '/';
    }
    for (size_t i = 0; name[i]; ++i) {
        if (used + 1u >= capacity) return -1;
        out_path[used++] = name[i];
    }
    out_path[used] = 0;
    return 0;
}

int program_main(int argc, char **argv, char **envp) {
    const char *path = ".";
    int path_set = 0, long_format = 0;
    int fd;
    rix_dirent_t entries[16];
    size_t count = 0;
    (void)envp;
    for (int i = 1; i < argc; ++i) {
        if (is_match(argv[i], "-l")) { long_format = 1; continue; }
        if (!argv[i] || argv[i][0] == '-' || path_set) { out("ls: usage: ls [-l] [path]\n"); return 2; }
        path = argv[i];
        path_set = 1;
    }
    fd = openat(-100, path, 0, 0);
    if (fd < 0) { out("ls: open failed\n"); return 1; }
    if (getdents(fd, entries, 16, &count) < 0) { (void)close(fd); out("ls: read failed\n"); return 1; }
    (void)close(fd);
    for (size_t i = 0; i < count; ++i) {
        if (!long_format) { out(entries[i].name); out("\n"); continue; }
        {
            char full[512];
            rix_stat_t st;
            char mode[11];
            if (join_path(path, entries[i].name, full, sizeof(full)) != 0 ||
                stat(full, &st) != 0) {
                out("ls: stat failed: ");
                out(entries[i].name);
                out("\n");
                return 1;
            }
            mode_string(st.mode, st.type == 1u, mode);
            out(mode);
            out(" ");
            number(st.uid);
            out(" ");
            number(st.gid);
            out(" ");
            number(st.size);
            out(" ");
            out(entries[i].name);
            out("\n");
        }
    }
    return 0;
}
