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
#define RIX_INIT_USER_CAP 32u
#define RIX_INIT_HOME_CAP 96u
#define RIX_INIT_PASSWD_CAP 2048u

static char shell_user[RIX_INIT_USER_CAP];
static char shell_home[RIX_INIT_HOME_CAP];

/* Bounded shell variable table ("NAME=value" each). PATH/PWD resolve
 * dynamically when not overridden here; everything else reads the table.
 * Command substitution and globbing are intentionally absent (see below). */
#define RIX_INIT_ENV_CAP 32u
#define RIX_INIT_ENV_KV 160u
#define RIX_INIT_EXPAND_CAP 1024u
static char shell_env[RIX_INIT_ENV_CAP][RIX_INIT_ENV_KV];
static size_t shell_env_count;
static char shell_pwd_cache[256];
static int shell_exit_pending;
static int shell_exit_code;
static int shell_last_status;
static const char *shell_path_variable(void);

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

static void write_sdec(int fd, long value) {
    char digits[24];
    size_t used = 0;
    unsigned long magnitude;
    if (value < 0) {
        (void)write_text(fd, "-");
        magnitude = (unsigned long)(-(value + 1)) + 1u;
    } else {
        magnitude = (unsigned long)value;
    }
    if (!magnitude) {
        (void)write_text(fd, "0");
        return;
    }
    while (magnitude && used + 1u < sizeof(digits)) {
        digits[used++] = (char)('0' + magnitude % 10u);
        magnitude /= 10u;
    }
    for (size_t k = 0; k < used / 2u; ++k) {
        char swap = digits[k];
        digits[k] = digits[used - 1u - k];
        digits[used - 1u - k] = swap;
    }
    digits[used] = 0;
    (void)write_text(fd, digits);
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

static int read_whole_file(const char *path, char *buffer, size_t capacity,
                           size_t *out_length) {    int fd;
    size_t used = 0;
    if (!path || !buffer || !out_length || capacity < 2u) return -1;
    fd = openat(RIX_AT_FDCWD, path, 0u, 0u);
    if (fd < 0) return -1;
    for (;;) {
        rix_ssize_t count = read(fd, buffer + used, capacity - used - 1u);
        if (count < 0) {
            (void)close(fd);
            return -1;
        }
        if (count == 0) break;
        used += (size_t)count;
        if (used >= capacity - 1u) {
            (void)close(fd);
            return -1;
        }
    }
    buffer[used] = 0;
    *out_length = used;
    return close(fd);
}

static int valid_user_name(const char *name, size_t length) {
    if (!name || !length || length >= RIX_INIT_USER_CAP) return 0;
    for (size_t i = 0; i < length; ++i) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return 0;
    }
    return 1;
}

static void copy_text(char *destination, size_t capacity, const char *source,
                      size_t length) {
    size_t n = length < capacity - 1u ? length : capacity - 1u;
    for (size_t i = 0; i < n; ++i) destination[i] = source[i];
    destination[n] = 0;
}

/* Resolve our uid to a passwd login name. Falls back to root/user so the
 * shell always has a usable identity for the prompt and home directory. */
static void resolve_shell_user(void) {
    char passwd[RIX_INIT_PASSWD_CAP];
    size_t length = 0, i = 0;
    uint32_t uid = getuid();
    copy_text(shell_user, sizeof(shell_user), uid == 0u ? "root" : "user", 4u);
#if 1
    (void)passwd; (void)length; (void)i;
    return;
#endif
    if (read_whole_file("/etc/passwd", passwd, sizeof(passwd), &length) != 0)
        return;
    while (i < length) {
        size_t line = i, name_end, value = 0;
        int digits = 0;
        while (i < length && passwd[i] != '\n') ++i;
        size_t end = i;
        if (i < length) ++i;
        if (end > line && passwd[end - 1] == '\r') --end;
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
        if (valid_user_name(passwd + line, name_end - line)) {
            copy_text(shell_user, sizeof(shell_user), passwd + line,
                      name_end - line);
            return;
        }
    }
}

/* Build /home/<login>/desktop,/documents once per shell startup. Missing
 * parents and existing directories are fine; failures are non-fatal. */
