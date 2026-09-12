/* win98_widgets.c - Windows 98 drawing primitives for the SharkOS desktop.
 *
 * Everything here paints into the `lfbptr` back buffer. Nothing here touches
 * hardware; the caller flushes once per frame with flush_screen_to_hw().
 */

#include "kernel.h"
#include "win98_theme.h"

static const uint16_t w98_glyph_min[W98_GLYPH_H]     = W98_GLYPH_MIN;
static const uint16_t w98_glyph_max[W98_GLYPH_H]     = W98_GLYPH_MAX;
static const uint16_t w98_glyph_restore[W98_GLYPH_H] = W98_GLYPH_RESTORE;
static const uint16_t w98_glyph_close[W98_GLYPH_H]   = W98_GLYPH_CLOSE;

/* Clamp a rect to the screen. Returns 0 when nothing is visible. */
static int w98_clip(const w98_rect_t* in, int x, int y, int w, int h,
                    int* x0, int* y0, int* x1, int* y1) {
    int sw = (int)screen_width;
    int sh = (int)screen_height;

    int ax0 = x, ay0 = y, ax1 = x + w, ay1 = y + h;

    if (ax0 < 0) ax0 = 0;
    if (ay0 < 0) ay0 = 0;
    if (ax1 > sw) ax1 = sw;
    if (ay1 > sh) ay1 = sh;

    if (in) {
        int cx1 = in->x + in->w;
        int cy1 = in->y + in->h;
        if (ax0 < in->x) ax0 = in->x;
        if (ay0 < in->y) ay0 = in->y;
        if (ax1 > cx1) ax1 = cx1;
        if (ay1 > cy1) ay1 = cy1;
    }

    if (ax0 >= ax1 || ay0 >= ay1) return 0;
    *x0 = ax0; *y0 = ay0; *x1 = ax1; *y1 = ay1;
    return 1;
}

void w98_fill(int x, int y, int w, int h, uint32_t color) {
    draw_rect(x, y, w, h, color);
}

void w98_fill_dither(int x, int y, int w, int h) {
    int x0, y0, x1, y1;
    if (!w98_clip(NULL, x, y, w, h, &x0, &y0, &x1, &y1)) return;

    uint32_t stride = screen_pitch / 4;
    /* Paint one row as alternating pairs, then repeat. The pattern is aligned
     * to absolute screen coordinates so it stays put while windows move. */
    for (int py = y0; py < y1; py++) {
        uint32_t* row = &lfbptr[(uint32_t)py * stride];
        uint32_t first = (py & 1) ? W98_DESKTOP_DARK : W98_DESKTOP;
        uint32_t second = (py & 1) ? W98_DESKTOP : W98_DESKTOP_DARK;
        for (int px = x0; px < x1; px++) {
            row[px] = (px & 1) ? second : first;
        }
    }
}

static void w98_hline(int x, int y, int w, uint32_t c) {
    draw_rect(x, y, w, 1, c);
}

static void w98_vline(int x, int y, int h, uint32_t c) {
    draw_rect(x, y, 1, h, c);
}

void w98_bevel(int x, int y, int w, int h, w98_bevel_t style) {
    if (w < 2 || h < 2) return;

    uint32_t lt_outer, lt_inner, rb_inner, rb_outer;

    switch (style) {
    case W98_BEVEL_RAISED:
        lt_outer = W98_BTNHILITE;  lt_inner = W98_BTNLIGHT;
        rb_inner = W98_BTNSHADOW;  rb_outer = W98_BTNDKSHADOW;
        break;
    case W98_BEVEL_RAISED_PRESSED:
        lt_outer = W98_BTNDKSHADOW; lt_inner = W98_BTNSHADOW;
        rb_inner = W98_BTNLIGHT;    rb_outer = W98_BTNHILITE;
        break;
    case W98_BEVEL_SUNKEN:
        lt_outer = W98_BTNSHADOW;   lt_inner = W98_BTNDKSHADOW;
        rb_inner = W98_BTNHILITE;   rb_outer = W98_BTNLIGHT;
        break;
    case W98_BEVEL_ETCHED:
    default:
        /* Win98 etched lines are a shadow with a highlight 1px below/right. */
        w98_hline(x, y, w, W98_BTNSHADOW);
        w98_hline(x, y + 1, w, W98_BTNHILITE);
        w98_vline(x, y, h, W98_BTNSHADOW);
        w98_vline(x + 1, y, h, W98_BTNHILITE);
        return;
    }

    /* Outer ring. */
    w98_hline(x, y, w, lt_outer);
    w98_vline(x, y, h, lt_outer);
    w98_hline(x, y + h - 1, w, rb_outer);
    w98_vline(x + w - 1, y, h, rb_outer);

    /* Inner ring. */
    w98_hline(x + 1, y + 1, w - 2, lt_inner);
    w98_vline(x + 1, y + 1, h - 2, lt_inner);
    w98_hline(x + 1, y + h - 2, w - 2, rb_inner);
    w98_vline(x + w - 2, y + 1, h - 2, rb_inner);
}

