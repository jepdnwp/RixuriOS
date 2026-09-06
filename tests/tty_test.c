#include "kernel/tty/tty.h"
#include <stdio.h>
#include <string.h>

static uint32_t seen_group;
static unsigned seen_signal;

static int signal_hook(uint32_t group, unsigned signal) {
    seen_group = group;
    seen_signal = signal;
    return 0;
}

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
    uint8_t output[64] = {0};
    if (expect(tty_read_output(0, output, sizeof(output), &count) == 0 && count == 3 &&
                memcmp(output, "ab\n", 3) == 0, "echo output queue")) return 1;
    if (expect(tty_set_canonical(0, 0) == 0 && tty_input(0, 'x') == 0 &&
                tty_read(0, buffer, sizeof(buffer), &count) == 0 && count == 1 &&
                buffer[0] == 'x', "raw input read")) return 1;
    rix_termios_t termios = {0};
    if (expect(tty_get_termios(0, &termios) == 0 &&
                (termios.lflag & (RIX_TTY_LFLAG_ECHO | RIX_TTY_LFLAG_UTF8)) ==
                    (RIX_TTY_LFLAG_ECHO | RIX_TTY_LFLAG_UTF8),
                "get termios flags")) return 1;
    termios.lflag = RIX_TTY_LFLAG_CANONICAL | RIX_TTY_LFLAG_ECHO |
                     RIX_TTY_LFLAG_ISIG | RIX_TTY_LFLAG_UTF8;
    if (expect(tty_set_termios(0, &termios) == 0, "set termios flags")) return 1;
    if (expect(tty_set_foreground_pgrp(0, 42) == 0, "set foreground pgrp")) return 1;
    uint32_t pgrp = 0;
    if (expect(tty_get_foreground_pgrp(0, &pgrp) == 0 && pgrp == 42,
                "get foreground pgrp")) return 1;
    tty_set_signal_hook(signal_hook);
    if (expect(tty_set_canonical(0, 1) == 0 && tty_input(0, 3) == 0 &&
                seen_group == 42 && seen_signal == 2,
                "CTRL-C sends SIGINT to foreground group")) return 1;
    if (expect(tty_set_session(0, 42, 1) == 0, "set controlling session")) return 1;
    uint32_t session = 0;
    int controlling = 0;
    if (expect(tty_get_session(0, &session, &controlling) == 0 && session == 42 &&
                controlling, "get controlling session")) return 1;
    if (expect(tty_set_dimensions(0, 24, 80) == 0 &&
                tty_output(0, "\x1b[5;10H", 7, &written) == 0 && written == 7,
                "ANSI cursor sequence")) return 1;
    uint16_t row = 0, column = 0, rows = 0, columns = 0;
    if (expect(tty_get_cursor(0, &row, &column) == 0 && row == 4 && column == 9 &&
                tty_get_dimensions(0, &rows, &columns) == 0 && rows == 24 && columns == 80,
                "cursor and dimensions")) return 1;
    if (expect(tty_output(0, "X", 1, &written) == 0 && written == 1,
                "screen character output")) return 1;
    if (expect(tty_output(0, "\xC3\xA9", 2, &written) == 0 && written == 2 &&
                tty_get_cursor(0, &row, &column) == 0 && row == 4 && column == 11,
                "UTF-8 sequence occupies one screen cell")) return 1;
    uint8_t screen[24u * 80u] = {0};
    size_t screen_size = 0;
    if (expect(tty_read_screen(0, screen, sizeof(screen), &screen_size) == 0 &&
                screen_size == sizeof(screen) && screen[4u * 80u + 9u] == 'X',
                "screen buffer read")) return 1;
    unsigned pty = 0;
    if (expect(tty_pty_open(&pty) == 0, "open pty")) return 1;
    if (expect(tty_pty_get_termios(pty, &termios) == 0 &&
                (termios.lflag & RIX_TTY_LFLAG_CANONICAL), "get pty termios")) return 1;
    termios.lflag = RIX_TTY_LFLAG_UTF8;
    if (expect(tty_pty_set_termios(pty, &termios) == 0 &&
                tty_pty_master_write(pty, "xy", 2, &written) == 0 && written == 2 &&
                tty_pty_slave_read(pty, buffer, sizeof(buffer), &count) == 0 && count == 2 &&
                memcmp(buffer, "xy", 2) == 0, "pty raw termios input")) return 1;
    if (expect(tty_pty_master_read(pty, output, sizeof(output), &count) == -3 && count == 0,
                "pty raw mode suppresses echo when disabled")) return 1;
    termios.lflag = RIX_TTY_LFLAG_CANONICAL | RIX_TTY_LFLAG_ECHO | RIX_TTY_LFLAG_ISIG | RIX_TTY_LFLAG_UTF8;
    if (expect(tty_pty_set_termios(pty, &termios) == 0, "restore pty termios")) return 1;
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
    if (expect(tty_recover(0) == 0 && tty_get_cursor(0, &row, &column) == 0 &&
                row == 1 && column == 0 && tty_read_output(0, output, sizeof(output), &count) == 0 &&
                count == 27 && memcmp(output, "RixuriOS recovery console\r\n", 27) == 0,
                "recovery console reset and banner")) return 1;
    puts("tty tests: PASS");
    return 0;
}
