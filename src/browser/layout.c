/* Layout + painting boxes for the SharkOS browser.
 *
 * A single pass turns the styled DOM into a flat list of paint boxes in page
 * coordinates. Blocks stack vertically; inline content flows into lines with
 * word wrapping; list items get bullets/numbers; tables get simple equal
 * column widths; images are decoded PNGs or placeholders. */

#include "browser_internal.h"

extern int png_decode_buf(const uint8_t* data, size_t len, int* out_w, int* out_h,
                          uint32_t* pixels, size_t max_pixels,
                          uint8_t* scratch, size_t scratch_len);
extern int gif_decode_buf(const uint8_t* data, size_t len, int* out_w, int* out_h,
                          uint32_t* pixels, size_t max_pixels,
                          uint8_t* scratch, size_t scratch_len);
extern int jpeg_decode_buf(const uint8_t* data, size_t len, int* out_w, int* out_h,
                           uint32_t* pixels, size_t max_pixels,
                           uint8_t* scratch, size_t scratch_len);

static br_box_t boxes[BR_MAX_BOXES];
static int box_count = 0;

int br_box_count(void) { return box_count; }
br_box_t* br_box(int i) { return (i >= 0 && i < box_count) ? &boxes[i] : NULL; }

/* ------------------------------------------------------------ images */

static br_image_t images[BR_MAX_IMAGES];
static int image_count = 0;
static uint8_t img_buf[BR_IMG_MAX_BYTES];

/* Decoded pixels live in a per-page arena (the kernel heap never frees, so
 * allocating per image would leak on every page load). Pages whose images do
 * not fit simply get placeholders for the rest. */
static uint32_t img_arena[BR_IMG_ARENA_PIXELS];
static size_t img_arena_used = 0;
static uint8_t img_scratch[BR_IMG_SCRATCH_BYTES];

void br_layout_reset_images(void) {
    image_count = 0;
    img_arena_used = 0;
    memset(images, 0, sizeof(images));
}

br_image_t* br_image_get(int i) { return (i >= 0 && i < image_count) ? &images[i] : NULL; }

/* Area-average an image down to nw x nh in place (nw <= w, nh <= h). */
static void downsample(uint32_t* px, int w, int h, int nw, int nh) {
    for (int dy = 0; dy < nh; dy++) {
        int sy0 = dy * h / nh, sy1 = (dy + 1) * h / nh; if (sy1 <= sy0) sy1 = sy0 + 1;
        for (int dx = 0; dx < nw; dx++) {
            int sx0 = dx * w / nw, sx1 = (dx + 1) * w / nw; if (sx1 <= sx0) sx1 = sx0 + 1;
            uint32_t a = 0, r = 0, g = 0, b = 0, n = 0;
            for (int y = sy0; y < sy1 && y < h; y++) {
                const uint32_t* row = px + (size_t)y * w;
                for (int x = sx0; x < sx1 && x < w; x++) {
                    uint32_t p = row[x];
                    a += p >> 24; r += (p >> 16) & 0xFF; g += (p >> 8) & 0xFF; b += p & 0xFF; n++;
                }
            }
            if (!n) n = 1;
            px[(size_t)dy * nw + dx] = ((a / n) << 24) | ((r / n) << 16) | ((g / n) << 8) | (b / n);   /* dst index <= src index: safe in place */
        }
    }
}

int br_image_request(const char* src) { return br_image_request_sized(src, 0, 0); }

int br_image_request_sized(const char* src, int want_w, int want_h) {
    for (int i = 0; i < image_count; i++) if (br_streq(images[i].src, src)) return i;
    if (image_count >= BR_MAX_IMAGES) return -1;
    br_image_t* im = &images[image_count];
    im->src = br_strdup(src);
    im->pixels = NULL; im->w = im->h = 0; im->failed = 0;
    /* Fetch synchronously and decode by file signature: PNG (incl. Adam7),
     * GIF (first frame) and baseline JPEG. Anything else (SVG, WebP,
     * progressive JPEG, oversize files) becomes a placeholder. */
    char abs[BR_URL_MAX];
    br_resolve_url(brs.url, src, abs, sizeof(abs));
    if (br_streq_prefix(abs, "data:")) { im->failed = 1; return image_count++; }
    br_status("Loading image...");
    int n = br_fetch_resource(abs, img_buf, sizeof(img_buf));
    im->failed = 1;
    if (n > 8) {
        int w = 0, h = 0, rc = -1;
        uint32_t* px = img_arena + img_arena_used;
        size_t room = BR_IMG_ARENA_PIXELS - img_arena_used;
        if (img_buf[0] == 0x89 && img_buf[1] == 'P' && img_buf[2] == 'N' && img_buf[3] == 'G')
            rc = png_decode_buf(img_buf, (size_t)n, &w, &h, px, room, img_scratch, sizeof(img_scratch));
        else if (img_buf[0] == 'G' && img_buf[1] == 'I' && img_buf[2] == 'F' && img_buf[3] == '8')
            rc = gif_decode_buf(img_buf, (size_t)n, &w, &h, px, room, img_scratch, sizeof(img_scratch));
        else if (img_buf[0] == 0xFF && img_buf[1] == 0xD8)
            rc = jpeg_decode_buf(img_buf, (size_t)n, &w, &h, px, room, img_scratch, sizeof(img_scratch));
        if (rc == 0 && w > 0 && h > 0) {
            /* keep only what will be shown: shrink to the display size so a
             * page of thumbnails does not exhaust the pixel arena */
            if (want_w > 0 && !want_h) want_h = h * want_w / w;
            if (want_h > 0 && !want_w) want_w = w * want_h / h;
            if (want_w > 0 && want_h > 0 && want_w < w && want_h < h) { downsample(px, w, h, want_w, want_h); w = want_w; h = want_h; }
            im->pixels = px; im->w = w; im->h = h; im->failed = 0;
            img_arena_used += (size_t)w * h;
        }
    }
    return image_count++;
}

