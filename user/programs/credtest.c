#include <stdint.h>
#include <unistd.h>

#define RIX_VFS_AT_FDCWD (-100)
#define RIX_VFS_O_WRONLY 1u
#define RIX_VFS_O_CREAT 4u
#define RIX_VFS_O_TRUNC 8u
#define RIX_EINVAL 22
#define RIX_EACCES 13

static size_t text_length(const char *text) {
    size_t length = 0;
    while (text && text[length]) ++length;
    return length;
}

static int emit(const char *text) {
    size_t length = text_length(text);
    return write(1, text, length) == (rix_ssize_t)length ? 0 : -1;
}

static int emit_number(uint32_t value) {
    char buffer[16];
    size_t length = 0;
    if (!value) buffer[length++] = '0';
    while (value) {
        buffer[length++] = (char)('0' + value % 10u);
        value /= 10u;
    }
    for (size_t i = length; i-- > 0;) {
        if (write(1, &buffer[i], 1) != 1) return -1;
    }
    return 0;
}

static int create_byte_file(const char *path, uint32_t mode, char value) {
    int fd = openat(RIX_VFS_AT_FDCWD, path,
                    RIX_VFS_O_WRONLY | RIX_VFS_O_CREAT | RIX_VFS_O_TRUNC,
                    mode);
    if (fd < 0) return fd;
    int result = write(fd, &value, 1) == 1 ? 0 : -1;
    if (close(fd) != 0) result = -1;
    return result;
}

static int read_byte_file(const char *path, char expected) {
    char value = 0;
    int fd = openat(RIX_VFS_AT_FDCWD, path, 0u, 0u);
    if (fd < 0) return fd;
    rix_ssize_t read_count = read(fd, &value, 1);
    int close_result = close(fd);
    if (read_count != 1 || value != expected || close_result != 0) return -1;
    return 0;
}

static int expect_error(int result, int error) {
    return result == -error ? 0 : -1;
}

int program_main(int argc, char **argv, char **envp) {
    (void)argv;
    (void)envp;
    uint32_t groups[2] = {1000u, 2000u};
    uint32_t received_groups[2] = {0, 0};
    uint64_t status = 0;
    rix_stat_t stat_result;
    char *id_argv[] = {(char *)"/usr/bin/id", (char *)0};
    char *empty_env[] = {(char *)0};

    if (argc != 1) return 2;
    if (emit("before uid=") != 0 || emit_number(getuid()) != 0 ||
        emit(" gid=") != 0 || emit_number(getgid()) != 0 || emit("\n") != 0)
        return 1;

    /* Root creates fixtures whose owner, group and other classes differ. */
    if (mkdir("/phase20-work", 0777u) != 0 ||
        mkdir("/phase20-mode", 0755u) != 0 ||
        chmod("/phase20-mode", 0700u) != 0)
        return 1;
    if (setgid(2000u) != 0 ||
        create_byte_file("/phase20-group", 0640u, 'g') != 0 ||
        setgid(3000u) != 0 ||
        create_byte_file("/phase20-other", 0004u, 'o') != 0 ||
        setgid(1000u) != 0)
        return 1;
    if (stat("/phase20-group", &stat_result) != 0 ||
        stat_result.uid != 0u || stat_result.gid != 2000u ||
        (stat_result.mode & 07777u) != 0640u)
        return 1;

    if (setgroups(2u, groups) != 0 || setgid(1000u) != 0 || setuid(1000u) != 0)
        return 1;
    if (emit("after uid=") != 0 || emit_number(getuid()) != 0 ||
        emit(" gid=") != 0 || emit_number(getgid()) != 0 || emit("\n") != 0)
        return 1;

    /* The process owns this file and reaches the group-owned file through gid 2000. */
    if (create_byte_file("/phase20-work/owner", 0600u, 'o') != 0 ||
        stat("/phase20-work/owner", &stat_result) != 0 ||
        stat_result.uid != 1000u || stat_result.gid != 1000u ||
        (stat_result.mode & 07777u) != 0600u ||
        read_byte_file("/phase20-work/owner", 'o') != 0)
        return 1;
    if (read_byte_file("/phase20-group", 'g') != 0 ||
        expect_error(openat(RIX_VFS_AT_FDCWD, "/phase20-group",
                            RIX_VFS_O_WRONLY, 0u), RIX_EACCES) != 0)
        return 1;
    if (read_byte_file("/phase20-other", 'o') != 0 ||
        expect_error(openat(RIX_VFS_AT_FDCWD, "/phase20-other",
                            RIX_VFS_O_WRONLY, 0u), RIX_EACCES) != 0)
        return 1;
    if (expect_error(openat(RIX_VFS_AT_FDCWD, "/phase20-missing",
                            0u, 0u), RIX_EINVAL) != 0)
        return 1;
    if (getgroups(0u, (uint32_t *)0) != 2 ||
        getgroups(2u, received_groups) != 2 ||
        received_groups[0] != 1000u || received_groups[1] != 2000u)
        return 1;
    if (setgroups(0u, (const uint32_t *)0) == 0 || setuid(0u) == 0 ||
        setgid(0u) == 0 || chmod("/phase20-mode", 0777u) == 0)
        return 1;
    if (expect_error(mkdir("/phase20-denied", 0755u), RIX_EACCES) != 0)
        return 1;
    if (emit("matrix=PASS\n") != 0) return 1;

    rix_pid_t child = fork();
    if (child == (rix_pid_t)-1) return 1;
    if (child == 0) {
        if (execve("/usr/bin/id", id_argv, empty_env) != 0) _exit(127);
        _exit(126);
    }
    if (wait(child, &status) != child || status != 0) {
        (void)emit("setid=FAIL\n");
        return 1;
    }
    if (emit("setid=PASS\n") != 0) return 1;
    return 0;
}

int main(int argc, char **argv, char **envp) {
    return program_main(argc, argv, envp);
}
