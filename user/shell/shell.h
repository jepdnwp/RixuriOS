#pragma once
#include <stddef.h>

#define RIX_SHELL_MAX_TOKENS 64u
#define RIX_SHELL_TOKEN_TEXT 128u
#define RIX_SHELL_MAX_COMMANDS 16u
#define RIX_SHELL_MAX_ARGS 16u
#define RIX_SHELL_MAX_REDIRS 8u

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

int rix_shell_lex(const char *input, rix_shell_tokens_t *out);
int rix_shell_parse_pipeline(rix_shell_tokens_t *tokens, rix_shell_pipeline_t *out);
typedef const char *(*rix_shell_variable_lookup_t)(const char *name, void *context);
int rix_shell_expand_word(const char *input, char *output, size_t capacity,
                          rix_shell_variable_lookup_t lookup, void *context);
int rix_shell_complete(const char *prefix, const char *const *candidates,
                       size_t candidate_count, char *output, size_t capacity,
                       size_t *match_count);
