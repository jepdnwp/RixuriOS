#pragma once
#include <stddef.h>
#include <stdint.h>

#define RIX_SHELL_MAX_TOKENS 64u
#define RIX_SHELL_TOKEN_TEXT 128u
#define RIX_SHELL_MAX_COMMANDS 16u
#define RIX_SHELL_MAX_ARGS 16u
#define RIX_SHELL_MAX_REDIRS 8u
#define RIX_SHELL_HISTORY_COUNT 64u

typedef enum {
    RIX_SHELL_WORD,
    RIX_SHELL_PIPE,
    RIX_SHELL_AND,
    RIX_SHELL_OR,
    RIX_SHELL_SEMI,
    RIX_SHELL_BG,
    RIX_SHELL_REDIR_IN,
    RIX_SHELL_REDIR_OUT,
    RIX_SHELL_TOKEN_REDIR_APPEND,
    RIX_SHELL_LPAREN,
    RIX_SHELL_RPAREN
} rix_shell_token_type_t;

typedef struct {
    rix_shell_token_type_t type;
    char text[RIX_SHELL_TOKEN_TEXT];
} rix_shell_token_t;

typedef struct {
    rix_shell_token_t token[RIX_SHELL_MAX_TOKENS];
    size_t count;
} rix_shell_tokens_t;

typedef enum { RIX_SHELL_REDIR_READ, RIX_SHELL_REDIR_WRITE, RIX_SHELL_REDIR_APPEND } rix_shell_redir_type_t;
typedef struct { rix_shell_redir_type_t type; char path[RIX_SHELL_TOKEN_TEXT]; } rix_shell_redir_t;
typedef struct {
    char *argv[RIX_SHELL_MAX_ARGS];
    size_t argc;
    rix_shell_redir_t redir[RIX_SHELL_MAX_REDIRS];
    size_t redir_count;
} rix_shell_command_t;
typedef struct {
    rix_shell_command_t command[RIX_SHELL_MAX_COMMANDS];
    rix_shell_token_type_t connector[RIX_SHELL_MAX_COMMANDS - 1u];
    size_t command_count;
    size_t background;
} rix_shell_pipeline_t;

typedef struct {
    char entry[RIX_SHELL_HISTORY_COUNT][RIX_SHELL_TOKEN_TEXT];
    size_t count;
    size_t cursor;
} rix_shell_history_t;

int rix_shell_lex(const char *input, rix_shell_tokens_t *out);
int rix_shell_parse_pipeline(rix_shell_tokens_t *tokens, rix_shell_pipeline_t *out);
typedef const char *(*rix_shell_variable_lookup_t)(const char *name, void *context);
int rix_shell_expand_word(const char *input, char *output, size_t capacity,
                          rix_shell_variable_lookup_t lookup, void *context);
int rix_shell_complete(const char *prefix, const char *const *candidates,
                       size_t candidate_count, char *output, size_t capacity,
                       size_t *match_count);
int rix_shell_expand_pathname(const char *pattern, const char *const *candidates,
                              size_t candidate_count, char *output, size_t capacity,
                              size_t *match_count);
void rix_shell_history_init(rix_shell_history_t *history);
int rix_shell_history_add(rix_shell_history_t *history, const char *line);
int rix_shell_history_prev(rix_shell_history_t *history, char *output, size_t capacity);
int rix_shell_history_next(rix_shell_history_t *history, char *output, size_t capacity);
int rix_shell_history_export(const rix_shell_history_t *history, char *output,
                             size_t capacity, size_t *written);
int rix_shell_history_import(rix_shell_history_t *history, const char *input);
int rix_shell_arithmetic_eval(const char *expression, int64_t *result);
typedef int (*rix_shell_command_runner_t)(const char *command, char *output,
                                          size_t capacity, void *context);
typedef int (*rix_shell_pipeline_runner_t)(const rix_shell_command_t *command,
                                           int input_fd, int output_fd, void *context);
typedef int (*rix_shell_output_writer_t)(const void *data, size_t length, void *context);
int rix_shell_run_builtin(const rix_shell_command_t *command,
                          rix_shell_output_writer_t writer, void *context,
                          int *handled, int *status);
int rix_shell_execute_pipeline(const rix_shell_pipeline_t *pipeline,
                               rix_shell_pipeline_runner_t runner, void *context,
                               int *status);
int rix_shell_command_substitute(const char *input, char *output, size_t capacity,
                                 rix_shell_command_runner_t runner, void *context);
