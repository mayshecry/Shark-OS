/* TrueType font engine for the SharkOS browser.
 *
 * Parses the glyf/loca/cmap/hmtx tables of a TrueType font (embedded DejaVu
 * faces, or @font-face fonts fetched from the web), flattens the quadratic
 * outlines and rasterises them with an exact-area anti-aliasing scanline
 * filler. Everything is integer / fixed point (the kernel has no FPU):
 * outline coordinates are 26.6 pixels, the coverage accumulator is 16.16.
 *
 * Rendered glyphs are cached (bitmap per face/size/glyph) in a static arena,
 * so text painting is a memcpy-speed blit with alpha blending.
 *
 * Supported: cmap formats 4 and 12 (Unicode BMP + full range), simple and
 * composite glyphs, unhinted rendering, font sizes 6..96 px.
 * Not supported: CFF/OpenType outlines, variable fonts, hinting, kerning. */

#ifdef FONT_HOST_TEST
#include <stdint.h>
#include <string.h>
#include <stddef.h>
extern uint32_t* lfbptr; extern uint64_t screen_width, screen_height, screen_pitch;
static int br_streq(const char* a, const char* b) { return strcmp(a, b) == 0; }
#else
#include "browser_internal.h"
#endif
#include "font.h"
#include "font_data.h"

/* ---------------------------------------------------------- big-endian */

static uint32_t rd16(const uint8_t* p) { return ((uint32_t)p[0] << 8) | p[1]; }
static int32_t  rds16(const uint8_t* p) { return (int16_t)rd16(p); }
static uint32_t rd32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

/* --------------------------------------------------------------- faces */

#define FONT_MAX_FACES 12                 /* 6 built-in + up to 6 web fonts per page */

typedef struct {
    const uint8_t* data;
    uint32_t len;
    /* table offsets */
    uint32_t loca, glyf, cmap, hmtx, hhea, head, maxp;
    uint32_t glyf_len, loca_len;
    int loca_long;
    int upem;
    int num_glyphs;
    int num_hmetrics;
    int ascent, descent, line_gap;        /* font units */
    int loaded;
    int web;                              /* 1 = @font-face font, discarded per page */
    char family[40];                      /* @font-face family name, lowercase */
    int bold, italic;
} face_t;

static face_t faces[FONT_MAX_FACES];
static int face_count = 0;

/* web font storage: fetched font files live here for the page's lifetime */
static uint8_t webfont_arena[FONT_WEB_ARENA];
static uint32_t webfont_used = 0;

static uint32_t find_table(const uint8_t* d, uint32_t len, const char* tag, uint32_t* out_len) {
    if (len < 12) return 0;
    uint32_t n = rd16(d + 4);
    if (n > 64) n = 64;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t* rec = d + 12 + i * 16;
        if ((uint32_t)(rec + 16 - d) > len) return 0;
        if (rec[0] == tag[0] && rec[1] == tag[1] && rec[2] == tag[2] && rec[3] == tag[3]) {
            uint32_t off = rd32(rec + 8), tl = rd32(rec + 12);
            if (off >= len || off + tl > len) return 0;
            if (out_len) *out_len = tl;
            return off;
        }
    }
    return 0;
}

static int face_load(face_t* f, const uint8_t* data, uint32_t len) {
    memset(f, 0, sizeof(*f));
    f->data = data; f->len = len;
    uint32_t tag = len >= 4 ? rd32(data) : 0;
    if (tag == 0x74746366u) {                      /* 'ttcf': use the first font */
        if (len < 16) return 0;
        uint32_t off = rd32(data + 12);
        if (off + 12 > len) return 0;
        data += off; len -= off; f->data = data; f->len = len;
        tag = rd32(data);
    }
    if (tag != 0x00010000u && tag != 0x74727565u) return 0;   /* not TrueType (CFF 'OTTO' etc.) */
    uint32_t hl;
    f->head = find_table(data, len, "head", &hl); if (!f->head || hl < 54) return 0;
    f->maxp = find_table(data, len, "maxp", &hl); if (!f->maxp || hl < 6) return 0;
    f->hhea = find_table(data, len, "hhea", &hl); if (!f->hhea || hl < 36) return 0;
    f->hmtx = find_table(data, len, "hmtx", &hl); if (!f->hmtx) return 0;
    f->cmap = find_table(data, len, "cmap", &hl); if (!f->cmap) return 0;
    f->loca = find_table(data, len, "loca", &f->loca_len); if (!f->loca) return 0;
    f->glyf = find_table(data, len, "glyf", &f->glyf_len); if (!f->glyf) return 0;
    f->upem = (int)rd16(data + f->head + 18); if (f->upem < 16 || f->upem > 16384) return 0;
    f->loca_long = rds16(data + f->head + 50) != 0;
    f->num_glyphs = (int)rd16(data + f->maxp + 4);
    f->num_hmetrics = (int)rd16(data + f->hhea + 34);
    f->ascent = rds16(data + f->hhea + 4);
    f->descent = rds16(data + f->hhea + 6);
    f->line_gap = rds16(data + f->hhea + 8);
    /* sanity: loca must cover all glyphs */
    uint32_t need = (uint32_t)(f->num_glyphs + 1) * (f->loca_long ? 4 : 2);
    if (f->loca_len < need) f->num_glyphs = (int)(f->loca_len / (f->loca_long ? 4 : 2)) - 1;
    if (f->num_glyphs < 1) return 0;
    f->loaded = 1;
    return 1;
}

