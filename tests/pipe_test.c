#include "kernel/ipc/pipe.h"
#include <stdio.h>
#include <string.h>

static int expect(int condition, const char *name) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        return 1;
    }
    return 0;
}

int main(void) {
    rix_pipe_t pipe;
    unsigned char input[RIX_IPC_CAPACITY + 1u];
    unsigned char output[RIX_IPC_CAPACITY];
    size_t count = 0;
    memset(input, 'x', sizeof(input));
    pipe_init(&pipe);
    if (expect(pipe_write(&pipe, input, sizeof(input), &count) == -3 &&
               count == RIX_IPC_CAPACITY, "bounded write")) return 1;
    if (expect(pipe_read(&pipe, output, sizeof(output), &count) == 0 &&
               count == RIX_IPC_CAPACITY && output[0] == 'x' &&
               output[RIX_IPC_CAPACITY - 1u] == 'x', "full read")) return 1;
    if (expect(pipe_close_write(&pipe) == 0 &&
               pipe_read(&pipe, output, 1u, &count) == -2 && count == 0u,
               "writer close produces EOF")) return 1;
    if (expect(pipe_close_read(&pipe) == 0 &&
               pipe_write(&pipe, input, 1u, &count) == -1 && count == 0u,
               "reader close rejects write")) return 1;
    puts("pipe tests: PASS");
    return 0;
}
