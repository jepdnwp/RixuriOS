#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

static size_t length(const char *s) { size_t n = 0; while (s && s[n]) ++n; return n; }
static void out(const char *s) { (void)write(1, s, length(s)); }
static int is_match(const char *a, const char *b) {
    size_t i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i] && a[i] == b[i]) ++i;
    return a[i] == 0 && b[i] == 0;
}
static void emit_escape(char c) {
    if (c == 'a') out("\a");
    else if (c == 'b') out("\b");
    else if (c == 'f') out("\f");
    else if (c == 'n') out("\n");
    else if (c == 'r') out("\r");
    else if (c == 't') out("\t");
    else if (c == 'v') out("\v");
    else if (c == '\\') out("\\");
    else {
        char b[2] = {'\\', c};
        (void)write(1, b, 2);
    }
}

int program_main(int argc, char **argv) {
    int interpret_escapes = 0;
    int omit_newline = 0;
    int arg_index = 1;
    int options_done = 0;

    while (arg_index < argc && !options_done) {
        const char *arg = argv[arg_index];
        if (arg[0] != '-') break;
        if (is_match(arg, "--")) {
            ++arg_index;
            options_done = 1;
            break;
        }
        if (is_match(arg, "-e")) {
            interpret_escapes = 1;
        } else if (is_match(arg, "-n")) {
            omit_newline = 1;
        } else if (is_match(arg, "-ne") || is_match(arg, "-en")) {
            interpret_escapes = 1;
            omit_newline = 1;
        } else {
            out(arg);
            out(" ");
        }
        ++arg_index;
    }

    for (; arg_index < argc; ++arg_index) {
        const char *arg = argv[arg_index];
        if (interpret_escapes) {
            for (size_t i = 0; arg[i]; ++i) {
                if (arg[i] == '\\' && arg[i + 1]) {
                    ++i;
                    emit_escape(arg[i]);
                } else {
                    char c = arg[i];
                    (void)write(1, &c, 1);
                }
            }
        } else {
            out(arg);
        }
        if (arg_index + 1 < argc) out(" ");
    }

    if (!omit_newline) out("\n");
    return 0;
}
