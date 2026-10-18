 
                                                      
  
                                                                            
                                                                           
                                                                           
                                                                             
                                                                              
                                                                              
                                                                               
                                                           
  
                                                                           
                                                                 
  
                                                                          
                                                                      
  

#include "browser_internal.h"

                                                                      
                                                                        
                                                                      

typedef int64_t sfx;                                                     
#define SFX_SHIFT 16
#define SFX_ONE   ((sfx)1 << SFX_SHIFT)
#define SFX_HALF  ((sfx)1 << (SFX_SHIFT - 1))
#define SFX_INT(x) ((sfx)(x) << SFX_SHIFT)

static const char* my_strstr_ci_n(const char* hay, int n, const char* needle);

static inline sfx smul(sfx a, sfx b) { return (a * b) >> SFX_SHIFT; }
static inline sfx sdiv(sfx a, sfx b) { return b ? (a << SFX_SHIFT) / b : 0; }
static inline sfx sabs(sfx a) { return a < 0 ? -a : a; }

static uint64_t isqrt64(uint64_t v) {
    uint64_t res = 0, bit = (uint64_t)1 << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= res + bit) { v -= res + bit; res = (res >> 1) + bit; }
        else res >>= 1;
        bit >>= 2;
    }
    return res;
}
static sfx ssqrt(sfx v) { if (v <= 0) return 0; return (sfx)isqrt64((uint64_t)v << SFX_SHIFT); }

                                      
static const uint32_t sin_tab[91] = {
        0,  1143,  2287,  3429,  4571,  5711,  6850,  7986,  9120, 10252, 11380, 12504, 13625, 14742, 15854, 16961,
    18064, 19160, 20251, 21336, 22414, 23486, 24550, 25606, 26655, 27696, 28729, 29752, 30767, 31772, 32768, 33753,
    34728, 35693, 36647, 37589, 38521, 39440, 40347, 41243, 42125, 42995, 43852, 44695, 45525, 46340, 47142, 47929,
    48702, 49460, 50203, 50931, 51643, 52339, 53019, 53683, 54331, 54963, 55577, 56175, 56755, 57319, 57864, 58393,
    58903, 59395, 59870, 60326, 60763, 61183, 61583, 61965, 62328, 62672, 62997, 63302, 63589, 63856, 64103, 64331,
    64540, 64729, 64898, 65047, 65176, 65286, 65376, 65446, 65496, 65526, 65536 };

                          
static sfx ssin(sfx deg) {
    deg %= SFX_INT(360);
    if (deg < 0) deg += SFX_INT(360);
    int neg = 0;
    if (deg >= SFX_INT(180)) { deg -= SFX_INT(180); neg = 1; }
    if (deg > SFX_INT(90)) deg = SFX_INT(180) - deg;
    int i = (int)(deg >> SFX_SHIFT);
    sfx frac = deg & (SFX_ONE - 1);
    if (i >= 90) return neg ? -SFX_ONE : SFX_ONE;
    sfx a = sin_tab[i], b = sin_tab[i + 1];
    sfx v = a + (((b - a) * frac) >> SFX_SHIFT);
    return neg ? -v : v;
}
static sfx scos(sfx deg) { return ssin(deg + SFX_INT(90)); }

                                             
static sfx satan2(sfx y, sfx x) {
    if (x == 0 && y == 0) return 0;
    sfx ax = sabs(x), ay = sabs(y);
    sfx z, r;
    int swap = ay > ax;
    z = swap ? sdiv(ax, ay) : sdiv(ay, ax);                         
                                                               
    sfx c1 = 918815, c2 = 249037;                                 
    r = smul(SFX_INT(45), z) - smul(smul(z, z - SFX_ONE), c1 + smul(c2, z));
    if (swap) r = SFX_INT(90) - r;
    if (x < 0) r = SFX_INT(180) - r;
    if (y < 0) r = -r;
    return r;
}

typedef struct { sfx a, b, c, d, e, f; } mat_t;                                              

static mat_t mat_identity(void) { mat_t m = { SFX_ONE, 0, 0, SFX_ONE, 0, 0 }; return m; }
static mat_t mat_mul(const mat_t* m, const mat_t* n) {                                       
    mat_t r;
    r.a = smul(m->a, n->a) + smul(m->c, n->b);
    r.b = smul(m->b, n->a) + smul(m->d, n->b);
    r.c = smul(m->a, n->c) + smul(m->c, n->d);
    r.d = smul(m->b, n->c) + smul(m->d, n->d);
    r.e = smul(m->a, n->e) + smul(m->c, n->f) + m->e;
    r.f = smul(m->b, n->e) + smul(m->d, n->f) + m->f;
    return r;
}
static void mat_apply(const mat_t* m, sfx x, sfx y, sfx* ox, sfx* oy) {
    *ox = smul(m->a, x) + smul(m->c, y) + m->e;
    *oy = smul(m->b, x) + smul(m->d, y) + m->f;
}
static sfx mat_scale(const mat_t* m) {                      
    sfx det = smul(m->a, m->d) - smul(m->b, m->c);
    return ssqrt(sabs(det));
}

                                                                      
                                                                        
                                                                      

#define SVG_MAX_EDGES  65536
#define SVG_MAX_PTS    16384
#define SVG_MAX_W      4096
#define SVG_MAX_CROSS  4096

typedef struct { int32_t x0, y0, x1, y1; int8_t dir; } edge_t;                         

static edge_t edges[SVG_MAX_EDGES];
static int edge_count;
static int edge_order[SVG_MAX_EDGES];
static int active[SVG_MAX_EDGES];
static int cov[SVG_MAX_W + 2];
static int32_t cross_x[SVG_MAX_CROSS];
static int8_t  cross_w[SVG_MAX_CROSS];

static uint32_t* out_px;
static int out_w, out_h;
static int sub_lines;

static inline int32_t to_dev(sfx v) {                                        
    sfx d = v >> 8;
    if (d > 0x3FFFFFF) d = 0x3FFFFFF;
    if (d < -0x3FFFFFF) d = -0x3FFFFFF;
    return (int32_t)d;
}

static void add_edge(sfx x0, sfx y0, sfx x1, sfx y1) {
    if (edge_count >= SVG_MAX_EDGES) return;
    int32_t ax = to_dev(x0), ay = to_dev(y0), bx = to_dev(x1), by = to_dev(y1);
    if (ay == by) return;
    edge_t* e = &edges[edge_count++];
    if (ay < by) { e->x0 = ax; e->y0 = ay; e->x1 = bx; e->y1 = by; e->dir = 1; }
    else         { e->x0 = bx; e->y0 = by; e->x1 = ax; e->y1 = ay; e->dir = -1; }
}

static void sort_edges_by_y(void) {
    for (int i = 0; i < edge_count; i++) edge_order[i] = i;
                        
    for (int gap = edge_count / 2; gap > 0; gap /= 2) {
        for (int i = gap; i < edge_count; i++) {
            int v = edge_order[i]; int j = i;
            while (j >= gap && edges[edge_order[j - gap]].y0 > edges[v].y0) { edge_order[j] = edge_order[j - gap]; j -= gap; }
            edge_order[j] = v;
        }
    }
}

static void sort_crossings(int n) {
    for (int gap = n / 2; gap > 0; gap /= 2) {
        for (int i = gap; i < n; i++) {
            int32_t vx = cross_x[i]; int8_t vw = cross_w[i]; int j = i;
            while (j >= gap && cross_x[j - gap] > vx) { cross_x[j] = cross_x[j - gap]; cross_w[j] = cross_w[j - gap]; j -= gap; }
            cross_x[j] = vx; cross_w[j] = vw;
        }
    }
}

