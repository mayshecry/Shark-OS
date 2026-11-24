#!/usr/bin/env python3
"""Render a PNG preview of what `imgfetch` prints, using the real SharkOS 8x16 font."""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_imgfetch import VGA, LOGO_SIZE

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def load_font():
    path = os.path.join(ROOT, "include", "term_font16_data.h")
    text = open(path).read()
    body = text[text.index("{", text.index("term_font8x16")):]
    rows = re.findall(r"\{([^{}]*)\}", body)
    font = []
    for r in rows:
        vals = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", r)]
        if len(vals) == 16:
            font.append(vals)
    return font[:95]


def decode_art(img_c, cols):
    cells = []
    line = []
    i = 0
    while i < len(img_c):
        run, glyph, fg, bg = img_c[i:i + 4]
        i += 4
        if run == 0:
            cells.append(line)
            line = []
            continue
        for _ in range(run):
            line.append((chr(glyph), fg, bg))
    return cells


def render(cells, scale=1):
    font = load_font()
    h = len(cells)
    w = max(len(r) for r in cells)
    img = Image.new("RGB", (w * 8, h * 16), (0, 0, 0))
    px = img.load()
    for y, row in enumerate(cells):
        for x, (ch, fg, bg) in enumerate(row):
            glyph = font[ord(ch) - 32]
            for gy in range(16):
                bits = glyph[gy]
                for gx in range(8):
                    on = bits & (0x80 >> gx)
                    px[x * 8 + gx, y * 16 + gy] = VGA[fg] if on else VGA[bg]
    if scale != 1:
        img = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
    return img


def main():
    import json
    argv = sys.argv[1:]
    cols = int(argv[0]) if argv else 64
    out = argv[1] if len(argv) > 1 else "/home/user/preview_imgfetch.png"

    from gen_imgfetch import build_art
    base = Image.open(os.path.join(ROOT, "assets", "imgfetch_logo.png")).convert("RGB")
    cells = build_art(base, cols)
    img = render(cells, scale=3)
    img.save(out)
    print("wrote", out, img.size)


if __name__ == "__main__":
    main()
