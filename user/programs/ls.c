#include "unistd.h"
#include <stddef.h>

static size_t length(const char *s) { size_t n=0; while (s && s[n]) ++n; return n; }
static void out(const char *s) { (void)write(1,s,length(s)); }

int program_main(int argc, char **argv, char **envp) {
    (void)envp;
    const char *path = argc > 1 ? argv[1] : "/";
    int fd = openat(-100, path, 0, 0);
    if (fd < 0) { out("ls: open failed\n"); return 1; }
    rix_dirent_t entries[16]; size_t count=0;
    if (getdents(fd, entries, 16, &count) < 0) { (void)close(fd); out("ls: read failed\n"); return 1; }
    (void)close(fd);
    for (size_t i=0; i<count; ++i) { out(entries[i].name); out("\n"); }
    return 0;
}