static inline void blend_pixel(uint32_t* p, uint32_t color, int alpha) {
    if (alpha <= 0) return;
    if (alpha > 255) alpha = 255;
    uint32_t d = *p;
    int da = (int)(d >> 24);
    int sr = (int)((color >> 16) & 0xFF), sg = (int)((color >> 8) & 0xFF), sb = (int)(color & 0xFF);
    if (da == 0 || alpha == 255) {
        *p = ((uint32_t)alpha << 24) | (uint32_t)(sr << 16) | (uint32_t)(sg << 8) | (uint32_t)sb;
        return;
    }
    int dr = (int)((d >> 16) & 0xFF), dg = (int)((d >> 8) & 0xFF), db = (int)(d & 0xFF);
    int inv = 255 - alpha;
    int oa = alpha + da * inv / 255;
    if (oa <= 0) { *p = 0; return; }
    int orr = (sr * alpha + dr * da * inv / 255) / oa;
    int og  = (sg * alpha + dg * da * inv / 255) / oa;
    int ob  = (sb * alpha + db * da * inv / 255) / oa;
    *p = ((uint32_t)oa << 24) | (uint32_t)(orr << 16) | (uint32_t)(og << 8) | (uint32_t)ob;
}

                                                                          
static void raster_fill(uint32_t color, int opacity, int evenodd) {
    if (edge_count == 0 || opacity <= 0) { edge_count = 0; return; }
    sort_edges_by_y();
    int32_t ymin = edges[edge_order[0]].y0, ymax = edges[edge_order[0]].y1;
    for (int i = 0; i < edge_count; i++) if (edges[i].y1 > ymax) ymax = edges[i].y1;
    int py0 = ymin >> 8, py1 = (ymax + 255) >> 8;
    if (py0 < 0) py0 = 0;
    if (py1 > out_h) py1 = out_h;
    int S = sub_lines;
    int next_edge = 0, n_active = 0;
    int32_t wmax = (int32_t)out_w << 8;

    for (int py = py0; py < py1; py++) {
        int any = 0;
        int32_t row_top = (int32_t)py << 8, row_bot = row_top + 256;
                                                                 
        while (next_edge < edge_count && edges[edge_order[next_edge]].y0 < row_bot) active[n_active++] = edge_order[next_edge++];
                                                
        int k = 0;
        for (int i = 0; i < n_active; i++) if (edges[active[i]].y1 > row_top) active[k++] = active[i];
        n_active = k;
        if (!n_active) { if (next_edge >= edge_count) break; continue; }

        for (int s = 0; s < S; s++) {
            int32_t sy = row_top + (int32_t)((2 * s + 1) * 128 / S);                                   
            int nc = 0;
            for (int i = 0; i < n_active && nc < SVG_MAX_CROSS; i++) {
                const edge_t* e = &edges[active[i]];
                if (sy < e->y0 || sy >= e->y1) continue;
                int64_t x = (int64_t)e->x0 + ((int64_t)(sy - e->y0) * (int64_t)(e->x1 - e->x0)) / (int64_t)(e->y1 - e->y0);
                cross_x[nc] = (int32_t)x; cross_w[nc] = e->dir; nc++;
            }
            if (nc < 2) continue;
            sort_crossings(nc);
            int wind = 0;
            for (int i = 0; i + 1 < nc; i++) {
                wind += cross_w[i];
                int inside = evenodd ? (wind & 1) : (wind != 0);
                if (!inside) continue;
                int32_t xa = cross_x[i], xb = cross_x[i + 1];
                if (xa < 0) xa = 0;
                if (xb > wmax) xb = wmax;
                if (xb <= xa) continue;
                int ia = xa >> 8, ib = xb >> 8;
                any = 1;
                if (ia == ib) cov[ia] += xb - xa;
                else {
                    cov[ia] += 256 - (xa & 255);
                    for (int q = ia + 1; q < ib; q++) cov[q] += 256;
                    if (ib < out_w) cov[ib] += xb & 255;
                }
            }
        }
        if (!any) continue;
        uint32_t* row = out_px + (size_t)py * out_w;
        int full = 256 * S;
        for (int x = 0; x < out_w; x++) {
            int c = cov[x];
            if (!c) continue;
            cov[x] = 0;
            if (c > full) c = full;
            int alpha = c * opacity / full;
            blend_pixel(&row[x], color, alpha);
        }
    }
    edge_count = 0;
}

                                                                      
                                                                        
                                                                      

static sfx pts_x[SVG_MAX_PTS], pts_y[SVG_MAX_PTS];
static int pts_n;
static int sub_start[512];                                     
static int sub_closed[512];
static int sub_n;

static void path_reset(void) { pts_n = 0; sub_n = 0; }
static void path_moveto(sfx x, sfx y) {
    if (pts_n >= SVG_MAX_PTS - 1 || sub_n >= 512) return;
    sub_start[sub_n] = pts_n; sub_closed[sub_n] = 0; sub_n++;
    pts_x[pts_n] = x; pts_y[pts_n] = y; pts_n++;
}
static void path_lineto(sfx x, sfx y) {
    if (pts_n >= SVG_MAX_PTS - 1) return;
    if (sub_n == 0) { path_moveto(x, y); return; }
    pts_x[pts_n] = x; pts_y[pts_n] = y; pts_n++;
}
static void path_close(void) { if (sub_n > 0) sub_closed[sub_n - 1] = 1; }

static int sub_end(int i) { return i + 1 < sub_n ? sub_start[i + 1] : pts_n; }

static void emit_fill_edges(void) {
    for (int s = 0; s < sub_n; s++) {
        int a = sub_start[s], b = sub_end(s);
        if (b - a < 2) continue;
        for (int i = a; i + 1 < b; i++) add_edge(pts_x[i], pts_y[i], pts_x[i + 1], pts_y[i + 1]);
        add_edge(pts_x[b - 1], pts_y[b - 1], pts_x[a], pts_y[a]);                     
    }
}

                                                              
static void add_poly_ccw(const sfx* xs, const sfx* ys, int n) {
    sfx area = 0;
    for (int i = 0; i < n; i++) { int j = (i + 1) % n; area += smul(xs[i], ys[j]) - smul(xs[j], ys[i]); }
    if (area >= 0) { for (int i = 0; i < n; i++) { int j = (i + 1) % n; add_edge(xs[i], ys[i], xs[j], ys[j]); } }
    else           { for (int i = n - 1; i >= 0; i--) { int j = (i + n - 1) % n; add_edge(xs[i], ys[i], xs[j], ys[j]); } }
}

static void add_disc(sfx cx, sfx cy, sfx r) {
    sfx xs[16], ys[16];
    int n = r > SFX_INT(6) ? 16 : r > SFX_INT(2) ? 12 : 8;
    for (int i = 0; i < n; i++) {
        sfx ang = SFX_INT(360) * i / n;
        xs[i] = cx + smul(r, scos(ang));
        ys[i] = cy + smul(r, ssin(ang));
    }
    add_poly_ccw(xs, ys, n);
}

static void emit_stroke_edges(sfx hw, int linecap, int linejoin) {
    if (hw < (SFX_ONE / 4)) hw = SFX_ONE / 4;                                    
    for (int s = 0; s < sub_n; s++) {
        int a = sub_start[s], b = sub_end(s);
        int n = b - a;
        if (n < 1) continue;
        if (n == 1) {
            if (linecap == 1) add_disc(pts_x[a], pts_y[a], hw);
            continue;
        }
        int closed = sub_closed[s];
        int segs = closed ? n : n - 1;
        for (int i = 0; i < segs; i++) {
            int i0 = a + i, i1 = a + (i + 1) % n;
            sfx x0 = pts_x[i0], y0 = pts_y[i0], x1 = pts_x[i1], y1 = pts_y[i1];
            sfx dx = x1 - x0, dy = y1 - y0;
            sfx len = ssqrt(smul(dx, dx) + smul(dy, dy));
            if (len == 0) continue;
            sfx nx = -sdiv(dy, len), ny = sdiv(dx, len);                     
            sfx ox = smul(nx, hw), oy = smul(ny, hw);
            sfx ex = 0, ey = 0;
            if (!closed && linecap == 2) { ex = smul(sdiv(dx, len), hw); ey = smul(sdiv(dy, len), hw); }
            sfx sx0 = x0, sy0 = y0, sx1 = x1, sy1 = y1;
            if (!closed && linecap == 2) {
                if (i == 0) { sx0 -= ex; sy0 -= ey; }
                if (i == segs - 1) { sx1 += ex; sy1 += ey; }
            }
            sfx qx[4] = { sx0 + ox, sx1 + ox, sx1 - ox, sx0 - ox };
            sfx qy[4] = { sy0 + oy, sy1 + oy, sy1 - oy, sy0 - oy };
            add_poly_ccw(qx, qy, 4);
        }
                 
        if (hw >= SFX_ONE * 3 / 4) {
            int first = closed ? 0 : 1, last = closed ? n : n - 1;
            for (int i = first; i < last; i++) {
                int idx = a + (i % n);
                if (linejoin == 2) {
                                                                                      
                    add_disc(pts_x[idx], pts_y[idx], hw);
                } else add_disc(pts_x[idx], pts_y[idx], hw);
            }
            if (!closed && linecap == 1) { add_disc(pts_x[a], pts_y[a], hw); add_disc(pts_x[b - 1], pts_y[b - 1], hw); }
        }
    }
}

                                                                      
                                                                        
                                                                      

