#include "tty.h"
#include "font_psf.h"
#include "../sync/lock.h"
#ifndef RIX_HOST_TEST
#include "../mm/vmm.h"
#endif

#ifdef RIX_HOST_TEST
const unsigned char _binary_assets_fonts_terminus_12x24_psf_start[] = {0};
const unsigned char _binary_assets_fonts_terminus_12x24_psf_end[] = {0};
#endif

static rix_tty_t ttys[RIX_TTY_COUNT];
/* Phase E3 output lock (irqsave: ps2-IRQ echo paths call tty_output).
 * Same discipline as serial.c's console_lock; serial.c never calls the
 * locking wrapper while holding its lock (nolock body instead). */
static rix_spinlock_t tty_output_lock;
static struct { volatile uint32_t *pixels; uint32_t size, width, height, pitch, format; uint16_t columns, rows; } framebuffer;
static uint8_t psf_ascii[128];
static const rix_psf2_header_t *psf;
static const uint8_t *psf_glyphs;

#define FG_WHITE 0x00ffffffu
#define BG_BLACK 0x00000000u

static const uint32_t ansi_colors[16] = {
    0x00000000u, 0x00aa0000u, 0x0000aa00u, 0x00aa5500u,
    0x000000aau, 0x00aa00aau, 0x0000aaaau, 0x00aaaaaau,
    0x00555555u, 0x00ff5555u, 0x0055ff55u, 0x00ffff55u,
    0x005555ffu, 0x00ff55ffu, 0x0055ffffu, 0x00ffffffu,
};

static uint32_t get_fg_color(rix_tty_t *t) {
    return ansi_colors[t->bold ? (t->fg_color < 8u ? t->fg_color + 8u : t->fg_color) : t->fg_color];
}

static uint32_t get_bg_color(rix_tty_t *t) {
    return ansi_colors[t->bg_color];
}

static void framebuffer_pixel(volatile uint32_t *pixel, uint32_t color) {
    if (!pixel) return;
    *pixel = framebuffer.format == 1u ? ((color & 0x0000ffu) << 16) |
             (color & 0x00ff00u) | ((color & 0xff0000u) >> 16) : color;
}

static void framebuffer_clear(void) {
    if (!framebuffer.pixels || !framebuffer.pitch) return;
    for (uint32_t y=0; y<framebuffer.height; ++y) {
        volatile uint32_t *row=(volatile uint32_t *)((uint8_t *)framebuffer.pixels+(size_t)y*framebuffer.pitch);
        for (uint32_t x=0; x<framebuffer.width; ++x) row[x]=0;
    }
}

static void framebuffer_glyph(uint16_t column,uint16_t row,uint8_t ch,uint32_t fg,uint32_t bg) {
    if (!framebuffer.pixels || column>=framebuffer.columns || row>=framebuffer.rows) return;
    uint32_t glyph_index=psf_ascii[ch<128u?ch:(uint8_t)'?'];
    if (!psf||glyph_index>=psf->glyph_count) glyph_index=0;
    const uint8_t *glyph=psf_glyphs+(size_t)glyph_index*psf->bytes_per_glyph;
    uint32_t bytes_per_line=(psf->width+7u)/8u;
    uint32_t x0=(uint32_t)column*psf->width,y0=(uint32_t)row*psf->height;
    for (uint32_t y=0;y<psf->height;++y) {
        uint32_t py=y0+y;
        if (py>=framebuffer.height) continue;
        volatile uint32_t *pixels=(volatile uint32_t *)((uint8_t *)framebuffer.pixels+(size_t)py*framebuffer.pitch);
        for (uint32_t x=0;x<psf->width;++x) {
            uint32_t px=x0+x;
            if (px>=framebuffer.width) continue;
            uint8_t bits=glyph[(size_t)y*bytes_per_line+(x/8u)];
            framebuffer_pixel(&pixels[px],bits&(uint8_t)(0x80u>>(x%8u))?fg:bg);
        }
    }
}

static void psf_init(void) {
    psf=(const rix_psf2_header_t *)_binary_assets_fonts_terminus_12x24_psf_start;
    psf_glyphs=_binary_assets_fonts_terminus_12x24_psf_start+psf->header_size;
    for (unsigned i=0;i<128u;i++) psf_ascii[i]=0;
    if (psf->magic!=RIX_PSF2_MAGIC||psf->header_size<sizeof(*psf)||
       !psf->glyph_count||!psf->bytes_per_glyph||!psf->width||!psf->height) return;
    for (unsigned i=0;i<128u&&i<psf->glyph_count;i++) psf_ascii[i]=(uint8_t)i;
    if (!(psf->flags&1u)) return;
    const uint8_t *p=psf_glyphs+(size_t)psf->glyph_count*psf->bytes_per_glyph;
    const uint8_t *end=_binary_assets_fonts_terminus_12x24_psf_end;
    unsigned glyph=0;
    while (p<end&&glyph<psf->glyph_count) {
        if (*p==0xffu){++glyph;++p;continue;}
        if (*p==0xfeu){++p;continue;}
        uint32_t cp=*p++;
        if (cp>=0xc0u&&cp<0xe0u&&p<end) cp=((cp&0x1fu)<<6u)|(*p++&0x3fu);
        else if (cp>=0xe0u&&cp<0xf0u&&p+1<end) { uint32_t b1=*p++; uint32_t b2=*p++; cp=((cp&0x0fu)<<12u)|((b1&0x3fu)<<6u)|(b2&0x3fu); }
        else if (cp>=0xf0u&&p+2<end) { uint32_t b1=*p++; uint32_t b2=*p++; uint32_t b3=*p++; cp=((cp&7u)<<18u)|((b1&0x3fu)<<12u)|((b2&0x3fu)<<6u)|(b3&0x3fu); }
        if (cp<128u) psf_ascii[cp]=(uint8_t)glyph;
    }
}

