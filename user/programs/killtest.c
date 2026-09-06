#include <unistd.h>

#define RIX_EACCES 13
#define RIX_EINTR 4
#define RIX_SIGUSR1 10u

static size_t text_length(const char *text) {
    size_t length = 0;
    while (text && text[length]) ++length;
    return length;
}

static int emit(const char *text) {
    size_t length = text_length(text);
    return write(1, text, length) == (rix_ssize_t)length ? 0 : -1;
}

static int wait_for_ready(int fd) {
    char marker = 0;
    rix_ssize_t result = read(fd, &marker, 1);
    return result == 1 && marker == 'R' ? 0 : -1;
}

static int release_child(int fd) {
    char marker = 'G';
    return write(fd, &marker, 1) == 1 ? 0 : -1;
}

static int wait_success(rix_pid_t child) {
    uint64_t status = 0;
    return wait(child, &status) == child && status == 0 ? 0 : -1;
}

static int allowed_cross_uid_case(void) {
    int ready[2];
    int gate[2];
    if (pipe(ready) != 0 || pipe(gate) != 0) return -1;
    rix_pid_t child = fork();
    if (child == (rix_pid_t)-1) return -1;
    if (child == 0) {
        char marker = 0;
        (void)close(ready[0]);
        (void)close(gate[1]);
        if (setuid(2000u) != 0 ||
            write(ready[1], "R", 1) != 1) _exit(2);
        rix_ssize_t result = read(gate[0], &marker, 1);
        _exit(result == -RIX_EINTR ? 0 : 3);
    }
    (void)close(ready[1]);
    (void)close(gate[0]);
    if (wait_for_ready(ready[0]) != 0 || kill(child, RIX_SIGUSR1) != 0) {
        (void)release_child(gate[1]);
        (void)wait_success(child);
        (void)close(ready[0]);
        (void)close(gate[1]);
        return -1;
    }
    (void)close(ready[0]);
    int result = wait_success(child);
    (void)close(gate[1]);
    return result;
}

static int denied_cross_uid_case(void) {
    int ready[2];
    int gate[2];
    if (pipe(ready) != 0 || pipe(gate) != 0) return -1;
    rix_pid_t child = fork();
    if (child == (rix_pid_t)-1) return -1;
    if (child == 0) {
        char marker = 0;
        (void)close(ready[0]);
        (void)close(gate[1]);
        if (setuid(2000u) != 0 ||
            write(ready[1], "R", 1) != 1) _exit(2);
        rix_ssize_t result = read(gate[0], &marker, 1);
        _exit(result == 1 && marker == 'G' ? 0 : 3);
    }
    (void)close(ready[1]);
    (void)close(gate[0]);
    if (wait_for_ready(ready[0]) != 0 ||
        kill(child, RIX_SIGUSR1) != -RIX_EACCES ||
        release_child(gate[1]) != 0) {
        (void)release_child(gate[1]);
        (void)wait_success(child);
        (void)close(ready[0]);
        (void)close(gate[1]);
        return -1;
    }
    (void)close(ready[0]);
    (void)close(gate[1]);
    return wait_success(child);
}

int program_main(int argc, char **argv, char **envp) {
    (void)argv;
    (void)envp;
    if (argc != 1) return 2;
    if (allowed_cross_uid_case() != 0) return 1;
    if (drop_capabilities(RIX_CAP_KILL) != 0) return 1;
    uint64_t capabilities = 0;
    if (get_capabilities(&capabilities) != 0 ||
        (capabilities & RIX_CAP_KILL) != 0u) return 1;
    if (denied_cross_uid_case() != 0) return 1;
    if (emit("kill=PASS\n") != 0) return 1;
    return 0;
}

int main(int argc, char **argv, char **envp) {
    return program_main(argc, argv, envp);
}