void br_font_init(void) {
    if (face_count) return;
    static const struct { const unsigned char* d; unsigned len; } builtin[6] = {
        { font_sans, sizeof(font_sans) }, { font_sans_bold, sizeof(font_sans_bold) },
        { font_serif, sizeof(font_serif) }, { font_serif_bold, sizeof(font_serif_bold) },
        { font_mono, sizeof(font_mono) }, { font_mono_bold, sizeof(font_mono_bold) },
    };
    for (int i = 0; i < 6; i++) { face_load(&faces[i], builtin[i].d, builtin[i].len); faces[i].bold = i & 1; }
    face_count = 6;
}

/* ---------------------------------------------------------- glyph cache */

typedef struct {
    int16_t face, size;
    uint16_t glyph;
    int16_t x0, y0;                       /* bitmap origin relative to pen (y down) */
    uint16_t w, h;
    int16_t advance;                      /* pixels */
    uint32_t bitmap;                      /* offset into cache_pixels */
    uint32_t lru;
} glyph_cache_t;

#define GC_ENTRIES 1024
static glyph_cache_t gcache[GC_ENTRIES];
static uint8_t cache_pixels[FONT_CACHE_BYTES];
static uint32_t cache_used = 0;
static uint32_t cache_tick = 0;
static int cache_count = 0;

static uint16_t gc_hash[GC_ENTRIES * 4];
void br_font_cache_flush(void) {
    cache_used = 0; cache_count = 0;
    memset(gcache, 0, sizeof(gcache));
    memset(gc_hash, 0, sizeof(gc_hash));
}

void br_font_reset_page(void) {
    /* drop web fonts; keep the built-in faces and their cached glyphs */
    int had_web = 0;
    for (int i = 6; i < face_count; i++) if (faces[i].web) had_web = 1;
    face_count = 6;
    webfont_used = 0;
    if (had_web) br_font_cache_flush();
}

/* --------------------------------------------------------------- cmap */

static int cmap_lookup(const face_t* f, uint32_t cp) {
    const uint8_t* d = f->data;
    const uint8_t* cm = d + f->cmap;
    if (f->cmap + 4 > f->len) return 0;
    uint32_t n = rd16(cm + 2);
    uint32_t best = 0; int best_score = -1;
    for (uint32_t i = 0; i < n && i < 16; i++) {
        const uint8_t* rec = cm + 4 + i * 8;
        if ((uint32_t)(rec + 8 - d) > f->len) break;
        uint32_t pid = rd16(rec), eid = rd16(rec + 2), off = rd32(rec + 4);
        if (f->cmap + off + 4 > f->len) continue;
        uint32_t fmt = rd16(d + f->cmap + off);
        int score = -1;
        if (pid == 3 && eid == 10 && fmt == 12) score = 4;
        else if (pid == 0 && fmt == 12) score = 3;
        else if (pid == 3 && eid == 1 && fmt == 4) score = 2;
        else if (pid == 0 && fmt == 4) score = 1;
        if (score > best_score) { best_score = score; best = f->cmap + off; }
    }
    if (best_score < 0) return 0;
    const uint8_t* t = d + best;
    uint32_t fmt = rd16(t);
    if (fmt == 4) {
        if (cp > 0xFFFF) return 0;
        uint32_t segx2 = rd16(t + 6);
        const uint8_t* ends = t + 14;
        const uint8_t* starts = ends + segx2 + 2;
        const uint8_t* deltas = starts + segx2;
        const uint8_t* ranges = deltas + segx2;
        if ((uint32_t)(ranges + segx2 - d) > f->len) return 0;
        for (uint32_t s = 0; s < segx2 / 2; s++) {
            uint32_t end = rd16(ends + s * 2);
            if (cp > end) continue;
            uint32_t start = rd16(starts + s * 2);
            if (cp < start) return 0;
            uint32_t delta = rd16(deltas + s * 2), ro = rd16(ranges + s * 2);
            if (ro == 0) return (int)((cp + delta) & 0xFFFF);
            const uint8_t* gp = ranges + s * 2 + ro + (cp - start) * 2;
            if ((uint32_t)(gp + 2 - d) > f->len) return 0;
            uint32_t g = rd16(gp);
            return g ? (int)((g + delta) & 0xFFFF) : 0;
        }
        return 0;
    }
    if (fmt == 12) {
        uint32_t ngroups = rd32(t + 12);
        if (ngroups > 20000) return 0;
        const uint8_t* g = t + 16;
        if ((uint32_t)(g + ngroups * 12 - d) > f->len) return 0;
        for (uint32_t i = 0; i < ngroups; i++, g += 12) {
            uint32_t sc = rd32(g), ec = rd32(g + 4);
            if (cp < sc) return 0;
            if (cp <= ec) return (int)(rd32(g + 8) + (cp - sc));
        }
    }
    return 0;
}

