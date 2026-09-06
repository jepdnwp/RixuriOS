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
    size_t written = 0;
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
    if (expect(tty_set_dimensions(0, 24, 80) == 0 &&
                tty_output(0, "\x1b[5;10H", 7, &written) == 0 && written == 7,
                "ANSI cursor sequence")) return 1;
    uint16_t row = 0, column = 0, rows = 0, columns = 0;
    if (expect(tty_get_cursor(0, &row, &column) == 0 && row == 4 && column == 9 &&
                tty_get_dimensions(0, &rows, &columns) == 0 && rows == 24 && columns == 80,
                "cursor and dimensions")) return 1;
    unsigned pty = 0;
    if (expect(tty_pty_open(&pty) == 0, "open pty")) return 1;
    if (expect(tty_pty_master_write(pty, "cmd\n", 4, &written) == 0 && written == 4,
                "pty master writes input")) return 1;
    if (expect(tty_pty_slave_read(pty, buffer, sizeof(buffer), &count) == 0 &&
                count == 4 && memcmp(buffer, "cmd\n", 4) == 0,
                "pty slave reads canonical line")) return 1;
    if (expect(tty_pty_master_read(pty, output, sizeof(output), &count) == 0 &&
                count == 4 && memcmp(output, "cmd\n", 4) == 0,
                "pty master reads echo")) return 1;
    if (expect(tty_pty_slave_write(pty, "ok\n", 3, &written) == 0 && written == 3 &&
                tty_pty_master_read(pty, output, sizeof(output), &count) == 0 &&
                count == 3 && memcmp(output, "ok\n", 3) == 0,
                "pty slave writes output")) return 1;
    if (expect(tty_pty_close(pty) == 0, "close pty")) return 1;
    puts("tty tests: PASS");
    return 0;
}
