from pathlib import Path
import struct

PSF_PATH = Path("assets/fonts/ter-powerline-v16n.psf")
OUT_PATH = Path("kernel/tty/font16x16.h")
MAGIC = 0x864AB572
CELL_WIDTH = 16
CELL_HEIGHT = 16

blob = PSF_PATH.read_bytes()
if len(blob) < 32:
    raise SystemExit("PSF file is shorter than a PSF2 header")
magic, version, header_size, flags, glyph_count, glyph_size, width, height = struct.unpack_from("<8I", blob, 0)
if magic != MAGIC:
    raise SystemExit(f"unsupported PSF magic: 0x{magic:08x}")
# This file labels itself 16x8, but its 16-byte glyph payload is the
# conventional 8x16 raster: one byte per row for sixteen rows. Use the payload
# layout, which is also what makes the supplied Terminus glyphs readable.
if not (width == 16 and height == 8 and glyph_size >= 16):
    raise SystemExit(f"expected the supplied 16-byte 8x16 payload, got {width}x{height} glyph_size={glyph_size}")
if header_size + glyph_count * glyph_size > len(blob):
    raise SystemExit("PSF glyph data is truncated")

# PSF2 stores a Unicode table after the glyph data. Each glyph's mappings end
# with one 0xFF byte; 0xFE marks a multi-codepoint sequence. Resolve ASCII
# characters instead of assuming that glyph index equals the character value.
def decode_utf8(data, offset):
    first = data[offset]
    if first < 0x80:
        return first, offset + 1
    if first < 0xE0:
        return ((first & 0x1F) << 6) | (data[offset + 1] & 0x3F), offset + 2
    if first < 0xF0:
        return ((first & 0x0F) << 12) | ((data[offset + 1] & 0x3F) << 6) | (data[offset + 2] & 0x3F), offset + 3
    return ((first & 0x07) << 18) | ((data[offset + 1] & 0x3F) << 12) | ((data[offset + 2] & 0x3F) << 6) | (data[offset + 3] & 0x3F), offset + 4

unicode_map = {}
pos = header_size + glyph_count * glyph_size
for glyph_index in range(glyph_count):
    while pos < len(blob):
        if blob[pos] == 0xFF:
            pos += 1
            break
        if blob[pos] == 0xFE:
            pos += 1
            continue
        codepoint, pos = decode_utf8(blob, pos)
        unicode_map.setdefault(codepoint, glyph_index)

# RixuriOS currently stores one byte per screen cell and selects 128 glyphs.
# The supplied PSF payload is 8x16. Scale each source pixel to a 2x1 block,
# filling the existing 16x16 framebuffer cell without introducing extra spacing.
rows = []
for code in range(128):
    glyph_index = unicode_map.get(code, code if code < glyph_count else 0)
    glyph = blob[header_size + glyph_index * glyph_size: header_size + (glyph_index + 1) * glyph_size]
    bitmap = []
    for source_y in range(16):
        source_row = glyph[source_y]
        value = 0
        for source_x in range(8):
            if source_row & (0x80 >> source_x):
                destination_x = source_x * 2
                value |= 1 << (15 - destination_x)
                value |= 1 << (15 - destination_x - 1)
        bitmap.append(f"0x{value:04X}")
    rows.append(bitmap)

out = [
    "#pragma once",
    "#include <stdint.h>",
    "",
    "/* Generated from assets/fonts/ter-powerline-v16n.psf (PSF2 payload: 8x16). */",
    "/* Source pixels are doubled horizontally for the 16x16 console cell. */",
    "static const uint16_t rix_font16x16[128][16] = {",
]
for code, bitmap in enumerate(rows):
    label = chr(code) if 32 <= code < 127 else ""
    label = label.replace("\\", "\\\\").replace("'", "\\'")
    comment = f" /* {code:3d} '{label}' */" if label else f" /* {code:3d} */"
    out.append("    {" + ", ".join(bitmap) + "}," + comment)
out.append("};")
OUT_PATH.write_text("\n".join(out) + "\n")
print(f"generated {OUT_PATH} from {PSF_PATH}: payload 8x16, mapped_ascii={sum(code in unicode_map for code in range(128))}")
