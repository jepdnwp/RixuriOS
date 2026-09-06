#include "shell.h"

static int is_space(char c) { return c == ' ' || c == '\t' || c == '\n'; }
static int is_operator(char c) {
    return c == '|' || c == '&' || c == ';' || c == '<' || c == '>' || c == '(' || c == ')';
}
static int copy_text(char *dst, const char *src, size_t length) {
    if (length >= RIX_SHELL_TOKEN_TEXT) return -1;
    for (size_t i = 0; i < length; ++i) dst[i] = src[i];
    dst[length] = 0;
    return 0;
}

static size_t text_length(const char *text) {
    size_t length = 0;
    while (length < RIX_SHELL_TOKEN_TEXT && text[length]) ++length;
    return length;
}

static int variable_start(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
}

static int variable_char(char ch) {
    return variable_start(ch) || (ch >= '0' && ch <= '9');
}

int rix_shell_expand_word(const char *input, char *output, size_t capacity,
                          rix_shell_variable_lookup_t lookup, void *context) {
    if (!input || !output || capacity == 0u) return -1;
    size_t in = 0, out = 0;
    char quote = 0;
    while (input[in]) {
        char ch = input[in++];
        if (ch == '\'' && quote != '"') { quote = quote == '\'' ? 0 : '\''; continue; }
        if (ch == '"' && quote != '\'') { quote = quote == '"' ? 0 : '"'; continue; }
        if (ch == '\\' && quote != '\'') {
            if (!input[in]) return -2;
            ch = input[in++];
            if (out + 1u >= capacity) return -3;
            output[out++] = ch;
            continue;
        }
        if (ch != '$' || quote == '\'') {
            if (out + 1u >= capacity) return -3;
            output[out++] = ch;
            continue;
        }
        size_t start = in;
        int braced = input[in] == '{';
        if (braced) ++start, ++in;
        if (!variable_start(input[start])) {
            if (out + 1u >= capacity) return -3;
            output[out++] = '$';
            continue;
        }
        in = start + 1u;
        while (variable_char(input[in])) ++in;
        size_t length = in - start;
        if (length >= RIX_SHELL_TOKEN_TEXT) return -4;
        if (braced) {
            if (input[in] != '}') return -5;
            ++in;
        }
        char name[RIX_SHELL_TOKEN_TEXT];
        for (size_t j = 0; j < length; ++j) name[j] = input[start + j];
        name[length] = 0;
        const char *value = lookup ? lookup(name, context) : NULL;
        if (!value) value = "";
        while (*value) {
            if (out + 1u >= capacity) return -3;
            output[out++] = *value++;
        }
    }
    if (quote) return -6;
    output[out] = 0;
    return 0;
}

int rix_shell_lex(const char *input, rix_shell_tokens_t *out) {
    if (!input || !out) return -1;
    out->count = 0;
    size_t i = 0;
    while (input[i]) {
        while (is_space(input[i])) ++i;
        if (!input[i]) break;
        if (input[i] == '#') {
            while (input[i] && input[i] != '\n') ++i;
            continue;
        }
        if (out->count >= RIX_SHELL_MAX_TOKENS) return -2;
        if (is_operator(input[i])) {
            rix_shell_token_t *token = &out->token[out->count++];
            token->text[0] = input[i]; token->text[1] = 0;
            if ((input[i] == '&' || input[i] == '|') && input[i + 1u] == input[i]) {
                token->text[1] = input[i]; token->text[2] = 0; ++i;
                token->type = input[i] == '&' ? RIX_SHELL_AND : RIX_SHELL_OR;
            } else if (input[i] == '|') token->type = RIX_SHELL_PIPE;
            else if (input[i] == '&') token->type = RIX_SHELL_BG;
            else if (input[i] == ';') token->type = RIX_SHELL_SEMI;
            else if (input[i] == '<') token->type = RIX_SHELL_REDIR_IN;
            else if (input[i] == '>') {
                token->type = RIX_SHELL_REDIR_OUT;
                if (input[i + 1u] == '>') { token->type = RIX_SHELL_TOKEN_REDIR_APPEND; token->text[1] = '>'; token->text[2] = 0; ++i; }
            } else if (input[i] == '(') token->type = RIX_SHELL_LPAREN;
            else token->type = RIX_SHELL_RPAREN;
            ++i;
            continue;
        }
        rix_shell_token_t *token = &out->token[out->count++];
        size_t length = 0;
        int quoted = 0;
        char quote = 0;
        while (input[i]) {
            char ch = input[i];
            if (!quote && (is_space(ch) || is_operator(ch) || ch == '#')) break;
            if (!quote && (ch == '\'' || ch == '"')) { quote = ch; quoted = 1; ++i; continue; }
            if (quote && ch == quote) { quote = 0; ++i; continue; }
            if (quote != '\'' && ch == '\\') {
                ++i;
                if (!input[i]) return -3;
                ch = input[i++];
            } else {
                ++i;
            }
            if (length + 1u >= RIX_SHELL_TOKEN_TEXT) return -4;
            token->text[length++] = ch;
        }
        if (quote) return -5;
        if (!length && !quoted) return -6;
        token->text[length] = 0;
        token->type = RIX_SHELL_WORD;
        if (input[i] == '#') while (input[i] && input[i] != '\n') ++i;
    }
    return 0;
}