void w98_surface(int x, int y, int w, int h, w98_bevel_t style, uint32_t fill) {
    w98_fill(x, y, w, h, fill);
    w98_bevel(x, y, w, h, style);
}

void w98_button(int x, int y, int w, int h, const char* label,
                bool pressed, bool enabled, bool focused) {
    w98_fill(x, y, w, h, W98_BTNFACE);
    w98_bevel(x, y, w, h,
              pressed ? W98_BEVEL_RAISED_PRESSED : W98_BEVEL_RAISED);

    if (!label) return;

    int ox = pressed ? 1 : 0;
    uint32_t fg = enabled ? W98_BTNTEXT : W98_GRAYTEXT;
    int tw = w98_text_width(label, 1);
    int th = w98_text_height(1);
    int tx = x + (w - tw) / 2 + ox;
    int ty = y + (h - th) / 2 + ox;

    if (!enabled) {
        /* Disabled text is drawn embossed: white 1px down-right, grey on top. */
        w98_text(label, tx + 1, ty + 1, W98_BTNHILITE, W98_BTNFACE, 1, NULL);
    }
    w98_text(label, tx, ty, fg, W98_BTNFACE, 1, NULL);

    if (focused) {
        /* Dotted focus rectangle, 3px inside the border. */
        for (int dx = x + 3; dx < x + w - 3; dx += 2) {
            draw_pixel(dx, y + 3, W98_BTNTEXT);
            draw_pixel(dx, y + h - 4, W98_BTNTEXT);
        }
        for (int dy = y + 3; dy < y + h - 3; dy += 2) {
            draw_pixel(x + 3, dy, W98_BTNTEXT);
            draw_pixel(x + w - 4, dy, W98_BTNTEXT);
        }
    }
}

static uint8_t w98_chan(uint32_t c, int shift) {
    return (uint8_t)((c >> shift) & 0xFF);
}

void w98_hgradient(int x, int y, int w, int h, uint32_t left, uint32_t right) {
    if (w <= 0 || h <= 0) return;

    int lr = w98_chan(left, 16), lg = w98_chan(left, 8), lb = w98_chan(left, 0);
    int rr = w98_chan(right, 16), rg = w98_chan(right, 8), rb = w98_chan(right, 0);

    int x0, y0, x1, y1;
    if (!w98_clip(NULL, x, y, w, h, &x0, &y0, &x1, &y1)) return;

    /* Ramp a single row into a scratch buffer, then stamp it down. Recomputing
     * the lerp per pixel per row costs h times more for the same result. */
    static uint32_t ramp[W98_RAMP_MAX];

    uint32_t stride = screen_pitch / 4;
    int denom = w - 1;
    if (denom < 1) denom = 1;

    int build = w;
    if (build > W98_RAMP_MAX) build = W98_RAMP_MAX;

    for (int t = 0; t < build; t++) {
        uint32_t r = (uint32_t)(lr + (rr - lr) * t / denom);
        uint32_t g = (uint32_t)(lg + (rg - lg) * t / denom);
        uint32_t b = (uint32_t)(lb + (rb - lb) * t / denom);
        ramp[t] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }

    for (int py = y0; py < y1; py++) {
        uint32_t* row = &lfbptr[(uint32_t)py * stride];
        for (int px = x0; px < x1; px++) {
            int t = px - x;
            if (t < 0) t = 0;
            if (t > build - 1) t = build - 1;
            row[px] = ramp[t];
        }
    }
}

void w98_vgradient(int x, int y, int w, int h, uint32_t top, uint32_t bottom) {
    if (w <= 0 || h <= 0) return;

    int tr = w98_chan(top, 16), tg = w98_chan(top, 8), tb = w98_chan(top, 0);
    int br = w98_chan(bottom, 16), bg = w98_chan(bottom, 8), bb = w98_chan(bottom, 0);

    int x0, y0, x1, y1;
    if (!w98_clip(NULL, x, y, w, h, &x0, &y0, &x1, &y1)) return;

    uint32_t stride = screen_pitch / 4;
    int denom = h - 1;
    if (denom < 1) denom = 1;
    int run = x1 - x0;

    for (int py = y0; py < y1; py++) {
        int t = py - y;
        if (t < 0) t = 0;
        if (t > denom) t = denom;
        uint32_t r = (uint32_t)(tr + (br - tr) * t / denom);
        uint32_t g = (uint32_t)(tg + (bg - tg) * t / denom);
        uint32_t b = (uint32_t)(tb + (bb - tb) * t / denom);
        uint32_t row_color = 0xFF000000u | (r << 16) | (g << 8) | b;
        uint32_t* row = &lfbptr[(uint32_t)py * stride + (uint32_t)x0];
        for (int px = 0; px < run; px++) {
            row[px] = row_color;
        }
    }
}

