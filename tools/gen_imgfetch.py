#!/usr/bin/env python3
"""Generate include/imgfetch_data.h for SharkOS `imgfetch`.

The logo is treated as a 500x500 source (assets/imgfetch_logo.png) and
down-sampled to several terminal-cell resolutions.

Each cell is 8x16 pixels on screen.  The source block behind a cell is reduced
to that 8x16 grid, split into two VGA palette colours, and turned into an ASCII
shading glyph whose ink coverage matches how much of the cell belongs to the
foreground colour (slightly contrast-stretched so lettering stays readable).
That is the sharpest a glyph-only terminal can do: no half-block or Unicode
characters exist in the SharkOS font.

Data is stored run-length encoded as (run_len, glyph, fg, bg) quadruples.

Usage:  python3 tools/gen_imgfetch.py
"""

import os
import sys

try:
    import numpy as np
    from PIL import Image, ImageFilter
except ImportError:
    sys.exit("numpy and pillow are required: pip install numpy pillow")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_IMAGE = os.path.join(ROOT, "assets", "imgfetch_logo.png")
OUT_HEADER = os.path.join(ROOT, "include", "imgfetch_data.h")

LOGO_SIZE = 500


VGA = [
    (0x00, 0x00, 0x00),
    (0x00, 0x00, 0xAA),
    (0x00, 0xAA, 0x00),
    (0x00, 0xAA, 0xAA),
    (0xAA, 0x00, 0x00),
    (0xAA, 0x00, 0xAA),
    (0xAA, 0x55, 0x00),
    (0xAA, 0xAA, 0xAA),
    (0x55, 0x55, 0x55),
    (0x55, 0x55, 0xFF),
    (0x55, 0xFF, 0x55),
    (0x55, 0xFF, 0xFF),
    (0xFF, 0x55, 0x55),
    (0xFF, 0x55, 0xFF),
    (0xFF, 0xFF, 0x55),
    (0xFF, 0xFF, 0xFF),
]
VGA_NP = np.array(VGA, dtype=np.float32)

RGBW = np.array([0.6, 1.3, 0.4], dtype=np.float32)


RAMP = " .:-=+*#%@"



CELL_ASPECT = 0.5
GLYPH_W, GLYPH_H = 8, 16



WIDTHS = [96, 80, 64, 56, 48, 40, 32, 24, 20, 16]



MIN_FG_FRACTION = 0.18



COVERAGE_CONTRAST = 1.6


FLAT_THRESHOLD = 30.0




PIXEL_SIZES = [160, 320]
PIXEL_COLORS = 24


def to_vga_index(colour):
    d = ((VGA_NP - np.asarray(colour, dtype=np.float32)) ** 2 * RGBW).sum(axis=1)
    return int(np.argmin(d))


def two_colours(pixels):
    """Split the samples of one cell into two continuous colour centres."""
    lum = (pixels * RGBW).sum(axis=1)
    lo, hi = pixels[np.argmin(lum)], pixels[np.argmax(lum)]
    if np.abs(hi - lo).sum() < FLAT_THRESHOLD:
        return None
    ca, cb = lo.copy(), hi.copy()
    for _ in range(6):
        da = ((pixels - ca) ** 2 * RGBW).sum(axis=1)
        db = ((pixels - cb) ** 2 * RGBW).sum(axis=1)
        in_a = da <= db
        if in_a.all() or not in_a.any():
            break
        ca = pixels[in_a].mean(axis=0)
        cb = pixels[~in_a].mean(axis=0)
    if np.abs(ca - cb).sum() < FLAT_THRESHOLD:
        return None
    return ca, cb


