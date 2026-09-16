#pragma once
#include <stdint.h>

/* TTY console font: ter-powerline 8x16 (smaller cells than the 12x24
 * Terminus, so more columns/rows fit the same GOP mode). The renderer in
 * tty.c is generic PSF2 and reads cell geometry from the header; this is
 * the only in-tree user of the embedded font blob. */

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t flags;
    uint32_t glyph_count;
    uint32_t bytes_per_glyph;
    uint32_t height;
    uint32_t width;
} rix_psf2_header_t;

#define RIX_PSF2_MAGIC 0x864AB572u

extern const unsigned char _binary_assets_fonts_ter_powerline_v16n_psf_start[];
extern const unsigned char _binary_assets_fonts_ter_powerline_v16n_psf_end[];