static int advance_units(const face_t* f, int glyph) {
    int i = glyph < f->num_hmetrics ? glyph : f->num_hmetrics - 1;
    if (i < 0) return f->upem / 2;
    uint32_t off = f->hmtx + (uint32_t)i * 4;
    if (off + 2 > f->len) return f->upem / 2;
    return (int)rd16(f->data + off);
}

/* ------------------------------------------------------------ outlines */

/* Flattened outline: line segments in 26.6 pixel space, y pointing down. */
#define MAX_EDGES 3000
typedef struct { int32_t x0, y0, x1, y1; } edge_t;
static edge_t edges[MAX_EDGES];
static int edge_count = 0;

static void add_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    if (y0 == y1) return;
    if (edge_count >= MAX_EDGES) return;
    edges[edge_count].x0 = x0; edges[edge_count].y0 = y0;
    edges[edge_count].x1 = x1; edges[edge_count].y1 = y1;
    edge_count++;
}

/* Quadratic Bézier flattening with a segment count based on the control
 * polygon size (all coordinates already scaled to 26.6 px). */
static void add_quad(int32_t x0, int32_t y0, int32_t cx, int32_t cy, int32_t x1, int32_t y1) {
    int32_t dx = (x0 > x1 ? x0 - x1 : x1 - x0) + (cx > x0 ? cx - x0 : x0 - cx);
    int32_t dy = (y0 > y1 ? y0 - y1 : y1 - y0) + (cy > y0 ? cy - y0 : y0 - cy);
    int32_t d = (dx > dy ? dx : dy) >> 6;                 /* px */
    int n = d <= 2 ? 2 : d <= 8 ? 4 : d <= 24 ? 8 : 16;
    int32_t px = x0, py = y0;
    for (int i = 1; i <= n; i++) {
        int32_t t = (i << 8) / n;                          /* 0..256 */
        int32_t mt = 256 - t;
        /* B(t) = mt^2 P0 + 2 mt t C + t^2 P1, in 16-bit fixed */
        int64_t ax = (int64_t)mt * mt * x0 + 2 * (int64_t)mt * t * cx + (int64_t)t * t * x1;
        int64_t ay = (int64_t)mt * mt * y0 + 2 * (int64_t)mt * t * cy + (int64_t)t * t * y1;
        int32_t nx = (int32_t)(ax >> 16), ny = (int32_t)(ay >> 16);
        add_line(px, py, nx, ny);
        px = nx; py = ny;
    }
}

/* Transform for composite glyphs: 2x2 matrix in 16.16 + offset in font units */
typedef struct { int32_t a, b, c, d; int32_t dx, dy; } xform_t;

/* scale: font units -> 26.6 px, as 16.16 multiplier */
static int32_t g_scale;
static int g_depth;

static void tx(const xform_t* m, int32_t x, int32_t y, int32_t* ox, int32_t* oy) {
    int64_t X = ((int64_t)m->a * x + (int64_t)m->c * y) >> 16;
    int64_t Y = ((int64_t)m->b * x + (int64_t)m->d * y) >> 16;
    X += m->dx; Y += m->dy;
    *ox = (int32_t)((X * g_scale) >> 16);
    *oy = -(int32_t)((Y * g_scale) >> 16);                /* flip: y down */
}

static void outline_glyph(const face_t* f, int glyph, const xform_t* m);