#define VT_NORMAL 0u
#define VT_ESC 1u
#define VT_CSI 2u

typedef struct {
    uint8_t opened;
    rix_tty_t slave;
    uint8_t master_output[RIX_TTY_OUTPUT];
    size_t master_output_head;
    size_t master_output_tail;
    size_t master_output_count;
} rix_pty_t;

static rix_pty_t ptys[RIX_PTY_COUNT];
static tty_signal_hook_t signal_hook;

static int tty_control_signal(rix_tty_t *t, uint8_t ch) {
    unsigned signal = ch == 0x03u ? 2u : (ch == 0x1au ? 20u : 3u);
    if (!signal_hook || !t->foreground_pgrp) return 0;
    return signal_hook(t->foreground_pgrp, signal);
}

static uint16_t vt_clamp(uint16_t value, uint16_t limit) {
    return value >= limit ? (uint16_t)(limit - 1u) : value;
}

static void vt_scroll_up(rix_tty_t *t) {
    uint16_t cols = t->columns;
    uint16_t rows = t->rows;
    for (uint16_t r = 0; r + 1u < rows; ++r) {
        for (uint16_t c = 0; c < cols; ++c) {
            size_t dst = (size_t)r * RIX_TTY_MAX_COLUMNS + c;
            size_t src = (size_t)(r + 1u) * RIX_TTY_MAX_COLUMNS + c;
            t->screen[dst] = t->screen[src];
            t->screen_fg[dst] = t->screen_fg[src];
            t->screen_bg[dst] = t->screen_bg[src];
        }
    }
    for (uint16_t c = 0; c < cols; ++c) {
        size_t idx = (size_t)(rows - 1u) * RIX_TTY_MAX_COLUMNS + c;
        t->screen[idx] = ' ';
        t->screen_fg[idx] = t->fg_color;
        t->screen_bg[idx] = t->bg_color;
    }
    for (uint16_t r = 0; r < rows; ++r) {
        for (uint16_t c = 0; c < cols; ++c) {
            size_t idx = (size_t)r * RIX_TTY_MAX_COLUMNS + c;
            framebuffer_glyph(c, r, t->screen[idx], ansi_colors[t->screen_fg[idx] < 16u ? t->screen_fg[idx] : 7u], ansi_colors[t->screen_bg[idx] < 16u ? t->screen_bg[idx] : 0u]);
        }
    }
}

static void vt_move(rix_tty_t *t, int row_delta, int column_delta) {
    int row = (int)t->cursor_row + row_delta;
    int column = (int)t->cursor_column + column_delta;
    if (column < 0) column = 0;
    if ((uint16_t)column >= t->columns) column = (int)t->columns - 1;
    t->cursor_column = (uint16_t)column;
    if (row >= (int)t->rows) {
        vt_scroll_up(t);
        t->cursor_row = t->rows - 1u;
    } else if (row < 0) {
        t->cursor_row = 0;
    } else {
        t->cursor_row = (uint16_t)row;
    }
}

static void vt_clear_screen(rix_tty_t *t) {
    uint16_t rows = t->rows;
    uint16_t cols = t->columns;
    size_t count = (size_t)RIX_TTY_MAX_ROWS * RIX_TTY_MAX_COLUMNS;
    for (size_t i = 0; i < count; ++i) {
        t->screen[i] = ' ';
        t->screen_fg[i] = t->fg_color;
        t->screen_bg[i] = t->bg_color;
    }
    for (uint16_t r = 0; r < rows; ++r) {
        for (uint16_t c = 0; c < cols; ++c) {
            framebuffer_glyph(c, r, ' ', get_fg_color(t), get_bg_color(t));
        }
    }
}

static void vt_print(rix_tty_t *t, uint8_t ch) {
    size_t index = (size_t)t->cursor_row * RIX_TTY_MAX_COLUMNS + t->cursor_column;
    t->screen[index] = ch;
    t->screen_fg[index] = t->fg_color;
    t->screen_bg[index] = t->bg_color;
    framebuffer_glyph(t->cursor_column, t->cursor_row, ch, get_fg_color(t), get_bg_color(t));
    if (t->cursor_column + 1u >= t->columns) {
        t->cursor_column = 0;
        if (t->cursor_row + 1u < t->rows) t->cursor_row++;
    } else {
        t->cursor_column++;
    }
}

static void vt_erase_line(rix_tty_t *t, uint16_t mode) {
    uint16_t start = mode == 1u ? 0u : t->cursor_column;
    uint16_t end = mode == 1u ? t->cursor_column : t->columns;
    if (mode == 2u) { start = 0u; end = t->columns; }
    for (uint16_t column = start; column < end; ++column) {
        size_t idx = (size_t)t->cursor_row * RIX_TTY_MAX_COLUMNS + column;
        t->screen[idx] = ' ';
        t->screen_fg[idx] = t->fg_color;
        t->screen_bg[idx] = t->bg_color;
        framebuffer_glyph(column, t->cursor_row, ' ', get_fg_color(t), get_bg_color(t));
    }
}

