#include <unistd.h>

static size_t text_length(const char *text) {
    size_t length = 0;
    while (text && text[length]) ++length;
    return length;
}

static int emit(const char *text) {
    size_t length = text_length(text);
    return write(1, text, length) == (rix_ssize_t)length ? 0 : -1;
}

int program_main(int argc, char **argv, char **envp) {
    uint64_t capabilities = 0;
    (void)argv;
    (void)envp;
    if (argc != 1 || get_capabilities(&capabilities) != 0 ||
        (capabilities & RIX_CAP_KILL) == 0)
        return 1;
    return emit("delegated-exec=PASS\n") == 0 ? 0 : 1;
}

int main(int argc, char **argv, char **envp) {
    return program_main(argc, argv, envp);
}
