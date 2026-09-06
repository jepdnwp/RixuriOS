#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

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

static int same_text(const char *left, const char *right) {
    size_t i = 0;
    if (!left || !right) return 0;
    while (left[i] && right[i] && left[i] == right[i]) ++i;
    return left[i] == 0 && right[i] == 0;
}

static int emit(const char *text) {
    size_t length = text_length(text);
    return write(1, text, length) == (rix_ssize_t)length ? 0 : -1;
}

static int create_source(void) {
    if (unlink("/usr/meta-source") != 0) {
        /* The disposable image may not contain the fixture yet. */
    }
    if (setgid(2000u) != 0) return -1;
    int fd = openat(RIX_VFS_AT_FDCWD, "/usr/meta-source",
                    RIX_VFS_O_WRONLY | RIX_VFS_O_CREAT | RIX_VFS_O_TRUNC,
                    06770u);
    if (fd < 0) return -1;
    char value = 'm';
    int status = write(fd, &value, 1) == 1 ? 0 : -1;
    if (close(fd) != 0) status = -1;
    if (setgid(0u) != 0) status = -1;
    rix_stat_t st;
    rix_acl_t acl = {RIX_ACL_VERSION, 1000u, 4u, 2000u, 6u, 6u};
    rix_acl_t read_acl;
    if (status != 0 || stat("/usr/meta-source", &st) != 0 ||
        st.uid != 0u || st.gid != 2000u || (st.mode & 07777u) != 06770u ||
        setacl("/usr/meta-source", &acl) != 0 ||
        getacl("/usr/meta-source", &read_acl) != 0 ||
        read_acl.user != 1000u || read_acl.user_perm != 4u ||
        read_acl.group != 2000u || read_acl.group_perm != 6u ||
        read_acl.mask != 6u)
        return -1;
    return emit("metadata-source=PASS\n");
}

static int check_chown_policy(void) {
    int fd = openat(RIX_VFS_AT_FDCWD, "/usr/meta-policy",
                    RIX_VFS_O_WRONLY | RIX_VFS_O_CREAT | RIX_VFS_O_TRUNC,
                    0600u);
    if (fd < 0 || close(fd) != 0) return -1;
    rix_pid_t child = fork();
    if (child == (rix_pid_t)-1) return -1;
    if (child == 0) {
        if (setuid(1000u) == 0 && chown("/usr/meta-policy", 1000u, 1000u) == -RIX_EACCES)
            _exit(0);
        _exit(1);
    }
    uint64_t status = 0;
    if (wait(child, &status) != child || status != 0) return -1;
    if (drop_capabilities(RIX_CAP_DAC_OVERRIDE) != 0 ||
        chown("/usr/meta-policy", 1000u, 1000u) != -RIX_EACCES)
        return -1;
    return emit("chown-policy=PASS\n");
}

static int check_copy(const char *path, const char *marker, int require_source_absent) {
    rix_stat_t st;
    rix_acl_t acl;
    if (stat(path, &st) != 0 || st.uid != 0u || st.gid != 2000u ||
        (st.mode & 07777u) != 06770u || getacl(path, &acl) != 0 ||
        acl.user != 1000u || acl.user_perm != 4u || acl.group != 2000u ||
        acl.group_perm != 6u || acl.mask != 6u)
        return -1;
    if (require_source_absent && stat("/usr/meta-source", &st) != -RIX_EINVAL)
        return -1;
    if (emit(marker) != 0 || emit("\n") != 0) return -1;
    return 0;
}

int program_main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc == 2 && same_text(argv[1], "init")) return create_source() == 0 ? 0 : 1;
    if (argc == 2 && same_text(argv[1], "policy")) return check_chown_policy() == 0 ? 0 : 1;
    if (argc == 4 && same_text(argv[1], "check"))
        return check_copy(argv[2], argv[3], 0) == 0 ? 0 : 1;
    if (argc == 4 && same_text(argv[1], "check-mv"))
        return check_copy(argv[2], argv[3], 1) == 0 ? 0 : 1;
    return 2;
}

int main(int argc, char **argv, char **envp) {
    return program_main(argc, argv, envp);
}