static void simple_outline(const uint8_t* g, uint32_t glen, int ncont, const xform_t* m) {
    if (glen < 10 + (uint32_t)ncont * 2 + 2) return;
    const uint8_t* ends = g + 10;
    int npts = (int)rd16(ends + (ncont - 1) * 2) + 1;
    if (npts <= 0 || npts > 1200) return;
    uint32_t ilen = rd16(ends + ncont * 2);
    const uint8_t* p = ends + ncont * 2 + 2 + ilen;
    const uint8_t* gend = g + glen;
    static uint8_t flags[1200];
    static int32_t xs[1200], ys[1200];
    /* flags */
    for (int i = 0; i < npts;) {
        if (p >= gend) return;
        uint8_t fl = *p++;
        flags[i++] = fl;
        if (fl & 8) { if (p >= gend) return; int r = *p++; while (r-- > 0 && i < npts) flags[i++] = fl; }
    }
    /* x */
    int32_t v = 0;
    for (int i = 0; i < npts; i++) {
        uint8_t fl = flags[i];
        if (fl & 2) { if (p >= gend) return; int32_t d = *p++; v += (fl & 16) ? d : -d; }
        else if (!(fl & 16)) { if (p + 2 > gend) return; v += rds16(p); p += 2; }
        xs[i] = v;
    }
    v = 0;
    for (int i = 0; i < npts; i++) {
        uint8_t fl = flags[i];
        if (fl & 4) { if (p >= gend) return; int32_t d = *p++; v += (fl & 32) ? d : -d; }
        else if (!(fl & 32)) { if (p + 2 > gend) return; v += rds16(p); p += 2; }
        ys[i] = v;
    }
    int start = 0;
    for (int c = 0; c < ncont; c++) {
        int end = (int)rd16(ends + c * 2);
        if (end < start || end >= npts) break;
        int n = end - start + 1;
        if (n < 2) { start = end + 1; continue; }
        /* find first on-curve point (or synthesize a midpoint) */
        int first_on = -1;
        for (int i = 0; i < n; i++) if (flags[start + i] & 1) { first_on = i; break; }
        int32_t sx, sy;
        if (first_on < 0) {
            int32_t ax, ay, bx, by;
            tx(m, xs[start], ys[start], &ax, &ay); tx(m, xs[start + 1], ys[start + 1], &bx, &by);
            sx = (ax + bx) / 2; sy = (ay + by) / 2;
        } else tx(m, xs[start + first_on], ys[start + first_on], &sx, &sy);
        int32_t cx = sx, cy = sy;                          /* current point */
        int have_ctrl = 0; int32_t qx = 0, qy = 0;
        int begin = first_on < 0 ? 1 : first_on + 1;
        for (int k = 0; k < n; k++) {
            int i = start + ((begin + k) % n);
            int32_t px, py; tx(m, xs[i], ys[i], &px, &py);
            int on = flags[i] & 1;
            if (on) {
                if (have_ctrl) { add_quad(cx, cy, qx, qy, px, py); have_ctrl = 0; }
                else add_line(cx, cy, px, py);
                cx = px; cy = py;
            } else {
                if (have_ctrl) {
                    int32_t mx = (qx + px) / 2, my = (qy + py) / 2;
                    add_quad(cx, cy, qx, qy, mx, my);
                    cx = mx; cy = my;
                }
                qx = px; qy = py; have_ctrl = 1;
            }
        }
        /* close */
        if (have_ctrl) add_quad(cx, cy, qx, qy, sx, sy);
        else add_line(cx, cy, sx, sy);
        start = end + 1;
    }
}

static void outline_glyph(const face_t* f, int glyph, const xform_t* m) {
    if (glyph < 0 || glyph >= f->num_glyphs || g_depth > 4) return;
    uint32_t off, next;
    if (f->loca_long) { off = rd32(f->data + f->loca + glyph * 4); next = rd32(f->data + f->loca + glyph * 4 + 4); }
    else { off = rd16(f->data + f->loca + glyph * 2) * 2; next = rd16(f->data + f->loca + glyph * 2 + 2) * 2; }
    if (next <= off || next > f->glyf_len || next - off < 10) return;    /* empty glyph */
    const uint8_t* g = f->data + f->glyf + off;
    uint32_t glen = next - off;
    int ncont = rds16(g);
    if (ncont >= 0) { simple_outline(g, glen, ncont, m); return; }
    /* composite */
    const uint8_t* p = g + 10;
    const uint8_t* gend = g + glen;
    for (int iter = 0; iter < 16; iter++) {
        if (p + 4 > gend) return;
        uint32_t flags = rd16(p); int gi = (int)rd16(p + 2); p += 4;
        int32_t dx, dy;
        if (flags & 1) { if (p + 4 > gend) return; dx = rds16(p); dy = rds16(p + 2); p += 4; }
        else { if (p + 2 > gend) return; dx = (int8_t)p[0]; dy = (int8_t)p[1]; p += 2; }
        xform_t sub = { 0x10000, 0, 0, 0x10000, 0, 0 };
        if (flags & 8) { if (p + 2 > gend) return; sub.a = sub.d = rds16(p) << 2; p += 2; }               /* 2.14 -> 16.16 */
        else if (flags & 0x40) { if (p + 4 > gend) return; sub.a = rds16(p) << 2; sub.d = rds16(p + 2) << 2; p += 4; }
        else if (flags & 0x80) { if (p + 8 > gend) return; sub.a = rds16(p) << 2; sub.b = rds16(p + 2) << 2; sub.c = rds16(p + 4) << 2; sub.d = rds16(p + 6) << 2; p += 8; }
        if (!(flags & 2)) { dx = 0; dy = 0; }             /* point-matching args: unsupported, treat as 0 offset */
        /* compose: new = m * sub (apply sub first, then m) */
        xform_t cm;
        cm.a = (int32_t)(((int64_t)m->a * sub.a + (int64_t)m->c * sub.b) >> 16);
        cm.b = (int32_t)(((int64_t)m->b * sub.a + (int64_t)m->d * sub.b) >> 16);
        cm.c = (int32_t)(((int64_t)m->a * sub.c + (int64_t)m->c * sub.d) >> 16);
        cm.d = (int32_t)(((int64_t)m->b * sub.c + (int64_t)m->d * sub.d) >> 16);
        cm.dx = (int32_t)((((int64_t)m->a * dx + (int64_t)m->c * dy) >> 16) + m->dx);
        cm.dy = (int32_t)((((int64_t)m->b * dx + (int64_t)m->d * dy) >> 16) + m->dy);
        g_depth++;
        outline_glyph(f, gi, &cm);
        g_depth--;
        if (!(flags & 0x20)) break;                       /* MORE_COMPONENTS */
    }
}

