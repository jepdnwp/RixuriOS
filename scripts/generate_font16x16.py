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
if width != 16 or height != 8 or glyph_size < height:
    raise SystemExit(f"expected a 16x8 PSF font, got {width}x{height} glyph_size={glyph_size}")
if header_size + glyph_count * glyph_size > len(blob):
    raise SystemExit("PSF glyph data is truncated")

# RixuriOS currently stores one byte per screen cell and selects 128 glyphs.
# The supplied PSF uses 16x8 glyphs; duplicate each raster row vertically so the
# existing 16x16 framebuffer cell keeps the same visual density as the old font.
rows = []
for code in range(128):
    glyph = blob[header_size + code * glyph_size: header_size + code * glyph_size + height]
    bitmap = []
    for source_row in glyph:
        value = 0
        for source_x in range(width):
            if source_row & (0x8000 >> source_x):
                value |= 1 << (15 - source_x)
        bitmap.extend([f"0x{value:04X}", f"0x{value:04X}"])
    rows.append(bitmap)

out = [
    "#pragma once",
    "#include <stdint.h>",
    "",
    "/* Generated from assets/fonts/ter-powerline-v16n.psf (PSF2, 16x8). */",
    "/* Each PSF row is doubled vertically for the existing 16x16 cell. */",
    "static const uint16_t rix_font16x16[128][16] = {",
]
for code, bitmap in enumerate(rows):
    label = chr(code) if 32 <= code < 127 else ""
    label = label.replace("\\", "\\\\").replace("'", "\\'")
    comment = f" /* {code:3d} '{label}' */" if label else f" /* {code:3d} */"
    out.append("    {" + ", ".join(bitmap) + "}," + comment)
out.append("};")
OUT_PATH.write_text("\n".join(out) + "\n")
print(f"generated {OUT_PATH} from {PSF_PATH}: {glyph_count} glyphs, {width}x{height}")