/* ------------------------------------------------------------- boxes */

static br_box_t* new_box(int kind, br_node_t* node) {
    if (box_count >= BR_MAX_BOXES) return NULL;
    br_box_t* b = &boxes[box_count++];
    memset(b, 0, sizeof(*b));
    b->kind = kind;
    b->node = node;
    b->scale = 2;
    return b;
}

static br_node_t* link_ancestor(br_node_t* n) {
    while (n) {
        if (n->type == BR_NODE_ELEMENT && br_streq(n->tag, "a") && br_attr(n, "href")) return n;
        n = n->parent;
    }
    return NULL;
}

/* ------------------------------------------------------ inline flow */

typedef struct {
    int x0, x1;            /* content left/right in page coords */
    int y;                 /* top of current line */
    int cur_x;             /* pen position */
    int line_h;            /* tallest thing on this line */
    int line_start_box;    /* first box of the current line (for align) */
    int align;             /* 0/1/2 */
    int any;               /* anything on the current line */
    int max_x;             /* widest line (for shrink-to-fit cells) */
    int x1_full;           /* x1 before a right float shortened this line */
} flow_t;

/* ---- text metrics: real TrueType faces (see font.c) ---- */

typedef struct { int face, px, ascent, descent, lh; } tm_t;

static void text_metrics(const br_style_t* st, tm_t* m) {
    int fam = st->font_family;
    if (st->monospace && fam < FONT_FAMILY_WEB) fam = FONT_FAMILY_MONO;
    m->px = st->font_px > 0 ? st->font_px : 16;
    m->face = br_font_face(fam, st->bold);
    m->ascent = br_font_ascent(m->face, m->px);
    m->descent = br_font_descent(m->face, m->px);
    int normal = br_font_line_height(m->face, m->px);
    if (st->line_height > 0) m->lh = st->line_height;
    else if (st->line_height < 0) m->lh = (m->px * -st->line_height) / 100;
    else m->lh = normal;
    if (m->lh < m->ascent + m->descent - 2) m->lh = m->ascent + m->descent - 2;   /* never clip glyphs */
    if (m->lh < 4) m->lh = 4;
}

static int line_height_of(const br_style_t* st) { tm_t m; text_metrics(st, &m); return m.lh; }

static int measuring = 0;          /* shrink-to-fit trial pass: no alignment */

static void flow_align_line(flow_t* f) {
    if (f->align == 0 || !f->any || measuring) return;
    int used = f->cur_x - f->x0;
    int free_px = (f->x1 - f->x0) - used;
    if (free_px <= 0) return;
    int shift = (f->align == 1) ? free_px / 2 : free_px;
    for (int i = f->line_start_box; i < box_count; i++) {
        if (boxes[i].y >= f->y || boxes[i].group) boxes[i].x += shift;
    }
    /* the nodes' hit-test rectangles follow their boxes */
    for (int i = f->line_start_box; i < box_count; i++) {
        br_node_t* n = boxes[i].node;
        if (n && n->type == BR_NODE_ELEMENT && boxes[i].group && boxes[i].group - 1 == i) n->lx += shift;
    }
}

static void flow_newline(flow_t* f) {
    /* a space glued to the last word of the line is not part of the line */
    if (box_count > f->line_start_box) {
        br_box_t* last = &boxes[box_count - 1];
        if (last->kind == BR_BOX_TEXT && !last->group && last->text_len == 1 && last->text[0] == ' ' && last->y == f->y) {
            f->cur_x -= last->w;
            box_count--;
        }
    }
    flow_align_line(f);
    /* Vertical alignment (vertical-align: baseline for everything):
     *   text runs contribute ascent(+half-leading) above and descent below;
     *   images/controls sit on the baseline; inline-blocks align the
     *   baseline of their last text line (or their bottom edge). */
    int text_above = 0, text_below = 0, nt_above = 0, nt_below = 0, has_text = 0;
    for (int i = f->line_start_box; i < box_count; i++) {
        br_box_t* b = &boxes[i];
        if (b->group) {
            if (b->group - 1 == i && b->group_base >= 0) { if (b->group_base > nt_above) nt_above = b->group_base; if (b->group_h - b->group_base > nt_below) nt_below = b->group_h - b->group_base; }
            continue;
        }
        if (b->kind == BR_BOX_RECT) continue;
        if (b->kind == BR_BOX_TEXT) {
            if (b->y != f->y) continue;
            int above = b->baseline + (b->lh - b->h) / 2;
            if (above > text_above) text_above = above;
            if (b->lh - above > text_below) text_below = b->lh - above;
            has_text = 1;
        } else if (b->y == f->y) {
            /* controls hang a little below the baseline (like real browsers
             * centre them on the text); images sit exactly on it */
            int drop = (b->kind == BR_BOX_INPUT || b->kind == BR_BOX_BUTTON || b->kind == BR_BOX_CHECKBOX) ? b->h / 4 : 0;
            if (b->h - drop > nt_above) nt_above = b->h - drop;
            if (drop > nt_below) nt_below = drop;
        }
    }
    int baseline = text_above > nt_above ? text_above : nt_above;
    int below = text_below > nt_below ? text_below : nt_below;
    if (has_text || nt_above) {
        for (int i = f->line_start_box; i < box_count; i++) {
            br_box_t* b = &boxes[i];
            if (b->group) {
                br_box_t* lead = &boxes[b->group - 1];
                if (lead->group_base < 0) continue;                 /* floats stay at the line top */
                int shift = baseline - lead->group_base;
                if (shift > 0) { b->y += shift; if (b->group - 1 == i && b->node) b->node->ly += shift; }
                continue;
            }
            if (b->kind == BR_BOX_RECT) continue;
            if (b->kind == BR_BOX_TEXT) { if (b->y == f->y) b->y = f->y + baseline - b->baseline; }
            else if (b->y == f->y) {
                int drop = (b->kind == BR_BOX_INPUT || b->kind == BR_BOX_BUTTON || b->kind == BR_BOX_CHECKBOX) ? b->h / 4 : 0;
                int shift = baseline - (b->h - drop);
                if (shift > 0) { b->y += shift; if (b->node) b->node->ly += shift; }
            }
        }
        if (baseline + below > f->line_h) f->line_h = baseline + below;
    }
    if (f->cur_x - f->x0 > f->max_x) f->max_x = f->cur_x - f->x0;
    f->y += f->line_h > 0 ? f->line_h : 19;
    f->cur_x = f->x0;
    f->line_h = 0;
    f->line_start_box = box_count;
    f->any = 0;
    if (f->x1_full) { f->x1 = f->x1_full; f->x1_full = 0; }   /* right floats end with their line */
}

