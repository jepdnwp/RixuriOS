#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

static size_t length(const char *text) {
    size_t n = 0;
    while (text && text[n]) ++n;
    return n;
}

static int emit(const char *text) {
    size_t n = length(text);
    return write(1, text, n) == (rix_ssize_t)n ? 0 : -1;
}

static const char help_text[] =
    "RixuriOS shell help\n"
    "usage: <command> [args]  (try 'help <command>' for one-liners)\n"
    "shell builtins: cd clear : true false echo\n"
    "files: cat ls cp mv rm mkdir rmdir touch stat ln find du tee\n"
    "text: grep head tail wc cut tr sort uniq sed test seq\n"
    "       args printf basename dirname xargs env pwd which\n"
    "system: ps kill uname date id whoami hostname rixtest proc-test\n"
    "       pipe-stress\n"
    "network: ping curl host\n"
    "accounts: accountctl authcheck auditcheck capdelegatecheck sessiontest\n"
    "config: /etc/passwd /etc/shadow /etc/hosts /etc/resolv.conf\n"
    "home directories live under /home/<login> (desktop, documents)\n";

int program_main(int argc, char **argv, char **envp) {
    static const char *topics[][2] = {
        {"ping", "ping <host>: ICMP echo via hosts file, then DNS\n"},
        {"curl", "curl <host>: HTTP/1.x GET, validates status line\n"},
        {"host", "host <name>: print the resolved IPv4 address\n"},
        {"help", "help [command]: show this overview or one line\n"},
        {"ls", "ls [-l] [path]: list names; -l adds mode uid gid size\n"},
        {"cat", "cat [-n] [file...]: copy files to stdout; -n numbers lines\n"},
        {"cp", "cp <src> <dst>: copy a file\n"},
        {"mv", "mv <src> <dst>: move/rename a file\n"},
        {"rm", "rm <file>: remove a file\n"},
        {"mkdir", "mkdir [-p] <dir>: make directories; -p makes parents\n"},
        {"rmdir", "rmdir <dir>: remove an empty directory\n"},
        {"touch", "touch <file>: create an empty file\n"},
        {"stat", "stat <path>: print inode type mode size\n"},
        {"ln", "ln <src> <dst>: hard-link a file\n"},
        {"find", "find <dir>: list files under a directory\n"},
        {"du", "du <path>: print size usage\n"},
        {"echo", "echo [-n] [words...]: print words; -n omits newline\n"},
        {"ps", "ps: list processes as PID PPID UID STAT NAME\n"},
        {"kill", "kill <pid> <signal>: send a signal\n"},
        {"uname", "uname [-a]: print system name (-a adds details)\n"},
        {"hostname", "hostname: print /etc/hostname\n"},
        {"id", "id: print uid/gid/groups\n"},
        {"whoami", "whoami: print your login name\n"},
        {"date", "date: print the realtime clock\n"},
        {"pwd", "pwd: print the working directory\n"},
        {"cd", "cd [path]: change directory (default /)\n"},
        {"history", "history: list this session's command history\n"},
    };
    (void)envp;
    if (argc == 2 && argv[1]) {
        for (size_t i = 0; i < sizeof(topics) / sizeof(topics[0]); ++i) {
            const char *name = topics[i][0], *want = argv[1];
            size_t k = 0;
            while (name[k] && want[k] && name[k] == want[k]) ++k;
            if (!name[k] && !want[k]) return emit(topics[i][1]);
        }
        (void)emit("help: unknown topic\n");
        return 1;
    }
    if (argc != 1) {
        (void)emit("usage: help [command]\n");
        return 2;
    }
    return emit(help_text);
}