static int seg_count_for(sfx d) {                                                 
    int px = (int)(d >> SFX_SHIFT);
    int n = px / 3 + 3;
    if (n > 48) n = 48;
    return n;
}

static void flatten_cubic(sfx x0, sfx y0, sfx x1, sfx y1, sfx x2, sfx y2, sfx x3, sfx y3) {
    sfx d = sabs(x1 - x0) + sabs(y1 - y0) + sabs(x2 - x1) + sabs(y2 - y1) + sabs(x3 - x2) + sabs(y3 - y2);
    int n = seg_count_for(d);
    for (int i = 1; i <= n; i++) {
        int64_t t = i, u = n - i;
        int64_t w0 = u * u * u, w1 = 3 * u * u * t, w2 = 3 * u * t * t, w3 = t * t * t, den = (int64_t)n * n * n;
        sfx x = (w0 * x0 + w1 * x1 + w2 * x2 + w3 * x3) / den;
        sfx y = (w0 * y0 + w1 * y1 + w2 * y2 + w3 * y3) / den;
        path_lineto(x, y);
    }
}

static void flatten_quad(sfx x0, sfx y0, sfx x1, sfx y1, sfx x2, sfx y2) {
    sfx d = sabs(x1 - x0) + sabs(y1 - y0) + sabs(x2 - x1) + sabs(y2 - y1);
    int n = seg_count_for(d);
    for (int i = 1; i <= n; i++) {
        int64_t t = i, u = n - i;
        int64_t w0 = u * u, w1 = 2 * u * t, w2 = t * t, den = (int64_t)n * n;
        path_lineto((w0 * x0 + w1 * x1 + w2 * x2) / den, (w0 * y0 + w1 * y1 + w2 * y2) / den);
    }
}

                                                                      
                                                                        
                                                                      

#define SVG_MAX_ATTR 48
typedef struct {
    const char* name; int name_len;
    struct { const char* k; int kl; const char* v; int vl; } attr[SVG_MAX_ATTR];
    int nattr;
    int self_closing;
    int end_tag;
    const char* start;            
    const char* after;                             
} elem_t;

static int is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
static int name_eq(const char* s, int n, const char* lit) {
    int i = 0;
    for (; i < n && lit[i]; i++) { char c = s[i]; if (c >= 'A' && c <= 'Z') c = (char)(c + 32); char l = lit[i]; if (l >= 'A' && l <= 'Z') l = (char)(l + 32); if (c != l) return 0; }
    return i == n && !lit[i];
}
static const char* attr_get(const elem_t* e, const char* name, int* len) {
    for (int i = 0; i < e->nattr; i++) if (name_eq(e->attr[i].k, e->attr[i].kl, name)) { *len = e->attr[i].vl; return e->attr[i].v; }
    return NULL;
}

                                                             
static int parse_elem(const char* p, const char* end, elem_t* e) {
    if (p >= end || *p != '<') return 0;
    e->start = p; e->nattr = 0; e->self_closing = 0; e->end_tag = 0;
    p++;
    if (p < end && *p == '/') { e->end_tag = 1; p++; }
    e->name = p;
    while (p < end && !is_ws(*p) && *p != '>' && *p != '/') p++;
    e->name_len = (int)(p - e->name);
    while (p < end && *p != '>') {
        while (p < end && is_ws(*p)) p++;
        if (p >= end) break;
        if (*p == '/') { e->self_closing = 1; p++; continue; }
        if (*p == '>') break;
        const char* k = p;
        while (p < end && !is_ws(*p) && *p != '=' && *p != '>' && *p != '/') p++;
        int kl = (int)(p - k);
        while (p < end && is_ws(*p)) p++;
        const char* v = p; int vl = 0;
        if (p < end && *p == '=') {
            p++;
            while (p < end && is_ws(*p)) p++;
            if (p < end && (*p == '"' || *p == '\'')) {
                char q = *p++; v = p;
                while (p < end && *p != q) p++;
                vl = (int)(p - v);
                if (p < end) p++;
            } else {
                v = p;
                while (p < end && !is_ws(*p) && *p != '>') p++;
                vl = (int)(p - v);
            }
        }
        if (kl > 0 && e->nattr < SVG_MAX_ATTR) { e->attr[e->nattr].k = k; e->attr[e->nattr].kl = kl; e->attr[e->nattr].v = v; e->attr[e->nattr].vl = vl; e->nattr++; }
    }
    if (p < end && *p == '>') p++;
    e->after = p;
    return 1;
}

                                                                                 
static const char* next_tag(const char* p, const char* end) {
    for (;;) {
        while (p < end && *p != '<') p++;
        if (p >= end) return end;
        if (p + 3 < end && p[1] == '!' && p[2] == '-' && p[3] == '-') {
            p += 4;
            while (p + 2 < end && !(p[0] == '-' && p[1] == '-' && p[2] == '>')) p++;
            p += 3;
            continue;
        }
        if (p + 8 < end && p[1] == '!' && p[2] == '[') {                                  
            while (p + 2 < end && !(p[0] == ']' && p[1] == ']' && p[2] == '>')) p++;
            p += 3;
            continue;
        }
        if (p + 1 < end && (p[1] == '!' || p[1] == '?')) {
            while (p < end && *p != '>') p++;
            p++;
            continue;
        }
        return p;
    }
}

                                                                                                   
static const char* skip_subtree(const elem_t* e, const char* end) {
    if (e->self_closing) return e->after;
    int depth = 1;
    const char* p = e->after;
    while (depth > 0) {
        p = next_tag(p, end);
        if (p >= end) return end;
        elem_t t;
        if (!parse_elem(p, end, &t)) return end;
        if (t.end_tag) depth--;
        else if (!t.self_closing) depth++;
        p = t.after;
    }
    return p;
}

                                                                      
                                                                        
                                                                      

                                                              
static int parse_sfx(const char** pp, const char* end, sfx* out) {
    const char* p = *pp;
    while (p < end && (is_ws(*p) || *p == ',')) p++;
    int neg = 0;
    if (p < end && (*p == '-' || *p == '+')) { neg = *p == '-'; p++; }
    const char* st = p;
    int64_t ip = 0;
    while (p < end && *p >= '0' && *p <= '9') { if (ip < 100000000) ip = ip * 10 + (*p - '0'); p++; }
    sfx v = SFX_INT(ip);
    if (p < end && *p == '.') {
        p++;
        int64_t scale = 1; sfx frac = 0;
        while (p < end && *p >= '0' && *p <= '9') {
            if (scale < 1000000) { frac = frac * 10 + (*p - '0'); scale *= 10; }
            p++;
        }
        v += (frac << SFX_SHIFT) / scale;
    }
    if (p == st || (p == st + 1 && st[0] == '.')) { if (p == st) return 0; }
    if (p < end && (*p == 'e' || *p == 'E')) {
        const char* q = p + 1;
        int eneg = 0;
        if (q < end && (*q == '-' || *q == '+')) { eneg = *q == '-'; q++; }
        if (q < end && *q >= '0' && *q <= '9') {
            int ex = 0;
            while (q < end && *q >= '0' && *q <= '9') { if (ex < 30) ex = ex * 10 + (*q - '0'); q++; }
            for (int i = 0; i < ex; i++) v = eneg ? v / 10 : v * 10;
            p = q;
        }
    }
    *out = neg ? -v : v;
    *pp = p;
    return 1;
}

                                                                          
static sfx parse_length(const char* s, int len, sfx ref, int* ok) {
    const char* p = s; const char* end = s + len;
    sfx v;
    if (!parse_sfx(&p, end, &v)) { *ok = 0; return 0; }
    *ok = 1;
    while (p < end && is_ws(*p)) p++;
    if (p < end) {
        if (*p == '%') return smul(v, ref) / 100;
        if (p + 1 < end) {
            if (p[0] == 'p' && p[1] == 't') return v * 4 / 3;
            if (p[0] == 'e' && p[1] == 'm') return v * 16;
            if (p[0] == 'p' && p[1] == 'c') return v * 16;
            if (p[0] == 'm' && p[1] == 'm') return v * 378 / 100;
            if (p[0] == 'c' && p[1] == 'm') return v * 3780 / 100;
            if (p[0] == 'i' && p[1] == 'n') return v * 96;
        }
    }
    return v;
}