static void flow_place(flow_t* f, int w, int h) {
    if (f->any && f->cur_x + w > f->x1) flow_newline(f);
    if (h > f->line_h) f->line_h = h;
    f->any = 1;
}

static br_box_t* text_box(flow_t* f, br_node_t* tn, const tm_t* m, const char* s, int len, int w,
                          uint32_t color, const br_style_t* st, br_node_t* link, int mono) {
    br_box_t* b = new_box(BR_BOX_TEXT, tn);
    if (!b) return NULL;
    b->x = f->cur_x; b->y = f->y; b->w = w; b->h = m->ascent + m->descent;
    b->text = s; b->text_len = len;
    b->color = color; b->bg = 0;
    b->scale = st->font_scale; b->font_px = m->px; b->face = m->face; b->baseline = m->ascent; b->lh = m->lh;
    b->bold = st->bold; b->italic = st->italic; b->underline = st->underline; b->strike = st->strike; b->mono = mono; b->link = link;
    return b;
}

/* Longest prefix of s (byte length n) that fits in max_w pixels, on a UTF-8
 * boundary. Always takes at least one character. */
static int fit_chars(const tm_t* m, const char* s, int n, int max_w, int* out_w) {
    int i = 0, w = 0;
    while (i < n) {
        uint32_t cp;
        int l = br_utf8_decode(s + i, n - i, &cp);
        int cw = br_font_char_width(m->face, m->px, cp);
        if (i > 0 && w + cw > max_w) break;
        w += cw; i += l;
    }
    *out_w = w;
    return i;
}

static void flow_text(flow_t* f, br_node_t* tn, const br_style_t* st, int pre) {
    const char* s = tn->text;
    tm_t m; text_metrics(st, &m);
    int lh = m.lh;
    br_node_t* link = link_ancestor(tn);
    uint32_t color = st->color;
    int i = 0;
    int len = (int)strlen(s);
    if (st->text_transform && br_text_in_pool(s)) {
        /* transform in place (ASCII only; the pool string belongs to this node) */
        char* t = (char*)s;
        int start = 1;
        for (int k = 0; k < len; k++) {
            char c = t[k];
            if (st->text_transform == 1 && c >= 'a' && c <= 'z') t[k] = (char)(c - 32);
            else if (st->text_transform == 2 && c >= 'A' && c <= 'Z') t[k] = (char)(c + 32);
            else if (st->text_transform == 3 && start && c >= 'a' && c <= 'z') t[k] = (char)(c - 32);
            start = (c == ' ' || c == '\n');
        }
    }
    int space_w = br_font_char_width(m.face, m.px, ' ');
    if (space_w < 2) space_w = 2;
    int avail = f->x1 - f->x0; if (avail < 8) avail = 8;
    if (tn->lx == 0 && tn->ly == 0) { tn->lx = f->cur_x; tn->ly = f->y; }
    while (i < len) {
        if (pre) {
            /* split on newlines only; long lines hard-wrap at the box edge */
            int st_i = i;
            while (i < len && s[i] != '\n') i++;
            int n = i - st_i;
            int off = 0;
            while (off < n || (n == 0 && off == 0)) {
                int w = 0;
                int take = n ? fit_chars(&m, &s[st_i + off], n - off, avail, &w) : 0;
                flow_place(f, w, lh);
                if (!text_box(f, tn, &m, &s[st_i + off], take, w, color, st, link, 1)) return;
                f->cur_x += w;
                off += take;
                if (off < n) flow_newline(f);
                if (n == 0) break;
            }
            if (i < len && s[i] == '\n') { flow_newline(f); i++; }
            continue;
        }
        if (s[i] == ' ') {
            /* a space glues to the previous word; it is dropped at a line start */
            if (f->cur_x > f->x0 && f->any && f->cur_x + space_w <= f->x1) {
                if (!text_box(f, tn, &m, &s[i], 1, space_w, color, st, link, st->monospace)) return;
                if (lh > f->line_h) f->line_h = lh;
                f->cur_x += space_w;
            }
            i++;
            continue;
        }
        int ws = i;
        while (i < len && s[i] != ' ') i++;
        int wl = i - ws;
        int ww = br_font_text_width(m.face, m.px, &s[ws], wl);
        if (ww <= avail) {
            flow_place(f, ww, lh);
            if (!text_box(f, tn, &m, &s[ws], wl, ww, color, st, link, st->monospace)) return;
            f->cur_x += ww;
            continue;
        }
        /* word wider than the line: hard-break it character-wise */
        int off = 0;
        while (off < wl) {
            int room = f->x1 - f->cur_x;
            if (f->any && room < avail / 4) { flow_newline(f); room = f->x1 - f->cur_x; }
            int w = 0;
            int take = fit_chars(&m, &s[ws + off], wl - off, room, &w);
            flow_place(f, w, lh);
            if (!text_box(f, tn, &m, &s[ws + off], take, w, color, st, link, st->monospace)) return;
            f->cur_x += w;
            off += take;
        }
    }
    tn->lw = f->cur_x - tn->lx; tn->lh = f->y + lh - tn->ly;
}

/* ------------------------------------------------------------ blocks */