def match_cell(block):
    """Pick (glyph, fg_vga, bg_vga) for one 16x8 block of RGB floats."""
    pixels = block.reshape(-1, 3)
    centres = two_colours(pixels)
    if centres is None:
        flat = to_vga_index(pixels.mean(axis=0))
        return " ", flat, flat

    ca, cb = centres
    da = ((pixels - ca) ** 2 * RGBW).sum(axis=1)
    db = ((pixels - cb) ** 2 * RGBW).sum(axis=1)
    count_a = int((da <= db).sum())

    fg_centre, bg_centre = (ca, cb) if len(pixels) - count_a > count_a else (cb, ca)

    d_fg = ((pixels - fg_centre) ** 2 * RGBW).sum(axis=1)
    d_bg = ((pixels - bg_centre) ** 2 * RGBW).sum(axis=1)
    frac = float((d_fg < d_bg).mean())

    fg = to_vga_index(fg_centre)
    bg = to_vga_index(bg_centre)
    if fg == bg or frac < MIN_FG_FRACTION:
        return " ", bg, bg

    stretched = min(1.0, max(0.0, (frac - 0.5) * COVERAGE_CONTRAST + 0.5))
    level = int(round(stretched * (len(RAMP) - 1)))
    return RAMP[min(len(RAMP) - 1, level)], fg, bg


def build_art(img, cols):
    rows = max(1, round(cols * CELL_ASPECT))
    cell_w = LOGO_SIZE / cols
    cell_h = LOGO_SIZE / rows
    art = []
    for row in range(rows):
        y0 = int(round(row * cell_h))
        y1 = max(y0 + 1, int(round((row + 1) * cell_h)))
        line = []
        for col in range(cols):
            x0 = int(round(col * cell_w))
            x1 = max(x0 + 1, int(round((col + 1) * cell_w)))
            block = np.asarray(
                img.crop((x0, y0, x1, y1)).resize((GLYPH_W, GLYPH_H), Image.BOX),
                dtype=np.float32,
            )
            line.append(match_cell(block))
        art.append(line)
    return art


def _pixel_indices(img, size, palette_rgb):
    """Down-sample to `size`, denoise, and map onto the shared palette."""
    from collections import Counter

    small = img.resize((size, size), Image.LANCZOS)
    arr = np.asarray(small).astype(np.int16)




    modal = np.array(Counter(map(tuple, arr.reshape(-1, 3))).most_common(1)[0][0], dtype=np.int16)
    distance = np.abs(arr - modal).sum(axis=2)
    snapped = np.where((distance < 48)[..., None], modal, arr).astype(np.uint8)
    smoothed = Image.fromarray(snapped).filter(ImageFilter.GaussianBlur(1.0))

    pixels = np.asarray(smoothed).reshape(-1, 3).astype(np.int32)
    palette = np.asarray(palette_rgb, dtype=np.int32)

    index = np.empty(len(pixels), dtype=np.uint8)
    step = 4096
    for i in range(0, len(pixels), step):
        chunk = pixels[i:i + step]
        d = ((chunk[:, None, :] - palette[None, :, :]) ** 2 * RGBW).sum(axis=2)
        index[i:i + step] = np.argmin(d, axis=1)
    return index


def build_pixel_palette(img):
    """One palette shared by every pixel resolution."""
    size = max(PIXEL_SIZES)
    small = img.resize((size, size), Image.LANCZOS)
    quant = small.quantize(colors=PIXEL_COLORS, method=Image.MEDIANCUT, dither=Image.NONE)
    raw = quant.getpalette()[:PIXEL_COLORS * 3]
    palette = [tuple(raw[i:i + 3]) for i in range(0, len(raw), 3)]
    while len(palette) < PIXEL_COLORS:
        palette.append((0, 0, 0))
    return palette


def build_pixel_data(img, palette, size):
    """Returns (run_length, palette_index) pairs in row-major order."""
    indices = _pixel_indices(img, size, palette)
    pairs = []
    current = int(indices[0])
    run = 1
    for value in indices[1:]:
        value = int(value)
        if value == current and run < 255:
            run += 1
        else:
            pairs.append((run, current))
            current = value
            run = 1
    pairs.append((run, current))
    return pairs


def encode_rle(art):
    out = []
    for line in art:
        run_glyph = None
        run_fg = run_bg = 0
        run = 0
        for glyph, fg, bg in line:
            if (glyph, fg, bg) == (run_glyph, run_fg, run_bg) and run < 255:
                run += 1
                continue
            if run:
                out += [run, ord(run_glyph), run_fg, run_bg]
            run_glyph, run_fg, run_bg, run = glyph, fg, bg, 1
        if run:
            out += [run, ord(run_glyph), run_fg, run_bg]

        out += [0, 0, 0, 0]
    return out


