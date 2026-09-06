#pragma once
#include <stddef.h>
#include <stdint.h>

#define RIX_TTY_COUNT 4u
#define RIX_TTY_INPUT 4096u
#define RIX_TTY_OUTPUT 4096u
#define RIX_PTY_COUNT 4u

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
    uint8_t canonical;
    uint8_t echo;
    uint32_t foreground_pgrp;
} rix_tty_t;

void tty_init(void);
rix_tty_t *tty_get(unsigned id);
int tty_input(unsigned id, uint8_t ch);
int tty_read(unsigned id, void *buf, size_t n, size_t *out);
int tty_output(unsigned id, const void *buf, size_t n, size_t *written);
int tty_read_output(unsigned id, void *buf, size_t n, size_t *out);
int tty_set_canonical(unsigned id, int enabled);
int tty_set_echo(unsigned id, int enabled);
int tty_set_foreground_pgrp(unsigned id, uint32_t pgrp);
int tty_get_foreground_pgrp(unsigned id, uint32_t *pgrp);

int tty_pty_open(unsigned *pty_id);
int tty_pty_close(unsigned pty_id);
int tty_pty_master_write(unsigned pty_id, const void *buf, size_t n, size_t *written);
int tty_pty_master_read(unsigned pty_id, void *buf, size_t n, size_t *out);
int tty_pty_slave_write(unsigned pty_id, const void *buf, size_t n, size_t *written);
int tty_pty_slave_read(unsigned pty_id, void *buf, size_t n, size_t *out);