static int layout_block(br_node_t* n, int x, int y, int width, int list_index);
static void layout_inline_children(br_node_t* n, flow_t* f, int pre);

static int is_inline_node(const br_node_t* c) {
    if (c->type == BR_NODE_TEXT) return 1;
    int d = c->style.display;
    if (d == BR_DISPLAY_NONE) return 0;
    if (c->style.float_dir && d != BR_DISPLAY_TABLE) return 1;         /* floats flow like inline-blocks */
    return d == BR_DISPLAY_INLINE || d == BR_DISPLAY_INLINE_BLOCK;
}

/* Natural (max-content) margin-box width of a block that has just been laid
 * out: its content's used width (recorded by layout_block in used_w) plus
 * padding, borders and margins, never below min-width. */
static int natural_width(const br_node_t* c) {
    const br_style_t* st = &c->style;
    int ml = st->margin_l > 0 ? st->margin_l : 0, mr = st->margin_r > 0 ? st->margin_r : 0;
    int extra = st->border_l + st->border_r + st->padding_l + st->padding_r;
    int bw;
    if (st->width > 0) bw = st->width + (st->border_box ? 0 : extra) + ml + mr;
    else bw = c->used_w + extra + ml + mr;
    if (st->min_width > 0) {
        int mw = st->min_width + ml + mr + (st->border_box ? 0 : extra);
        if (mw > bw) bw = mw;
    }
    return bw;
}

static int ib_depth = 0;           /* nesting of shrink-to-fit passes (cost guard) */

static void flow_inline_block(br_node_t* c, flow_t* f) {
    /* inline-blocks and floats are shrink-to-fit:
     * width = min(max(min-content, available), preferred). Pass 1 lays the
     * block out at the full container width (so nothing wraps that does not
     * have to) and reads its natural width; pass 2 lays it out at exactly
     * that width. When it does not fit on the current line we break first;
     * when it is wider than a whole line its content wraps within it. */
    int save = box_count;
    int full = f->x1 - f->x0;
    int fixed = c->style.width > 0 ? natural_width(c) :
                c->style.width < -1 ? full * (-(c->style.width) - 2) / 100 + c->style.margin_l + c->style.margin_r : 0;
    if (fixed > full) fixed = full;
    int avail = f->x1 - f->cur_x;
    int h, bw;
    if (fixed) {
        if (f->any && fixed > avail) { flow_newline(f); avail = f->x1 - f->cur_x; }
        bw = fixed;
    } else if (ib_depth >= 5) {
        bw = avail;                                   /* absurd nesting: no measuring pass */
    } else {
        /* pass 1: measure the preferred width (alignment off) */
        int was = measuring;
        measuring = 1; ib_depth++;
        layout_block(c, f->cur_x, f->y, full, 0);
        bw = natural_width(c);
        box_count = save;
        if (bw > avail && f->any && avail < full) { flow_newline(f); avail = f->x1 - f->cur_x; }
        if (bw > avail) {
            /* wider than the line: wrap the content within it and re-measure */
            layout_block(c, f->cur_x, f->y, avail, 0);
            bw = natural_width(c);
            box_count = save;
            if (bw > avail) bw = avail;
        }
        measuring = was; ib_depth--;
    }
    /* float:right (and only that) is placed against the right edge of the
     * line; everything else takes the pen position */
    int px = f->cur_x;
    if (c->style.float_dir == 2 && !measuring) {
        int rx = f->x1 - bw;
        if (rx > f->cur_x) px = rx;
    }
    /* pass 2: final layout at exactly that width */
    h = layout_block(c, px, f->y, bw, 0);
    if (px > f->cur_x) {
        /* right float: it does not advance the pen, but it does shorten the
         * line for whatever follows on it, and it stays at the line top */
        if (box_count > save) {
            for (int i = save; i < box_count; i++) boxes[i].group = save + 1;
            boxes[save].group_h = h; boxes[save].group_base = -1;
        }
        if (h > f->line_h) f->line_h = h;
        if (!f->x1_full) f->x1_full = f->x1;
        f->x1 = px;
        f->any = 1;
        return;
    }
    /* group the boxes so flow_newline can baseline-align the whole block:
     * baseline = last text line's baseline, or the bottom edge */
    if (box_count > save) {
        int base = h;
        int last_base = -1;
        for (int i = save; i < box_count; i++) if (boxes[i].kind == BR_BOX_TEXT && boxes[i].y + boxes[i].baseline > last_base) last_base = boxes[i].y + boxes[i].baseline;
        if (last_base >= 0) base = last_base - f->y + (c->style.margin_b > 0 ? 0 : 0);
        if (base > h) base = h;
        for (int i = save; i < box_count; i++) boxes[i].group = save + 1;
        boxes[save].group_h = h; boxes[save].group_base = base;
    }
    if (h > f->line_h) f->line_h = h;
    f->cur_x += bw;
    f->any = 1;
}

static void flow_image(br_node_t* c, flow_t* f) {
    const char* src = br_attr(c, "src");
    int w = 0, h = 0;
    const char* wa = br_attr(c, "width"); const char* ha = br_attr(c, "height");
    if (wa) w = br_atoi(wa);
    if (ha) h = br_atoi(ha);
    if (c->style.width > 0) w = c->style.width;
    if (c->style.height > 0) h = c->style.height;
    int idx = -1;
    int maxw0 = f->x1 - f->x0;
    if (src && src[0]) idx = br_image_request_sized(src, w > maxw0 ? maxw0 : w, w > maxw0 ? 0 : h);
    br_image_t* im = br_image_get(idx);
    if (im && im->pixels) {
        if (!w && !h) { w = im->w; h = im->h; }
        else if (!w) w = im->w * h / (im->h ? im->h : 1);
        else if (!h) h = im->h * w / (im->w ? im->w : 1);
    } else {
        if (!w) w = 64;
        if (!h) h = 48;
    }
    int maxw = f->x1 - f->x0;
    if (w > maxw) { h = h * maxw / (w ? w : 1); w = maxw; }
    flow_place(f, w, h);
    br_box_t* b = new_box(BR_BOX_IMAGE, c);
    if (!b) return;
    b->x = f->cur_x; b->y = f->y; b->w = w; b->h = h; b->img_index = idx; b->link = link_ancestor(c);
    const char* alt = br_attr(c, "alt");
    b->text = alt ? alt : "";
    b->text_len = (int)strlen(b->text);
    b->border = c->style.border; b->border_color = c->style.border_color;
    c->lx = b->x; c->ly = b->y; c->lw = w; c->lh = h;
    f->cur_x += w;
}