def c_array(name, values, per_line=16):
    lines = ["static const uint8_t %s[] = {" % name]
    for i in range(0, len(values), per_line):
        chunk = ", ".join("0x%02X" % v for v in values[i:i + per_line])
        lines.append("    %s," % chunk)
    lines.append("};")
    return "\n".join(lines)


def main():
    if not os.path.exists(SRC_IMAGE):
        sys.exit("missing source image: %s" % SRC_IMAGE)

    base = Image.open(SRC_IMAGE).convert("RGB")
    if base.size != (LOGO_SIZE, LOGO_SIZE):
        base = base.resize((LOGO_SIZE, LOGO_SIZE), Image.LANCZOS)

    body = []
    entries = []
    for cols in WIDTHS:
        art = build_art(base, cols)
        rle = encode_rle(art)
        name = "imgfetch_art_%d" % cols
        body.append(c_array(name, rle))
        entries.append((cols, len(art), name, len(rle)))

    header = []
    header.append("#ifndef IMGFETCH_DATA_H")
    header.append("#define IMGFETCH_DATA_H")
    header.append("")
    header.append("#define IMGFETCH_LOGO_SIZE %d" % LOGO_SIZE)
    header.append("")
    header.append("typedef struct {")
    header.append("    uint16_t cols;")
    header.append("    uint16_t rows;")
    header.append("    const uint8_t* rle;")
    header.append("    uint32_t rle_len;")
    header.append("} imgfetch_art_t;")
    header.append("")
    palette = build_pixel_palette(base)
    pal_entries = ["0xFF%02X%02X%02X" % rgb for rgb in palette]
    pixel_entries = []
    for size in PIXEL_SIZES:
        pixel_bytes = []
        for run, value in build_pixel_data(base, palette, size):
            pixel_bytes += [run, value]
        name = "imgfetch_pixel_rle_%d" % size
        body.append(c_array(name, pixel_bytes))
        pixel_entries.append((size, name, len(pixel_bytes)))

    header_pixel = []
    header_pixel.append("#define IMGFETCH_PIXEL_COLORS %d" % PIXEL_COLORS)
    header_pixel.append("")
    header_pixel.append("static const uint32_t imgfetch_pixel_palette[%d] = {" % PIXEL_COLORS)
    for i in range(0, len(pal_entries), 4):
        header_pixel.append("    " + ", ".join(pal_entries[i:i + 4]) + ",")
    header_pixel.append("};")
    header_pixel.append("")
    header_pixel.append("typedef struct {")
    header_pixel.append("    uint16_t size;")
    header_pixel.append("    const uint8_t* rle;")
    header_pixel.append("    uint32_t rle_len;")
    header_pixel.append("} imgfetch_pixel_t;")
    header_pixel.append("")
    header_pixel.append("static const imgfetch_pixel_t imgfetch_pixels[] = {")
    for size, name, length in pixel_entries:
        header_pixel.append("    {%d, %s, %d}," % (size, name, length))
    header_pixel.append("};")
    header_pixel.append("#define IMGFETCH_PIXEL_COUNT %d" % len(pixel_entries))
    header_pixel.append("")

    header.append("\n\n".join(body))
    header.append("")
    header.extend(header_pixel)
    header.append("")
    header.append("static const imgfetch_art_t imgfetch_arts[] = {")
    for cols, rows, name, rle_len in entries:
        header.append("    {%d, %d, %s, %d}," % (cols, rows, name, rle_len))
    header.append("};")
    header.append("#define IMGFETCH_ART_COUNT %d" % len(entries))
    header.append("")
    header.append("#endif")
    header.append("")

    with open(OUT_HEADER, "w") as fh:
        fh.write("\n".join(header))

    total = sum(e[3] for e in entries)
    print("wrote %s (%d art sizes, %d RLE bytes)" % (OUT_HEADER, len(entries), total))


if __name__ == "__main__":
    main()