static void clear_command(rix_shell_command_t *command) {
    command->argc = 0;
    command->redir_count = 0;
    for (size_t i = 0; i < RIX_SHELL_MAX_ARGS; ++i) command->argv[i] = NULL;
}

int rix_shell_parse_pipeline(rix_shell_tokens_t *tokens, rix_shell_pipeline_t *out) {
    if (!tokens || !out || tokens->count == 0) return -1;
    out->command_count = 0;
    out->background = 0;
    for (size_t i = 0; i < RIX_SHELL_MAX_COMMANDS; ++i) clear_command(&out->command[i]);
    size_t command = 0;
    out->command_count = 1;
    size_t i = 0;
    while (i < tokens->count) {
        rix_shell_token_t *token = &tokens->token[i];
        if (token->type == RIX_SHELL_WORD) {
            if (out->command[command].argc + 1u >= RIX_SHELL_MAX_ARGS) return -2;
            out->command[command].argv[out->command[command].argc++] = token->text;
            ++i;
            continue;
        }
        if (token->type == RIX_SHELL_REDIR_IN || token->type == RIX_SHELL_REDIR_OUT ||
            token->type == RIX_SHELL_TOKEN_REDIR_APPEND) {
            if (i + 1u >= tokens->count || tokens->token[i + 1u].type != RIX_SHELL_WORD ||
                out->command[command].redir_count >= RIX_SHELL_MAX_REDIRS) return -3;
            rix_shell_redir_t *redir = &out->command[command].redir[out->command[command].redir_count++];
            redir->type = token->type == RIX_SHELL_REDIR_IN ? RIX_SHELL_REDIR_READ :
                          (token->type == RIX_SHELL_TOKEN_REDIR_APPEND ? RIX_SHELL_REDIR_APPEND : RIX_SHELL_REDIR_WRITE);
            if (copy_text(redir->path, tokens->token[i + 1u].text,
                          text_length(tokens->token[i + 1u].text)) != 0) return -4;
            i += 2u;
            continue;
        }
        if (token->type == RIX_SHELL_BG) {
            if (i + 1u != tokens->count) return -5;
            out->background = 1;
            ++i;
            continue;
        }
        if (token->type == RIX_SHELL_PIPE || token->type == RIX_SHELL_AND ||
            token->type == RIX_SHELL_OR || token->type == RIX_SHELL_SEMI) {
            if (out->command[command].argc == 0 || command + 1u >= RIX_SHELL_MAX_COMMANDS) return -6;
            out->connector[command++] = token->type;
            clear_command(&out->command[command]);
            out->command_count = command + 1u;
            ++i;
            continue;
        }
        return -7;
    }
    if (out->command[out->command_count - 1u].argc == 0) return -8;
    return 0;
}