/* ----------------------------------------------------------- rasterizer */

/* Signed-area accumulation rasteriser (the font-rs / stb_truetype v2 idea):
 * for every edge, deposit signed coverage deltas into an accumulation buffer;
 * a prefix sum along each row yields exact-area anti-aliased coverage with
 * the non-zero winding rule approximated by clamping |acc| to 1. */
#define RAST_MAX_W 200
#define RAST_MAX_H 200
static int32_t acc[(RAST_MAX_W + 2) * RAST_MAX_H];      /* 16.16 */

static void rast_edge(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int w, int h) {
    /* coordinates in 26.6 relative to bitmap origin; convert to 16.16 pixels */
    int dir = 1;
    if (y0 > y1) { int32_t t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; dir = -1; }
    /* to 16.16 */
    int64_t X0 = (int64_t)x0 << 10, Y0 = (int64_t)y0 << 10, X1 = (int64_t)x1 << 10, Y1 = (int64_t)y1 << 10;
    if (Y1 <= 0 || Y0 >= ((int64_t)h << 16)) return;
    int64_t dxdy = ((X1 - X0) << 16) / (Y1 - Y0);       /* 16.16 */
    int64_t ystart = Y0 < 0 ? 0 : Y0, yend = Y1 > ((int64_t)h << 16) ? ((int64_t)h << 16) : Y1;
    int row0 = (int)(ystart >> 16), row1 = (int)((yend - 1) >> 16);
    if (row1 >= h) row1 = h - 1;
    for (int row = row0; row <= row1; row++) {
        int64_t ya = (int64_t)row << 16, yb = ya + 0x10000;
        if (ya < ystart) ya = ystart;
        if (yb > yend) yb = yend;
        int64_t dy = yb - ya;                              /* 16.16, 0..1 */
        if (dy <= 0) continue;
        int64_t xa = X0 + ((ya - Y0) * dxdy >> 16);
        int64_t xb = X0 + ((yb - Y0) * dxdy >> 16);
        if (xa > xb) { int64_t t = xa; xa = xb; xb = t; }
        /* clamp to [0, w] */
        if (xa < 0) xa = 0;
        if (xb < 0) xb = 0;
        if (xa > ((int64_t)w << 16)) xa = (int64_t)w << 16;
        if (xb > ((int64_t)w << 16)) xb = (int64_t)w << 16;
        int32_t* line = acc + row * (w + 2);
        int64_t cov = dy * dir;                            /* 16.16 total coverage for this row */
        int xi0 = (int)(xa >> 16), xi1 = (int)(xb >> 16);
        if (xi0 >= w) { line[w] += (int32_t)cov; continue; }   /* entirely right: still counts for winding beyond */
        if (xi0 == xi1) {
            /* within one pixel column: split by the horizontal center */
            int64_t xm = ((xa + xb) >> 1) - ((int64_t)xi0 << 16);   /* 0..1 */
            int64_t right = (cov * xm) >> 16;
            line[xi0] += (int32_t)(cov - right);
            line[xi0 + 1] += (int32_t)right;
        } else {
            /* spans several columns: distribute linearly */
            int64_t span = xb - xa;
            int64_t slope = (cov << 16) / span;            /* coverage per unit x, 16.16 */
            /* first partial column */
            int64_t fa = (((int64_t)(xi0 + 1) << 16) - xa);
            int64_t c0 = (slope * fa) >> 16;               /* coverage entering in column xi0 */
            /* area within first column: triangle-ish -> half of c0 lands in xi0, half in xi0+1 (linear approx) */
            line[xi0] += (int32_t)((c0 * fa >> 16) >> 1);
            int64_t carried = c0 - ((c0 * fa >> 16) >> 1);
            int64_t per = (slope);                         /* full column coverage */
            for (int x = xi0 + 1; x < xi1 && x < w; x++) {
                line[x] += (int32_t)(carried + (per >> 1));
                carried = per - (per >> 1);
            }
            if (xi1 < w) {
                int64_t fb = xb - ((int64_t)xi1 << 16);
                int64_t c1 = (slope * fb) >> 16;
                line[xi1] += (int32_t)(carried + c1 - ((c1 * (0x10000 - fb) >> 16) >> 1));
                line[xi1 + 1] += (int32_t)((c1 * (0x10000 - fb) >> 16) >> 1);
            } else {
                line[w] += (int32_t)carried;
            }
        }
    }
}

static void rasterize(int w, int h, uint8_t* out) {
    memset(acc, 0, sizeof(int32_t) * (size_t)(w + 2) * (size_t)h);
    for (int i = 0; i < edge_count; i++) rast_edge(edges[i].x0, edges[i].y0, edges[i].x1, edges[i].y1, w, h);
    for (int y = 0; y < h; y++) {
        int32_t* line = acc + y * (w + 2);
        int32_t sum = 0;
        for (int x = 0; x < w; x++) {
            sum += line[x];
            int32_t a = sum < 0 ? -sum : sum;
            if (a > 0x10000) a = 0x10000;
            out[y * w + x] = font_gamma[(a * 255) >> 16];
        }
    }
}

