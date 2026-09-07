#include "unistd.h"
#include <stddef.h>

static int wait_success(rix_pid_t child) {
    uint64_t status = 0;
    return wait(child, &status) == child && status == 0 ? 0 : -1;
}

static int has_session(const rix_session_info_t *sessions, size_t count,
                       rix_pid_t session, rix_pid_t leader) {
    for (size_t index = 0; index < count; ++index)
        if (sessions[index].session == session && sessions[index].leader == leader)
            return 1;
    return 0;
}

int program_main(int argc, char **argv, char **envp) {
    const rix_timespec_t child_hold = { 1u, 0u };
    const rix_timespec_t parent_delay = { 0u, 100000000u };
    rix_pid_t child;
    rix_session_info_t sessions[8];
    size_t count = 0;
    (void)argv;
    (void)envp;
    if (argc != 1) return 2;
    child = fork();
    if (child == (rix_pid_t)-1) return 1;
    if (child == 0) {
        rix_pid_t session = 0;
        if (create_session(&session) != 0 || session != getpid()) _exit(1);
        if (nanosleep(&child_hold, (rix_timespec_t *)0) != 0) _exit(1);
        if (logout_session() != 0) _exit(1);
        _exit(0);
    }
    if (nanosleep(&parent_delay, (rix_timespec_t *)0) != 0 ||
        list_sessions(sessions, 8u, &count) < 2 ||
        !has_session(sessions, count, child, child))
        return 1;
    if (wait_success(child) != 0 || list_sessions(sessions, 8u, &count) < 1 ||
        has_session(sessions, count, child, child))
        return 1;
    (void)write(1, "session-registry=PASS\n", 22);
    return 0;
}

int main(int argc, char **argv, char **envp) {
    return program_main(argc, argv, envp);
}