static void flow_form_control(br_node_t* c, flow_t* f) {
    const char* type = br_attr(c, "type");
    int is_button = br_streq(c->tag, "button") || (type && (br_strieq(type, "submit") || br_strieq(type, "button") || br_strieq(type, "reset")));
    int is_check = type && (br_strieq(type, "checkbox") || br_strieq(type, "radio"));
    int is_hidden = type && br_strieq(type, "hidden");
    if (is_hidden) return;
    int scale = c->style.font_scale; if (scale > 2) scale = 2;
    br_style_t cst = c->style; if (cst.font_px > 18) cst.font_px = 16;
    tm_t m; text_metrics(&cst, &m);
    if (is_check) {
        int s = 13;
        flow_place(f, s + 4, s);
        br_box_t* b = new_box(BR_BOX_CHECKBOX, c);
        if (!b) return;
        b->x = f->cur_x + 2; b->y = f->y; b->w = s; b->h = s;
        c->lx = b->x; c->ly = b->y; c->lw = s; c->lh = s;
        f->cur_x += s + 4;
        return;
    }
    if (is_button) {
        static char label[64];
        label[0] = 0;
        if (br_streq(c->tag, "button")) br_node_text_content(c, label, sizeof(label));
        else { const char* v = br_attr(c, "value"); br_strlcpy(label, v ? v : (type && br_strieq(type, "reset") ? "Reset" : "Submit"), sizeof(label)); }
        int w = br_font_text_width(m.face, m.px, label, (int)strlen(label)) + 16;
        if (c->style.width > 0) w = c->style.width;
        if (w < 40) w = 40;
        int h = m.ascent + m.descent + 8;
        flow_place(f, w + 2, h);
        br_box_t* b = new_box(BR_BOX_BUTTON, c);
        if (!b) return;
        b->x = f->cur_x + 1; b->y = f->y; b->w = w; b->h = h; b->scale = scale;
        b->font_px = m.px; b->face = m.face; b->baseline = m.ascent;
        b->text = br_strdup(label); b->text_len = (int)strlen(b->text);
        b->bg = c->style.background ? c->style.background : 0xFFC0C0C0u;
        b->color = c->style.color;
        c->lx = b->x; c->ly = b->y; c->lw = w; c->lh = h;
        f->cur_x += w + 2;
        return;
    }
    /* text input */
    int cols = 20;
    const char* sz = br_attr(c, "size");
    if (sz && br_atoi(sz) > 0) cols = br_atoi(sz);
    int w = c->style.width > 0 ? c->style.width : cols * br_font_char_width(m.face, m.px, 'n') + 6;
    int maxw = f->x1 - f->x0; if (w > maxw) w = maxw;
    int h = m.ascent + m.descent + 6;
    if (br_streq(c->tag, "textarea")) { const char* rows = br_attr(c, "rows"); int r = rows ? br_atoi(rows) : 3; if (r < 2) r = 2; if (r > 12) r = 12; h = m.lh * r + 6; }
    if (c->style.height > 0) h = c->style.height;
    if (!c->value) { const char* v = br_attr(c, "value"); c->value = br_strdup(v ? v : ""); }
    flow_place(f, w + 2, h);
    br_box_t* b = new_box(BR_BOX_INPUT, c);
    if (!b) return;
    b->x = f->cur_x + 1; b->y = f->y; b->w = w; b->h = h; b->scale = scale;
    b->font_px = m.px; b->face = m.face; b->baseline = m.ascent;
    b->color = c->style.color;
    c->lx = b->x; c->ly = b->y; c->lw = w; c->lh = h;
    f->cur_x += w + 2;
}

static void layout_inline_children(br_node_t* n, flow_t* f, int pre) {
    if (br_stack_headroom() < BR_STACK_MIN) return;
    for (br_node_t* c = n->first_child; c; c = c->next) {
        if (c->type == BR_NODE_TEXT) {
            flow_text(f, c, &n->style, pre);
            continue;
        }
        if (c->type != BR_NODE_ELEMENT) continue;
        if (c->style.display == BR_DISPLAY_NONE || !c->style.visible) continue;
        if (br_streq(c->tag, "br")) { if (!f->any) f->line_h = line_height_of(&n->style); flow_newline(f); continue; }
        if (br_streq(c->tag, "img")) { flow_image(c, f); continue; }
        if (br_streq(c->tag, "input") || br_streq(c->tag, "button")) { flow_form_control(c, f); continue; }
        if (br_streq(c->tag, "select") || br_streq(c->tag, "textarea")) { flow_form_control(c, f); continue; }
        if (c->style.display == BR_DISPLAY_INLINE_BLOCK || c->style.float_dir) { flow_inline_block(c, f); continue; }
        if (!is_inline_node(c)) {
            /* A block inside inline content: break the line, lay out the
             * block at full width, continue flowing after it. */
            if (f->any) flow_newline(f);
            int h = layout_block(c, f->x0, f->y, f->x1 - f->x0, 0);
            int nw = natural_width(c);
            if (nw > f->max_x) f->max_x = nw;
            f->y += h;
            f->cur_x = f->x0; f->line_h = 0; f->line_start_box = box_count; f->any = 0;
            continue;
        }
        /* inline element: background highlight for <mark>/<code> etc. */
        int save = box_count;
        int sx = f->cur_x, sy = f->y;
        c->lx = sx; c->ly = sy;
        layout_inline_children(c, f, pre || br_streq(c->tag, "pre"));
        if (c->style.background) {
            for (int i = save; i < box_count; i++) if (boxes[i].kind == BR_BOX_TEXT && boxes[i].node && boxes[i].node->parent) boxes[i].bg = c->style.background;
        }
        if (c->style.border && box_count > save) {
            /* a bordered inline span: draw a rect behind its first line */
            (void)0;
        }
        c->lw = (f->cur_x > sx) ? f->cur_x - sx : f->x1 - sx;
        c->lh = f->y + (f->line_h ? f->line_h : 19) - sy;
    }
}

