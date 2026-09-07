#pragma once
#include <stdint.h>

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

extern const unsigned char _binary_assets_fonts_terminus_12x24_psf_start[];
extern const unsigned char _binary_assets_fonts_terminus_12x24_psf_end[];