static void ensure_home_skeleton(void) {
    static const char *leaf_names[3] = {"", "/desktop", "/documents"};
    copy_text(shell_home, sizeof(shell_home), "/home/", 6u);
    {
        size_t used = 6u, i = 0;
        while (shell_user[i] && used + 1u < sizeof(shell_home)) {
            shell_home[used++] = shell_user[i++];
        }
        shell_home[used] = 0;
    }
    (void)mkdir(shell_home, 0755u);
    for (size_t leaf = 1; leaf < 3; ++leaf) {
        char path[RIX_INIT_HOME_CAP];
        size_t used = 0;
        while (shell_home[used] && used + 1u < sizeof(path)) {
            path[used] = shell_home[used];
            ++used;
        }
        for (size_t k = 0; leaf_names[leaf][k] && used + 1u < sizeof(path); ++k)
            path[used++] = leaf_names[leaf][k];
        path[used] = 0;
        (void)mkdir(path, 0755u);
    }
    (void)chdir(shell_home);
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
    (void)write_text(1, "RIXURI:DBG R enter\r\n");
    (void)write_text(1, "RIXURI:DBG jobs0=");
    write_sdec(1, (long)jobs[0].active);
    (void)write_text(1, "\r\n");
    (void)write_text(1, "RIXURI:DBG R exit\r\n");
    return;
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
    (void)write_text(1, "RIXURI:DBG R exit\r\n");
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
    static char path_env[256];
    static char *shell_environment[3 + RIX_INIT_ENV_CAP];
    char path[RIX_INIT_PATH_CAP];
    char *child_argv[RIX_SHELL_MAX_ARGS];
    int handled = 0;
    int status = 2;
    int output_fd = 1;
    if (getcwd(cwd, sizeof(cwd)) < 0) child_error("rixuri: cwd unavailable\n", 125);
    {
        const char *pathvar = shell_path_variable();
        size_t k = 0;
        path_env[k++]='P'; path_env[k++]='A'; path_env[k++]='T'; path_env[k++]='H'; path_env[k++]='=';
        while (pathvar[k-5u] && k + 1u < sizeof(path_env)) { path_env[k] = pathvar[k-5u]; ++k; }
        path_env[k] = 0;
    }
    shell_environment[0] = path_env;
    shell_environment[1] = pwd_env;
    {
        size_t count = 2u;
        for (size_t i = 0; i < shell_env_count && count + 1u < sizeof(shell_environment)/sizeof(shell_environment[0]); ++i)
            shell_environment[count++] = shell_env[i];
        shell_environment[count] = NULL;
    }
    pwd_env[0]='P'; pwd_env[1]='W'; pwd_env[2]='D'; pwd_env[3]='=';
    size_t cwd_length = text_length(cwd);
    if (cwd_length + 5u > sizeof(pwd_env)) child_error("rixuri: cwd too long\n", 125);
    for (size_t i=0; i<=cwd_length; ++i) pwd_env[4u+i]=cwd[i];
    for (size_t i = 0; i < command->argc; ++i) child_argv[i] = command->argv[i];
    child_argv[command->argc] = NULL;
    if (rix_shell_run_builtin(command, fd_writer, &output_fd, &handled, &status) != 0)
        child_error("rixuri: builtin failed\n", 125);
    if (handled) _exit(status);
    if (rix_shell_resolve_path(command->argv[0], shell_path_variable(),
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

static int streq(const char *a, const char *b) {
    size_t i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i] && a[i] == b[i]) ++i;
    return a[i] == 0 && b[i] == 0;
}

static const char *shell_path_variable(void) {
    for (size_t i = 0; i < shell_env_count; ++i) {
        const char *entry = shell_env[i];
        if (entry[0]=='P'&&entry[1]=='A'&&entry[2]=='T'&&entry[3]=='H'&&entry[4]=='=')
            return entry + 5;
    }
    return "/bin:/usr/bin:/sbin:/usr/sbin";
}

static const char *shell_lookup_var(const char *name, void *context) {
    (void)context;
    size_t nlen = 0;
    if (!name) return NULL;
    while (name[nlen]) ++nlen;
    if (streq(name, "PWD")) {
        if (getcwd(shell_pwd_cache, sizeof(shell_pwd_cache)) < 0) return NULL;
        return shell_pwd_cache;
    }
    if (streq(name, "PATH")) return shell_path_variable();
    for (size_t i = 0; i < shell_env_count; ++i) {
        const char *entry = shell_env[i];
        size_t k = 0;
        while (k < nlen && entry[k] == name[k]) ++k;
        if (k == nlen && entry[k] == '=') return entry + k + 1u;
    }
    return "";
}

static int valid_env_name(const char *name, size_t length) {
    if (!name || !length || length >= RIX_INIT_ENV_KV) return 0;
    if (!((name[0]>='A'&&name[0]<='Z')||(name[0]>='a'&&name[0]<='z')||name[0]=='_'))
        return 0;
    for (size_t i = 1; i < length; ++i) {
        char c = name[i];
        if (!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'))
            return 0;
    }
    return 1;
}

static void format_s64(char *buffer, size_t capacity, int64_t value) {
    char digits[24];
    size_t used = 0;
    unsigned long long magnitude;
    size_t pos = 0;
    if (!buffer || capacity == 0u) return;
    if (value < 0) {
        magnitude = (unsigned long long)(-(value + 1)) + 1u;
    } else {
        magnitude = (unsigned long long)value;
    }
    if (value < 0) {
        if (pos + 1u < capacity) buffer[pos++] = '-';
        else { buffer[0] = 0; return; }
    }
    if (!magnitude) {
        if (pos + 1u < capacity) buffer[pos++] = '0';
        buffer[pos < capacity ? pos : capacity - 1u] = 0;
        return;
    }
    while (magnitude && used + 1u < sizeof(digits)) {
        digits[used++] = (char)('0' + magnitude % 10u);
        magnitude /= 10u;
    }
    while (used) {
        if (pos + 1u >= capacity) { buffer[0] = 0; return; }
        buffer[pos++] = digits[--used];
    }
    buffer[pos] = 0;
}

/* Splice one $((...)) group at input[start] (input[start]=='$').
 * Returns 1 with *consumed set when shaped-but-broken (caller fails the
 * line), 0 with *consumed set and number[] filled on success. Callers only
 * invoke it after seeing "$((", so "not shaped" cannot occur here. */
static int arithmetic_group(const char *input, size_t start, char *number,
                            size_t numcap, size_t *consumed) {
    size_t j = start + 3u;
    unsigned depth = 0u;
    if (!input || !number || numcap == 0u || !consumed) return 1;
    for (;;) {
        if (!input[j]) return 1;
        if (input[j] == '(') {
            ++depth;
        } else if (input[j] == ')') {
            if (depth == 0u) break;
            --depth;
        }
        ++j;
        if (j - start > 160u) return 1;
    }
    /* j is the first ')' of the final '))'. */
    if (input[j+1u] != ')') return 1;
    {
        char expression[128];
        size_t length = j - (start + 3u);
        int64_t value = 0;
        if (length >= sizeof(expression)) return 1;
        for (size_t k = 0; k < length; ++k) expression[k] = input[start + 3u + k];
        expression[length] = 0;
        if (rix_shell_arithmetic_eval(expression, &value) != 0) return 1;
        format_s64(number, numcap, value);
        if (!number[0]) return 1;
        *consumed = (j + 2u) - start;
        return 0;
    }
}

/* Raw-line expansion pre-pass: variable ($V/${V}) and arithmetic ($(()))
 * substitution honoring single-quote suppression, applied BEFORE lexing so
 * the existing lexer/parser (which strips quotes) see final text. Quote and
 * backslash characters are preserved for the lexer; command substitution and
 * globbing stay literal (deferred with rationale in the closure report). */
static int expand_shell_line(const char *input, char *output, size_t capacity) {
    size_t in = 0, out = 0;
    char quote = 0;
    if (!input || !output || capacity < 2u) return -1;
    while (input[in]) {
        char ch = input[in];
        if (ch == '\'' && quote != '"') {
            quote = quote == '\'' ? 0 : '\'';
            if (out + 1u >= capacity) return -1;
            output[out++] = ch;
            ++in;
            continue;
        }
        if (ch == '"' && quote != '\'') {
            quote = quote == '"' ? 0 : '"';
            if (out + 1u >= capacity) return -1;
            output[out++] = ch;
            ++in;
            continue;
        }
        if (ch == '\\' && quote != '\'') {
            if (out + 2u >= capacity) return -1;
            output[out++] = ch;
            ++in;
            if (!input[in]) return -1;
            output[out++] = input[in++];
            continue;
        }
        if (ch == '$' && quote != '\'') {
            if (input[in+1u] == '(' && input[in+2u] == '(') {
                char number[32];
                size_t consumed = 0;
                if (arithmetic_group(input, in, number, sizeof(number),
                                     &consumed) != 0)
                    return -1;
                for (size_t a = 0; number[a]; ++a) {
                    if (out + 1u >= capacity) return -1;
                    output[out++] = number[a];
                }
                in += consumed;
                continue;
            }
            {
                size_t start = in + 1u;
                int braced = input[start] == '{';
                size_t name = braced ? start + 1u : start;
                size_t m = name;
                int ok = ((input[m]>='A'&&input[m]<='Z')||(input[m]>='a'&&input[m]<='z')||input[m]=='_');
                if (!ok) {
                    if (out + 1u >= capacity) return -1;
                    output[out++] = ch;
                    ++in;
                    continue;
                }
                ++m;
                while ((input[m]>='A'&&input[m]<='Z')||(input[m]>='a'&&input[m]<='z')||
                       (input[m]>='0'&&input[m]<='9')||input[m]=='_') ++m;
                if (braced) {
                    if (input[m] != '}') {
                        if (out + 1u >= capacity) return -1;
                        output[out++] = ch;
                        ++in;
                        continue;
                    }
                    ++m;
                }
                char varname[64];
                size_t vlen = (braced ? m - 1u : m) - name;
                if (vlen == 0u || vlen >= sizeof(varname)) {
                    if (out + 1u >= capacity) return -1;
                    output[out++] = ch;
                    ++in;
                    continue;
                }
                for (size_t q = 0; q < vlen; ++q) varname[q] = input[name + q];
                varname[vlen] = 0;
                const char *value = shell_lookup_var(varname, NULL);
                if (!value) value = "";
                while (*value) {
                    if (out + 1u >= capacity) return -1;
                    output[out++] = *value++;
                }
                in = m;
                continue;
            }
        }
        if (out + 1u >= capacity) return -1;
        output[out++] = ch;
        ++in;
    }
    if (quote) return -1;
    output[out] = 0;
    return 0;
}

static int shell_history_builtin(const rix_shell_pipeline_t *pipeline,
                                 rix_shell_history_t *history, int *handled) {
    const rix_shell_command_t *command;
    if (handled) *handled = 0;
    if (!pipeline || pipeline->command_count != 1u || pipeline->background || !history) return 0;
    command = &pipeline->command[0];
    if (!command->argc || !command->argv[0]) return 0;
    if (!(command->argv[0][0]=='h'&&command->argv[0][1]=='i'&&command->argv[0][2]=='s'&&
          command->argv[0][3]=='t'&&command->argv[0][4]=='o'&&command->argv[0][5]=='r'&&
          command->argv[0][6]=='y'&&command->argv[0][7]==0)) return 0;
    if (handled) *handled = 1;
    if (command->argc > 1u) { (void)write_text(2, "history: arguments unsupported\n"); return 2; }
    for (size_t i = 0; i < history->count; ++i) {
        (void)write_text(1, "  ");
        write_sdec(1, (long)(i + 1u));
        (void)write_text(1, "  ");
        (void)write_text(1, history->entry[i]);
        (void)write_text(1, "\n");
    }
    return 0;
}

static char expand_arg_store[RIX_SHELL_MAX_COMMANDS][RIX_SHELL_MAX_ARGS][RIX_SHELL_TOKEN_TEXT];
static char expand_redir_store[RIX_SHELL_MAX_COMMANDS][RIX_SHELL_MAX_REDIRS][RIX_SHELL_TOKEN_TEXT];

/* Expand every argv word and redirection path in place (variable + $(( ))).
 * Breakfast rule: expansion failure fails the line closed, never runs a
 * half-expanded command. Command substitution and globbing stay literal
 * (deferred: execution/capture and quoting-metadata redesign, documented in
 * the closure report). */
static int expand_pipeline_words(rix_shell_pipeline_t *pipeline) {
    char stage[RIX_INIT_EXPAND_CAP];
    if (!pipeline) return -1;
    for (size_t c = 0; c < pipeline->command_count; ++c) {
        rix_shell_command_t *command = &pipeline->command[c];
        for (size_t a = 0; a < command->argc; ++a) {
            if (!command->argv[a] ||
                expand_shell_line(command->argv[a], stage, sizeof(stage)) != 0) {
                (void)write_text(2, "rixuri: expansion failed\n");
                return -1;
            }
            if (rix_shell_expand_word(stage, expand_arg_store[c][a],
                                      sizeof(expand_arg_store[c][a]),
                                      shell_lookup_var, NULL) != 0) {
                (void)write_text(2, "rixuri: expansion failed\n");
                return -1;
            }
            command->argv[a] = expand_arg_store[c][a];
        }
        for (size_t r = 0; r < command->redir_count; ++r) {
            if (expand_shell_line(command->redir[r].path, stage, sizeof(stage)) != 0 ||
                rix_shell_expand_word(stage, expand_redir_store[c][r],
                                      sizeof(expand_redir_store[c][r]),
                                      shell_lookup_var, NULL) != 0) {
                (void)write_text(2, "rixuri: expansion failed\n");
                return -1;
            }
            {
                size_t k = 0;
                while (expand_redir_store[c][r][k] && k + 1u < sizeof(command->redir[r].path)) {
                    command->redir[r].path[k] = expand_redir_store[c][r][k];
                    ++k;
                }
                if (expand_redir_store[c][r][k]) {
                    (void)write_text(2, "rixuri: expansion failed\n");
                    return -1;
                }
                command->redir[r].path[k] = 0;
            }
        }
    }
    return 0;
}

static int shell_export_builtin(const rix_shell_pipeline_t *pipeline, int *handled) {
    const rix_shell_command_t *command;
    if (handled) *handled = 0;
    if (!pipeline || pipeline->command_count != 1u || pipeline->background) return 0;
    command = &pipeline->command[0];
    if (!command->argc || !command->argv[0] || !streq(command->argv[0], "export")) return 0;
    if (handled) *handled = 1;
    if (command->argc == 1u) {
        for (size_t i = 0; i < shell_env_count; ++i) {
            (void)write_text(1, shell_env[i]);
            (void)write_text(1, "\n");
        }
        (void)write_text(1, "PATH=");
        (void)write_text(1, shell_path_variable());
        (void)write_text(1, "\nPWD=");
        if (getcwd(shell_pwd_cache, sizeof(shell_pwd_cache)) < 0) return 1;
        (void)write_text(1, shell_pwd_cache);
        (void)write_text(1, "\n");
        return 0;
    }
    for (size_t a = 1; a < command->argc; ++a) {
        const char *arg = command->argv[a];
        size_t nlen = 0;
        while (arg[nlen] && arg[nlen] != '=') ++nlen;
        if (!arg[nlen] || !valid_env_name(arg, nlen)) {
            (void)write_text(2, "export: expected NAME=VALUE\n");
            return 2;
        }
        size_t vlen = text_length(arg + nlen + 1u);
        if (nlen + 1u + vlen + 1u > RIX_INIT_ENV_KV) {
            (void)write_text(2, "export: value too long\n");
            return 1;
        }
        {
            size_t slot = shell_env_count;
            for (size_t i = 0; i < shell_env_count; ++i) {
                size_t k = 0;
                while (k < nlen && shell_env[i][k] == arg[k]) ++k;
                if (k == nlen && shell_env[i][k] == '=') { slot = i; break; }
            }
            if (slot == shell_env_count) {
                if (shell_env_count >= RIX_INIT_ENV_CAP) {
                    (void)write_text(2, "export: table full\n");
                    return 1;
                }
                ++shell_env_count;
            }
            for (size_t k = 0; k < nlen; ++k) shell_env[slot][k] = arg[k];
            shell_env[slot][nlen] = '=';
            for (size_t k = 0; k <= vlen; ++k) shell_env[slot][nlen + 1u + k] = arg[nlen + 1u + k];
        }
    }
    return 0;
}

/* Mark one reaped pid inside the job table without printing. Returns 1 when
 * the pid belonged to a job, 0 otherwise. */
static int note_job_reaped(rix_pid_t pid) {
    for (size_t j = 0; j < RIX_INIT_JOBS_CAP; ++j) {
        rix_shell_job_t *job = &jobs[j];
        if (!job->active) continue;
        for (size_t i = 0; i < job->pid_count; ++i) {
            if (job->pid[i] != pid) continue;
            job->pid[i] = 0;
            ++job->complete;
            if (job->complete == job->pid_count) job->active = 0;
            return 1;
        }
    }
    return 0;
}

static int parse_decimal_pid(const char *s, rix_pid_t *out) {
    rix_pid_t value = 0;
    size_t digits = 0;
    if (!s || !out) return -1;
    while (*s) {
        if (*s < '0' || *s > '9' || digits >= 10u) return -1;
        value = value * 10u + (rix_pid_t)(*s - '0');
        ++digits;
        ++s;
    }
    if (!digits) return -1;
    *out = value;
    return 0;
}

static int shell_wait_builtin(const rix_shell_pipeline_t *pipeline, int *handled) {
    const rix_shell_command_t *command;
    int last = 0;
    int waited_any = 0;
    if (handled) *handled = 0;
    if (!pipeline || pipeline->command_count != 1u || pipeline->background) return 0;
    command = &pipeline->command[0];
    if (!command->argc || !command->argv[0] || !streq(command->argv[0], "wait")) return 0;
    if (handled) *handled = 1;
    if (command->argc == 1u) {
        for (size_t j = 0; j < RIX_INIT_JOBS_CAP; ++j) {
            rix_shell_job_t *job = &jobs[j];
            rix_pid_t pids[RIX_INIT_PIDS_CAP];
            size_t count = 0;
            if (!job->active) continue;
            for (size_t i = 0; i < job->pid_count; ++i)
                if (job->pid[i]) pids[count++] = job->pid[i];
            for (size_t i = 0; i < count; ++i) {
                uint64_t child_status = 0;
                rix_pid_t got = waitpid(pids[i], &child_status, 0u);
                if (got != pids[i]) return 1;
                last = (int)child_status;
                waited_any = 1;
                (void)note_job_reaped(pids[i]);
            }
        }
        return waited_any ? last : 0;
    }
    for (size_t a = 1; a < command->argc; ++a) {
        rix_pid_t pid = 0;
        uint64_t child_status = 0;
        if (parse_decimal_pid(command->argv[a], &pid) != 0) {
            (void)write_text(2, "wait: bad pid\n");
            return 2;
        }
        {
            int known = 0;
            for (size_t j = 0; j < RIX_INIT_JOBS_CAP && !known; ++j) {
                rix_shell_job_t *job = &jobs[j];
                if (!job->active) continue;
                for (size_t i = 0; i < job->pid_count; ++i)
                    if (job->pid[i] == pid) known = 1;
            }
            if (!known) {
                (void)write_text(2, "wait: no such job\n");
                return 1;
            }
        }
        if (waitpid(pid, &child_status, 0u) != pid) return 1;
        last = (int)child_status;
        waited_any = 1;
        (void)note_job_reaped(pid);
    }
    return waited_any ? last : 0;
}

static int shell_jobs_builtin(const rix_shell_pipeline_t *pipeline, int *handled) {
    const rix_shell_command_t *command;
    if (handled) *handled = 0;
    if (!pipeline || pipeline->command_count != 1u || pipeline->background) return 0;
    command = &pipeline->command[0];
    if (!command->argc || !command->argv[0] || !streq(command->argv[0], "jobs")) return 0;
    if (handled) *handled = 1;
    if (command->argc > 1u) { (void)write_text(2, "jobs: arguments unsupported\n"); return 2; }
    reap_background_jobs();
    for (size_t j = 0; j < RIX_INIT_JOBS_CAP; ++j) {
        rix_shell_job_t *job = &jobs[j];
        if (!job->active) continue;
        (void)write_text(1, "[");
        write_sdec(1, (long)(j + 1u));
        (void)write_text(1, "]");
        for (size_t i = 0; i < job->pid_count; ++i) {
            if (!job->pid[i]) continue;
            (void)write_text(1, " ");
            write_sdec(1, (long)job->pid[i]);
        }
        (void)write_text(1, " running\n");
    }
    return 0;
}

static int shell_exit_builtin(const rix_shell_pipeline_t *pipeline, int *handled) {
    const rix_shell_command_t *command;
    long code = 0;
    if (handled) *handled = 0;
    if (!pipeline || pipeline->command_count != 1u || pipeline->background) return 0;
    command = &pipeline->command[0];
    if (!command->argc || !command->argv[0] || !streq(command->argv[0], "exit")) return 0;
    if (handled) *handled = 1;
    if (command->argc > 2u) { (void)write_text(2, "exit: too many arguments\n"); return 2; }
    if (command->argc == 2u) {
        const char *s = command->argv[1];
        size_t digits = 0;
        code = 0;
        if (!s || !*s) { (void)write_text(2, "exit: numeric argument required\n"); return 2; }
        while (*s) {
            if (*s < '0' || *s > '9' || digits >= 9u) {
                (void)write_text(2, "exit: numeric argument required\n");
                return 2;
            }
            code = code * 10L + (long)(*s - '0');
            ++digits;
            ++s;
        }
    } else {
        code = (long)shell_last_status;
    }
    shell_exit_code = (int)code;
    shell_exit_pending = 1;
    return (int)code;
}

static void shell_prompt(void) {
    static char cwd[256];
    if (getcwd(cwd, sizeof(cwd)) < 0) cwd[0] = 0;
    (void)write_text(1, "\033[1;32m");
    (void)write_text(1, shell_user[0] ? shell_user : "user");
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
    if (expand_pipeline_words(&pipeline) != 0) return 1;
    { int handled = 0; int cd_status = shell_cd_builtin(&pipeline, &handled); if (handled) return cd_status; }
    { int handled = 0; int history_status = shell_history_builtin(&pipeline, history, &handled); if (handled) return history_status; }
    { int handled = 0; int export_status = shell_export_builtin(&pipeline, &handled); if (handled) return export_status; }
    { int handled = 0; int wait_status = shell_wait_builtin(&pipeline, &handled); if (handled) return wait_status; }
    { int handled = 0; int jobs_status = shell_jobs_builtin(&pipeline, &handled); if (handled) return jobs_status; }
    { int handled = 0; int exit_status = shell_exit_builtin(&pipeline, &handled); if (handled) return exit_status; }
    if (pipeline.command_count == 1u && !pipeline.background) {
        const rix_shell_command_t *cmd = &pipeline.command[0];
        if (cmd->argc == 1u && cmd->argv[0][0]=='c' && cmd->argv[0][1]=='l' && cmd->argv[0][2]=='e' && cmd->argv[0][3]=='a' && cmd->argv[0][4]=='r' && cmd->argv[0][5]==0) {
            (void)write_text(1, "\033[2J\033[H");
            return 0;
        }
    }
    if (run_command(&pipeline, &status) != 0) {
        (void)write_text(2, "rixuri: command execution failed\n");
        return 125;
    }
    return status;
}

void _start(void) {
    char line[RIX_INIT_LINE_CAP];
    rix_shell_history_t history;
    (void)write_text(1, "RIXURI:DBG entry jobs0=");
    write_sdec(1, (long)jobs[0].active);
    (void)write_text(1, " pc=");
    write_sdec(1, (long)jobs[0].pid_count);
    (void)write_text(1, " envc=");
    write_sdec(1, (long)shell_env_count);
    (void)write_text(1, "\r\n");
    rix_shell_history_init(&history);
    /* Qualify Ring 3 and the first syscall before exercising the shell. */
    (void)write_text(1, "RIXURI:USER_ENTER\r\n");
    if (getpid() != 1u) {
        (void)write_text(2, "RIXURI:SYSCALL_FAIL\r\n");
        _exit(127);
    }
    (void)write_text(1, "RIXURI:SYSCALL_OK\r\n");
    resolve_shell_user();
    (void)write_text(1, "RIXURI:DBG post-resolve jobs0=");
    write_sdec(1, (long)jobs[0].active);
    (void)write_text(1, " pc=");
    write_sdec(1, (long)(long long)jobs[0].pid_count);
    (void)write_text(1, " jobs1=");
    write_sdec(1, (long)jobs[1].active);
    (void)write_text(1, " envc=");
    write_sdec(1, (long)shell_env_count);
    (void)write_text(1, " last=");
    write_sdec(1, (long)shell_last_status);
    (void)write_text(1, "\r\n");
    ensure_home_skeleton();
    (void)write_text(1, "RIXURI:DBG post-ensure jobs0=");
    write_sdec(1, (long)jobs[0].active);
    (void)write_text(1, "\r\n");
    (void)write_text(1, "RIXURI: SHELL READY\r\n");
    (void)write_text(1, "RixuriOS shell ready\r\n");
    for (;;) {
        (void)write_text(1, "RIXURI:DBG A loop\r\n");
        reap_background_jobs();
        (void)write_text(1, "RIXURI:DBG B reap-ok\r\n");
        shell_prompt();
        (void)write_text(1, "RIXURI:DBG C prompt-ok\r\n");
        if (shell_read_line(line, sizeof(line)) != 0) break;
        shell_last_status = shell_execute_line(line, &history);
        if (shell_exit_pending) break;
    }
    (void)write_text(1, "RIXURI:USER_EXIT\r\n");
    _exit(shell_exit_code);
}
