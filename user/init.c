#include "shell.h"
#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

#define RIX_AT_FDCWD (-100)
#define RIX_VFS_O_WRONLY 1u
#define RIX_VFS_O_CREAT 4u
#define RIX_VFS_O_TRUNC 8u
#define RIX_VFS_O_APPEND 16u
#define RIX_SHELL_MODE 0644u
#define RIX_INIT_LINE_CAP 256u
#define RIX_INIT_PATH_CAP 128u
#define RIX_INIT_PIDS_CAP RIX_SHELL_MAX_COMMANDS
#define RIX_INIT_JOBS_CAP 8u
#define RIX_WAITPID_NOHANG 1u

static size_t text_length(const char *text) {
    size_t length = 0;
    if (!text) return 0;
    while (text[length]) ++length;
    return length;
}

static int write_all(int fd, const void *buffer, size_t length) {
    const uint8_t *bytes = buffer;
    size_t written = 0;
    while (written < length) {
        rix_ssize_t count = write(fd, bytes + written, length - written);
        if (count <= 0) return -1;
        written += (size_t)count;
    }
    return 0;
}

static int write_text(int fd, const char *text) {
    return write_all(fd, text, text_length(text));
}

static int fd_writer(const void *data, size_t length, void *context) {
    if (!context) return -1;
    return write_all(*(const int *)context, data, length);
}

static int path_exists(const char *path, void *context) {
    (void)context;
    int fd = openat(RIX_AT_FDCWD, path, 0u, 0u);
    if (fd < 0) return -1;
    return close(fd);
}

static int apply_redirections(const rix_shell_command_t *command) {
    if (!command) return -1;
    for (size_t i = 0; i < command->redir_count; ++i) {
        const rix_shell_redir_t *redir = &command->redir[i];
        uint32_t flags = 0u;
        int target = 0;
        if (redir->type == RIX_SHELL_REDIR_READ) {
            target = 0;
        } else if (redir->type == RIX_SHELL_REDIR_APPEND) {
            flags = RIX_VFS_O_WRONLY | RIX_VFS_O_CREAT | RIX_VFS_O_APPEND;
            target = 1;
        } else if (redir->type == RIX_SHELL_REDIR_WRITE) {
            flags = RIX_VFS_O_WRONLY | RIX_VFS_O_CREAT | RIX_VFS_O_TRUNC;
            target = 1;
        } else {
            return -1;
        }
        int fd = openat(RIX_AT_FDCWD, redir->path, flags, RIX_SHELL_MODE);
        if (fd < 0 || dup2(fd, target) < 0) {
            if (fd >= 0) (void)close(fd);
            return -1;
        }
        if (close(fd) < 0) return -1;
    }
    return 0;
}

typedef struct {
    int input_fd;
    rix_pid_t pid[RIX_INIT_PIDS_CAP];
    size_t pid_count;
    int background;
} rix_shell_execution_t;

typedef struct {
    rix_pid_t pid[RIX_INIT_PIDS_CAP];
    size_t pid_count;
    size_t complete;
    int active;
} rix_shell_job_t;

static rix_shell_job_t jobs[RIX_INIT_JOBS_CAP];

static void reset_execution(rix_shell_execution_t *execution) {
    execution->input_fd = -1;
    execution->pid_count = 0;
    execution->background = 0;
}

static void reap_background_jobs(void) {
    for (size_t j = 0; j < RIX_INIT_JOBS_CAP; ++j) {
        rix_shell_job_t *job = &jobs[j];
        if (!job->active) continue;
        for (size_t i = 0; i < job->pid_count; ++i) {
            if (!job->pid[i]) continue;
            uint64_t child_status = 0;
            rix_pid_t result = waitpid(job->pid[i], &child_status, RIX_WAITPID_NOHANG);
            if (result == job->pid[i]) {
                job->pid[i] = 0;
                ++job->complete;
            }
        }
        if (job->complete == job->pid_count) {
            (void)write_text(1, "[job] done\n");
            job->active = 0;
        }
    }
}

static int save_background_job(const rix_shell_execution_t *execution) {
    if (!execution || !execution->pid_count) return -1;
    for (size_t j = 0; j < RIX_INIT_JOBS_CAP; ++j) {
        if (jobs[j].active) continue;
        jobs[j].pid_count = execution->pid_count;
        jobs[j].complete = 0;
        jobs[j].active = 1;
        for (size_t i = 0; i < execution->pid_count; ++i) jobs[j].pid[i] = execution->pid[i];
        return 0;
    }
    return -1;
}

