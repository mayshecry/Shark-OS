#!/usr/bin/env python3
"""Convert an image into a SharkOS embedded wallpaper header.

Usage:
    python3 tools/make_wallpaper.py IMAGE NAME [--size W H] [--add-thumb]

Writes include/wallpaper_NAME_data.h with a raw 0xAARRGGBB pixel array
(cover-cropped to --size, default 800x450), optionally appends a 72x45
thumbnail to include/wallpaper_thumbs_data.h, and prints the exact C
snippets to paste into include/desktop.h and src/desktop/desktop.c.

Needs: pip install pillow
"""
import argparse
import os
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("need Pillow: pip install pillow")

THUMB_W, THUMB_H = 72, 45


def cover_crop(im, w, h):
    sw, sh = im.size
    s = max(w / sw, h / sh)
    im = im.resize((int(sw * s + 0.5), int(sh * s + 0.5)), Image.LANCZOS)
    x, y = (im.width - w) // 2, (im.height - h) // 2
    return im.crop((x, y, x + w, y + h))


def dump_array(f, arrname, im):
    px = im.load()
    w, h = im.size
    f.write("static const unsigned int %s[] = {\n" % arrname)
    row = []
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y][:3]
            row.append("0xff%02x%02x%02xu" % (r, g, b))
            if len(row) == 8:
                f.write("    " + ", ".join(row) + ",\n")
                row = []
    if row:
        f.write("    " + ", ".join(row) + ",\n")
    f.write("};\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("name", help="lowercase identifier, e.g. 'vapor'")
    ap.add_argument("--size", nargs=2, type=int, default=(800, 450),
                    metavar=("W", "H"))
    ap.add_argument("--add-thumb", action="store_true",
                    help="also append a thumbnail to wallpaper_thumbs_data.h")
    a = ap.parse_args()

    name = a.name.lower()
    W, H = a.size
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    im = Image.open(a.image).convert("RGB")
    im = cover_crop(im, W, H)

    out = os.path.join(repo, "include", "wallpaper_%s_data.h" % name)
    up = name.upper()
    with open(out, "w") as f:
        f.write("#ifndef WALLPAPER_%s_DATA_H\n" % up)
        f.write("#define WALLPAPER_%s_DATA_H\n\n" % up)
        f.write("#define %s_WIDTH %d\n" % (up, W))
        f.write("#define %s_HEIGHT %d\n\n" % (up, H))
        dump_array(f, name + "_pixels", im)
        f.write("\n#endif\n")
    print("wrote", os.path.relpath(out, repo))

    if a.add_thumb:
        tp = os.path.join(repo, "include", "wallpaper_thumbs_data.h")
        thumb = im.resize((THUMB_W, THUMB_H), Image.LANCZOS)
        import io
        buf = io.StringIO()
        dump_array(buf, "wallpaper_thumb_%s" % name, thumb)
        src = open(tp).read()
        if "wallpaper_thumb_%s[" % name in src:
            print("thumb for '%s' already present, skipping" % name)
        else:
            src = src.replace("#endif", buf.getvalue() + "\n#endif", 1)
            open(tp, "w").write(src)
            print("appended thumbnail to", os.path.relpath(tp, repo))

    print("""
Now paste into include/desktop.h (bump the count too):

    #define WP_ID_%s %d

and into src/desktop/desktop.c:

    #include "wallpaper_%s_data.h"
    ...
    { "<Display Name>", %s_pixels,
      %s_WIDTH, %s_HEIGHT, wallpaper_thumb_%s },
""" % (up, -1, name, name, up, up, name))
    print("(pick the WP_ID value: current WP_COUNT in desktop.h,")
    print(" then increment WP_COUNT to match)")


if __name__ == "__main__":
    main()