/* One 8x8 glyph at `scale`, with the trailing column filled so glyphs do not
 * run into each other. Returns the advance width. */
static void w98_glyph_cell(char c, int x, int y, uint32_t fg, uint32_t bg,
                           int scale, const w98_rect_t* clip) {
    if (c < 32 || c > 126) return;

    uint8_t glyph[8];
    for (int r = 0; r < 8; r++) glyph[r] = font8x8[c - 32][r];

    int x0, y0, x1, y1;
    if (!w98_clip(clip, x, y, 8 * scale, 8 * scale, &x0, &y0, &x1, &y1)) return;

    uint32_t stride = screen_pitch / 4;

    for (int py = y0; py < y1; py++) {
        int grow = (py - y) / scale;
        if (grow < 0 || grow > 7) continue;
        uint8_t bits = glyph[grow];
        uint32_t* row = &lfbptr[(uint32_t)py * stride];
        for (int px = x0; px < x1; px++) {
            int gcol = (px - x) / scale;
            uint32_t color;
            if (gcol < 0 || gcol > 7) {
                color = bg;                       /* padding column */
            } else {
                color = ((bits >> (7 - gcol)) & 1) ? fg : bg;
            }
            row[px] = color;
        }
    }
}

int w98_text_width(const char* s, int scale) {
    if (scale < 1) scale = 1;
    int n = 0;
    while (s && s[n]) n++;
    return n * 6 * scale;
}

int w98_text_height(int scale) {
    if (scale < 1) scale = 1;
    return 8 * scale;
}

void w98_text(const char* s, int x, int y, uint32_t fg, uint32_t bg,
              int scale, const w98_rect_t* clip) {
    if (!s) return;
    if (scale < 1) scale = 1;
    for (int i = 0; s[i]; i++) {
        w98_glyph_cell(s[i], x + i * 6 * scale, y, fg, bg, scale, clip);
    }
}

void w98_text_bold(const char* s, int x, int y, uint32_t fg, uint32_t bg,
                   int scale, const w98_rect_t* clip) {
    if (!s) return;
    w98_text(s, x + 1, y, fg, bg, scale, clip);
    w98_text(s, x, y, fg, bg, scale, clip);
}

void w98_text_outline(const char* s, int x, int y, uint32_t fg, int scale,
                      const w98_rect_t* clip) {
    if (!s) return;
    uint32_t halo = W98_BTNDKSHADOW;
    w98_text(s, x - 1, y,     halo, halo, scale, clip);
    w98_text(s, x + 1, y,     halo, halo, scale, clip);
    w98_text(s, x,     y - 1, halo, halo, scale, clip);
    w98_text(s, x,     y + 1, halo, halo, scale, clip);
    w98_text(s, x,     y,     fg,   halo, scale, clip);
}

void w98_text_vertical(const char* s, int x, int y_bottom, uint32_t fg,
                       int scale, const w98_rect_t* clip) {
    if (!s) return;
    if (scale < 1) scale = 1;

    int n = 0;
    while (s[n]) n++;
    if (n == 0) return;

    /* The string runs bottom-to-top, so the first character ends up lowest. */
    int advance = 6 * scale;
    int total = n * advance;
    int y = y_bottom - total;

    int x0, y0, x1, y1;
    if (!w98_clip(clip, x, y, 8 * scale, total, &x0, &y0, &x1, &y1)) return;

    uint32_t stride = screen_pitch / 4;
    int thick = 8 * scale;

    for (int py = y0; py < y1; py++) {
        int along = py - y;                 /* distance up the string  */
        int gidx = along / advance;         /* which character         */
        int gcol_row = along % advance;     /* row within that glyph   */
        if (gidx < 0 || gidx >= n) continue;
        int grow = gcol_row / scale;
        if (grow < 0 || grow > 7) continue;

        /* Transpose: glyph row becomes the horizontal axis, MSB at the top
         * of the rotated string, i.e. the right-hand side on screen. */
        uint8_t bits = font8x8[s[gidx] - 32][grow];
        uint32_t* row = &lfbptr[(uint32_t)py * stride];
        for (int px = x0; px < x1; px++) {
            int off = px - x;
            int gcol = thick - 1 - off;
            if (gcol < 0 || gcol > 7) continue;
            if ((bits >> gcol) & 1) {
                row[px] = fg;
            }
        }
    }
}

