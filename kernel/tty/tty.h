#pragma once
#include <stddef.h>
#include <stdint.h>

#define RIX_TTY_COUNT 4u
#define RIX_TTY_INPUT 4096u
#define RIX_TTY_OUTPUT 4096u
#define RIX_PTY_COUNT 4u
#define RIX_TTY_MAX_ROWS 64u
#define RIX_TTY_MAX_COLUMNS 128u

typedef struct {
    uint8_t input[RIX_TTY_INPUT];
    uint8_t output[RIX_TTY_OUTPUT];
    size_t head;
    size_t tail;
    size_t count;
    size_t output_head;
    size_t output_tail;
    size_t output_count;
    size_t line_chars;
    size_t canonical_ready;
    uint16_t rows;
    uint16_t columns;
    uint16_t cursor_row;
    uint16_t cursor_column;
    uint16_t vt_value[2];
    uint8_t vt_state;
    uint8_t vt_value_index;
    uint8_t screen[RIX_TTY_MAX_ROWS * RIX_TTY_MAX_COLUMNS];
    uint8_t canonical;
    uint8_t echo;
    uint8_t isig;
    uint8_t utf8;
    uint8_t controlling;
    uint32_t session;
    uint32_t foreground_pgrp;
    uint32_t utf8_codepoint;
    uint8_t utf8_expected;
    uint8_t utf8_seen;
} rix_tty_t;

void tty_init(void);
void tty_set_framebuffer(uint64_t base, uint32_t size, uint32_t width,
                         uint32_t height, uint32_t pitch, uint32_t format);
rix_tty_t *tty_get(unsigned id);
int tty_input(unsigned id, uint8_t ch);
int tty_read(unsigned id, void *buf, size_t n, size_t *out);
int tty_output(unsigned id, const void *buf, size_t n, size_t *written);
int tty_read_output(unsigned id, void *buf, size_t n, size_t *out);
int tty_set_canonical(unsigned id, int enabled);
int tty_set_echo(unsigned id, int enabled);

#define RIX_TTY_LFLAG_CANONICAL 0x0001u
#define RIX_TTY_LFLAG_ECHO      0x0002u
#define RIX_TTY_LFLAG_ISIG      0x0004u
#define RIX_TTY_LFLAG_UTF8      0x0008u

typedef struct {
    uint32_t lflag;
} rix_termios_t;

int tty_get_termios(unsigned id, rix_termios_t *termios);
int tty_set_termios(unsigned id, const rix_termios_t *termios);
int tty_set_foreground_pgrp(unsigned id, uint32_t pgrp);
int tty_get_foreground_pgrp(unsigned id, uint32_t *pgrp);
int tty_set_session(unsigned id, uint32_t session, int controlling);
int tty_get_session(unsigned id, uint32_t *session, int *controlling);
int tty_attach_session(unsigned id, uint32_t session, uint32_t foreground_pgrp);
int tty_detach_session(unsigned id, uint32_t session);
typedef int (*tty_signal_hook_t)(uint32_t process_group, unsigned signal);
void tty_set_signal_hook(tty_signal_hook_t hook);
int tty_set_dimensions(unsigned id, uint16_t rows, uint16_t columns);
int tty_get_dimensions(unsigned id, uint16_t *rows, uint16_t *columns);
int tty_get_cursor(unsigned id, uint16_t *row, uint16_t *column);
int tty_read_screen(unsigned id, void *buf, size_t capacity, size_t *out);
int tty_recover(unsigned id);

int tty_pty_open(unsigned *pty_id);
int tty_pty_close(unsigned pty_id);
int tty_pty_get_termios(unsigned pty_id, rix_termios_t *termios);
int tty_pty_set_termios(unsigned pty_id, const rix_termios_t *termios);
int tty_pty_master_write(unsigned pty_id, const void *buf, size_t n, size_t *written);
int tty_pty_master_read(unsigned pty_id, void *buf, size_t n, size_t *out);
int tty_pty_slave_write(unsigned pty_id, const void *buf, size_t n, size_t *written);
int tty_pty_slave_read(unsigned pty_id, void *buf, size_t n, size_t *out);
