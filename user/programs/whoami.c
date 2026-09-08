#include "unistd.h"
#include <stddef.h>

static size_t length(const char *s) { size_t n = 0; while (s && s[n]) ++n; return n; }
static void out(const char *s) { (void)write(1, s, length(s)); }

static int resolve_user_name(char *buffer, size_t capacity) {
    char passwd[2048];
    size_t passwd_length = 0;
    int fd = openat(-100, "/etc/passwd", 0u, 0u);
    if (fd < 0) return -1;
    for (;;) {
        rix_ssize_t n = read(fd, passwd + passwd_length, sizeof(passwd) - passwd_length - 1u);
        if (n < 0) { (void)close(fd); return -1; }
        if (n == 0) break;
        passwd_length += (size_t)n;
    }
    (void)close(fd);
    if (passwd_length == 0) return -1;
    passwd[passwd_length] = 0;

    uint32_t uid = getuid();
    size_t i = 0;
    while (i < passwd_length) {
        size_t line = i, name_end, value = 0;
        int digits = 0;
        while (i < passwd_length && passwd[i] != '\n') ++i;
        size_t end = i;
        if (i < passwd_length) ++i;
        name_end = line;
        while (name_end < end && passwd[name_end] != ':') ++name_end;
        size_t cursor = name_end;
        if (cursor < end && passwd[cursor] == ':') ++cursor;
        while (cursor < end && passwd[cursor] >= '0' && passwd[cursor] <= '9') {
            value = value * 10u + (uint32_t)(passwd[cursor] - '0');
            ++cursor;
            ++digits;
        }
        if (!digits || value != uid) continue;
        size_t name_length = name_end - line;
        if (name_length >= capacity) name_length = capacity - 1;
        for (size_t k = 0; k < name_length; ++k) buffer[k] = passwd[line + k];
        buffer[name_length] = 0;
        return 0;
    }
    return -1;
}

int program_main(int argc, char **argv, char **envp) {
    (void)argv;
    (void)envp;
    if (argc != 1) {
        (void)write(2, "whoami: arguments unsupported\n", 31);
        return 2;
    }
    char name[64];
    if (resolve_user_name(name, sizeof(name)) == 0) {
        out(name);
        out("\n");
        return 0;
    }
    if (getuid() == 0u) {
        out("root\n");
        return 0;
    }
    out("user\n");
    return 0;
}