void w98_text_fit(const char* src, char* dst, int dstlen, int maxw, int scale) {
    if (!src || !dst || dstlen <= 0) return;
    if (dstlen == 1) { dst[0] = '\0'; return; }

    int adv = 6 * (scale < 1 ? 1 : scale);
    int avail = maxw / adv;
    if (avail < 1) avail = 1;

    int n = 0;
    while (src[n] && n < dstlen - 1) n++;

    if (n <= avail) {
        for (int i = 0; i < n; i++) dst[i] = src[i];
        dst[n] = '\0';
        return;
    }

    int keep = avail - 1;
    if (keep < 1) keep = 1;
    if (keep > dstlen - 2) keep = dstlen - 2;
    for (int i = 0; i < keep; i++) dst[i] = src[i];
    dst[keep] = '.';
    dst[keep + 1] = '\0';
}

void w98_glyph(int kind, int x, int y, uint32_t fg) {
    const uint16_t* g;
    switch (kind) {
    case W98_GLYPHKIND_MIN:     g = w98_glyph_min;     break;
    case W98_GLYPHKIND_MAX:     g = w98_glyph_max;     break;
    case W98_GLYPHKIND_RESTORE: g = w98_glyph_restore; break;
    case W98_GLYPHKIND_CLOSE:   g = w98_glyph_close;   break;
    default: return;
    }

    for (int row = 0; row < W98_GLYPH_H; row++) {
        for (int col = 0; col < W98_GLYPH_W; col++) {
            if (g[row] & (1u << (W98_GLYPH_W - 1 - col))) {
                draw_pixel(x + col, y + row, fg);
            }
        }
    }
}

void w98_icon_blit(const uint32_t* src, int src_size, int dst_size,
                   int x, int y, const w98_rect_t* clip) {
    if (!src || src_size < 1 || dst_size < 1) return;

    int x0, y0, x1, y1;
    if (!w98_clip(clip, x, y, dst_size, dst_size, &x0, &y0, &x1, &y1)) return;

    uint32_t stride = screen_pitch / 4;

    for (int py = y0; py < y1; py++) {
        int sy = (py - y) * src_size / dst_size;
        if (sy < 0) sy = 0;
        if (sy >= src_size) sy = src_size - 1;
        const uint32_t* srow = &src[(uint32_t)sy * (uint32_t)src_size];
        uint32_t* drow = &lfbptr[(uint32_t)py * stride];
        for (int px = x0; px < x1; px++) {
            int sx = (px - x) * src_size / dst_size;
            if (sx < 0) sx = 0;
            if (sx >= src_size) sx = src_size - 1;
            uint32_t p = srow[sx];
            if ((p >> 24) > 128) {
                drow[px] = 0xFF000000u | (p & 0x00FFFFFFu);
            }
        }
    }
}

void w98_icon_blit_small(const uint32_t* src, int src_size, int dst_size,
                         int x, int y, const w98_rect_t* clip) {
    if (!src || src_size < 1 || dst_size < 1) return;

    int x0, y0, x1, y1;
    if (!w98_clip(clip, x, y, dst_size, dst_size, &x0, &y0, &x1, &y1)) return;

    uint32_t stride = screen_pitch / 4;
    int step = src_size / dst_size;
    if (step < 1) step = 1;

    for (int py = y0; py < y1; py++) {
        int sy = (py - y) * src_size / dst_size;
        uint32_t* drow = &lfbptr[(uint32_t)py * stride];
        for (int px = x0; px < x1; px++) {
            int sx = (px - x) * src_size / dst_size;

            unsigned int r = 0, g = 0, b = 0, a = 0, n = 0;
            for (int oy = 0; oy < step; oy++) {
                int yy = sy + oy;
                if (yy >= src_size) break;
                const uint32_t* srow = &src[(uint32_t)yy * (uint32_t)src_size];
                for (int ox = 0; ox < step; ox++) {
                    int xx = sx + ox;
                    if (xx >= src_size) break;
                    uint32_t p = srow[xx];
                    uint32_t pa = (p >> 24) & 0xFF;
                    if (pa <= 128) continue;
                    r += (p >> 16) & 0xFF;
                    g += (p >> 8) & 0xFF;
                    b += p & 0xFF;
                    a += pa;
                    n++;
                }
            }

            if (n == 0) continue;
            uint32_t avg_a = a / n;
            if (avg_a <= 128) continue;
            drow[px] = 0xFF000000u | ((r / n) << 16) | ((g / n) << 8) | (b / n);
        }
    }
}