static int wait_for_execution(rix_shell_execution_t *execution, int *status) {
    int final_status = 1;
    if (!execution || !status) return -1;
    for (size_t i = 0; i < execution->pid_count; ++i) {
        uint64_t child_status = 0;
        rix_pid_t child = wait(execution->pid[i], &child_status);
        if (child != execution->pid[i]) return -1;
        final_status = (int)child_status;
    }
    reset_execution(execution);
    *status = final_status;
    return 0;
}

static void child_error(const char *message, int status) {
    (void)write_text(2, message);
    _exit(status);
}

static void snapshot_command(rix_shell_command_t *destination,
                             const rix_shell_command_t *source) {
    volatile uint8_t *dst = (volatile uint8_t *)destination;
    const volatile uint8_t *src = (const volatile uint8_t *)source;
    for (size_t i = 0; i < sizeof(*destination); ++i) dst[i] = src[i];
}

static int run_external_child(const rix_shell_command_t *command) {
    char cwd[256];
    char pwd_env[260];
    static char *shell_environment[3];
    char path[RIX_INIT_PATH_CAP];
    char *child_argv[RIX_SHELL_MAX_ARGS];
    int handled = 0;
    int status = 2;
    int output_fd = 1;
    if (getcwd(cwd, sizeof(cwd)) < 0) child_error("rixuri: cwd unavailable\n", 125);
    shell_environment[0] = (char *)"PATH=/bin:/usr/bin:/sbin:/usr/sbin";
    shell_environment[1] = pwd_env;
    shell_environment[2] = NULL;
    pwd_env[0]='P'; pwd_env[1]='W'; pwd_env[2]='D'; pwd_env[3]='=';
    size_t cwd_length = text_length(cwd);
    if (cwd_length + 5u > sizeof(pwd_env)) child_error("rixuri: cwd too long\n", 125);
    for (size_t i=0; i<=cwd_length; ++i) pwd_env[4u+i]=cwd[i];
    for (size_t i = 0; i < command->argc; ++i) child_argv[i] = command->argv[i];
    child_argv[command->argc] = NULL;
    if (rix_shell_run_builtin(command, fd_writer, &output_fd, &handled, &status) != 0)
        child_error("rixuri: builtin failed\n", 125);
    if (handled) _exit(status);
    if (rix_shell_resolve_path(command->argv[0], "/bin:/usr/bin:/sbin:/usr/sbin",
                               path_exists, NULL, path, sizeof(path)) != 0) {
        (void)write_text(2, "rixuri: command not found: ");
        (void)write_text(2, command->argv[0]);
        (void)write_text(2, "\n");
        _exit(127);
    }
    (void)execve(path, child_argv, shell_environment);
    (void)write_text(2, "rixuri: exec failed: ");
    (void)write_text(2, path);
    (void)write_text(2, "\n");
    _exit(126);
}

static int run_pipeline_command(const rix_shell_command_t *command, size_t command_index,
                                int input_fd, int output_fd, void *context) {
    rix_shell_execution_t *execution = context;
    int next_pipe[2] = {-1, -1};
    volatile int child_input;
    if (!execution || !command || !command->argc || command_index >= RIX_INIT_PIDS_CAP) return -1;
    if (command_index == 0u) {
        int background = execution->background;
        reset_execution(execution);
        execution->background = background;
    }
    child_input = input_fd == (int)RIX_SHELL_PIPE_INPUT_MARKER ? execution->input_fd : -1;
    if (input_fd == (int)RIX_SHELL_PIPE_INPUT_MARKER && child_input < 0) return -1;
    if (output_fd != 1 && pipe(next_pipe) != 0) return -1;

    rix_pid_t child = fork();
    if (child == (rix_pid_t)-1) {
        if (next_pipe[0] >= 0) (void)close(next_pipe[0]);
        if (next_pipe[1] >= 0) (void)close(next_pipe[1]);
        if (child_input >= 0) (void)close(child_input);
        return -1;
    }
    if (child == 0) {
        /* Redirection syscalls must not leave the child using mutable parser
         * storage through the parent's command pointer.  Keep the command
         * descriptor stable across fd setup and exec preparation. */
        rix_shell_command_t child_command;
        snapshot_command(&child_command, command);
        if (child_input >= 0 && child_input != 0) {
            if (dup2(child_input, 0) < 0) child_error("rixuri: stdin setup failed\n", 125);
        }
        if (next_pipe[1] >= 0 && next_pipe[1] != 1) {
            if (dup2(next_pipe[1], 1) < 0) child_error("rixuri: stdout setup failed\n", 125);
        }
        if (child_input >= 0 && child_input != 0) (void)close(child_input);
        if (next_pipe[1] >= 0 && next_pipe[1] != 1) (void)close(next_pipe[1]);
        if (next_pipe[0] >= 0 && next_pipe[0] != 0) (void)close(next_pipe[0]);
        if (close_pipes_except(child_input >= 0 ? 0 : -1,
                               next_pipe[1] >= 0 ? 1 : -1) != 0)
            child_error("rixuri: pipe cleanup failed\n", 125);
        if (apply_redirections(&child_command) != 0) child_error("rixuri: redirection failed\n", 125);
        run_external_child(&child_command);
    }

    if (child_input >= 0) (void)close(child_input);
    if (next_pipe[1] >= 0) (void)close(next_pipe[1]);
    execution->input_fd = next_pipe[0];
    if (execution->pid_count >= RIX_INIT_PIDS_CAP) {
        if (next_pipe[0] >= 0) (void)close(next_pipe[0]);
        return -1;
    }
    execution->pid[execution->pid_count++] = child;
    if (output_fd == 1) {
        if (execution->background) {
            if (save_background_job(execution) != 0) return -1;
            reset_execution(execution);
            return 0;
        }
        int status = 1;
        if (wait_for_execution(execution, &status) != 0) return -1;
        return status;
    }
    return 0;
}