/* ------------------------------------------------------------ glyph API */

/* Open-addressed hash index over gcache (face, size, glyph) -> slot + 1.
 * Text measurement calls cache_find for every character of every word on
 * every layout pass; the old linear scan of up to 1024 entries dominated
 * layout time on long pages. */
#define GC_HASH (GC_ENTRIES * 4)

static uint32_t gc_key(int face, int size, int glyph) {
    uint32_t h = (uint32_t)glyph * 2654435761u ^ ((uint32_t)size * 40503u) ^ ((uint32_t)face * 97u);
    return (h ^ (h >> 15)) & (GC_HASH - 1);
}

static glyph_cache_t* cache_find(int face, int size, int glyph) {
    uint32_t h = gc_key(face, size, glyph);
    for (int probe = 0; probe < GC_HASH; probe++) {
        uint16_t v = gc_hash[h];
        if (!v) return NULL;
        glyph_cache_t* e = &gcache[v - 1];
        if (e->face == face && e->size == size && e->glyph == glyph) { e->lru = ++cache_tick; return e; }
        h = (h + 1) & (GC_HASH - 1);
    }
    return NULL;
}

static void cache_index(int slot) {
    glyph_cache_t* e = &gcache[slot];
    uint32_t h = gc_key(e->face, e->size, e->glyph);
    for (int probe = 0; probe < GC_HASH; probe++) {
        if (!gc_hash[h]) { gc_hash[h] = (uint16_t)(slot + 1); return; }
        h = (h + 1) & (GC_HASH - 1);
    }
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static glyph_cache_t* render_glyph(int face, int size, int glyph) {
    face_t* f = &faces[face];
    if (!f->loaded) return NULL;
    if (cache_count >= GC_ENTRIES || cache_used + RAST_MAX_W * RAST_MAX_H > FONT_CACHE_BYTES) br_font_cache_flush();
    glyph_cache_t* e = &gcache[cache_count];
    memset(e, 0, sizeof(*e));
    e->face = (int16_t)face; e->size = (int16_t)size; e->glyph = (uint16_t)glyph;
    e->lru = ++cache_tick;
    /* scale: size px per upem, into 26.6 => (size * 64 / upem) in 16.16 */
    g_scale = (int32_t)(((int64_t)size << 22) / f->upem);
    e->advance = (int16_t)(((int64_t)advance_units(f, glyph) * size + f->upem / 2) / f->upem);
    edge_count = 0; g_depth = 0;
    xform_t id = { 0x10000, 0, 0, 0x10000, 0, 0 };
    outline_glyph(f, glyph, &id);
    if (edge_count == 0) { e->w = e->h = 0; cache_index(cache_count); cache_count++; return e; }
    /* bounds in 26.6 */
    int32_t minx = edges[0].x0, maxx = edges[0].x0, miny = edges[0].y0, maxy = edges[0].y0;
    for (int i = 0; i < edge_count; i++) {
        edge_t* d = &edges[i];
        if (d->x0 < minx) minx = d->x0;
        if (d->x0 > maxx) maxx = d->x0;
        if (d->x1 < minx) minx = d->x1;
        if (d->x1 > maxx) maxx = d->x1;
        if (d->y0 < miny) miny = d->y0;
        if (d->y0 > maxy) maxy = d->y0;
        if (d->y1 < miny) miny = d->y1;
        if (d->y1 > maxy) maxy = d->y1;
    }
    int x0 = (minx >> 6) - 1, y0 = (miny >> 6) - 1;
    int x1 = ((maxx + 63) >> 6) + 1, y1 = ((maxy + 63) >> 6) + 1;
    int w = clampi(x1 - x0, 1, RAST_MAX_W), h = clampi(y1 - y0, 1, RAST_MAX_H);
    /* translate edges to bitmap space */
    for (int i = 0; i < edge_count; i++) {
        edges[i].x0 -= x0 << 6; edges[i].x1 -= x0 << 6;
        edges[i].y0 -= y0 << 6; edges[i].y1 -= y0 << 6;
    }
    e->x0 = (int16_t)x0; e->y0 = (int16_t)y0; e->w = (uint16_t)w; e->h = (uint16_t)h;
    e->bitmap = cache_used;
    rasterize(w, h, cache_pixels + cache_used);
    cache_used += (uint32_t)w * h;
    cache_index(cache_count);
    cache_count++;
    return e;
}

static glyph_cache_t* get_glyph(int face, int size, int glyph) {
    glyph_cache_t* e = cache_find(face, size, glyph);
    if (e) return e;
    return render_glyph(face, size, glyph);
}

/* Pick the face for a style: family 0 sans, 1 serif, 2 mono, >=3 web face id */
int br_font_face(int family, int bold) {
    if (family >= FONT_FAMILY_WEB) {
        int idx = family - FONT_FAMILY_WEB + 6;
        if (idx < face_count && faces[idx].loaded) {
            /* pick the variant of the same family matching the requested weight */
            if ((bold != 0) != (faces[idx].bold != 0)) {
                for (int i = 6; i < face_count; i++) if (faces[i].loaded && (faces[i].bold != 0) == (bold != 0) && br_streq(faces[i].family, faces[idx].family)) return i;
            }
            return idx;
        }
        family = 0;
    }
    if (family < 0 || family > 2) family = 0;
    return family * 2 + (bold ? 1 : 0);
}

int br_font_ascent(int face, int size) {
    face_t* f = &faces[face];
    if (!f->loaded) return size * 3 / 4;
    return (int)(((int64_t)f->ascent * size + f->upem / 2) / f->upem);
}
int br_font_descent(int face, int size) {
    face_t* f = &faces[face];
    if (!f->loaded) return size / 4;
    return (int)(((int64_t)(-f->descent) * size + f->upem / 2) / f->upem);
}
int br_font_line_height(int face, int size) {
    face_t* f = &faces[face];
    if (!f->loaded) return size + 4;
    int lh = (int)(((int64_t)(f->ascent - f->descent + f->line_gap) * size + f->upem / 2) / f->upem);
    int min = size + size / 5;
    return lh < min ? min : lh;
}

/* Decode one UTF-8 code point from s; returns bytes consumed. */
int br_utf8_decode(const char* s, int len, uint32_t* cp) {
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80 || len < 2) { *cp = c; return 1; }
    int n = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1;
    if (n == 1) { *cp = 0xFFFD; return 1; }
    if (n > len) n = len;
    uint32_t v = (c >= 0xF0) ? (c & 7) : (c >= 0xE0) ? (c & 15) : (c & 31);
    int i;
    for (i = 1; i < n; i++) {
        if (((unsigned char)s[i] & 0xC0) != 0x80) break;
        v = (v << 6) | ((unsigned char)s[i] & 0x3F);
    }
    *cp = v;
    return i;
}

