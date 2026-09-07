#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

#define RIX_VFS_AT_FDCWD (-100)
#define RIX_VFS_O_WRONLY 1u
#define RIX_VFS_O_CREAT 4u
#define RIX_VFS_O_TRUNC 8u
#define RIX_EINVAL 22
#define RIX_EEXIST 17

static size_t text_length(const char *text) {
    size_t length = 0;
    while (text && text[length]) ++length;
    return length;
}

static int emit(const char *text) {
    size_t length = text_length(text);
    return write(1, text, length) == (rix_ssize_t)length ? 0 : -1;
}

static int same_stat(const rix_stat_t *left, const rix_stat_t *right) {
    return left && right && left->inode == right->inode &&
           left->type == right->type && left->mode == right->mode &&
           left->uid == right->uid && left->gid == right->gid &&
           left->size == right->size;
}

static int create_file(const char *path, uint32_t mode) {
    int fd = openat(RIX_VFS_AT_FDCWD, path,
                    RIX_VFS_O_WRONLY | RIX_VFS_O_CREAT | RIX_VFS_O_TRUNC,
                    mode);
    if (fd < 0) return -1;
    char value = 'r';
    int status = write(fd, &value, 1) == 1 ? 0 : -1;
    if (close(fd) != 0) status = -1;
    return status;
}

int program_main(int argc, char **argv, char **envp) {
    (void)argc;
    (void)argv;
    (void)envp;
    (void)unlink("/usr/rename-source");
    (void)unlink("/usr/rename-target");
    (void)unlink("/usr/rename-existing");
    if (create_file("/usr/rename-source", 0670u) != 0) return 1;

    rix_stat_t before;
    rix_stat_t after;
    if (stat("/usr/rename-source", &before) != 0)
        return 1;
    if (rename("/usr/rename-source", "/usr/rename-target") != 0)
        return 1;
    if (stat("/usr/rename-source", &after) != -RIX_EINVAL)
        return 1;
    if (stat("/usr/rename-target", &after) != 0)
        return 1;
    if (!same_stat(&before, &after))
        return 1;
    if (emit("rename-inode=PASS\n") != 0) return 1;

    if (create_file("/usr/rename-existing", 0600u) != 0 ||
        rename("/usr/rename-target", "/usr/rename-existing") != -RIX_EEXIST ||
        stat("/usr/rename-target", &after) != 0 || !same_stat(&before, &after))
        return 1;
    if (emit("rename-exists=PASS\n") != 0) return 1;

    if (rename("/usr/rename-target", "/usr/rename-source") != 0 ||
        stat("/usr/rename-source", &after) != 0 || !same_stat(&before, &after) ||
        unlink("/usr/rename-source") != 0 ||
        unlink("/usr/rename-existing") != 0)
        return 1;
    return emit("rename-roundtrip=PASS\n") == 0 ? 0 : 1;
}

int main(int argc, char **argv, char **envp) {
    return program_main(argc, argv, envp);
}