static void vt_erase_display(rix_tty_t *t, uint16_t mode) {
    if (mode == 2u) { vt_clear_screen(t); return; }
    if (mode == 0u) {
        for (uint16_t row = t->cursor_row; row < t->rows; ++row) {
            uint16_t start = row == t->cursor_row ? t->cursor_column : 0u;
            for (uint16_t column = start; column < t->columns; ++column) {
                size_t idx = (size_t)row * RIX_TTY_MAX_COLUMNS + column;
                t->screen[idx] = ' ';
                t->screen_fg[idx] = t->fg_color;
                t->screen_bg[idx] = t->bg_color;
                framebuffer_glyph(column, row, ' ', get_fg_color(t), get_bg_color(t));
            }
        }
    } else if (mode == 1u) {
        for (uint16_t row = 0; row <= t->cursor_row; ++row) {
            uint16_t end = row == t->cursor_row ? t->cursor_column + 1u : t->columns;
            for (uint16_t column = 0; column < end; ++column) {
                size_t idx = (size_t)row * RIX_TTY_MAX_COLUMNS + column;
                t->screen[idx] = ' ';
                t->screen_fg[idx] = t->fg_color;
                t->screen_bg[idx] = t->bg_color;
                framebuffer_glyph(column, row, ' ', get_fg_color(t), get_bg_color(t));
            }
        }
    }
}

static void vt_consume(rix_tty_t *t, uint8_t ch) {
    if (!t->utf8) {
        /* In raw byte mode the screen still receives each byte as a cell. */
    } else if (t->utf8_expected != 0u) {
        if ((ch & 0xc0u) == 0x80u) {
            t->utf8_codepoint = (t->utf8_codepoint << 6) | (uint32_t)(ch & 0x3fu);
            t->utf8_seen++;
            if (t->utf8_seen == t->utf8_expected) {
                /* The bounded screen stores one display cell per code point. */
                uint8_t cell = t->utf8_codepoint <= 0x7fu ? (uint8_t)t->utf8_codepoint : (uint8_t)'?';
                t->utf8_expected = 0;
                t->utf8_seen = 0;
                t->utf8_codepoint = 0;
                vt_print(t, cell);
            }
            return;
        }
        t->utf8_expected = 0;
        t->utf8_seen = 0;
        t->utf8_codepoint = 0;
        vt_print(t, '?');
    }
    if ((t->utf8_expected == 0u) && ch >= 0x80u && ch <= 0xf4u) {
        if (ch >= 0xc2u && ch <= 0xdfu) {
            t->utf8_expected = 1u;
            t->utf8_codepoint = ch & 0x1fu;
            t->utf8_seen = 0;
            return;
        }
        if (ch >= 0xe0u && ch <= 0xefu) {
            t->utf8_expected = 2u;
            t->utf8_codepoint = ch & 0x0fu;
            t->utf8_seen = 0;
            return;
        }
        if (ch >= 0xf0u && ch <= 0xf4u) {
            t->utf8_expected = 3u;
            t->utf8_codepoint = ch & 0x07u;
            t->utf8_seen = 0;
            return;
        }
        vt_print(t, '?');
        return;
    }
    if (t->vt_state == VT_NORMAL) {
        if (ch == 0x1bu) {
            t->vt_state = VT_ESC;
        } else if (ch == '\n') {
            t->cursor_column = 0;
            vt_move(t, 1, 0);
        } else if (ch == '\r') {
            t->cursor_column = 0;
        } else if (ch == 8u) {
            vt_move(t, 0, -1);
        } else if (ch >= 0x20u && ch != 0x7fu) {
            vt_print(t, ch);
        }
        return;
    }
    if (t->vt_state == VT_ESC) {
        if (ch == '[') {
            t->vt_state = VT_CSI;
            t->vt_value_index = 0;
            for (uint8_t i = 0; i < 8u; ++i) t->vt_params[i] = 0;
        } else {
            t->vt_state = VT_NORMAL;
        }
        return;
    }
    if (t->vt_state == VT_CSI) {
        if (ch >= '0' && ch <= '9') {
            if (t->vt_value_index < 8u) {
                t->vt_params[t->vt_value_index] = (uint8_t)(t->vt_params[t->vt_value_index] * 10u + (ch - '0'));
            }
            return;
        }
        if (ch == ';') {
            if (t->vt_value_index < 7u) t->vt_value_index++;
            return;
        }
        uint8_t param0 = t->vt_params[0];
        uint8_t param1 = t->vt_params[1];
        switch (ch) {
            case 'A': vt_move(t, -(int)(param0 ? param0 : 1u), 0); break;
            case 'B': vt_move(t, (int)(param0 ? param0 : 1u), 0); break;
            case 'C': vt_move(t, 0, (int)(param0 ? param0 : 1u)); break;
            case 'D': vt_move(t, 0, -(int)(param0 ? param0 : 1u)); break;
            case 'G': t->cursor_column = vt_clamp((uint16_t)(param0 - 1u), t->columns); break;
            case 'H':
            case 'f':
                t->cursor_row = vt_clamp((uint16_t)(param0 ? param0 - 1u : 0u), t->rows);
                t->cursor_column = vt_clamp((uint16_t)(param1 ? param1 - 1u : 0u), t->columns);
                break;
            case 'J': vt_erase_display(t, param0); break;
            case 'K': vt_erase_line(t, param0); break;
            case 'm':
                for (uint8_t i = 0; i <= t->vt_value_index; ++i) {
                    uint8_t p = t->vt_params[i];
                    if (p == 0u) { t->fg_color = 7u; t->bg_color = 0u; t->bold = 0u; }
                    else if (p == 1u) { t->bold = 1u; }
                    else if (p == 22u) { t->bold = 0u; }
                    else if (p >= 30u && p <= 37u) { t->fg_color = p - 30u; }
                    else if (p >= 40u && p <= 47u) { t->bg_color = p - 40u; }
                    else if (p >= 90u && p <= 97u) { t->fg_color = p - 90u + 8u; }
                    else if (p >= 100u && p <= 107u) { t->bg_color = p - 100u + 8u; }
                }
                break;
            default:
                break;
        }
        t->vt_state = VT_NORMAL;
        t->vt_value_index = 0;
        for (uint8_t i = 0; i < 8u; ++i) t->vt_params[i] = 0;
        return;
    }
}