#define SVG_MAX_GRAD 64
static struct { const char* id; int idl; uint32_t color; int has; } grads[SVG_MAX_GRAD];
static int grad_n;

#define SVG_MAX_IDS 512
static struct { const char* id; int idl; const char* elem; } ids[SVG_MAX_IDS];
static int id_n;

static uint32_t cur_color_val = 0xFF000000u;

                                                              
static int parse_paint(const char* s, int len, uint32_t* out) {
    while (len > 0 && is_ws(*s)) { s++; len--; }
    while (len > 0 && is_ws(s[len - 1])) len--;
    if (len <= 0) return -1;
    if (name_eq(s, len, "none") || name_eq(s, len, "transparent")) return 0;
    if (name_eq(s, len, "currentcolor")) { *out = cur_color_val; return 1; }
    if (name_eq(s, len, "inherit")) return -1;
    if (len > 5 && s[0] == 'u' && s[1] == 'r' && s[2] == 'l' && s[3] == '(') {
        const char* q = s + 4; int ql = len - 4;
        while (ql > 0 && (is_ws(*q) || *q == '"' || *q == '\'')) { q++; ql--; }
        if (ql > 0 && *q == '#') { q++; ql--; }
        int il = 0; while (il < ql && q[il] != ')' && q[il] != '"' && q[il] != '\'' && !is_ws(q[il])) il++;
        for (int i = 0; i < grad_n; i++) {
            if (grads[i].idl != il) continue;
            int same = 1; for (int k = 0; k < il; k++) if (grads[i].id[k] != q[k]) { same = 0; break; }
            if (same) { if (grads[i].has) { *out = grads[i].color; return 1; } break; }
        }
                                                                                    
        *out = 0xFF9E9E9Eu; return 1;
    }
    char tmp[64]; int n = len < 63 ? len : 63;
    for (int i = 0; i < n; i++) tmp[i] = s[i];
    tmp[n] = 0;
    int ok = 0;
    uint32_t c = br_css_parse_color(tmp, &ok);
    if (!ok) return -1;
    if ((c >> 24) == 0) return 0;
    *out = c;
    return 1;
}

static int parse_opacity(const char* s, int len) {                 
    const char* p = s; sfx v;
    if (!parse_sfx(&p, s + len, &v)) return 255;
    if (p < s + len && *p == '%') v = v / 100;
    if (v < 0) v = 0;
    if (v > SFX_ONE) v = SFX_ONE;
    return (int)((v * 255) >> SFX_SHIFT);
}

                                                                      
                                                                        
                                                                      

typedef struct {
    uint32_t fill, stroke;
    int fill_none, stroke_none;
    sfx stroke_width;
    int opacity, fill_opacity, stroke_opacity;
    int evenodd;
    int linecap, linejoin;
    int visible;
    mat_t ctm;
} sstyle_t;

static void apply_style_prop(sstyle_t* st, const char* k, int kl, const char* v, int vl) {
    uint32_t c;
    if (name_eq(k, kl, "fill")) { int r = parse_paint(v, vl, &c); if (r == 1) { st->fill = c; st->fill_none = 0; } else if (r == 0) st->fill_none = 1; }
    else if (name_eq(k, kl, "stroke")) { int r = parse_paint(v, vl, &c); if (r == 1) { st->stroke = c; st->stroke_none = 0; } else if (r == 0) st->stroke_none = 1; }
    else if (name_eq(k, kl, "stroke-width")) { int ok; sfx w = parse_length(v, vl, SFX_INT(100), &ok); if (ok) st->stroke_width = w; }
    else if (name_eq(k, kl, "opacity")) st->opacity = st->opacity * parse_opacity(v, vl) / 255;
    else if (name_eq(k, kl, "fill-opacity")) st->fill_opacity = parse_opacity(v, vl);
    else if (name_eq(k, kl, "stroke-opacity")) st->stroke_opacity = parse_opacity(v, vl);
    else if (name_eq(k, kl, "fill-rule")) st->evenodd = name_eq(v, vl, "evenodd");
    else if (name_eq(k, kl, "stroke-linecap")) st->linecap = name_eq(v, vl, "round") ? 1 : name_eq(v, vl, "square") ? 2 : 0;
    else if (name_eq(k, kl, "stroke-linejoin")) st->linejoin = name_eq(v, vl, "round") ? 1 : name_eq(v, vl, "bevel") ? 2 : 0;
    else if (name_eq(k, kl, "display")) { if (name_eq(v, vl, "none")) st->visible = 0; }
    else if (name_eq(k, kl, "visibility")) { if (name_eq(v, vl, "hidden") || name_eq(v, vl, "collapse")) st->visible = 0; }
    else if (name_eq(k, kl, "color")) { int r = parse_paint(v, vl, &c); if (r == 1) cur_color_val = c; }
}

static void apply_style_string(sstyle_t* st, const char* s, int len) {
    const char* end = s + len;
    while (s < end) {
        while (s < end && (is_ws(*s) || *s == ';')) s++;
        const char* k = s;
        while (s < end && *s != ':' && *s != ';') s++;
        int kl = (int)(s - k);
        while (kl > 0 && is_ws(k[kl - 1])) kl--;
        if (s < end && *s == ':') {
            s++;
            while (s < end && is_ws(*s)) s++;
            const char* v = s;
            while (s < end && *s != ';') s++;
            int vl = (int)(s - v);
            while (vl > 0 && is_ws(v[vl - 1])) vl--;
            if (vl > 10 && v[vl - 1] == 't' && v[vl - 10] == '!') vl -= 10;                 
            while (vl > 0 && is_ws(v[vl - 1])) vl--;
            if (kl > 0) apply_style_prop(st, k, kl, v, vl);
        }
    }
}

                                                
#define SVG_MAX_RULES 256
static struct { const char* sel; int sl; const char* decl; int dl; } rules[SVG_MAX_RULES];
static int rule_n;

