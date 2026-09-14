#!/usr/bin/env python3
"""Rasterise a TTF into the SharkOS 8x16 terminal font header.

Usage:
    python3 tools/make_termfont.py [TTF_PATH] [OUT_HEADER]

Defaults to DejaVu Sans Mono -> include/term_font16_data.h.
Glyphs 32..126 are rendered 4x supersampled and box-filtered down into
8x16 cells with a shared baseline and monospace advance placement.
"""
import sys
from PIL import Image, ImageDraw, ImageFont

TTF = sys.argv[1] if len(sys.argv) > 1 else \
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
OUT = sys.argv[2] if len(sys.argv) > 2 else "include/term_font16_data.h"

SIZE = 13
SS = 4                      # supersample factor
big = SIZE * SS
font = ImageFont.truetype(TTF, big)

top_H = font.getbbox("H")[1]          # ascender ink top at origin
y_draw = SS * 1 - top_H               # put ascender line at cell row 1

glyphs = []
for ch in range(32, 127):
    cw = int(font.getlength("M") + SS)  # advance in super-pixels
    canvas = Image.new("L", (cw + 8, 16 * SS + 8), 0)
    d = ImageDraw.Draw(canvas)
    d.text((2, y_draw + 2), chr(ch), font=font, fill=255)
    px = canvas.load()
    cell = [0] * 16
    for r in range(16):
        byte = 0
        for c in range(8):
            acc = 0
            for sy in range(SS):
                yy = 2 + r * SS + sy
                for sx in range(SS):
                    xx = 2 + c * SS + sx
                    acc += px[xx, yy]
            if acc > (SS * SS * 255) // 2:
                byte |= 1 << (7 - c)
        cell[r] = byte
    glyphs.append(cell)

with open(OUT, "w") as f:
    f.write("#ifndef TERM_FONT16_DATA_H\n#define TERM_FONT16_DATA_H\n\n")
    f.write("/* 8x16 terminal font, glyphs 32..126 (see tools/make_termfont.py) */\n")
    f.write("static const unsigned char term_font8x16[95][16] = {\n")
    for gi, g in enumerate(glyphs):
        f.write("    { " + ", ".join("0x%02X" % row for row in g) + " },  "
                "/* %s */\n" % repr(chr(32 + gi)))
    f.write("};\n\n#endif\n")
print("wrote", OUT)
