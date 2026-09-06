#include "unistd.h"

static int wait_success(rix_pid_t child) {
    uint64_t status = 0;
    return wait(child, &status) == child && status == 0 ? 0 : -1;
}

int program_main(int argc, char **argv, char **envp) {
    (void)argv;
    (void)envp;
    if (argc != 1) return 2;

    rix_pid_t first = fork();
    if (first == (rix_pid_t)-1) return 1;
    if (first == 0) {
        rix_pid_t session = 0;
        rix_pid_t observed = 0;
        if (create_session(&session) != 0 || session != getpid() ||
            get_session(0, &observed) != 0 || observed != session)
            _exit(1);
        if (attach_tty(0) != 0 || detach_tty(0) != 0 ||
            logout_session() != 0 || get_session(0, &observed) != 0 || observed != 0)
            _exit(1);
        _exit(0);
    }
    if (wait_success(first) != 0) return 1;

    rix_pid_t second = fork();
    if (second == (rix_pid_t)-1) return 1;
    if (second == 0) {
        rix_pid_t session = 0;
        rix_pid_t observed = 0;
        if (login_session(0, &session) != 0 || session != getpid() ||
            get_session(0, &observed) != 0 || observed != session)
            _exit(1);
        if (detach_tty(0) != 0 || logout_session() != 0 ||
            get_session(0, &observed) != 0 || observed != 0)
            _exit(1);
        _exit(0);
    }
    if (wait_success(second) != 0) return 1;
    (void)write(1, "session=PASS\n", 14);
    return 0;
}

int main(int argc, char **argv, char **envp) {
    return program_main(argc, argv, envp);
}