static void collect_style_rules(const char* s, const char* end) {
    while (s < end) {
        while (s < end && (is_ws(*s) || *s == '}')) s++;
        if (s + 3 < end && s[0] == '<' && s[1] == '!' && s[2] == '[') { s += 9; continue; }
        if (s + 2 < end && s[0] == ']' && s[1] == ']' && s[2] == '>') { s += 3; continue; }
        if (s < end && *s == '@') {                                     
            int depth = 0;
            while (s < end) { if (*s == '{') depth++; else if (*s == '}') { depth--; if (depth <= 0) { s++; break; } } else if (*s == ';' && depth == 0) { s++; break; } s++; }
            continue;
        }
        const char* sel = s;
        while (s < end && *s != '{') s++;
        int sl = (int)(s - sel);
        if (s >= end) break;
        s++;
        const char* decl = s;
        while (s < end && *s != '}') s++;
        int dl = (int)(s - decl);
        if (s < end) s++;
        while (sl > 0 && is_ws(sel[sl - 1])) sl--;
        if (sl > 0 && rule_n < SVG_MAX_RULES) { rules[rule_n].sel = sel; rules[rule_n].sl = sl; rules[rule_n].decl = decl; rules[rule_n].dl = dl; rule_n++; }
    }
}

                                                                                                      
static int selector_matches(const char* sel, int sl, const elem_t* e, int pass) {
                                                                      
    const char* end = sel + sl;
    const char* p = sel;
    while (p < end) {
        while (p < end && (is_ws(*p) || *p == ',')) p++;
        const char* part = p;
        while (p < end && *p != ',') p++;
        int pl = (int)(p - part);
        while (pl > 0 && is_ws(part[pl - 1])) pl--;
                                                                         
        const char* last = part + pl;
        while (last > part && !is_ws(last[-1]) && last[-1] != '>') last--;
        int ll = (int)(part + pl - last);
        if (ll <= 0) continue;
        if (pass == 2 && last[0] == '#') {
            int idl; const char* id = attr_get(e, "id", &idl);
            if (id && idl == ll - 1) { int same = 1; for (int i = 0; i < idl; i++) if (id[i] != last[1 + i]) { same = 0; break; } if (same) return 1; }
        } else if (pass == 1 && last[0] == '.') {
            int cl; const char* cls = attr_get(e, "class", &cl);
            if (cls) {
                                                             
                const char* q = last + 1; int all = 1, anyc = 0;
                while (q < last + ll) {
                    const char* c = q; while (q < last + ll && *q != '.') q++;
                    int cn = (int)(q - c); if (q < last + ll) q++;
                    if (cn <= 0) continue;
                    anyc = 1;
                    int found = 0;
                    for (int i = 0; i + cn <= cl; i++) {
                        if ((i == 0 || is_ws(cls[i - 1])) && (i + cn == cl || is_ws(cls[i + cn]))) {
                            int same = 1; for (int k = 0; k < cn; k++) if (cls[i + k] != c[k]) { same = 0; break; }
                            if (same) { found = 1; break; }
                        }
                    }
                    if (!found) { all = 0; break; }
                }
                if (all && anyc) return 1;
            }
        } else if (pass == 0 && last[0] != '.' && last[0] != '#') {
            int tl = 0; while (tl < ll && last[tl] != '.' && last[tl] != '#' && last[tl] != ':' && last[tl] != '[') tl++;
            if (tl == ll) {
                if (tl == e->name_len) { int same = 1; for (int i = 0; i < tl; i++) { char a = last[i], b = e->name[i]; if (a >= 'A' && a <= 'Z') a = (char)(a + 32); if (b >= 'A' && b <= 'Z') b = (char)(b + 32); if (a != b) { same = 0; break; } } if (same) return 1; }
                if (tl == 1 && last[0] == '*') return 1;
            }
        }
    }
    return 0;
}

static void apply_element_style(sstyle_t* st, const elem_t* e) {
                                          
    for (int pass = 0; pass < 3 && rule_n; pass++)
        for (int i = 0; i < rule_n; i++)
            if (selector_matches(rules[i].sel, rules[i].sl, e, pass)) apply_style_string(st, rules[i].decl, rules[i].dl);
                               
    for (int i = 0; i < e->nattr; i++) {
        if (name_eq(e->attr[i].k, e->attr[i].kl, "style") || name_eq(e->attr[i].k, e->attr[i].kl, "transform")) continue;
        apply_style_prop(st, e->attr[i].k, e->attr[i].kl, e->attr[i].v, e->attr[i].vl);
    }
                         
    int sl; const char* sv = attr_get(e, "style", &sl);
    if (sv) apply_style_string(st, sv, sl);
}

                                                                      
                                                                        
                                                                      

static mat_t parse_transform(const char* s, int len) {
    mat_t m = mat_identity();
    const char* p = s; const char* end = s + len;
    while (p < end) {
        while (p < end && (is_ws(*p) || *p == ',')) p++;
        const char* fn = p;
        while (p < end && *p != '(' && !is_ws(*p)) p++;
        int fl = (int)(p - fn);
        while (p < end && *p != '(') p++;
        if (p >= end) break;
        p++;
        sfx a[6]; int n = 0;
        while (p < end && *p != ')') {
            sfx v;
            if (n < 6 && parse_sfx(&p, end, &v)) a[n++] = v;
            else p++;
        }
        if (p < end) p++;
        mat_t t = mat_identity();
        if (name_eq(fn, fl, "matrix") && n == 6) { t.a = a[0]; t.b = a[1]; t.c = a[2]; t.d = a[3]; t.e = a[4]; t.f = a[5]; }
        else if (name_eq(fn, fl, "translate")) { t.e = n > 0 ? a[0] : 0; t.f = n > 1 ? a[1] : 0; }
        else if (name_eq(fn, fl, "scale")) { t.a = n > 0 ? a[0] : SFX_ONE; t.d = n > 1 ? a[1] : t.a; }
        else if (name_eq(fn, fl, "rotate") && n >= 1) {
            sfx c = scos(a[0]), sn = ssin(a[0]);
            t.a = c; t.b = sn; t.c = -sn; t.d = c;
            if (n >= 3) {
                mat_t t1 = mat_identity(); t1.e = a[1]; t1.f = a[2];
                mat_t t2 = mat_identity(); t2.e = -a[1]; t2.f = -a[2];
                mat_t r = mat_mul(&t1, &t); t = mat_mul(&r, &t2);
            }
        }
        else if (name_eq(fn, fl, "skewx") && n >= 1) { sfx c = scos(a[0]); t.c = c ? sdiv(ssin(a[0]), c) : 0; }
        else if (name_eq(fn, fl, "skewy") && n >= 1) { sfx c = scos(a[0]); t.b = c ? sdiv(ssin(a[0]), c) : 0; }
        else continue;
        m = mat_mul(&m, &t);
    }
    return m;
}

                                                                      
                                                                        
                                                                      

static mat_t cur_mat;
static const mat_t* cur_m = &cur_mat;

static void P(sfx x, sfx y, sfx* ox, sfx* oy) { mat_apply(cur_m, x, y, ox, oy); }
static void moveto_u(sfx x, sfx y) { sfx dx, dy; P(x, y, &dx, &dy); path_moveto(dx, dy); }
static void lineto_u(sfx x, sfx y) { sfx dx, dy; P(x, y, &dx, &dy); path_lineto(dx, dy); }
static void cubic_u(sfx x0, sfx y0, sfx x1, sfx y1, sfx x2, sfx y2, sfx x3, sfx y3) {
    sfx a, b, c, d, e, f, g, h;
    P(x0, y0, &a, &b); P(x1, y1, &c, &d); P(x2, y2, &e, &f); P(x3, y3, &g, &h);
    flatten_cubic(a, b, c, d, e, f, g, h);
}
static void quad_u(sfx x0, sfx y0, sfx x1, sfx y1, sfx x2, sfx y2) {
    sfx a, b, c, d, e, f;
    P(x0, y0, &a, &b); P(x1, y1, &c, &d); P(x2, y2, &e, &f);
    flatten_quad(a, b, c, d, e, f);
}

                                                               
static void arc_u(sfx x1, sfx y1, sfx rx, sfx ry, sfx phi_deg, int large, int sweep, sfx x2, sfx y2) {
    if (rx == 0 || ry == 0) { lineto_u(x2, y2); return; }
    if (x1 == x2 && y1 == y2) return;
    rx = sabs(rx); ry = sabs(ry);
    sfx cphi = scos(phi_deg), sphi = ssin(phi_deg);
    sfx dx2 = (x1 - x2) / 2, dy2 = (y1 - y2) / 2;
    sfx x1p = smul(cphi, dx2) + smul(sphi, dy2);
    sfx y1p = -smul(sphi, dx2) + smul(cphi, dy2);
                                
    sfx lam = sdiv(smul(x1p, x1p), smul(rx, rx)) + sdiv(smul(y1p, y1p), smul(ry, ry));
    if (lam > SFX_ONE) { sfx s = ssqrt(lam); rx = smul(rx, s); ry = smul(ry, s); }
    sfx rx2 = smul(rx, rx), ry2 = smul(ry, ry);
    sfx num = smul(rx2, ry2) - smul(rx2, smul(y1p, y1p)) - smul(ry2, smul(x1p, x1p));
    sfx den = smul(rx2, smul(y1p, y1p)) + smul(ry2, smul(x1p, x1p));
    sfx coef = 0;
    if (den > 0 && num > 0) coef = ssqrt(sdiv(num, den));
    if (large == sweep) coef = -coef;
    sfx cxp = smul(coef, sdiv(smul(rx, y1p), ry));
    sfx cyp = -smul(coef, sdiv(smul(ry, x1p), rx));
    sfx cx = smul(cphi, cxp) - smul(sphi, cyp) + (x1 + x2) / 2;
    sfx cy = smul(sphi, cxp) + smul(cphi, cyp) + (y1 + y2) / 2;
                             
    sfx ux = sdiv(x1p - cxp, rx), uy = sdiv(y1p - cyp, ry);
    sfx vx = sdiv(-x1p - cxp, rx), vy = sdiv(-y1p - cyp, ry);
    sfx th1 = satan2(uy, ux);
    sfx th2 = satan2(vy, vx);
    sfx dth = th2 - th1;
    if (!sweep && dth > 0) dth -= SFX_INT(360);
    else if (sweep && dth < 0) dth += SFX_INT(360);
                                            
    sfx sc = mat_scale(cur_m);
    sfx dev_r = smul(rx > ry ? rx : ry, sc);
    int n = (int)((sabs(dth) >> SFX_SHIFT) / 6) + 2;
    int nr = (int)(dev_r >> SFX_SHIFT) / 2 + 4;
    if (n > nr) n = nr;
    if (n > 64) n = 64;
    if (n < 2) n = 2;
    for (int i = 1; i <= n; i++) {
        sfx th = th1 + dth * i / n;
        sfx ex = smul(rx, scos(th)), ey = smul(ry, ssin(th));
        sfx px = smul(cphi, ex) - smul(sphi, ey) + cx;
        sfx py = smul(sphi, ex) + smul(cphi, ey) + cy;
        if (i == n) lineto_u(x2, y2); else lineto_u(px, py);
    }
}

