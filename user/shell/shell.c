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

int rix_shell_complete(const char *prefix, const char *const *candidates,
                       size_t candidate_count, char *output, size_t capacity,
                       size_t *match_count) {
    if (!prefix || !output || capacity == 0u || (!candidates && candidate_count)) return -1;
    size_t prefix_length = text_length(prefix);
    if (prefix_length >= RIX_SHELL_TOKEN_TEXT) return -2;
    size_t matches = 0;
    size_t common_length = 0;
    const char *first = NULL;
    for (size_t i = 0; i < candidate_count; ++i) {
        const char *candidate = candidates[i];
        if (!candidate || text_length(candidate) < prefix_length) continue;
        size_t candidate_length = text_length(candidate);
        int matches_prefix = 1;
        for (size_t j = 0; j < prefix_length; ++j)
            if (candidate[j] != prefix[j]) matches_prefix = 0;
        if (!matches_prefix) continue;
        if (!matches++) {
            first = candidate;
            common_length = candidate_length;
        } else {
            while (common_length > prefix_length &&
                   (common_length > candidate_length ||
                    first[common_length - 1u] != candidate[common_length - 1u]))
                --common_length;
        }
    }
    if (match_count) *match_count = matches;
    if (!matches) { output[0] = 0; return 0; }
    if (common_length >= capacity) return -3;
    for (size_t i = 0; i < common_length; ++i) output[i] = first[i];
    output[common_length] = 0;
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

int rix_shell_resolve_path(const char *command, const char *path,
                           rix_shell_path_exists_t exists, void *context,
                           char *output, size_t capacity) {
    if (!command || !command[0] || !exists || !output || capacity == 0u) return -1;
    size_t command_length = text_length(command);
    if (command_length == 0u || command_length >= RIX_SHELL_TOKEN_TEXT) return -1;
    int has_slash = 0;
    for (size_t i = 0; i < command_length; ++i) if (command[i] == '/') has_slash = 1;
    if (has_slash) {
        if (command_length >= capacity) return -3;
        for (size_t i = 0; i < command_length; ++i) output[i] = command[i];
        output[command_length] = 0;
        return exists(output, context) ? 0 : -2;
    }
    if (!path) return -2;
    const char *segment = path;
    for (;;) {
        size_t segment_length = 0;
        while (segment[segment_length] && segment[segment_length] != ':') ++segment_length;
        const char *prefix = segment_length ? segment : ".";
        size_t prefix_length = segment_length ? segment_length : 1u;
        if (prefix_length + 1u + command_length >= capacity) return -3;
        size_t used = 0;
        for (size_t i = 0; i < prefix_length; ++i) output[used++] = prefix[i];
        output[used++] = '/';
        for (size_t i = 0; i < command_length; ++i) output[used++] = command[i];
        output[used] = 0;
        if (exists(output, context) == 0) return 0;
        if (!segment[segment_length]) break;
        segment += segment_length + 1u;
    }
    return -2;
}

void rix_shell_history_init(rix_shell_history_t *history) {
    if (!history) return;
    history->count = 0;
    history->cursor = 0;
}

int rix_shell_history_add(rix_shell_history_t *history, const char *line) {
    if (!history || !line) return -1;
    size_t length = text_length(line);
    if (length == 0u || length >= RIX_SHELL_TOKEN_TEXT) return -2;
    if (history->count && text_length(history->entry[history->count - 1u]) == length) {
        size_t i = 0;
        while (i < length && history->entry[history->count - 1u][i] == line[i]) ++i;
        if (i == length) { history->cursor = history->count; return 0; }
    }
    if (history->count == RIX_SHELL_HISTORY_COUNT) {
        for (size_t i = 1; i < history->count; ++i)
            for (size_t j = 0; j < RIX_SHELL_TOKEN_TEXT; ++j)
                history->entry[i - 1u][j] = history->entry[i][j];
        history->count--;
    }
    for (size_t i = 0; i < length; ++i) history->entry[history->count][i] = line[i];
    history->entry[history->count][length] = 0;
    history->count++;
    history->cursor = history->count;
    return 0;
}

static int history_copy(const char *line, char *output, size_t capacity) {
    size_t length = text_length(line);
    if (length >= capacity) return -1;
    for (size_t i = 0; i < length; ++i) output[i] = line[i];
    output[length] = 0;
    return 0;
}

int rix_shell_history_prev(rix_shell_history_t *history, char *output, size_t capacity) {
    if (!history || !output || capacity == 0u || history->count == 0u) return -1;
    if (history->cursor > 0u) --history->cursor;
    return history_copy(history->entry[history->cursor], output, capacity);
}

int rix_shell_history_next(rix_shell_history_t *history, char *output, size_t capacity) {
    if (!history || !output || capacity == 0u || history->count == 0u) return -1;
    if (history->cursor + 1u < history->count) {
        ++history->cursor;
        return history_copy(history->entry[history->cursor], output, capacity);
    }
    history->cursor = history->count;
    output[0] = 0;
    return 0;
}

int rix_shell_history_export(const rix_shell_history_t *history, char *output,
                             size_t capacity, size_t *written) {
    if (written) *written = 0;
    if (!history || !output || capacity == 0u) return -1;
    size_t used = 0;
    for (size_t i = 0; i < history->count; ++i) {
        size_t length = text_length(history->entry[i]);
        if (used + length + 1u >= capacity) return -2;
        for (size_t j = 0; j < length; ++j) output[used++] = history->entry[i][j];
        output[used++] = '\n';
    }
    output[used] = 0;
    if (written) *written = used;
    return 0;
}

int rix_shell_history_import(rix_shell_history_t *history, const char *input) {
    if (!history || !input) return -1;
    char line[RIX_SHELL_TOKEN_TEXT];
    size_t length = 0;
    for (size_t i = 0;; ++i) {
        char ch = input[i];
        if (ch == '\n' || ch == 0) {
            line[length] = 0;
            if (length && rix_shell_history_add(history, line) != 0) return -2;
            length = 0;
            if (!ch) break;
        } else {
            if (length + 1u >= sizeof(line)) return -3;
            line[length++] = ch;
        }
    }
    return 0;
}

typedef struct { const char *text; size_t offset; int error; } arithmetic_parser_t;

static void arithmetic_space(arithmetic_parser_t *parser) {
    while (parser->text[parser->offset] == ' ' || parser->text[parser->offset] == '\t') ++parser->offset;
}

static int64_t arithmetic_expression(arithmetic_parser_t *parser);

static int arithmetic_add_overflow(int64_t left, int64_t right, int subtract, int64_t *value) {
    if (subtract) {
        if ((right > 0 && left < INT64_MIN + right) ||
            (right < 0 && left > INT64_MAX + right)) return 1;
        *value = left - right;
    } else {
        if ((right > 0 && left > INT64_MAX - right) ||
            (right < 0 && left < INT64_MIN - right)) return 1;
        *value = left + right;
    }
    return 0;
}

static int arithmetic_mul_overflow(int64_t left, int64_t right, int64_t *value) {
    if (left == 0 || right == 0) { *value = 0; return 0; }
    if ((left == -1 && right == INT64_MIN) || (right == -1 && left == INT64_MIN)) return 1;
    if (left > 0) {
        if (right > 0 && left > INT64_MAX / right) return 1;
        if (right < 0 && right < INT64_MIN / left) return 1;
    } else {
        if (right > 0 && left < INT64_MIN / right) return 1;
        if (right < 0 && left < INT64_MAX / right) return 1;
    }
    *value = left * right;
    return 0;
}

static int64_t arithmetic_primary(arithmetic_parser_t *parser) {
    arithmetic_space(parser);
    if (parser->text[parser->offset] == '(') {
        ++parser->offset;
        int64_t value = arithmetic_expression(parser);
        arithmetic_space(parser);
        if (parser->text[parser->offset] != ')') parser->error = 1;
        else ++parser->offset;
        return value;
    }
    if (parser->text[parser->offset] < '0' || parser->text[parser->offset] > '9') {
        parser->error = 1;
        return 0;
    }
    int64_t value = 0;
    while (parser->text[parser->offset] >= '0' && parser->text[parser->offset] <= '9') {
        uint8_t digit = (uint8_t)(parser->text[parser->offset++] - '0');
        if (value > (INT64_MAX - digit) / 10) { parser->error = 1; return 0; }
        value = value * 10 + digit;
    }
    return value;
}

static int64_t arithmetic_unary(arithmetic_parser_t *parser) {
    arithmetic_space(parser);
    if (parser->text[parser->offset] == '+') { ++parser->offset; return arithmetic_unary(parser); }
    if (parser->text[parser->offset] == '-') {
        ++parser->offset;
        int64_t value = arithmetic_unary(parser);
        if (value == INT64_MIN) parser->error = 1;
        return -value;
    }
    return arithmetic_primary(parser);
}

static int64_t arithmetic_term(arithmetic_parser_t *parser) {
    int64_t value = arithmetic_unary(parser);
    for (;;) {
        arithmetic_space(parser);
        char op = parser->text[parser->offset];
        if (op != '*' && op != '/' && op != '%') return value;
        ++parser->offset;
        int64_t rhs = arithmetic_unary(parser);
        if (op == '*') {
            int64_t result = 0;
            if (arithmetic_mul_overflow(value, rhs, &result)) parser->error = 1;
            value = result;
        }
        else if (rhs == 0) parser->error = 1;
        else if (op == '/') {
            if (value == INT64_MIN && rhs == -1) parser->error = 1;
            else value /= rhs;
        }
        else {
            if (value == INT64_MIN && rhs == -1) parser->error = 1;
            else value %= rhs;
        }
    }
}

static int64_t arithmetic_expression(arithmetic_parser_t *parser) {
    int64_t value = arithmetic_term(parser);
    for (;;) {
        arithmetic_space(parser);
        char op = parser->text[parser->offset];
        if (op != '+' && op != '-') return value;
        ++parser->offset;
        int64_t rhs = arithmetic_term(parser);
        int64_t result = 0;
        if (arithmetic_add_overflow(value, rhs, op == '-', &result)) parser->error = 1;
        value = result;
    }
}

int rix_shell_arithmetic_eval(const char *expression, int64_t *result) {
    if (!expression || !result) return -1;
    arithmetic_parser_t parser = {expression, 0, 0};
    int64_t value = arithmetic_expression(&parser);
    arithmetic_space(&parser);
    if (parser.error || expression[parser.offset] != 0) return -2;
    *result = value;
    return 0;
}

int rix_shell_command_substitute(const char *input, char *output, size_t capacity,
                                 rix_shell_command_runner_t runner, void *context) {
    if (!input || !output || capacity == 0u || !runner) return -1;
    size_t in = 0, out = 0;
    while (input[in]) {
        if (input[in] != '$' || input[in + 1u] != '(') {
            if (out + 1u >= capacity) return -2;
            output[out++] = input[in++];
            continue;
        }
        in += 2u;
        size_t start = in;
        size_t depth = 1u;
        while (input[in] && depth) {
            if (input[in] == '(') ++depth;
            else if (input[in] == ')') --depth;
            ++in;
        }
        if (depth != 0u) return -3;
        size_t length = in - start - 1u;
        if (length >= RIX_SHELL_TOKEN_TEXT) return -4;
        char command[RIX_SHELL_TOKEN_TEXT];
        for (size_t i = 0; i < length; ++i) command[i] = input[start + i];
        command[length] = 0;
        char result[RIX_SHELL_TOKEN_TEXT];
        int rc = runner(command, result, sizeof(result), context);
        if (rc != 0) return rc;
        size_t result_length = text_length(result);
        while (result_length && result[result_length - 1u] == '\n') --result_length;
        for (size_t i = 0; i < result_length; ++i) {
            if (out + 1u >= capacity) return -2;
            output[out++] = result[i];
        }
    }
    output[out] = 0;
    return 0;
}

static int pathname_match(const char *pattern, const char *candidate) {
    if (!pattern || !candidate) return 0;
    if (*pattern == 0) return *candidate == 0;
    if (*pattern == '*') {
        while (*pattern == '*') ++pattern;
        if (*pattern == 0) return 1;
        while (*candidate) {
            if (pathname_match(pattern, candidate)) return 1;
            ++candidate;
        }
        return pathname_match(pattern, candidate);
    }
    if (*pattern == '?' || *pattern == *candidate)
        return pathname_match(pattern + 1u, candidate + (*candidate != 0));
    return 0;
}

int rix_shell_expand_pathname(const char *pattern, const char *const *candidates,
                              size_t candidate_count, char *output, size_t capacity,
                              size_t *match_count) {
    if (match_count) *match_count = 0;
    if (!pattern || !candidates || !output || capacity == 0u) return -1;
    size_t used = 0;
    size_t matches = 0;
    for (size_t i = 0; i < candidate_count; ++i) {
        const char *candidate = candidates[i];
        if (!candidate || !pathname_match(pattern, candidate)) continue;
        size_t length = text_length(candidate);
        if (used + length + (matches ? 1u : 0u) >= capacity) return -2;
        if (matches) output[used++] = ' ';
        for (size_t j = 0; j < length; ++j) output[used++] = candidate[j];
        ++matches;
    }
    if (!matches) {
        size_t length = text_length(pattern);
        if (length >= capacity) return -2;
        for (size_t i = 0; i < length; ++i) output[i] = pattern[i];
        used = length;
    }
    output[used] = 0;
    if (match_count) *match_count = matches;
    return 0;
}

int rix_shell_run_builtin(const rix_shell_command_t *command,
                          rix_shell_output_writer_t writer, void *context,
                          int *handled, int *status) {
    if (handled) *handled = 0;
    if (status) *status = 2;
    if (!command || !command->argc || !command->argv[0] || !handled || !status) return -1;
    const char *name = command->argv[0];
    int is_echo = 0;
    int result = 0;
    if (name[0] == ':' && name[1] == 0) {
        *handled = 1;
    } else if (name[0] == 't' && name[1] == 'r' && name[2] == 'u' && name[3] == 'e' && name[4] == 0) {
        *handled = 1;
    } else if (name[0] == 'f' && name[1] == 'a' && name[2] == 'l' && name[3] == 's' && name[4] == 'e' && name[5] == 0) {
        *handled = 1;
        result = 1;
    } else if (name[0] == 'e' && name[1] == 'c' && name[2] == 'h' && name[3] == 'o' && name[4] == 0) {
        *handled = 1;
        is_echo = 1;
    } else {
        return 0;
    }
    if (is_echo && writer) {
        for (size_t i = 1; i < command->argc; ++i) {
            if (i != 1u) {
                static const char space = ' ';
                if (writer(&space, 1u, context) != 0) return -2;
            }
            const char *word = command->argv[i];
            size_t length = text_length(word);
            if (writer(word, length, context) != 0) return -3;
        }
        static const char newline = '\n';
        if (writer(&newline, 1u, context) != 0) return -4;
    }
    *status = result;
    return 0;
}

int rix_shell_execute_pipeline(const rix_shell_pipeline_t *pipeline,
                               rix_shell_pipeline_runner_t runner, void *context,
                               int *status) {
    if (!pipeline || !runner || !status || pipeline->command_count == 0 ||
        pipeline->command_count > RIX_SHELL_MAX_COMMANDS) return -1;
    int last_status = 0;
    for (size_t i = 0; i < pipeline->command_count; ++i) {
        if (i != 0u) {
            rix_shell_token_type_t connector = pipeline->connector[i - 1u];
            if (connector == RIX_SHELL_AND && last_status != 0) continue;
            if (connector == RIX_SHELL_OR && last_status == 0) continue;
            if (connector != RIX_SHELL_PIPE && connector != RIX_SHELL_AND &&
                connector != RIX_SHELL_OR && connector != RIX_SHELL_SEMI) {
                *status = -2;
                return -2;
            }
        }
        int input_fd = 0;
        int output_fd = 1;
        if (i != 0u && pipeline->connector[i - 1u] == RIX_SHELL_PIPE)
            input_fd = (int)i;
        if (i + 1u < pipeline->command_count && pipeline->connector[i] == RIX_SHELL_PIPE)
            output_fd = (int)(i + 1u);
        else if (i + 1u < pipeline->command_count && pipeline->connector[i] != RIX_SHELL_AND &&
                 pipeline->connector[i] != RIX_SHELL_OR && pipeline->connector[i] != RIX_SHELL_SEMI) {
            *status = -2;
            return -2;
        }
        int rc = runner(&pipeline->command[i], input_fd, output_fd, context);
        if (rc < 0) {
            *status = rc;
            return rc;
        }
        last_status = rc;
    }
    *status = last_status;
    return 0;
}
