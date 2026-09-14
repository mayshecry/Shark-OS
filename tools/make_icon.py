#!/usr/bin/env python3
"""Convert a PNG (any size, alpha kept) into a SharkOS 32x32 icon C array.

Usage:
    python3 tools/make_icon.py IMAGE NAME [--modern]

Prints `icon_data_NAME[32*32]` (classic set) or `modern_icon_data_NAME`
ready to paste into include/icon_data.h / include/modern_icon_data.h.
Alpha 0x00 pixels are transparent.

Needs: pip install pillow
"""
import argparse
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("need Pillow: pip install pillow")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("name")
    ap.add_argument("--modern", action="store_true")
    a = ap.parse_args()

    im = Image.open(a.image).convert("RGBA").resize((32, 32), Image.LANCZOS)
    px = im.load()
    prefix = "modern_icon_data_" if a.modern else "icon_data_"
    print("static const uint32_t %s%s[32 * 32] = {" % (prefix, a.name.lower()))
    row = []
    for y in range(32):
        for x in range(32):
            r, g, b, al = px[x, y]
            row.append("0x%02x%02x%02x%02x" % (al, r, g, b))
            if len(row) == 8:
                print("    " + ", ".join(row) + ",")
                row = []
    if row:
        print("    " + ", ".join(row) + ",")
    print("};")


if __name__ == "__main__":
    main()