static void build_path(const char* d, int len) {
    const char* p = d; const char* end = d + len;
    char cmd = 0;
    sfx cx = 0, cy = 0, sx = 0, sy = 0, lcx = 0, lcy = 0;
    char last = 0;
    path_reset();
    while (p < end) {
        while (p < end && (is_ws(*p) || *p == ',')) p++;
        if (p >= end) break;
        char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) { cmd = c; p++; }
        else if (!cmd) break;
        else if (cmd == 'M') cmd = 'L';
        else if (cmd == 'm') cmd = 'l';
        int rel = cmd >= 'a' && cmd <= 'z';
        char C = rel ? (char)(cmd - 32) : cmd;
        sfx a[7];
        int need = (C == 'M' || C == 'L' || C == 'T') ? 2 : (C == 'H' || C == 'V') ? 1 : C == 'C' ? 6 : (C == 'S' || C == 'Q') ? 4 : C == 'A' ? 7 : 0;
        if (C == 'Z') { path_close(); cx = sx; cy = sy; last = 'Z'; continue; }
        if (!need) { p++; continue; }
        int got = 0;
        for (; got < need; got++) {
            if (C == 'A' && (got == 3 || got == 4)) {
                                                                            
                while (p < end && (is_ws(*p) || *p == ',')) p++;
                if (p < end && (*p == '0' || *p == '1')) { a[got] = *p == '1' ? SFX_ONE : 0; p++; continue; }
                break;
            }
            if (!parse_sfx(&p, end, &a[got])) break;
        }
        if (got < need) break;
        sfx ox = rel ? cx : 0, oy = rel ? cy : 0;
        switch (C) {
        case 'M': cx = ox + a[0]; cy = oy + a[1]; sx = cx; sy = cy; moveto_u(cx, cy); break;
        case 'L': cx = ox + a[0]; cy = oy + a[1]; lineto_u(cx, cy); break;
        case 'H': cx = ox + a[0]; lineto_u(cx, cy); break;
        case 'V': cy = oy + a[0]; lineto_u(cx, cy); break;
        case 'C': {
            sfx x1 = ox + a[0], y1 = oy + a[1], x2 = ox + a[2], y2 = oy + a[3], x3 = ox + a[4], y3 = oy + a[5];
            cubic_u(cx, cy, x1, y1, x2, y2, x3, y3); lcx = x2; lcy = y2; cx = x3; cy = y3; break;
        }
        case 'S': {
            sfx x1 = (last == 'C' || last == 'S') ? 2 * cx - lcx : cx, y1 = (last == 'C' || last == 'S') ? 2 * cy - lcy : cy;
            sfx x2 = ox + a[0], y2 = oy + a[1], x3 = ox + a[2], y3 = oy + a[3];
            cubic_u(cx, cy, x1, y1, x2, y2, x3, y3); lcx = x2; lcy = y2; cx = x3; cy = y3; break;
        }
        case 'Q': {
            sfx x1 = ox + a[0], y1 = oy + a[1], x2 = ox + a[2], y2 = oy + a[3];
            quad_u(cx, cy, x1, y1, x2, y2); lcx = x1; lcy = y1; cx = x2; cy = y2; break;
        }
        case 'T': {
            sfx x1 = (last == 'Q' || last == 'T') ? 2 * cx - lcx : cx, y1 = (last == 'Q' || last == 'T') ? 2 * cy - lcy : cy;
            sfx x2 = ox + a[0], y2 = oy + a[1];
            quad_u(cx, cy, x1, y1, x2, y2); lcx = x1; lcy = y1; cx = x2; cy = y2; break;
        }
        case 'A': {
            sfx x2 = ox + a[5], y2 = oy + a[6];
            arc_u(cx, cy, a[0], a[1], a[2], a[3] != 0, a[4] != 0, x2, y2);
            cx = x2; cy = y2; break;
        }
        }
        last = C;
    }
}

static void paint_current_path(const sstyle_t* st) {
    if (!st->visible || st->opacity <= 0) return;
    if (!st->fill_none && pts_n > 1) {
        emit_fill_edges();
        raster_fill(st->fill, st->opacity * st->fill_opacity / 255, st->evenodd);
    }
    if (!st->stroke_none && st->stroke_width > 0 && pts_n > 0) {
        sfx hw = smul(st->stroke_width, mat_scale(&st->ctm)) / 2;
        emit_stroke_edges(hw, st->linecap, st->linejoin);
        raster_fill(st->stroke, st->opacity * st->stroke_opacity / 255, 0);
    }
}

static void ellipse_path(sfx cx, sfx cy, sfx rx, sfx ry) {
    path_reset();
    sfx sc = mat_scale(cur_m);
    int n = (int)(smul(rx > ry ? rx : ry, sc) >> SFX_SHIFT) / 2 + 12;
    if (n > 96) n = 96;
    for (int i = 0; i < n; i++) {
        sfx ang = SFX_INT(360) * i / n;
        sfx x = cx + smul(rx, scos(ang)), y = cy + smul(ry, ssin(ang));
        if (i == 0) moveto_u(x, y); else lineto_u(x, y);
    }
    path_close();
}

static void rect_path(sfx x, sfx y, sfx w, sfx h, sfx rx, sfx ry) {
    path_reset();
    if (w <= 0 || h <= 0) return;
    if (rx > w / 2) rx = w / 2;
    if (ry > h / 2) ry = h / 2;
    if (rx <= 0 || ry <= 0) {
        moveto_u(x, y); lineto_u(x + w, y); lineto_u(x + w, y + h); lineto_u(x, y + h); path_close();
        return;
    }
                                                          
    moveto_u(x + rx, y);
    lineto_u(x + w - rx, y);
    for (int i = 1; i <= 6; i++) { sfx ang = SFX_INT(270) + SFX_INT(90) * i / 6; lineto_u(x + w - rx + smul(rx, scos(ang)), y + ry + smul(ry, ssin(ang))); }
    lineto_u(x + w, y + h - ry);
    for (int i = 1; i <= 6; i++) { sfx ang = SFX_INT(90) * i / 6; lineto_u(x + w - rx + smul(rx, scos(ang)), y + h - ry + smul(ry, ssin(ang))); }
    lineto_u(x + rx, y + h);
    for (int i = 1; i <= 6; i++) { sfx ang = SFX_INT(90) + SFX_INT(90) * i / 6; lineto_u(x + rx + smul(rx, scos(ang)), y + h - ry + smul(ry, ssin(ang))); }
    lineto_u(x, y + ry);
    for (int i = 1; i <= 6; i++) { sfx ang = SFX_INT(180) + SFX_INT(90) * i / 6; lineto_u(x + rx + smul(rx, scos(ang)), y + ry + smul(ry, ssin(ang))); }
    path_close();
}

static void poly_path(const char* s, int len, int close) {
    path_reset();
    const char* p = s; const char* end = s + len;
    int first = 1;
    for (;;) {
        sfx x, y;
        if (!parse_sfx(&p, end, &x)) break;
        if (!parse_sfx(&p, end, &y)) break;
        if (first) { moveto_u(x, y); first = 0; } else lineto_u(x, y);
    }
    if (close) path_close();
}

                                                                      
                                                                        
                                                                      

