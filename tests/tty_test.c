#include "kernel/tty/tty.h"
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
    tty_init();
    if (expect(tty_input(0, 'a') == 0 && tty_input(0, 'b') == 0,
                "canonical input accepted")) return 1;
    char buffer[16] = {0};
    size_t count = 0;
    if (expect(tty_read(0, buffer, sizeof(buffer), &count) == -3 && count == 0,
                "canonical read waits for newline")) return 1;
    if (expect(tty_input(0, '\n') == 0 && tty_read(0, buffer, sizeof(buffer), &count) == 0 &&
                count == 3 && memcmp(buffer, "ab\n", 3) == 0,
                "canonical line read")) return 1;
    uint8_t output[16] = {0};
    if (expect(tty_read_output(0, output, sizeof(output), &count) == 0 && count == 3 &&
                memcmp(output, "ab\n", 3) == 0, "echo output queue")) return 1;
    if (expect(tty_set_canonical(0, 0) == 0 && tty_input(0, 'x') == 0 &&
                tty_read(0, buffer, sizeof(buffer), &count) == 0 && count == 1 &&
                buffer[0] == 'x', "raw input read")) return 1;
    if (expect(tty_set_foreground_pgrp(0, 42) == 0, "set foreground pgrp")) return 1;
    uint32_t pgrp = 0;
    if (expect(tty_get_foreground_pgrp(0, &pgrp) == 0 && pgrp == 42,
                "get foreground pgrp")) return 1;
    puts("tty tests: PASS");
    return 0;
}