static rix_tty_t *tty_valid(unsigned id) {
    return id < RIX_TTY_COUNT ? &ttys[id] : NULL;
}

void tty_init(void) {
    psf_init();
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
        t->edit_length = 0;
        t->edit_cursor = 0;
        t->history_count = 0;
        t->history_cursor = 0;
        t->rows = 25;
        t->columns = 80;
        t->cursor_row = 0;
        t->cursor_column = 0;
        t->vt_state = VT_NORMAL;
        t->vt_value_index = 0;
        t->vt_params[0] = 0; t->vt_params[1] = 0;
        t->utf8_codepoint = 0;
        t->utf8_expected = 0;
        t->utf8_seen = 0;
        t->fg_color = 7u;
        t->bg_color = 0u;
        t->bold = 0u;
        vt_clear_screen(t);
        t->canonical = 1;
        t->echo = 1;
        t->isig = 1;
        t->utf8 = 1;
        t->controlling = 0;
        t->session = 0;
        t->foreground_pgrp = 0;
    }
    for (unsigned i = 0; i < RIX_PTY_COUNT; ++i) ptys[i].opened = 0;
}

void tty_set_framebuffer(uint64_t base,uint32_t size,uint32_t width,uint32_t height,
                         uint32_t pitch,uint32_t format) {
    uint64_t required = height ? (uint64_t)(height - 1u) * pitch +
                                  (uint64_t)width * sizeof(uint32_t) : 0;
    if (!base || !width || !height || (uint64_t)pitch < (uint64_t)width * sizeof(uint32_t) ||
        required > size || (format != 0u && format != 1u)) {
        framebuffer.pixels = 0;
        framebuffer.size = framebuffer.width = framebuffer.height = framebuffer.pitch = 0;
        framebuffer.columns = framebuffer.rows = 0;
        return;
    }
    framebuffer.pixels=(volatile uint32_t *)(uintptr_t)base; framebuffer.size=size;
    /* The console must stay writable under ANY CR3: exception diagnostics
     * and the first user syscalls run on user roots. Ranges in
     * kernel-shared PML4 slots are already visible everywhere via the
     * identity map; anything else (a dGPU VRAM BAR above 512 GiB lands in
     * the private user-image slot) would nested-fault on first touch and
     * freeze the machine with zero output. Remap those through the shared
     * MMIO window instead. */
#ifndef RIX_HOST_TEST
    {
        unsigned idx=(unsigned)((base>>39)&0x1FFu);
        int shared=idx==0||idx==2||idx==3||idx>=256;
        if(!shared){
            uint64_t mapped=vmm_map_mmio(base,size);
            if(!mapped){
                framebuffer.pixels=0;
                framebuffer.size=framebuffer.width=framebuffer.height=framebuffer.pitch=0;
                framebuffer.columns=framebuffer.rows=0;
                return;
            }
            framebuffer.pixels=(volatile uint32_t *)(uintptr_t)mapped;
        }
    }
#endif
    framebuffer.width=width; framebuffer.height=height; framebuffer.pitch=pitch;
    framebuffer.format=format;
    uint32_t cell_width=(psf&&psf->width)?psf->width:8u;
    uint32_t cell_height=(psf&&psf->height)?psf->height:16u;
    framebuffer.columns=(uint16_t)(width/cell_width);
    framebuffer.rows=(uint16_t)(height/cell_height);
    if(framebuffer.columns>RIX_TTY_MAX_COLUMNS) framebuffer.columns=RIX_TTY_MAX_COLUMNS;
    if(framebuffer.rows>RIX_TTY_MAX_ROWS) framebuffer.rows=RIX_TTY_MAX_ROWS;
    framebuffer_clear();
    if(framebuffer.columns>=2u&&framebuffer.rows>=2u){ttys[0].columns=framebuffer.columns;ttys[0].rows=framebuffer.rows;}
}

rix_tty_t *tty_get(unsigned id) {
    return tty_valid(id);
}

void tty_set_signal_hook(tty_signal_hook_t hook) {
    signal_hook = hook;
}

/* Phase E3: unlocked body. External callers use tty_output (locks);
 * serial.c's console mirror uses this directly (lock already held). */
int tty_output_nolock(unsigned id, const void *buf, size_t n, size_t *written) {
    if (written) *written = 0;
    rix_tty_t *t = tty_valid(id);
    if (!t || (!buf && n)) return -1;
    const uint8_t *src = (const uint8_t *)buf;
    size_t done = 0;
    /* The output ring has no consumer (tty_read_output is currently
     * unreferenced); rendering happens synchronously in vt_consume, so a
     * full ring must drop the oldest byte rather than wedge or silence
     * all stdio once 4096 bytes have accumulated. */
    while (done < n) {
        if (t->output_count >= RIX_TTY_OUTPUT) {
            t->output_head = (t->output_head + 1u) % RIX_TTY_OUTPUT;
            t->output_count--;
        }
        t->output[t->output_tail] = src[done++];
        t->output_tail = (t->output_tail + 1u) % RIX_TTY_OUTPUT;
        t->output_count++;
        vt_consume(t, src[done - 1u]);
    }
    if (written) *written = done;
    return 0;
}