static const char* doc_begin;
static const char* doc_end;
static sfx vp_w, vp_h;                                                                
static int use_depth;
static int shapes_drawn;
#define SVG_MAX_SHAPES 6000

static sfx attr_len(const elem_t* e, const char* name, sfx ref, sfx dflt) {
    int l; const char* v = attr_get(e, name, &l);
    if (!v) return dflt;
    int ok; sfx r = parse_length(v, l, ref, &ok);
    return ok ? r : dflt;
}

static void render_children(const char* p, const char* end, const sstyle_t* inherited);

static void render_element(const elem_t* e, const char* content_end, const sstyle_t* parent) {
    if (shapes_drawn > SVG_MAX_SHAPES) return;
    sstyle_t st = *parent;
    int tl; const char* tr = attr_get(e, "transform", &tl);
    if (tr) { mat_t t = parse_transform(tr, tl); st.ctm = mat_mul(&parent->ctm, &t); }
    apply_element_style(&st, e);
    if (!st.visible) return;
    cur_mat = st.ctm;

    const char* n = e->name; int nl = e->name_len;
    if (name_eq(n, nl, "g") || name_eq(n, nl, "a") || name_eq(n, nl, "switch") || name_eq(n, nl, "svg") || name_eq(n, nl, "symbol")) {
        if (name_eq(n, nl, "svg") && e->start != doc_begin) {
                                                                       
            sfx x = attr_len(e, "x", vp_w, 0), y = attr_len(e, "y", vp_h, 0);
            mat_t t = mat_identity(); t.e = x; t.f = y;
            st.ctm = mat_mul(&st.ctm, &t);
        }
        if (!e->self_closing) render_children(e->after, content_end, &st);
        return;
    }
    if (name_eq(n, nl, "path")) {
        int dl; const char* d = attr_get(e, "d", &dl);
        if (d && dl > 0) { build_path(d, dl); paint_current_path(&st); shapes_drawn++; }
        return;
    }
    if (name_eq(n, nl, "rect")) {
        sfx x = attr_len(e, "x", vp_w, 0), y = attr_len(e, "y", vp_h, 0);
        sfx w = attr_len(e, "width", vp_w, 0), h = attr_len(e, "height", vp_h, 0);
        sfx rx = attr_len(e, "rx", vp_w, -1), ry = attr_len(e, "ry", vp_h, -1);
        if (rx < 0 && ry < 0) rx = ry = 0; else if (rx < 0) rx = ry; else if (ry < 0) ry = rx;
        rect_path(x, y, w, h, rx, ry); paint_current_path(&st); shapes_drawn++;
        return;
    }
    if (name_eq(n, nl, "circle")) {
        sfx cx = attr_len(e, "cx", vp_w, 0), cy = attr_len(e, "cy", vp_h, 0), r = attr_len(e, "r", vp_w, 0);
        if (r > 0) { ellipse_path(cx, cy, r, r); paint_current_path(&st); shapes_drawn++; }
        return;
    }
    if (name_eq(n, nl, "ellipse")) {
        sfx cx = attr_len(e, "cx", vp_w, 0), cy = attr_len(e, "cy", vp_h, 0);
        sfx rx = attr_len(e, "rx", vp_w, 0), ry = attr_len(e, "ry", vp_h, 0);
        if (rx > 0 && ry > 0) { ellipse_path(cx, cy, rx, ry); paint_current_path(&st); shapes_drawn++; }
        return;
    }
    if (name_eq(n, nl, "line")) {
        path_reset();
        moveto_u(attr_len(e, "x1", vp_w, 0), attr_len(e, "y1", vp_h, 0));
        lineto_u(attr_len(e, "x2", vp_w, 0), attr_len(e, "y2", vp_h, 0));
        st.fill_none = 1;
        paint_current_path(&st); shapes_drawn++;
        return;
    }
    if (name_eq(n, nl, "polyline") || name_eq(n, nl, "polygon")) {
        int pl; const char* pv = attr_get(e, "points", &pl);
        int closed = name_eq(n, nl, "polygon");
        if (pv) { poly_path(pv, pl, closed); if (!closed) {                                              } paint_current_path(&st); shapes_drawn++; }
        return;
    }
    if (name_eq(n, nl, "use")) {
        int hl; const char* href = attr_get(e, "href", &hl);
        if (!href) href = attr_get(e, "xlink:href", &hl);
        if (!href || hl < 2 || href[0] != '#' || use_depth >= 4) return;
        const char* target = NULL;
        for (int i = 0; i < id_n; i++) {
            if (ids[i].idl != hl - 1) continue;
            int same = 1; for (int k = 0; k < hl - 1; k++) if (ids[i].id[k] != href[1 + k]) { same = 0; break; }
            if (same) { target = ids[i].elem; break; }
        }
        if (!target) return;
        elem_t te;
        if (!parse_elem(target, doc_end, &te)) return;
        sfx x = attr_len(e, "x", vp_w, 0), y = attr_len(e, "y", vp_h, 0);
        mat_t t = mat_identity(); t.e = x; t.f = y;
        sstyle_t us = st; us.ctm = mat_mul(&st.ctm, &t);
        const char* tend = skip_subtree(&te, doc_end);
        use_depth++;
        render_element(&te, tend, &us);
        use_depth--;
        return;
    }
                                                                                                
}

static void render_children(const char* p, const char* end, const sstyle_t* inherited) {
    if (br_stack_headroom() < BR_STACK_MIN) return;
    while (p < end) {
        p = next_tag(p, end);
        if (p >= end) break;
        elem_t e;
        if (!parse_elem(p, end, &e)) break;
        if (e.end_tag) { p = e.after; return; }
        const char* sub_end = skip_subtree(&e, end);
        render_element(&e, sub_end, inherited);
        p = sub_end;
        if (shapes_drawn > SVG_MAX_SHAPES) return;
    }
}

                                                     
static void prescan(const char* p, const char* end) {
    id_n = 0; rule_n = 0; grad_n = 0;
    while (p < end) {
        p = next_tag(p, end);
        if (p >= end) break;
        elem_t e;
        if (!parse_elem(p, end, &e)) break;
        if (e.end_tag) { p = e.after; continue; }
        int il; const char* id = attr_get(&e, "id", &il);
        if (id && il > 0 && id_n < SVG_MAX_IDS) { ids[id_n].id = id; ids[id_n].idl = il; ids[id_n].elem = e.start; id_n++; }
        if (name_eq(e.name, e.name_len, "style") && !e.self_closing) {
            const char* q = e.after;
            while (q < end && !(q[0] == '<' && q + 7 < end && q[1] == '/' && (q[2] == 's' || q[2] == 'S'))) q++;
            collect_style_rules(e.after, q);
            p = q;
            continue;
        }
        if ((name_eq(e.name, e.name_len, "linearGradient") || name_eq(e.name, e.name_len, "radialGradient")) && id && grad_n < SVG_MAX_GRAD) {
            grads[grad_n].id = id; grads[grad_n].idl = il; grads[grad_n].has = 0; grads[grad_n].color = 0xFF808080u;
                                   
            const char* q = e.after; const char* gend = skip_subtree(&e, end);
            int stops = 0; uint32_t r = 0, g = 0, b = 0, a = 0;
            while (q < gend) {
                q = next_tag(q, gend);
                if (q >= gend) break;
                elem_t s;
                if (!parse_elem(q, gend, &s)) break;
                if (!s.end_tag && name_eq(s.name, s.name_len, "stop")) {
                    uint32_t c = 0; int have = 0;
                    int cl; const char* cv = attr_get(&s, "stop-color", &cl);
                    if (cv && parse_paint(cv, cl, &c) == 1) have = 1;
                    int sl; const char* sv = attr_get(&s, "style", &sl);
                    if (sv) { sstyle_t tmp; memset(&tmp, 0, sizeof(tmp)); tmp.opacity = 255; const char* z = my_strstr_ci_n(sv, sl, "stop-color"); if (z) { z += 10; while (z < sv + sl && (*z == ':' || is_ws(*z))) z++; int zl = 0; while (z + zl < sv + sl && z[zl] != ';') zl++; if (parse_paint(z, zl, &c) == 1) have = 1; } }
                    if (have) { r += (c >> 16) & 0xFF; g += (c >> 8) & 0xFF; b += c & 0xFF; a += c >> 24; stops++; }
                }
                q = s.after;
            }
            if (stops) { grads[grad_n].color = ((a / stops) << 24) | ((r / stops) << 16) | ((g / stops) << 8) | (b / stops); grads[grad_n].has = 1; }
            grad_n++;
        }
        p = e.after;
    }
}

                                                                                  
static const char* my_strstr_ci_n(const char* hay, int n, const char* needle) {
    int nl = (int)strlen(needle);
    for (int i = 0; i + nl <= n; i++) {
        int k = 0;
        for (; k < nl; k++) { char a = hay[i + k], b = needle[k]; if (a >= 'A' && a <= 'Z') a = (char)(a + 32); if (b >= 'A' && b <= 'Z') b = (char)(b + 32); if (a != b) break; }
        if (k == nl) return hay + i;
    }
    return NULL;
}

                                                                      
                                                                        
                                                                      

