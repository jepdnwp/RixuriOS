#include "tty.h"

static rix_tty_t ttys[RIX_TTY_COUNT];

static rix_tty_t *tty_valid(unsigned id) {
    return id < RIX_TTY_COUNT ? &ttys[id] : NULL;
}

void tty_init(void) {
    for (unsigned i = 0; i < RIX_TTY_COUNT; ++i) {
        rix_tty_t *t = &ttys[i];
        t->head = 0;
        t->tail = 0;
        t->count = 0;
        t->output_head = 0;
        t->output_tail = 0;
        t->output_count = 0;
        t->line_chars = 0;
        t->canonical_ready = 0;
        t->canonical = 1;
        t->echo = 1;
        t->foreground_pgrp = 0;
    }
}

rix_tty_t *tty_get(unsigned id) {
    return tty_valid(id);
}

int tty_output(unsigned id, const void *buf, size_t n, size_t *written) {
    if (written) *written = 0;
    rix_tty_t *t = tty_valid(id);
    if (!t || (!buf && n)) return -1;
    const uint8_t *src = (const uint8_t *)buf;
    size_t done = 0;
    while (done < n && t->output_count < RIX_TTY_OUTPUT) {
        t->output[t->output_tail] = src[done++];
        t->output_tail = (t->output_tail + 1u) % RIX_TTY_OUTPUT;
        t->output_count++;
    }
    if (written) *written = done;
    return done == n ? 0 : -2;
}

int tty_read_output(unsigned id, void *buf, size_t n, size_t *out) {
    if (out) *out = 0;
    rix_tty_t *t = tty_valid(id);
    if (!t || (!buf && n)) return -1;
    uint8_t *dst = (uint8_t *)buf;
    size_t done = 0;
    while (done < n && t->output_count) {
        dst[done++] = t->output[t->output_head];
        t->output_head = (t->output_head + 1u) % RIX_TTY_OUTPUT;
        t->output_count--;
    }
    if (out) *out = done;
    return done ? 0 : -3;
}

static int echo_bytes(rix_tty_t *t, const uint8_t *data, size_t n) {
    (void)t;
    size_t written = 0;
    return tty_output((unsigned)(t - ttys), data, n, &written) == 0 ? 0 : -3;
}

int tty_input(unsigned id, uint8_t ch) {
    rix_tty_t *t = tty_valid(id);
    if (!t) return -1;
    if (t->canonical && (ch == 8u || ch == 127u)) {
        if (t->line_chars != 0u && t->count != 0u) {
            t->tail = (t->tail + RIX_TTY_INPUT - 1u) % RIX_TTY_INPUT;
            t->count--;
            t->line_chars--;
            if (t->echo) {
                static const uint8_t erase[] = {8u, ' ', 8u};
                return echo_bytes(t, erase, sizeof(erase));
            }
        }
        return 0;
    }
    if (t->count == RIX_TTY_INPUT) return -2;
    t->input[t->tail] = ch;
    t->tail = (t->tail + 1u) % RIX_TTY_INPUT;
    t->count++;
    if (ch == '\n') {
        if (t->canonical) t->canonical_ready++;
        t->line_chars = 0;
    } else {
        t->line_chars++;
    }
    if (t->echo) return echo_bytes(t, &ch, 1u);
    return 0;
}

int tty_read(unsigned id, void *buf, size_t n, size_t *out) {
    if (out) *out = 0;
    rix_tty_t *t = tty_valid(id);
    if (!t || (!buf && n)) return -1;
    if (t->canonical && t->canonical_ready == 0u) return -3;
    uint8_t *dst = (uint8_t *)buf;
    size_t done = 0;
    while (done < n && t->count) {
        uint8_t ch = t->input[t->head];
        dst[done++] = ch;
        t->head = (t->head + 1u) % RIX_TTY_INPUT;
        t->count--;
        if (t->canonical && ch == '\n') {
            t->canonical_ready--;
            break;
        }
    }
    if (out) *out = done;
    return done ? 0 : -3;
}

int tty_set_canonical(unsigned id, int enabled) {
    rix_tty_t *t = tty_valid(id);
    if (!t) return -1;
    t->canonical = enabled ? 1u : 0u;
    return 0;
}

int tty_set_echo(unsigned id, int enabled) {
    rix_tty_t *t = tty_valid(id);
    if (!t) return -1;
    t->echo = enabled ? 1u : 0u;
    return 0;
}

int tty_set_foreground_pgrp(unsigned id, uint32_t pgrp) {
    rix_tty_t *t = tty_valid(id);
    if (!t) return -1;
    t->foreground_pgrp = pgrp;
    return 0;
}

int tty_get_foreground_pgrp(unsigned id, uint32_t *pgrp) {
    rix_tty_t *t = tty_valid(id);
    if (!t || !pgrp) return -1;
    *pgrp = t->foreground_pgrp;
    return 0;
}
