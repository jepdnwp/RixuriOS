#include "user/shell/shell.h"
#include <stdio.h>
#include <string.h>

static int expect(int condition, const char *name) {
    if (!condition) { fprintf(stderr, "FAIL: %s\n", name); return 1; }
    return 0;
}

static const char *lookup(const char *name, void *context) {
    (void)context;
    if (strcmp(name, "USER") == 0) return "rix";
    if (strcmp(name, "N") == 0) return "42";
    return NULL;
}

static int run_command(const char *command, char *output, size_t capacity, void *context) {
    (void)context;
    if (strcmp(command, "whoami") != 0 || capacity < 4u) return -1;
    strcpy(output, "rix\n");
    return 0;
}

int main(void) {
    rix_shell_tokens_t tokens;
    if (expect(rix_shell_lex("echo 'hello world' a\\ b # comment", &tokens) == 0 &&
                tokens.count == 3 && strcmp(tokens.token[1].text, "hello world") == 0 &&
                strcmp(tokens.token[2].text, "a b") == 0,
                "quotes escapes comments")) return 1;
    if (expect(rix_shell_lex("cat < in | grep x >> out && echo ok &", &tokens) == 0,
                "operators lex")) return 1;
    rix_shell_pipeline_t pipeline;
    if (expect(rix_shell_parse_pipeline(&tokens, &pipeline) == 0 && pipeline.background &&
                pipeline.command_count == 3 && pipeline.command[0].argc == 1 &&
                pipeline.command[0].redir_count == 1 &&
                pipeline.command[1].argc == 2 && pipeline.command[1].redir_count == 1 &&
                pipeline.command[2].argc == 2 && pipeline.connector[0] == RIX_SHELL_PIPE &&
                pipeline.connector[1] == RIX_SHELL_AND,
                "pipeline AST")) return 1;
    if (expect(rix_shell_lex("echo \"unterminated", &tokens) != 0,
                "unterminated quote rejected")) return 1;
    char expanded[64];
    if (expect(rix_shell_expand_word("hi-$USER-${N}-'$USER'-\\$N", expanded,
                                     sizeof(expanded), lookup, NULL) == 0 &&
                strcmp(expanded, "hi-rix-42-$USER-$N") == 0,
                "variable expansion and quoting")) return 1;
    static const char *const commands[] = {"cat", "caller", "cd", "echo"};
    size_t matches = 0;
    if (expect(rix_shell_complete("call", commands, 4, expanded, sizeof(expanded),
                                  &matches) == 0 && matches == 1 &&
                strcmp(expanded, "caller") == 0, "single autocomplete match")) return 1;
    if (expect(rix_shell_complete("ca", commands, 4, expanded, sizeof(expanded),
                                  &matches) == 0 && matches == 2 &&
                strcmp(expanded, "ca") == 0, "common autocomplete prefix")) return 1;
    if (expect(rix_shell_complete("z", commands, 4, expanded, sizeof(expanded),
                                  &matches) == 0 && matches == 0 && expanded[0] == 0,
                "no autocomplete match")) return 1;
    rix_shell_history_t history;
    rix_shell_history_init(&history);
    if (expect(rix_shell_history_add(&history, "one") == 0 &&
                rix_shell_history_add(&history, "one") == 0 &&
                rix_shell_history_add(&history, "two") == 0,
                "history add and duplicate suppression")) return 1;
    if (expect(rix_shell_history_prev(&history, expanded, sizeof(expanded)) == 0 &&
                strcmp(expanded, "two") == 0 &&
                rix_shell_history_prev(&history, expanded, sizeof(expanded)) == 0 &&
                strcmp(expanded, "one") == 0, "history previous navigation")) return 1;
    if (expect(rix_shell_history_next(&history, expanded, sizeof(expanded)) == 0 &&
                strcmp(expanded, "two") == 0 &&
                rix_shell_history_next(&history, expanded, sizeof(expanded)) == 0 &&
                expanded[0] == 0, "history next navigation")) return 1;
    char persisted[128];
    size_t persisted_size = 0;
    if (expect(rix_shell_history_export(&history, persisted, sizeof(persisted),
                                        &persisted_size) == 0 && persisted_size == 8,
                "history export")) return 1;
    rix_shell_history_t restored;
    rix_shell_history_init(&restored);
    if (expect(rix_shell_history_import(&restored, persisted) == 0 && restored.count == 2,
                "history import")) return 1;
    int64_t arithmetic = 0;
    if (expect(rix_shell_arithmetic_eval("2 + 3 * (4 - 1)", &arithmetic) == 0 &&
                arithmetic == 11, "arithmetic expansion")) return 1;
    if (expect(rix_shell_arithmetic_eval("4 / 0", &arithmetic) != 0,
                "arithmetic division by zero rejected")) return 1;
    if (expect(rix_shell_arithmetic_eval("9223372036854775807 + 1", &arithmetic) != 0 &&
                rix_shell_arithmetic_eval("3037000500 * 3037000500", &arithmetic) != 0,
                "arithmetic overflow rejected")) return 1;
    if (expect(rix_shell_command_substitute("user=$(whoami)", expanded,
                                             sizeof(expanded), run_command, NULL) == 0 &&
                strcmp(expanded, "user=rix") == 0,
                "command substitution")) return 1;
    puts("shell parser tests: PASS");
    return 0;
}