static int has_inline_content(const br_node_t* n) {
    for (const br_node_t* c = n->first_child; c; c = c->next) {
        if (c->type == BR_NODE_TEXT) return 1;
        if (c->type == BR_NODE_ELEMENT && c->style.display != BR_DISPLAY_NONE && is_inline_node(c)) return 1;
    }
    return 0;
}

static int layout_table(br_node_t* t, int x, int y, int width) {
    /* Count columns from the widest row. */
    int cols = 0;
    br_node_t* rows[64]; int nrows = 0;
    /* rows may be under thead/tbody */
    for (br_node_t* s = t->first_child; s && nrows < 64; s = s->next) {
        if (s->type != BR_NODE_ELEMENT) continue;
        if (br_streq(s->tag, "tr")) rows[nrows++] = s;
        else if (br_streq(s->tag, "thead") || br_streq(s->tag, "tbody") || br_streq(s->tag, "tfoot")) {
            for (br_node_t* r = s->first_child; r && nrows < 64; r = r->next) if (r->type == BR_NODE_ELEMENT && br_streq(r->tag, "tr")) rows[nrows++] = r;
        } else if (br_streq(s->tag, "caption")) {
            y += layout_block(s, x, y, width, 0);
        }
    }
    for (int r = 0; r < nrows; r++) {
        int c = 0;
        for (br_node_t* cell = rows[r]->first_child; cell; cell = cell->next) {
            if (cell->type == BR_NODE_ELEMENT && (br_streq(cell->tag, "td") || br_streq(cell->tag, "th"))) {
                const char* cs = br_attr(cell, "colspan");
                c += (cs && br_atoi(cs) > 1) ? br_atoi(cs) : 1;
            }
        }
        if (c > cols) cols = c;
    }
    if (cols == 0) return 0;
    if (cols > 16) cols = 16;
    int border = t->style.border;
    int spacing = 2;
    int tw = t->style.width > 0 ? t->style.width : (t->style.width < -1 ? width * (-(t->style.width) - 2) / 100 : width);
    if (tw > width) tw = width;
    /* Column widths (automatic table layout, simplified):
     *   1. measure every cell's natural width at the full width (alignment
     *      off, boxes dropped) -> per-column max (colnat); cells with a
     *      width attribute/CSS pin their column (colfix, percent of tw);
     *   2. an auto-width table shrinks to the sum of the naturals;
     *   3. otherwise the free space is spread over the unpinned columns
     *      in proportion to their natural widths, and if the naturals do
     *      not fit, columns are squeezed proportionally instead. */
    int colnat[16], colfix[16], colw[16];
    for (int i = 0; i < 16; i++) { colnat[i] = 0; colfix[i] = 0; colw[i] = 0; }
    {
        int save = box_count, was = measuring; measuring = 1;
        for (int r = 0; r < nrows && ib_depth < 4; r++) {
            int col = 0;
            for (br_node_t* cell = rows[r]->first_child; cell && col < cols; cell = cell->next) {
                if (cell->type != BR_NODE_ELEMENT || !(br_streq(cell->tag, "td") || br_streq(cell->tag, "th"))) continue;
                const char* cs = br_attr(cell, "colspan");
                int span = (cs && br_atoi(cs) > 1) ? br_atoi(cs) : 1;
                if (span == 1) {
                    int w = cell->style.width;
                    if (w > 0 && w > colfix[col]) colfix[col] = w;
                    else if (w < -1) { int pw = (tw - spacing * (cols + 1)) * (-w - 2) / 100; if (pw > colfix[col]) colfix[col] = pw; }
                }
                ib_depth++;
                layout_block(cell, x, y, width, 0);
                ib_depth--;
                int nw = natural_width(cell);
                if (span == 1 && nw > colnat[col]) colnat[col] = nw;
                col += span;
                box_count = save;
            }
        }
        measuring = was;
    }
    int natural = spacing * (cols + 1), fixed_sum = 0, auto_nat = 0, nauto = 0;
    for (int i = 0; i < cols; i++) {
        if (colnat[i] < 20) colnat[i] = 20;
        natural += colfix[i] > colnat[i] ? colfix[i] : colnat[i];
        if (colfix[i]) fixed_sum += colfix[i]; else { auto_nat += colnat[i]; nauto++; }
    }
    t->used_w = natural;
    if (t->style.width == -1 && natural < tw) tw = natural;      /* auto tables shrink to fit */
    {
        int inner = tw - spacing * (cols + 1);
        int for_auto = inner - fixed_sum;
        if (nauto == 0 || for_auto < 20 * nauto) {
            /* pinned columns alone overflow: scale everything to fit */
            int tot = 0; for (int i = 0; i < cols; i++) tot += colfix[i] > colnat[i] ? colfix[i] : colnat[i];
            if (tot < 1) tot = 1;
            for (int i = 0; i < cols; i++) colw[i] = inner * (colfix[i] > colnat[i] ? colfix[i] : colnat[i]) / tot;
        } else {
            for (int i = 0; i < cols; i++) {
                if (colfix[i]) colw[i] = colfix[i];
                else colw[i] = auto_nat > 0 ? for_auto * colnat[i] / auto_nat : for_auto / nauto;
            }
        }
        for (int i = 0; i < cols; i++) if (colw[i] < 8) colw[i] = 8;
    }
    /* a shrunk table inside text-align:center / margin:auto is centred */
    if (tw < width) {
        const br_style_t* ts = &t->style;
        if ((ts->margin_auto & 3) == 3 || (t->parent && t->parent->style.text_align == 1 && ts->margin_l == 0)) x += (width - tw) / 2;
        else if (ts->margin_auto & 1) x += width - tw;
        else if (t->parent && t->parent->style.text_align == 2) x += width - tw;
    }
    int cy = y + spacing;
    int table_box = box_count;
    br_box_t* tb = new_box(BR_BOX_RECT, t);
    if (tb) { tb->x = x; tb->y = y; tb->w = tw; tb->h = 0; tb->bg = t->style.background; tb->border = border; tb->border_color = t->style.border_color; tb->bt = t->style.border_t; tb->bb = t->style.border_b; tb->bl = t->style.border_l; tb->br_ = t->style.border_r; }
    for (int r = 0; r < nrows; r++) {
        int cx = x + spacing;
        int row_h = 0;
        int cell_boxes_start = box_count;
        int col = 0;
        int row_top = cy;
        uint32_t row_bg = rows[r]->style.background;
        for (br_node_t* cell = rows[r]->first_child; cell && col < cols; cell = cell->next) {
            if (cell->type != BR_NODE_ELEMENT || !(br_streq(cell->tag, "td") || br_streq(cell->tag, "th"))) continue;
            const char* cs = br_attr(cell, "colspan");
            int span = (cs && br_atoi(cs) > 1) ? br_atoi(cs) : 1;
            if (col + span > cols) span = cols - col;
            int cw = spacing * (span - 1);
            for (int k = 0; k < span; k++) cw += colw[col + k];
            if (!cell->style.background && row_bg) cell->style.background = row_bg;
            /* the cell's own width was only a column hint */
            int saved_w = cell->style.width; cell->style.width = -1;
            int h = layout_block(cell, cx, cy, cw, 0);
            cell->style.width = saved_w;
            if (h > row_h) row_h = h;
            cx += cw + spacing;
            col += span;
        }
        /* equalise cell heights: the cell's background rect is its first box */
        for (int i = cell_boxes_start; i < box_count; i++) {
            if (boxes[i].kind == BR_BOX_RECT && boxes[i].node && boxes[i].node->parent == rows[r] && boxes[i].y == row_top) boxes[i].h = row_h;
        }
        rows[r]->lx = x; rows[r]->ly = row_top; rows[r]->lw = tw; rows[r]->lh = row_h;
        cy += row_h + spacing;
    }
    if (tb) boxes[table_box].h = cy - y;
    t->lx = x; t->ly = y; t->lw = tw; t->lh = cy - y;
    return cy - y;
}

