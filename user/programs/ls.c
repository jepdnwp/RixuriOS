#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

static size_t length(const char *s) { size_t n = 0; while (s && s[n]) ++n; return n; }
static void out(const char *s) { (void)write(1, s, length(s)); }
static void err(const char *s) { (void)write(2, s, length(s)); }
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
static int is_hidden(const char *name) {
    return name && name[0] == '.';
}

static int list_directory(const char *path, int long_format, int show_all) {
    int fd = openat(-100, path, 0, 0);
    if (fd < 0) { err("ls: open failed: "); err(path); err("\n"); return 1; }
    rix_dirent_t entries[16];
    size_t count = 0;
    if (getdents(fd, entries, 16, &count) < 0) { (void)close(fd); err("ls: read failed\n"); return 1; }
    (void)close(fd);

    size_t visible = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!show_all && is_hidden(entries[i].name)) continue;
        ++visible;
    }

    if (long_format && visible > 0) {
        uint64_t total_size = 0;
        for (size_t i = 0; i < count; ++i) {
            if (!show_all && is_hidden(entries[i].name)) continue;
            char full[512];
            rix_stat_t st;
            if (join_path(path, entries[i].name, full, sizeof(full)) == 0 &&
                stat(full, &st) == 0) {
                total_size += st.size;
            }
        }
        out("total ");
        number(total_size);
        out("\n");
    }

    for (size_t i = 0; i < count; ++i) {
        if (!show_all && is_hidden(entries[i].name)) continue;
        if (!long_format) { out(entries[i].name); out("\n"); continue; }
        char full[512];
        rix_stat_t st;
        char mode[11];
        if (join_path(path, entries[i].name, full, sizeof(full)) != 0 ||
            stat(full, &st) != 0) {
            err("ls: stat failed: ");
            err(entries[i].name);
            err("\n");
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
    return 0;
}

int program_main(int argc, char **argv, char **envp) {
    (void)envp;
    int long_format = 0, show_all = 0;
    int path_count = 0;
    const char *paths[8];
    int arg_index = 1;

    while (arg_index < argc && argv[arg_index][0] == '-') {
        for (size_t i = 1; argv[arg_index][i]; ++i) {
            if (argv[arg_index][i] == 'l') long_format = 1;
            else if (argv[arg_index][i] == 'a') show_all = 1;
            else {
                err("ls: invalid option\n");
                return 2;
            }
        }
        ++arg_index;
    }

    while (arg_index < argc && path_count < 8) {
        paths[path_count++] = argv[arg_index++];
    }

    if (path_count == 0) {
        paths[path_count++] = ".";
    }

    int status = 0;
    for (int i = 0; i < path_count; ++i) {
        if (path_count > 1) {
            out(paths[i]);
            out(":\n");
        }
        if (list_directory(paths[i], long_format, show_all) != 0) status = 1;
        if (i + 1 < path_count) out("\n");
    }
    return status;
}
