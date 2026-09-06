#include "user/shell/shell.h"
#include <stdio.h>
#include <string.h>

static int expect(int condition, const char *name) {
    if (!condition) { fprintf(stderr, "FAIL: %s\n", name); return 1; }
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
    puts("shell parser tests: PASS");
    return 0;
}
