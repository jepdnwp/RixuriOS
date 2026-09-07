#include <unistd.h>

#define RIX_EINVAL 22

static size_t text_length(const char *text) {
    size_t length = 0;
    while (text && text[length]) ++length;
    return length;
}

static int emit(const char *text) {
    size_t length = text_length(text);
    return write(1, text, length) == (rix_ssize_t)length ? 0 : -1;
}

static int expect_error(int result, int error) {
    return result == -error ? 0 : -1;
}

int program_main(int argc, char **argv, char **envp) {
    uint64_t capabilities = 0;
    uint64_t status = 0;
    int ready[2] = {-1, -1};
    int release[2] = {-1, -1};
    char signal = 'R';
    char *child_argv[] = {(char *)"/usr/bin/capdelegatecheck", (char *)0};
    char *empty_env[] = {(char *)0};
    (void)argv;
    (void)envp;

    if (argc != 1 || get_capabilities(&capabilities) != 0 ||
        (capabilities & RIX_CAP_DELEGATE) == 0 ||
        (capabilities & RIX_CAP_KILL) == 0 || pipe(ready) != 0 ||
        pipe(release) != 0)
        return 1;

    rix_pid_t child = fork();
    if (child == (rix_pid_t)-1) return 1;
    if (child == 0) {
        char received = 0;
        (void)close(ready[0]);
        (void)close(release[1]);
        if (drop_capabilities(RIX_CAP_KILL) != 0 ||
            write(ready[1], &signal, 1) != 1 ||
            read(release[0], &received, 1) != 1 || received != signal)
            _exit(1);
        (void)close(ready[1]);
        (void)close(release[0]);
        if (execve("/usr/bin/capdelegatecheck", child_argv, empty_env) != 0)
            _exit(127);
        _exit(126);
    }

    (void)close(ready[1]);
    (void)close(release[0]);
    if (read(ready[0], &signal, 1) != 1 ||
        delegate_capabilities(child, RIX_CAP_KILL) != 0 ||
        get_capabilities(&capabilities) != 0 ||
        (capabilities & RIX_CAP_KILL) != 0 ||
        expect_error(delegate_capabilities(child, RIX_CAP_DELEGATE), RIX_EINVAL) != 0 ||
        write(release[1], &signal, 1) != 1) {
        (void)close(ready[0]);
        (void)close(release[1]);
        return 1;
    }
    (void)close(ready[0]);
    (void)close(release[1]);

    if (wait(child, &status) != child || status != 0) return 1;
    return emit("delegation=PASS\n") == 0 ? 0 : 1;
}

int main(int argc, char **argv, char **envp) {
    return program_main(argc, argv, envp);
}