static int glyph_for(int face, uint32_t cp) {
    int g = cmap_lookup(&faces[face], cp);
    if (!g && cp > 0x7F) {
        /* fallbacks for characters missing from the face */
        if (cp >= 0x2018 && cp <= 0x2019) g = cmap_lookup(&faces[face], '\'');
        else if (cp >= 0x201C && cp <= 0x201D) g = cmap_lookup(&faces[face], '"');
        else if (cp == 0x2013 || cp == 0x2014 || cp == 0x2212) g = cmap_lookup(&faces[face], '-');
        else if (cp == 0xA0 || cp == 0x2009 || cp == 0x200A || cp == 0x2002 || cp == 0x2003) g = cmap_lookup(&faces[face], ' ');
        else if (cp == 0x200B || cp == 0xFEFF || cp == 0x00AD) return -1;      /* zero width */
        else if (face >= 6) {
            /* web font lacks it: fall back to the matching built-in face */
            int fb = faces[face].bold ? 1 : 0;
            int g2 = cmap_lookup(&faces[fb], cp);
            if (g2) return -(2 + g2);                                          /* encoded: use built-in */
        }
    }
    return g;
}

int br_font_text_width(int face, int size, const char* s, int len) {
    int w = 0;
    int i = 0;
    while (i < len) {
        uint32_t cp; i += br_utf8_decode(s + i, len - i, &cp);
        if (cp == '\t') cp = ' ';
        int g = glyph_for(face, cp);
        int use_face = face;
        if (g == -1) continue;
        if (g <= -2) { g = -g - 2; use_face = faces[face].bold ? 1 : 0; }
        glyph_cache_t* e = get_glyph(use_face, size, g);
        if (e) w += e->advance;
    }
    return w;
}

int br_font_char_width(int face, int size, uint32_t cp) {
    int g = glyph_for(face, cp);
    if (g == -1) return 0;
    int use_face = face;
    if (g <= -2) { g = -g - 2; use_face = faces[face].bold ? 1 : 0; }
    glyph_cache_t* e = get_glyph(use_face, size, g);
    return e ? e->advance : 0;
}

/* Blend one glyph bitmap into the framebuffer. */
static void blit_glyph(const glyph_cache_t* e, int px, int py, uint32_t fg, const w98_rect_t* clip, int synth_bold, int synth_italic) {
    if (!e->w || !e->h) return;
    int x0 = px + e->x0, y0 = py + e->y0;
    int cx0 = clip->x, cy0 = clip->y, cx1 = clip->x + clip->w, cy1 = clip->y + clip->h;
    if (cx0 < 0) cx0 = 0;
    if (cy0 < 0) cy0 = 0;
    if (cx1 > (int)screen_width) cx1 = (int)screen_width;
    if (cy1 > (int)screen_height) cy1 = (int)screen_height;
    uint32_t stride = screen_pitch / 4;
    int fr = (fg >> 16) & 0xFF, fgc = (fg >> 8) & 0xFF, fb = fg & 0xFF;
    const uint8_t* bm = cache_pixels + e->bitmap;
    int ew = e->w + (synth_bold ? 1 : 0);
    for (int y = 0; y < e->h; y++) {
        int sy = y0 + y;
        if (sy < cy0 || sy >= cy1) continue;
        uint32_t* row = &lfbptr[(uint32_t)sy * stride];
        const uint8_t* src = bm + y * e->w;
        int shear = synth_italic ? (py - sy) / 4 : 0;        /* lean right above the baseline */
        for (int x = 0; x < ew; x++) {
            int sx = x0 + x + shear;
            if (sx < cx0 || sx >= cx1) continue;
            int a = x < e->w ? src[x] : 0;
            if (synth_bold && x > 0) { int b = src[x - 1]; if (b > a) a = b; }
            if (!a) continue;
            if (a >= 255) { row[sx] = 0xFF000000u | fg; continue; }
            uint32_t d = row[sx];
            int dr = (d >> 16) & 0xFF, dg = (d >> 8) & 0xFF, db = d & 0xFF;
            int ia = 255 - a;
            dr = (fr * a + dr * ia) / 255; dg = (fgc * a + dg * ia) / 255; db = (fb * a + db * ia) / 255;
            row[sx] = 0xFF000000u | ((uint32_t)dr << 16) | ((uint32_t)dg << 8) | (uint32_t)db;
        }
    }
}