static int run_command(const rix_shell_pipeline_t *pipeline, int *status) {
    rix_shell_execution_t execution;
    reset_execution(&execution);
    execution.background = pipeline->background != 0;
    return rix_shell_execute_pipeline_indexed(pipeline, run_pipeline_command,
                                              &execution, status);
}

static int shell_cd_builtin(const rix_shell_pipeline_t *pipeline, int *handled) {
    const rix_shell_command_t *command;
    const char *target;
    if (handled) *handled = 0;
    if (!pipeline || pipeline->command_count != 1u || pipeline->background) return 0;
    command = &pipeline->command[0];
    if (!command->argc || !command->argv[0]) return 0;
    if (!(command->argv[0][0]=='c'&&command->argv[0][1]=='d'&&command->argv[0][2]==0)) return 0;
    if (handled) *handled = 1;
    if (command->argc > 2u) { (void)write_text(2, "cd: expected one path\n"); return 2; }
    target = command->argc == 2u ? command->argv[1] : "/";
    if (chdir(target) != 0) { (void)write_text(2, "cd: no such directory\n"); return 1; }
    return 0;
}

static void shell_prompt(void) {
    static char cwd[256];
    const char *user = getuid() == 0u ? "root" : "user";
    if (getcwd(cwd, sizeof(cwd)) < 0) cwd[0] = 0;
    (void)write_text(1, "\033[1;32m");
    (void)write_text(1, user);
    (void)write_text(1, "\033[0m@\033[1;34mrixurios\033[0m ");
    (void)write_text(1, "\033[1;36m");
    (void)write_text(1, cwd);
    (void)write_text(1, "\033[0m \033[1;37m:\033[0m ");
}

static int shell_read_line(char *line, size_t capacity) {
    size_t used = 0;
    if (!line || capacity < 2u) return -1;
    for (;;) {
        rix_ssize_t count = read(0, line + used, capacity - used - 1u);
        if (count <= 0) return -1;
        used += (size_t)count;
        if (used && line[used - 1u] == '\n') {
            line[used - 1u] = 0;
            if (used > 1u && line[used - 2u] == '\r') line[used - 2u] = 0;
            return 0;
        }
        if (used + 1u >= capacity) return -1;
    }
}

static int shell_execute_line(const char *line, rix_shell_history_t *history) {
    rix_shell_tokens_t tokens;
    rix_shell_pipeline_t pipeline;
    int status = 2;
    if (rix_shell_lex(line, &tokens) != 0 || tokens.count == 0) return 0;
    if (rix_shell_parse_pipeline(&tokens, &pipeline) != 0) {
        (void)write_text(2, "rixuri: syntax error\n");
        return 2;
    }
    if (history) (void)rix_shell_history_add(history, line);
    { int handled = 0; int cd_status = shell_cd_builtin(&pipeline, &handled); if (handled) return cd_status; }
    if (run_command(&pipeline, &status) != 0) {
        (void)write_text(2, "rixuri: command execution failed\n");
        return 125;
    }
    return status;
}

void _start(void) {
    char line[RIX_INIT_LINE_CAP];
    rix_shell_history_t history;
    rix_shell_history_init(&history);
    (void)write_text(1, "RixuriOS shell ready\r\n");
    for (;;) {
        reap_background_jobs();
        shell_prompt();
        if (shell_read_line(line, sizeof(line)) != 0) break;
        (void)shell_execute_line(line, &history);
    }
    _exit(0);
}