/* Lays out block `n` with its top-left margin corner at (x, y) in a
 * containing width of `width`. Returns the height consumed (margins
 * included). */
static int layout_block(br_node_t* n, int x, int y, int width, int list_index) {
    br_style_t* st = &n->style;
    if (st->display == BR_DISPLAY_NONE) return 0;
    if (br_stack_headroom() < BR_STACK_MIN) return 0;      /* absurdly deep tree: skip */
    if (box_count >= BR_MAX_BOXES - 4) return 0;

    int mt = st->margin_t, mb = st->margin_b, ml = st->margin_l, mr = st->margin_r;
    int bw = st->border;
    int bt = st->border_t, bb = st->border_b, bl = st->border_l, brr = st->border_r;
    if (ml < -width / 2) ml = 0;                    /* wild negative margins: ignore */
    if (mr < -width / 2) mr = 0;
    if (mt < 0) mt = 0;
    if (mb < 0) mb = 0;
    int bx = x + ml, by = y + mt;
    int outer_w = width - ml - mr;
    if (st->width > 0) outer_w = st->width;
    else if (st->width < -1) outer_w = (width - ml - mr) * (-(st->width) - 2) / 100;
    if (st->max_width > 0 && outer_w > st->max_width) outer_w = st->max_width;
    if (outer_w > width - ml - mr) outer_w = width - ml - mr;
    if (outer_w < 8) outer_w = 8;
    /* margin:auto centring (or a fixed-width block inside text-align:center) */
    if (outer_w < width - ml - mr) {
        int slack = width - ml - mr - outer_w;
        if ((st->margin_auto & 3) == 3) bx = x + ml + slack / 2;
        else if (st->margin_auto & 1) bx = x + ml + slack;
        else if (st->width > 0 && n->parent && n->parent->style.text_align == 1) bx = x + ml + slack / 2;
    }

    int cx = bx + bl + st->padding_l;
    int cw = outer_w - bl - brr - st->padding_l - st->padding_r;
    if (cw < 8) cw = 8;
    int cy = by + bt + st->padding_t;

    n->lx = bx; n->ly = by; n->lw = outer_w;

    /* background/border rect first so content paints over it */
    int rect_idx = -1;
    if (st->background || bw) {
        br_box_t* b = new_box(BR_BOX_RECT, n);
        if (b) {
            rect_idx = box_count - 1;
            b->x = bx; b->y = by; b->w = outer_w; b->h = 0;
            b->bg = st->background; b->border = bw; b->border_color = st->border_color;
            b->bt = bt; b->bb = bb; b->bl = bl; b->br_ = brr;
        }
    }

    n->used_w = 0;
    if (br_streq(n->tag, "svg")) {
        /* inline SVG is not rendered; it keeps the space its width/height
         * attributes (or CSS) ask for so icon layouts do not collapse */
        int w = 0, h = 0;
        const char* wa = br_attr(n, "width"); const char* ha = br_attr(n, "height");
        if (wa) w = br_atoi(wa);
        if (ha) h = br_atoi(ha);
        if (st->width > 0) w = st->width;
        if (st->height > 0) h = st->height;
        if (w <= 0 && h > 0) w = h;
        if (h <= 0 && w > 0) h = w;
        if (w < 0) w = 0;
        if (h < 0) h = 0;
        if (w > width) w = width;
        n->lw = w; n->lh = h; n->used_w = w;
        return mt + h + mb;
    }
    if (br_streq(n->tag, "hr")) {
        /* the UA sheet gives <hr> a 1px border: draw it as the rule itself */
        if (rect_idx >= 0) { boxes[rect_idx].h = 2; boxes[rect_idx].bt = 1; boxes[rect_idx].bb = 1; boxes[rect_idx].bl = boxes[rect_idx].br_ = 0; }
        else { br_box_t* b = new_box(BR_BOX_HR, n); if (b) { b->x = cx; b->y = cy; b->w = cw; b->h = 2; b->color = st->border_color; } }
        int h = 2;
        n->lh = h;
        return mt + h + mb;
    }
    if (br_streq(n->tag, "table") || st->display == BR_DISPLAY_TABLE) {
        int th = layout_table(n, cx, cy, cw);          /* sets n->used_w */
        int h = th + st->padding_t + st->padding_b + bt + bb;
        n->lh = h;
        if (rect_idx >= 0) boxes[rect_idx].h = h;
        return mt + h + mb;
    }

    /* list marker */
    if (st->display == BR_DISPLAY_LIST_ITEM && st->list_style != 2) {
        br_box_t* b = new_box(BR_BOX_BULLET, n);
        if (b) {
            tm_t m; text_metrics(st, &m);
            b->x = cx - 16; b->y = cy; b->w = 12; b->h = m.lh; b->color = st->color; b->scale = st->font_scale;
            b->font_px = m.px; b->face = m.face; b->baseline = m.ascent + (m.lh - m.ascent - m.descent) / 2;
            if (st->list_style == 1) {
                char num[12]; br_itoa(list_index, num);
                int l = (int)strlen(num);
                num[l] = '.'; num[l + 1] = 0;
                b->text = br_strdup(num); b->text_len = l + 1;
                b->w = br_font_text_width(m.face, m.px, num, l + 1);
                b->x = cx - 6 - b->w;
            }
        }
    }

    int inner_h = 0;
    int pre = br_streq(n->tag, "pre") || br_streq(n->tag, "textarea");
    if (st->display == BR_DISPLAY_FLEX && !st->flex_col && !has_inline_content(n)) {
        /* flex row: children side by side (shrink-to-fit), wrapping when full */
        flow_t f;
        memset(&f, 0, sizeof(f));
        f.x0 = cx; f.x1 = cx + cw; f.y = cy; f.cur_x = cx; f.align = st->text_align;
        f.line_start_box = box_count;
        for (br_node_t* c = n->first_child; c; c = c->next) {
            if (c->type != BR_NODE_ELEMENT || c->style.display == BR_DISPLAY_NONE) continue;
            flow_inline_block(c, &f);
            if (st->gap > 0 && f.cur_x + st->gap < f.x1) f.cur_x += st->gap;
        }
        if (f.any || f.line_h) flow_newline(&f);
        inner_h = f.y - cy;
        n->used_w = f.max_x;
    } else if (has_inline_content(n)) {
        flow_t f;
        memset(&f, 0, sizeof(f));
        f.x0 = cx; f.x1 = cx + cw; f.y = cy; f.cur_x = cx; f.align = st->text_align;
        f.line_start_box = box_count;
        layout_inline_children(n, &f, pre);
        if (f.any || f.line_h) flow_newline(&f);
        inner_h = f.y - cy;
        n->used_w = f.max_x;
    } else {
        int yy = cy;
        int li = 1;
        const char* start = br_attr(n, "start");
        if (start) li = br_atoi(start);
        int prev_mb = 0;
        int first = 1;
        for (br_node_t* c = n->first_child; c; c = c->next) {
            if (c->type != BR_NODE_ELEMENT) continue;
            if (c->style.display == BR_DISPLAY_NONE) continue;
            /* margin collapsing between vertical siblings */
            int cmt = c->style.margin_t;
            int collapse = first ? 0 : (cmt < prev_mb ? cmt : prev_mb);
            if (!first) yy -= collapse;
            if (first && st->padding_t == 0 && bw == 0 && cmt > 0 && !br_streq(n->tag, "body") && !br_streq(n->tag, "html")) {
                /* collapse child's top margin through us: keep it simple by not doing it */
            }
            int h = layout_block(c, cx, yy, cw, li);
            yy += h;
            int nw = natural_width(c);
            if (nw > n->used_w) n->used_w = nw;
            prev_mb = c->style.margin_b;
            if (c->style.display == BR_DISPLAY_LIST_ITEM) li++;
            first = 0;
        }
        inner_h = yy - cy;
    }
    if (st->height > 0 && inner_h < st->height) inner_h = st->height;
    int h = inner_h + st->padding_t + st->padding_b + bt + bb;
    n->lh = h;
    if (rect_idx >= 0) boxes[rect_idx].h = h;
    return mt + h + mb;
}

void br_layout(br_node_t* doc, int width) {
    box_count = 0;
    if (!doc) { brs.page_h = 0; return; }
    /* clear stale geometry */
    for (int i = 0; i < br_dom_node_count(); i++) { br_node_t* n = br_dom_node(i); n->lx = n->ly = n->lw = n->lh = 0; }
    br_node_t* body = doc->body;
    if (!body) { brs.page_h = 0; return; }
    /* page background */
    br_box_t* bg = new_box(BR_BOX_RECT, body);
    if (bg) { bg->x = 0; bg->y = 0; bg->w = width; bg->h = 0; bg->bg = body->style.background ? body->style.background : 0xFFFFFFFFu; }
    uint32_t saved_bg = body->style.background;
    body->style.background = 0;                      /* painted by the page rect */
    int h = layout_block(body, 0, 0, width, 0);
    body->style.background = saved_bg;
    if (bg) bg->h = h + 400;
    brs.page_h = h;
}