/* Draw text with its baseline at (x, baseline_y). Returns the advance.
 * flags: FONT_DRAW_ITALIC shears the glyphs (no real italic faces are
 * embedded), FONT_DRAW_BOLD emboldens (used when a web font has no bold). */
int br_font_draw_ex(int face, int size, const char* s, int len, int x, int baseline_y, uint32_t fg, const w98_rect_t* clip, int flags) {
    int pen = x;
    int i = 0;
    int synth_bold = (flags & FONT_DRAW_BOLD) && !faces[face].bold;
    int synth_italic = (flags & FONT_DRAW_ITALIC) != 0;
    while (i < len) {
        uint32_t cp; i += br_utf8_decode(s + i, len - i, &cp);
        if (cp == '\t') cp = ' ';
        int g = glyph_for(face, cp);
        if (g == -1) continue;
        int use_face = face;
        if (g <= -2) { g = -g - 2; use_face = faces[face].bold ? 1 : 0; }
        glyph_cache_t* e = get_glyph(use_face, size, g);
        if (!e) continue;
        if (pen + e->x0 + e->w + 4 >= clip->x && pen + e->x0 <= clip->x + clip->w) blit_glyph(e, pen, baseline_y, fg, clip, synth_bold, synth_italic);
        pen += e->advance;
    }
    return pen - x;
}
int br_font_draw(int face, int size, const char* s, int len, int x, int baseline_y, uint32_t fg, const w98_rect_t* clip) {
    return br_font_draw_ex(face, size, s, len, x, baseline_y, fg, clip, 0);
}

/* ------------------------------------------------------------ web fonts */

/* Register a fetched font file under a family name. Returns the family id
 * (FONT_FAMILY_WEB + n) or -1. The data is copied into the page arena. */
int br_font_add_web(const char* family, int bold, int italic, const uint8_t* data, uint32_t len) {
    if (face_count >= FONT_MAX_FACES) return -1;
    if (len < 12 || webfont_used + len > FONT_WEB_ARENA) return -1;
    uint8_t* dst = webfont_arena + webfont_used;
    if (data != dst) memcpy(dst, data, len);
    face_t* f = &faces[face_count];
    if (!face_load(f, dst, len)) return -1;
    webfont_used += (len + 15) & ~15u;
    f->web = 1; f->bold = bold; f->italic = italic;
    int i = 0;
    while (family[i] && i < (int)sizeof(f->family) - 1) { char c = family[i]; f->family[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; i++; }
    f->family[i] = 0;
    face_count++;
    return FONT_FAMILY_WEB + (face_count - 1 - 6);
}

/* Find a registered web font by family (case-insensitive), preferring the
 * requested weight. Returns family id or -1. */
int br_font_find_web(const char* family, int bold) {
    char lc[40]; int i = 0;
    while (family[i] && i < 39) { char c = family[i]; lc[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; i++; }
    lc[i] = 0;
    int best = -1;
    for (int k = 6; k < face_count; k++) {
        if (!faces[k].loaded || !br_streq(faces[k].family, lc)) continue;
        if (best < 0 || (faces[k].bold == bold && faces[best].bold != bold)) best = k;
    }
    return best < 0 ? -1 : FONT_FAMILY_WEB + (best - 6);
}

uint8_t* br_font_web_alloc(uint32_t max, uint32_t* avail) {
    uint32_t left = FONT_WEB_ARENA - webfont_used;
    *avail = left < max ? left : max;
    return webfont_arena + webfont_used;
}

int br_font_web_count(void) { return face_count - 6; }
int br_font_face_is_bold(int face) { return face >= 0 && face < face_count && faces[face].bold; }

/* Is a web face with exactly this family+weight registered? */
int br_font_web_has(const char* family, int bold) {
    char lc[40]; int i = 0;
    while (family[i] && i < 39) { char c = family[i]; lc[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; i++; }
    lc[i] = 0;
    for (int k = 6; k < face_count; k++) if (faces[k].loaded && faces[k].bold == bold && br_streq(faces[k].family, lc)) return 1;
    return 0;
}
uint32_t br_font_web_bytes(void) { return webfont_used; }