/* Phase E3: locking wrapper (see console_lock in serial.c). Per-call
 * FB atomicity for syscall-write and echo paths. serial.c's mirror
 * uses the nolock body (lock already held there). */
int tty_output(unsigned id, const void *buf, size_t n, size_t *written) {
    uint64_t f;rix_spin_lock_irqsave(&tty_output_lock,&f);
    int rc=tty_output_nolock(id,buf,n,written);
    rix_spin_unlock_irqrestore(&tty_output_lock,f);
    return rc;
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

/* Incremental canonical echo.  The shell prompt printed before input
 * starts is not part of edit_line, so a full-line redraw ("\r ESC[2K" +
 * buffer) wipes the prompt on every keystroke.  Echo only the cells that
 * changed instead; plain end-of-line typing therefore echoes exactly the
 * typed bytes. */
static void tty_echo_left(rix_tty_t *t, uint16_t count) {
    if (!count) return;
    char seq[8]; size_t n = 0;
    seq[n++] = 0x1b; seq[n++] = '[';
    char digits[6]; size_t nd = 0; uint16_t v = count;
    while (v) { digits[nd++] = (char)('0' + v % 10u); v /= 10u; }
    while (nd) seq[n++] = digits[--nd];
    seq[n++] = 'D';
    size_t written = 0;
    (void)tty_output((unsigned)(t - ttys), seq, n, &written);
}
static void tty_edit_echo_insert(rix_tty_t *t, uint16_t from) {
    /* Re-emit edit_line[from..length) (new byte plus shifted tail), then
     * step back over the tail so the cursor stays after the new byte. */
    size_t written = 0;
    if (from < t->edit_length)
        (void)tty_output((unsigned)(t - ttys), t->edit_line + from,
                         (size_t)(t->edit_length - from), &written);
    tty_echo_left(t, (uint16_t)(t->edit_length - t->edit_cursor));
}
static void tty_edit_echo_erase(rix_tty_t *t, uint16_t from) {
    /* Cell `from` was deleted: back up onto it, re-emit the shifted tail
     * plus one clearing space, then step back into place. */
    static const uint8_t back = 8u, space = ' ';
    size_t written = 0;
    (void)tty_output((unsigned)(t - ttys), &back, 1u, &written);
    if (from < t->edit_length)
        (void)tty_output((unsigned)(t - ttys), t->edit_line + from,
                         (size_t)(t->edit_length - from), &written);
    (void)tty_output((unsigned)(t - ttys), &space, 1u, &written);
    tty_echo_left(t, (uint16_t)(t->edit_length - t->edit_cursor + 1u));
}
static void tty_edit_history_save(rix_tty_t *t) {
    if(!t->edit_length)return;
    if(t->history_count==RIX_TTY_HISTORY_COUNT){for(uint8_t i=1;i<RIX_TTY_HISTORY_COUNT;++i)for(uint16_t j=0;j<RIX_TTY_LINE_MAX;++j)t->history[i-1u][j]=t->history[i][j];--t->history_count;}
    for(uint16_t i=0;i<t->edit_length;++i)t->history[t->history_count][i]=t->edit_line[i];
    t->history[t->history_count][t->edit_length]=0;++t->history_count;t->history_cursor=t->history_count;
}
static void tty_edit_load(rix_tty_t *t,uint8_t direction) {
    if(!t->history_count)return;
    uint16_t old_cursor=t->edit_cursor;
    if(direction==RIX_TTY_KEY_UP){if(t->history_cursor)--t->history_cursor;}else if(t->history_cursor<t->history_count)++t->history_cursor;
    if(t->history_cursor==t->history_count)t->edit_length=0;else{uint16_t i=0;while(i+1u<RIX_TTY_LINE_MAX&&t->history[t->history_cursor][i]){t->edit_line[i]=t->history[t->history_cursor][i];++i;}t->edit_length=i;}
    t->edit_cursor=t->edit_length;
    if (!t->echo) return;
    /* Step back over the old input, erase to end-of-line (the prompt left
     * of the cursor survives), then write the recalled buffer. */
    tty_echo_left(t, old_cursor);
    {static const uint8_t el[] = {0x1b, '[', 'K'}; size_t written = 0;
     (void)tty_output((unsigned)(t - ttys), el, sizeof(el), &written);}
    if (t->edit_length) {size_t written = 0;
        (void)tty_output((unsigned)(t - ttys), t->edit_line,
                         t->edit_length, &written);}
}

int tty_input(unsigned id, uint8_t ch) {
    rix_tty_t *t = tty_valid(id);
    if (!t) return -1;
    if (t->canonical && t->isig && (ch == 0x03u || ch == 0x1au || ch == 0x1cu)) {
        return tty_control_signal(t, ch);
    }
    if (ch >= RIX_TTY_KEY_UP && ch <= RIX_TTY_KEY_RIGHT) {
        if (t->canonical) {
            if(ch==RIX_TTY_KEY_UP||ch==RIX_TTY_KEY_DOWN)tty_edit_load(t,ch);
            else if(ch==RIX_TTY_KEY_LEFT&&t->edit_cursor){--t->edit_cursor;if(t->echo){static const uint8_t left[]={0x1b,'[','D'};size_t written=0;(void)tty_output(id,left,sizeof(left),&written);}}
            else if(ch==RIX_TTY_KEY_RIGHT&&t->edit_cursor<t->edit_length){++t->edit_cursor;if(t->echo){static const uint8_t right[]={0x1b,'[','C'};size_t written=0;(void)tty_output(id,right,sizeof(right),&written);}}
            return 0;
        }
        if (t->count == RIX_TTY_INPUT) return -2;
        t->input[t->tail] = ch;
        t->tail = (t->tail + 1u) % RIX_TTY_INPUT;
        t->count++;
        return 0;
    }
    if (t->canonical && (ch == 8u || ch == 127u)) {
        if(t->edit_cursor){uint16_t from=(uint16_t)(t->edit_cursor-1u);for(uint16_t i=t->edit_cursor;i<t->edit_length;++i)t->edit_line[i-1u]=t->edit_line[i];--t->edit_cursor;--t->edit_length;t->line_chars=t->edit_length;if(t->echo)tty_edit_echo_erase(t,from);}
        return 0;
    }
    if(t->canonical&&(ch=='\n'||ch=='\r')){
        if((size_t)t->count+t->edit_length+1u>RIX_TTY_INPUT)return -2;
        for(uint16_t i=0;i<t->edit_length;++i){t->input[t->tail]= (uint8_t)t->edit_line[i];t->tail=(t->tail+1u)%RIX_TTY_INPUT;++t->count;}
        t->input[t->tail]='\n';t->tail=(t->tail+1u)%RIX_TTY_INPUT;++t->count;++t->canonical_ready;tty_edit_history_save(t);t->edit_length=t->edit_cursor=t->line_chars=0;
        if(t->echo){if(ch=='\r'){static const uint8_t crlf[]={'\r','\n'};return echo_bytes(t,crlf,2u);}return echo_bytes(t,&ch,1u);}return 0;
    }
    if(t->canonical){if(ch<0x20u||ch==0x7fu||t->edit_length+1u>=RIX_TTY_LINE_MAX)return 0;uint16_t from=t->edit_cursor;for(uint16_t i=t->edit_length;i>t->edit_cursor;--i)t->edit_line[i]=t->edit_line[i-1u];t->edit_line[t->edit_cursor++]=(char)ch;++t->edit_length;t->line_chars=t->edit_length;if(t->echo)tty_edit_echo_insert(t,from);return 0;}
    if (t->count == RIX_TTY_INPUT) return -2;
    t->input[t->tail] = ch;
    t->tail = (t->tail + 1u) % RIX_TTY_INPUT;
    t->count++;
    if (ch == '\n' || ch == '\r') {
        if (t->canonical) t->canonical_ready++;
        t->line_chars = 0;
    } else {
        t->line_chars++;
    }
    if (t->echo) {
        if (ch == '\r') { static const uint8_t crlf[] = {'\r','\n'}; return echo_bytes(t, crlf, 2u); }
        return echo_bytes(t, &ch, 1u);
    }
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
        t->head = (t->head + 1u) % RIX_TTY_INPUT;
        t->count--;
        if (ch == '\r') ch = '\n';
        dst[done++] = ch;
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

int tty_get_termios(unsigned id, rix_termios_t *termios) {
    rix_tty_t *t = tty_valid(id);
    if (!t || !termios) return -1;
    termios->lflag = (t->canonical ? RIX_TTY_LFLAG_CANONICAL : 0u) |
                     (t->echo ? RIX_TTY_LFLAG_ECHO : 0u) |
                     (t->isig ? RIX_TTY_LFLAG_ISIG : 0u) |
                     (t->utf8 ? RIX_TTY_LFLAG_UTF8 : 0u);
    return 0;
}

int tty_set_termios(unsigned id, const rix_termios_t *termios) {
    rix_tty_t *t = tty_valid(id);
    if (!t || !termios || (termios->lflag & ~(RIX_TTY_LFLAG_CANONICAL |
        RIX_TTY_LFLAG_ECHO | RIX_TTY_LFLAG_ISIG | RIX_TTY_LFLAG_UTF8))) return -1;
    t->canonical = (termios->lflag & RIX_TTY_LFLAG_CANONICAL) != 0u;
    t->echo = (termios->lflag & RIX_TTY_LFLAG_ECHO) != 0u;
    t->isig = (termios->lflag & RIX_TTY_LFLAG_ISIG) != 0u;
    t->utf8 = (termios->lflag & RIX_TTY_LFLAG_UTF8) != 0u;
    if ((termios->lflag & RIX_TTY_LFLAG_UTF8) == 0u) {
        t->utf8_expected = 0;
        t->utf8_seen = 0;
        t->utf8_codepoint = 0;
    }
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

int tty_set_session(unsigned id, uint32_t session, int controlling) {
    rix_tty_t *t = tty_valid(id);
    if (!t || (controlling && session == 0u)) return -1;
    t->session = session;
    t->controlling = controlling ? 1u : 0u;
    if (!t->controlling) t->foreground_pgrp = 0;
    return 0;
}

int tty_get_session(unsigned id, uint32_t *session, int *controlling) {
    rix_tty_t *t = tty_valid(id);
    if (!t || !session || !controlling) return -1;
    *session = t->session;
    *controlling = t->controlling != 0u;
    return 0;
}

int tty_attach_session(unsigned id, uint32_t session, uint32_t foreground_pgrp) {
    rix_tty_t *t = tty_valid(id);
    if (!t || session == 0u || foreground_pgrp == 0u) return -1;
    if (t->controlling && t->session != session) return -2;
    t->session = session;
    t->controlling = 1u;
    t->foreground_pgrp = foreground_pgrp;
    return 0;
}

int tty_detach_session(unsigned id, uint32_t session) {
    rix_tty_t *t = tty_valid(id);
    if (!t || session == 0u) return -1;
    if (!t->controlling) return 0;
    if (t->session != session) return -2;
    t->session = 0;
    t->controlling = 0;
    t->foreground_pgrp = 0;
    return 0;
}

int tty_set_dimensions(unsigned id, uint16_t rows, uint16_t columns) {
    rix_tty_t *t = tty_valid(id);
    if (!t || rows == 0u || columns == 0u || rows > RIX_TTY_MAX_ROWS ||
        columns > RIX_TTY_MAX_COLUMNS) return -1;
    t->rows = rows;
    t->columns = columns;
    t->cursor_row = vt_clamp(t->cursor_row, rows);
    t->cursor_column = vt_clamp(t->cursor_column, columns);
    return 0;
}

int tty_get_dimensions(unsigned id, uint16_t *rows, uint16_t *columns) {
    rix_tty_t *t = tty_valid(id);
    if (!t || !rows || !columns) return -1;
    *rows = t->rows;
    *columns = t->columns;
    return 0;
}

int tty_get_cursor(unsigned id, uint16_t *row, uint16_t *column) {
    rix_tty_t *t = tty_valid(id);
    if (!t || !row || !column) return -1;
    *row = t->cursor_row;
    *column = t->cursor_column;
    return 0;
}

int tty_read_screen(unsigned id, void *buf, size_t capacity, size_t *out) {
    if (out) *out = 0;
    rix_tty_t *t = tty_valid(id);
    size_t required = t ? (size_t)t->rows * t->columns : 0;
    if (!t || !buf || capacity < required) return -1;
    for (size_t row = 0; row < t->rows; ++row)
        for (size_t column = 0; column < t->columns; ++column)
            ((uint8_t *)buf)[row * t->columns + column] =
                t->screen[row * RIX_TTY_MAX_COLUMNS + column];
    if (out) *out = required;
    return 0;
}

int tty_recover(unsigned id) {
    rix_tty_t *t = tty_valid(id);
    if (!t) return -1;
    t->head = t->tail = t->count = 0;
    t->output_head = t->output_tail = t->output_count = 0;
    t->line_chars = t->canonical_ready = 0;
    t->vt_state = VT_NORMAL;
    t->vt_value_index = 0;
    for (uint8_t i = 0; i < 8u; ++i) t->vt_params[i] = 0;
    t->cursor_row = t->cursor_column = 0;
    t->fg_color = 7u;
    t->bg_color = 0u;
    t->bold = 0u;
    vt_clear_screen(t);
    static const uint8_t banner[] = "RixuriOS recovery console\r\n";
    size_t written = 0;
    return tty_output(id, banner, sizeof(banner) - 1u, &written) == 0 ? 0 : -2;
}

static rix_pty_t *pty_valid(unsigned id) {
    return id < RIX_PTY_COUNT && ptys[id].opened ? &ptys[id] : NULL;
}

static int pty_queue_put(uint8_t *queue, size_t *tail, size_t *count,
                         size_t capacity, uint8_t value) {
    if (*count == capacity) return -1;
    queue[*tail] = value;
    *tail = (*tail + 1u) % capacity;
    (*count)++;
    return 0;
}

static size_t pty_queue_get(uint8_t *queue, size_t *head, size_t *count,
                            size_t capacity, uint8_t *out, size_t n) {
    size_t done = 0;
    while (done < n && *count) {
        out[done++] = queue[*head];
        *head = (*head + 1u) % capacity;
        (*count)--;
    }
    return done;
}

int tty_pty_open(unsigned *pty_id) {
    if (!pty_id) return -1;
    for (unsigned i = 0; i < RIX_PTY_COUNT; ++i) {
        if (ptys[i].opened) continue;
        ptys[i].opened = 1;
        ptys[i].slave.head = 0;
        ptys[i].slave.tail = 0;
        ptys[i].slave.count = 0;
        ptys[i].slave.line_chars = 0;
        ptys[i].slave.canonical_ready = 0;
        ptys[i].slave.edit_length = 0;
        ptys[i].slave.edit_cursor = 0;
        ptys[i].slave.history_count = 0;
        ptys[i].slave.history_cursor = 0;
        ptys[i].slave.rows = 25;
        ptys[i].slave.columns = 80;
        ptys[i].slave.cursor_row = 0;
        ptys[i].slave.cursor_column = 0;
        ptys[i].slave.vt_state = VT_NORMAL;
        ptys[i].slave.vt_value_index = 0;
        ptys[i].slave.vt_params[0] = 0;
        ptys[i].slave.vt_params[1] = 0;
        ptys[i].slave.utf8_codepoint = 0;
        ptys[i].slave.utf8_expected = 0;
        ptys[i].slave.utf8_seen = 0;
        ptys[i].slave.fg_color = 7u;
        ptys[i].slave.bg_color = 0u;
        ptys[i].slave.bold = 0u;
        vt_clear_screen(&ptys[i].slave);
        ptys[i].slave.canonical = 1;
        ptys[i].slave.echo = 1;
        ptys[i].slave.isig = 1;
        ptys[i].slave.utf8 = 1;
        ptys[i].slave.controlling = 0;
        ptys[i].slave.session = 0;
        ptys[i].slave.foreground_pgrp = 0;
        ptys[i].master_output_head = 0;
        ptys[i].master_output_tail = 0;
        ptys[i].master_output_count = 0;
        *pty_id = i;
        return 0;
    }
    return -2;
}

int tty_pty_close(unsigned pty_id) {
    rix_pty_t *pty = pty_valid(pty_id);
    if (!pty) return -1;
    pty->opened = 0;
    return 0;
}

int tty_pty_get_termios(unsigned pty_id, rix_termios_t *termios) {
    rix_pty_t *pty = pty_valid(pty_id);
    if (!pty || !termios) return -1;
    termios->lflag = (pty->slave.canonical ? RIX_TTY_LFLAG_CANONICAL : 0u) |
                     (pty->slave.echo ? RIX_TTY_LFLAG_ECHO : 0u) |
                     (pty->slave.isig ? RIX_TTY_LFLAG_ISIG : 0u) |
                     (pty->slave.utf8 ? RIX_TTY_LFLAG_UTF8 : 0u);
    return 0;
}

int tty_pty_set_termios(unsigned pty_id, const rix_termios_t *termios) {
    rix_pty_t *pty = pty_valid(pty_id);
    if (!pty || !termios || (termios->lflag & ~(RIX_TTY_LFLAG_CANONICAL |
        RIX_TTY_LFLAG_ECHO | RIX_TTY_LFLAG_ISIG | RIX_TTY_LFLAG_UTF8))) return -1;
    pty->slave.canonical = (termios->lflag & RIX_TTY_LFLAG_CANONICAL) != 0u;
    pty->slave.echo = (termios->lflag & RIX_TTY_LFLAG_ECHO) != 0u;
    pty->slave.isig = (termios->lflag & RIX_TTY_LFLAG_ISIG) != 0u;
    pty->slave.utf8 = (termios->lflag & RIX_TTY_LFLAG_UTF8) != 0u;
    if (!pty->slave.utf8) {
        pty->slave.utf8_expected = 0;
        pty->slave.utf8_seen = 0;
        pty->slave.utf8_codepoint = 0;
    }
    return 0;
}

int tty_pty_master_write(unsigned pty_id, const void *buf, size_t n, size_t *written) {
    if (written) *written = 0;
    rix_pty_t *pty = pty_valid(pty_id);
    if (!pty || (!buf && n)) return -1;
    const uint8_t *src = (const uint8_t *)buf;
    size_t done = 0;
    while (done < n) {
        uint8_t ch = src[done];
        if (pty->slave.canonical && pty->slave.isig &&
            (ch == 0x03u || ch == 0x1au || ch == 0x1cu)) {
            int signal_rc = tty_control_signal(&pty->slave, ch);
            if (signal_rc != 0) return signal_rc;
            done++;
            continue;
        }
        if (pty->slave.canonical && (ch == 8u || ch == 127u)) {
            if (pty->slave.line_chars) {
                pty->slave.tail = (pty->slave.tail + RIX_TTY_INPUT - 1u) % RIX_TTY_INPUT;
                pty->slave.count--;
                pty->slave.line_chars--;
                (void)pty_queue_put(pty->master_output, &pty->master_output_tail,
                                    &pty->master_output_count, RIX_TTY_OUTPUT, 8u);
                (void)pty_queue_put(pty->master_output, &pty->master_output_tail,
                                    &pty->master_output_count, RIX_TTY_OUTPUT, ' ');
                (void)pty_queue_put(pty->master_output, &pty->master_output_tail,
                                    &pty->master_output_count, RIX_TTY_OUTPUT, 8u);
            }
        } else {
            if (pty->slave.count == RIX_TTY_INPUT) break;
            pty->slave.input[pty->slave.tail] = ch;
            pty->slave.tail = (pty->slave.tail + 1u) % RIX_TTY_INPUT;
            pty->slave.count++;
            if (ch == '\n') {
                if (pty->slave.canonical) pty->slave.canonical_ready++;
                pty->slave.line_chars = 0;
            } else pty->slave.line_chars++;
            if (pty->slave.echo) (void)pty_queue_put(pty->master_output,
                &pty->master_output_tail, &pty->master_output_count,
                RIX_TTY_OUTPUT, ch);
        }
        done++;
    }
    if (written) *written = done;
    return done == n ? 0 : -2;
}

int tty_pty_master_read(unsigned pty_id, void *buf, size_t n, size_t *out) {
    if (out) *out = 0;
    rix_pty_t *pty = pty_valid(pty_id);
    if (!pty || (!buf && n)) return -1;
    size_t done = pty_queue_get(pty->master_output, &pty->master_output_head,
                                &pty->master_output_count, RIX_TTY_OUTPUT,
                                (uint8_t *)buf, n);
    if (out) *out = done;
    return done ? 0 : -3;
}

int tty_pty_slave_write(unsigned pty_id, const void *buf, size_t n, size_t *written) {
    if (written) *written = 0;
    rix_pty_t *pty = pty_valid(pty_id);
    if (!pty || (!buf && n)) return -1;
    const uint8_t *src = (const uint8_t *)buf;
    size_t done = 0;
    while (done < n && pty->master_output_count < RIX_TTY_OUTPUT) {
        (void)pty_queue_put(pty->master_output, &pty->master_output_tail,
                            &pty->master_output_count, RIX_TTY_OUTPUT, src[done++]);
    }
    if (written) *written = done;
    return done == n ? 0 : -2;
}

int tty_pty_slave_read(unsigned pty_id, void *buf, size_t n, size_t *out) {
    if (out) *out = 0;
    rix_pty_t *pty = pty_valid(pty_id);
    if (!pty || (!buf && n)) return -1;
    if (pty->slave.canonical && pty->slave.canonical_ready == 0u) return -3;
    size_t done = 0;
    uint8_t *dst = (uint8_t *)buf;
    while (done < n && pty->slave.count) {
        uint8_t ch = pty->slave.input[pty->slave.head];
        dst[done++] = ch;
        pty->slave.head = (pty->slave.head + 1u) % RIX_TTY_INPUT;
        pty->slave.count--;
        if (pty->slave.canonical && ch == '\n') {
            pty->slave.canonical_ready--;
            break;
        }
    }
    if (out) *out = done;
    return done ? 0 : -3;
}