int br_svg_looks_like(const uint8_t* data, int len) {
    int i = 0;
    while (i < len && (is_ws((char)data[i]) || data[i] == 0xEF || data[i] == 0xBB || data[i] == 0xBF)) i++;
    if (i + 4 < len && data[i] == '<') {
        if (my_strstr_ci_n((const char*)data + i, len - i < 1024 ? len - i : 1024, "<svg")) return 1;
    }
    return 0;
}

                                                                                                              
int br_svg_intrinsic_size(const char* src, int len, int* w, int* h) {
    const char* end = src + len;
    const char* p = next_tag(src, end);
    elem_t e;
    *w = *h = 0;
    while (p < end && parse_elem(p, end, &e) && !name_eq(e.name, e.name_len, "svg")) p = next_tag(e.after, end);
    if (p >= end || !name_eq(e.name, e.name_len, "svg")) return 0;
    int vl; const char* vb = attr_get(&e, "viewBox", &vl);
    sfx vx = 0, vy = 0, vw = 0, vh = 0;
    if (vb) { const char* q = vb; parse_sfx(&q, vb + vl, &vx); parse_sfx(&q, vb + vl, &vy); parse_sfx(&q, vb + vl, &vw); parse_sfx(&q, vb + vl, &vh); }
    int wl, hl; const char* ws = attr_get(&e, "width", &wl); const char* hs = attr_get(&e, "height", &hl);
    int ok_w = 0, ok_h = 0; sfx aw = 0, ah = 0;
    if (ws && !(wl > 0 && ws[wl - 1] == '%')) aw = parse_length(ws, wl, 0, &ok_w);
    if (hs && !(hl > 0 && hs[hl - 1] == '%')) ah = parse_length(hs, hl, 0, &ok_h);
    if (ok_w && ok_h && aw > 0 && ah > 0) { *w = (int)((aw + SFX_HALF) >> SFX_SHIFT); *h = (int)((ah + SFX_HALF) >> SFX_SHIFT); return 1; }
    if (vw > 0 && vh > 0) {
        if (ok_w && aw > 0) { *w = (int)((aw + SFX_HALF) >> SFX_SHIFT); *h = (int)((smul(aw, sdiv(vh, vw)) + SFX_HALF) >> SFX_SHIFT); return 1; }
        if (ok_h && ah > 0) { *h = (int)((ah + SFX_HALF) >> SFX_SHIFT); *w = (int)((smul(ah, sdiv(vw, vh)) + SFX_HALF) >> SFX_SHIFT); return 1; }
        *w = (int)((vw + SFX_HALF) >> SFX_SHIFT); *h = (int)((vh + SFX_HALF) >> SFX_SHIFT);
        return 0;
    }
    return 0;
}

 
                                                                          
                                                                          
  
int br_svg_render(const char* src, int len, int want_w, int want_h, uint32_t current_color,
                  uint32_t* out, size_t max_pixels, int* out_w_p, int* out_h_p) {
    if (!src || len < 5) return -1;
    const char* end = src + len;
    const char* p = next_tag(src, end);
    elem_t root;
    while (p < end && parse_elem(p, end, &root) && !name_eq(root.name, root.name_len, "svg")) p = next_tag(root.after, end);
    if (p >= end || !name_eq(root.name, root.name_len, "svg")) return -1;
    doc_begin = root.start; doc_end = end;

               
    int vl; const char* vb = attr_get(&root, "viewBox", &vl);
    sfx vx = 0, vy = 0, vw = 0, vh = 0;
    if (vb) { const char* q = vb; parse_sfx(&q, vb + vl, &vx); parse_sfx(&q, vb + vl, &vy); parse_sfx(&q, vb + vl, &vw); parse_sfx(&q, vb + vl, &vh); }
    int iw = 0, ih = 0;
    int has_intrinsic = br_svg_intrinsic_size(src, len, &iw, &ih);
    if (vw <= 0 || vh <= 0) { vx = vy = 0; vw = SFX_INT(iw > 0 ? iw : (want_w > 0 ? want_w : 300)); vh = SFX_INT(ih > 0 ? ih : (want_h > 0 ? want_h : 150)); }

    int W = want_w, H = want_h;
    if (W <= 0 && H <= 0) {
        if (has_intrinsic) { W = iw; H = ih; }
        else { W = (int)(vw >> SFX_SHIFT); H = (int)(vh >> SFX_SHIFT); }
    } else if (W <= 0) W = (int)((SFX_INT(H) * vw / vh) >> SFX_SHIFT);
    else if (H <= 0) H = (int)((SFX_INT(W) * vh / vw) >> SFX_SHIFT);
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (W > SVG_MAX_W) W = SVG_MAX_W;
    if (H > 4096) H = 4096;
    if ((size_t)W * (size_t)H > max_pixels) return -1;

    out_px = out; out_w = W; out_h = H;
    memset(out, 0, (size_t)W * H * 4);
    sub_lines = H <= 48 ? 16 : H <= 200 ? 8 : 4;
    edge_count = 0;
    shapes_drawn = 0;
    use_depth = 0;
    cur_color_val = current_color ? current_color : 0xFF000000u;

                                                                                     
    sfx sx = sdiv(SFX_INT(W), vw), sy = sdiv(SFX_INT(H), vh);
    int pl; const char* par = attr_get(&root, "preserveAspectRatio", &pl);
    int none = par && my_strstr_ci_n(par, pl, "none");
    int slice = par && my_strstr_ci_n(par, pl, "slice");
    int ax = 1, ay = 1;                                  
    if (par) {
        if (my_strstr_ci_n(par, pl, "xMin")) ax = 0; else if (my_strstr_ci_n(par, pl, "xMax")) ax = 2;
        if (my_strstr_ci_n(par, pl, "YMin")) ay = 0; else if (my_strstr_ci_n(par, pl, "YMax")) ay = 2;
    }
    sfx s = sx;
    if (!none) {
        s = slice ? (sx > sy ? sx : sy) : (sx < sy ? sx : sy);
        sx = sy = s;
    }
    sfx tx = -smul(vx, sx), ty = -smul(vy, sy);
    sfx slack_x = SFX_INT(W) - smul(vw, sx), slack_y = SFX_INT(H) - smul(vh, sy);
    tx += ax == 1 ? slack_x / 2 : ax == 2 ? slack_x : 0;
    ty += ay == 1 ? slack_y / 2 : ay == 2 ? slack_y : 0;

    sstyle_t st;
    memset(&st, 0, sizeof(st));
    st.fill = 0xFF000000u; st.fill_none = 0;
    st.stroke = 0; st.stroke_none = 1;
    st.stroke_width = SFX_ONE;
    st.opacity = st.fill_opacity = st.stroke_opacity = 255;
    st.visible = 1;
    st.ctm.a = sx; st.ctm.b = 0; st.ctm.c = 0; st.ctm.d = sy; st.ctm.e = tx; st.ctm.f = ty;
    vp_w = vw; vp_h = vh;

    prescan(root.after, end);

                                                                                  
    apply_element_style(&st, &root);
    cur_mat = st.ctm;
    if (!root.self_closing) render_children(root.after, end, &st);

    *out_w_p = W; *out_h_p = H;
    return 0;
}
