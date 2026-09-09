/* Small, correct PNG decoder for SharkOS.
 *
 * The previous version of this file read chunk lengths little-endian and only
 * knew the fixed Huffman table, so it rejected every real-world PNG. This
 * rewrite handles:
 *   - zlib/DEFLATE: stored, fixed and dynamic Huffman blocks (puff-style
 *     canonical decoding, no tables to build beyond counts/symbols)
 *   - colour types 0 (grey), 2 (RGB), 3 (palette + tRNS), 4 (grey+alpha),
 *     6 (RGBA); bit depths 1/2/4/8/16 (16-bit samples are truncated)
 *   - all five scanline filters
 * Adam7 interlacing is supported. Output is 0xAARRGGBB.
 *
 * Integer-only, no libc beyond memset/memcpy, no recursion.
 *
 * Two entry points:
 *   png_decode_buf()  decodes into caller-provided buffers (used by the
 *                     browser, which keeps a per-page pixel arena)
 *   decode_png()      convenience wrapper that kmalloc()s the buffers */

#include "kernel.h"

/* ------------------------------------------------------------ inflate */

typedef struct {
    const uint8_t* in;
    size_t in_len, in_pos;
    uint32_t bit_buf;
    int bit_cnt;
    uint8_t* out;
    size_t out_len, out_pos;
    int error;
} inf_t;

static int inf_bits(inf_t* s, int need) {
    uint32_t val = s->bit_buf;
    while (s->bit_cnt < need) {
        if (s->in_pos >= s->in_len) { s->error = 1; return 0; }
        val |= (uint32_t)s->in[s->in_pos++] << s->bit_cnt;
        s->bit_cnt += 8;
    }
    s->bit_buf = val >> need;
    s->bit_cnt -= need;
    return (int)(val & ((1u << need) - 1));
}

typedef struct {
    uint16_t count[16];
    uint16_t symbol[320];
} huff_t;

/* Build canonical code from lengths; returns 0 on success. */
static int huff_build(huff_t* h, const uint8_t* length, int n) {
    uint16_t offs[16];
    for (int len = 0; len < 16; len++) h->count[len] = 0;
    for (int sym = 0; sym < n; sym++) h->count[length[sym]]++;
    if (h->count[0] == n) return 0;                  /* no codes: legal */
    int left = 1;
    for (int len = 1; len < 16; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return -1;                     /* over-subscribed */
    }
    offs[1] = 0;
    for (int len = 1; len < 15; len++) offs[len + 1] = offs[len] + h->count[len];
    for (int sym = 0; sym < n; sym++)
        if (length[sym]) h->symbol[offs[length[sym]]++] = (uint16_t)sym;
    return 0;
}

static int huff_decode(inf_t* s, const huff_t* h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        code |= inf_bits(s, 1);
        if (s->error) return -1;
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static const uint16_t len_base[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t len_extra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t dist_base[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const uint8_t dist_extra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static int inf_codes(inf_t* s, const huff_t* lencode, const huff_t* distcode) {
    for (;;) {
        int sym = huff_decode(s, lencode);
        if (sym < 0) return -1;
        if (sym < 256) {
            if (s->out_pos >= s->out_len) return -2;
            s->out[s->out_pos++] = (uint8_t)sym;
        } else if (sym == 256) {
            return 0;
        } else {
            sym -= 257;
            if (sym >= 29) return -1;
            int len = len_base[sym] + inf_bits(s, len_extra[sym]);
            int dsym = huff_decode(s, distcode);
            if (dsym < 0 || dsym >= 30) return -1;
            size_t dist = dist_base[dsym] + (size_t)inf_bits(s, dist_extra[dsym]);
            if (s->error) return -1;
            if (dist > s->out_pos) return -1;                 /* before start */
            if (s->out_pos + (size_t)len > s->out_len) return -2;
            uint8_t* dst = s->out + s->out_pos;
            const uint8_t* src = dst - dist;
            for (int i = 0; i < len; i++) dst[i] = src[i];    /* overlap-safe forward copy */
            s->out_pos += (size_t)len;
        }
    }
}

static int inf_stored(inf_t* s) {
    s->bit_buf = 0; s->bit_cnt = 0;                           /* drop to byte boundary */
    if (s->in_pos + 4 > s->in_len) return -1;
    unsigned len = s->in[s->in_pos] | (s->in[s->in_pos + 1] << 8);
    unsigned nlen = s->in[s->in_pos + 2] | (s->in[s->in_pos + 3] << 8);
    s->in_pos += 4;
    if (len != (~nlen & 0xFFFF)) return -1;
    if (s->in_pos + len > s->in_len) return -1;
    if (s->out_pos + len > s->out_len) return -2;
    memcpy(s->out + s->out_pos, s->in + s->in_pos, len);
    s->in_pos += len;
    s->out_pos += len;
    return 0;
}

static int inf_fixed(inf_t* s) {
    static huff_t lencode, distcode;
    static int built = 0;
    if (!built) {
        uint8_t lengths[288];
        int sym = 0;
        for (; sym < 144; sym++) lengths[sym] = 8;
        for (; sym < 256; sym++) lengths[sym] = 9;
        for (; sym < 280; sym++) lengths[sym] = 7;
        for (; sym < 288; sym++) lengths[sym] = 8;
        huff_build(&lencode, lengths, 288);
        for (sym = 0; sym < 30; sym++) lengths[sym] = 5;
        huff_build(&distcode, lengths, 30);
        built = 1;
    }
    return inf_codes(s, &lencode, &distcode);
}

static int inf_dynamic(inf_t* s) {
    static const uint8_t order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    uint8_t lengths[320];
    huff_t lencode, distcode;

    int nlen = inf_bits(s, 5) + 257;
    int ndist = inf_bits(s, 5) + 1;
    int ncode = inf_bits(s, 4) + 4;
    if (s->error || nlen > 286 || ndist > 30) return -1;

    int i;
    for (i = 0; i < ncode; i++) lengths[order[i]] = (uint8_t)inf_bits(s, 3);
    for (; i < 19; i++) lengths[order[i]] = 0;
    if (huff_build(&lencode, lengths, 19) != 0) return -1;

    i = 0;
    while (i < nlen + ndist) {
        int sym = huff_decode(s, &lencode);
        if (sym < 0) return -1;
        if (sym < 16) {
            lengths[i++] = (uint8_t)sym;
        } else {
            int len = 0, rep;
            if (sym == 16) {
                if (i == 0) return -1;
                len = lengths[i - 1];
                rep = 3 + inf_bits(s, 2);
            } else if (sym == 17) {
                rep = 3 + inf_bits(s, 3);
            } else {
                rep = 11 + inf_bits(s, 7);
            }
            if (i + rep > nlen + ndist) return -1;
            while (rep--) lengths[i++] = (uint8_t)len;
        }
    }
    if (lengths[256] == 0) return -1;                         /* no end-of-block code */
    if (huff_build(&lencode, lengths, nlen) != 0 && nlen - lencode.count[0] != 1) return -1;
    if (huff_build(&distcode, lengths + nlen, ndist) != 0 && ndist - distcode.count[0] != 1) return -1;
    return inf_codes(s, &lencode, &distcode);
}

/* Inflate a zlib stream. Returns bytes produced, or -1 on error. */
static long zlib_inflate(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_len) {
    if (in_len < 2) return -1;
    if ((in[0] & 0x0F) != 8) return -1;                       /* not deflate */
    if (((in[0] << 8) | in[1]) % 31 != 0) return -1;          /* bad zlib header */
    if (in[1] & 0x20) return -1;                              /* preset dictionary */

    inf_t s;
    s.in = in + 2; s.in_len = in_len - 2; s.in_pos = 0;
    s.bit_buf = 0; s.bit_cnt = 0;
    s.out = out; s.out_len = out_len; s.out_pos = 0;
    s.error = 0;

    int last, type, err;
    do {
        last = inf_bits(&s, 1);
        type = inf_bits(&s, 2);
        if (s.error) return -1;
        if (type == 0) err = inf_stored(&s);
        else if (type == 1) err = inf_fixed(&s);
        else if (type == 2) err = inf_dynamic(&s);
        else err = -1;
        if (err == -2) break;                                 /* output full: keep what we have */
        if (err != 0 || s.error) return -1;
    } while (!last);
    return (long)s.out_pos;
}

/* ---------------------------------------------------------------- PNG */

static inline uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static inline uint8_t paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return (uint8_t)a;
    if (pb <= pc) return (uint8_t)b;
    return (uint8_t)c;
}

/* Read sample `idx` (0-based across the row) of `depth` bits; returns 0..255 scaled. */
static inline int sample_at(const uint8_t* row, int idx, int depth) {
    switch (depth) {
    case 8:  return row[idx];
    case 16: return row[idx * 2];
    case 4:  return ((row[idx >> 1] >> ((1 - (idx & 1)) * 4)) & 0x0F) * 17;
    case 2:  return ((row[idx >> 2] >> ((3 - (idx & 3)) * 2)) & 0x03) * 85;
    case 1:  return ((row[idx >> 3] >> (7 - (idx & 7))) & 0x01) * 255;
    default: return 0;
    }
}

/* Raw palette index (unscaled) for colour type 3. */
static inline int index_at(const uint8_t* row, int idx, int depth) {
    switch (depth) {
    case 8:  return row[idx];
    case 4:  return (row[idx >> 1] >> ((1 - (idx & 1)) * 4)) & 0x0F;
    case 2:  return (row[idx >> 2] >> ((3 - (idx & 3)) * 2)) & 0x03;
    case 1:  return (row[idx >> 3] >> (7 - (idx & 7))) & 0x01;
    default: return 0;
    }
}

/* Decode into caller buffers.
 *   pixels      : receives w*h ARGB values (must hold max_pixels)
 *   scratch     : work area for the concatenated IDAT stream + the inflated
 *                 filtered scanlines (scratch_len bytes)
 * Returns 0 on success, negative on failure (bad data or too big). */
int png_decode_buf(const uint8_t* data, size_t len, int* out_w, int* out_h,
                   uint32_t* pixels, size_t max_pixels,
                   uint8_t* scratch, size_t scratch_len) {
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    *out_w = 0; *out_h = 0;
    if (!data || len < 8 + 25) return -1;
    for (int i = 0; i < 8; i++) if (data[i] != sig[i]) return -1;

    uint32_t width = 0, height = 0;
    int depth = 0, ctype = 0, interlace = 0;
    uint8_t palette[256][4];
    int pal_len = 0;
    for (int i = 0; i < 256; i++) { palette[i][0] = palette[i][1] = palette[i][2] = 0; palette[i][3] = 255; }

    /* Pass 1: header + total IDAT size. */
    size_t pos = 8, idat_total = 0;
    int saw_ihdr = 0;
    while (pos + 12 <= len) {
        uint32_t clen = be32(data + pos);
        uint32_t ctyp = be32(data + pos + 4);
        if (clen > len || pos + 12 + clen > len) return -1;
        const uint8_t* body = data + pos + 8;
        if (ctyp == 0x49484452u) {                            /* IHDR */
            if (clen < 13) return -1;
            width = be32(body); height = be32(body + 4);
            depth = body[8]; ctype = body[9]; interlace = body[12];
            saw_ihdr = 1;
        } else if (ctyp == 0x504C5445u) {                     /* PLTE */
            pal_len = (int)(clen / 3);
            if (pal_len > 256) pal_len = 256;
            for (int i = 0; i < pal_len; i++) { palette[i][0] = body[i * 3]; palette[i][1] = body[i * 3 + 1]; palette[i][2] = body[i * 3 + 2]; }
        } else if (ctyp == 0x74524E53u) {                     /* tRNS */
            if (ctype == 3) for (uint32_t i = 0; i < clen && i < 256; i++) palette[i][3] = body[i];
        } else if (ctyp == 0x49444154u) {                     /* IDAT */
            idat_total += clen;
        } else if (ctyp == 0x49454E44u) {                     /* IEND */
            break;
        }
        pos += 12 + clen;
    }
    if (!saw_ihdr || width == 0 || height == 0 || idat_total == 0) return -1;
    if (interlace && interlace != 1) return -1;
    if (width > 4096 || height > 4096) return -3;
    if ((size_t)width * height > max_pixels) return -3;

    int channels;
    switch (ctype) {
    case 0: channels = 1; break;
    case 2: channels = 3; break;
    case 3: channels = 1; break;
    case 4: channels = 2; break;
    case 6: channels = 4; break;
    default: return -1;
    }
    if (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16) return -1;
    if ((ctype == 2 || ctype == 4 || ctype == 6) && depth < 8) return -1;

    size_t bits_per_pixel = (size_t)channels * depth;
    size_t stride = (width * bits_per_pixel + 7) / 8;         /* bytes per scanline (no filter byte) */
    size_t bpp = bits_per_pixel < 8 ? 1 : bits_per_pixel / 8;  /* filter unit */
    size_t raw_len;
    /* Adam7: seven sub-images, each with its own filtered scanlines. */
    static const int a7_x0[7] = { 0, 4, 0, 2, 0, 1, 0 }, a7_y0[7] = { 0, 0, 4, 0, 2, 0, 1 };
    static const int a7_dx[7] = { 8, 8, 4, 4, 2, 2, 1 }, a7_dy[7] = { 8, 8, 8, 4, 4, 2, 2 };
    if (interlace) {
        raw_len = 0;
        for (int p = 0; p < 7; p++) {
            uint32_t pw = (width - a7_x0[p] + a7_dx[p] - 1) / a7_dx[p];
            uint32_t ph = (height - a7_y0[p] + a7_dy[p] - 1) / a7_dy[p];
            if (pw == 0 || ph == 0) continue;
            raw_len += ((pw * bits_per_pixel + 7) / 8 + 1) * ph;
        }
    } else raw_len = (stride + 1) * height;
    if (idat_total + raw_len > scratch_len) return -3;

    /* Pass 2: gather IDAT into scratch[0..idat_total). */
    uint8_t* zdata = scratch;
    uint8_t* raw = scratch + idat_total;
    size_t zpos = 0;
    pos = 8;
    while (pos + 12 <= len) {
        uint32_t clen = be32(data + pos);
        uint32_t ctyp = be32(data + pos + 4);
        if (ctyp == 0x49444154u) { memcpy(zdata + zpos, data + pos + 8, clen); zpos += clen; }
        else if (ctyp == 0x49454E44u) break;
        pos += 12 + clen;
    }

    long got = zlib_inflate(zdata, zpos, raw, raw_len);
    if (got < 0) return -1;
    if ((size_t)got < raw_len) {
        /* truncated stream: zero the rest so we still show what we have */
        memset(raw + got, 0, raw_len - (size_t)got);
    }

    /* Unfilter in place, then convert each row to ARGB. For Adam7 the same
     * routine runs once per pass with a pixel-placement stride. */
    struct pass { uint32_t pw, ph, x0, y0, dx, dy; } passes[7];
    int npasses = 0;
    if (interlace) {
        for (int p = 0; p < 7; p++) {
            uint32_t pw = (width - a7_x0[p] + a7_dx[p] - 1) / a7_dx[p];
            uint32_t ph = (height - a7_y0[p] + a7_dy[p] - 1) / a7_dy[p];
            if (a7_x0[p] >= (int)width || a7_y0[p] >= (int)height || pw == 0 || ph == 0) continue;
            passes[npasses].pw = pw; passes[npasses].ph = ph;
            passes[npasses].x0 = (uint32_t)a7_x0[p]; passes[npasses].y0 = (uint32_t)a7_y0[p];
            passes[npasses].dx = (uint32_t)a7_dx[p]; passes[npasses].dy = (uint32_t)a7_dy[p];
            npasses++;
        }
    } else {
        passes[0].pw = width; passes[0].ph = height; passes[0].x0 = passes[0].y0 = 0; passes[0].dx = passes[0].dy = 1;
        npasses = 1;
    }
    uint8_t* rp = raw;
    for (int pi = 0; pi < npasses; pi++) {
        uint32_t pw = passes[pi].pw, ph = passes[pi].ph;
        size_t pstride = (pw * bits_per_pixel + 7) / 8;
        uint8_t* prev = NULL;
        for (uint32_t y = 0; y < ph; y++) {
            uint8_t* line = rp + y * (pstride + 1);
            int filter = line[0];
            uint8_t* cur = line + 1;
            switch (filter) {
            case 0: break;
            case 1: for (size_t i = bpp; i < pstride; i++) cur[i] = (uint8_t)(cur[i] + cur[i - bpp]); break;
            case 2: if (prev) for (size_t i = 0; i < pstride; i++) cur[i] = (uint8_t)(cur[i] + prev[i]); break;
            case 3:
                for (size_t i = 0; i < pstride; i++) {
                    int a = i >= bpp ? cur[i - bpp] : 0;
                    int b = prev ? prev[i] : 0;
                    cur[i] = (uint8_t)(cur[i] + ((a + b) >> 1));
                }
                break;
            case 4:
                for (size_t i = 0; i < pstride; i++) {
                    int a = i >= bpp ? cur[i - bpp] : 0;
                    int b = prev ? prev[i] : 0;
                    int c = (prev && i >= bpp) ? prev[i - bpp] : 0;
                    cur[i] = (uint8_t)(cur[i] + paeth(a, b, c));
                }
                break;
            default: return -1;
            }
            prev = cur;

            uint32_t* dst = pixels + (size_t)(passes[pi].y0 + y * passes[pi].dy) * width;
            for (uint32_t x = 0; x < pw; x++) {
                int r, g, b, a = 255;
                switch (ctype) {
                case 0:
                    r = g = b = sample_at(cur, (int)x, depth);
                    break;
                case 2:
                    r = sample_at(cur, (int)x * 3, depth); g = sample_at(cur, (int)x * 3 + 1, depth); b = sample_at(cur, (int)x * 3 + 2, depth);
                    break;
                case 3: {
                    int idx = index_at(cur, (int)x, depth);
                    if (idx >= 256) idx = 0;
                    r = palette[idx][0]; g = palette[idx][1]; b = palette[idx][2]; a = palette[idx][3];
                    break;
                }
                case 4:
                    r = g = b = sample_at(cur, (int)x * 2, depth); a = sample_at(cur, (int)x * 2 + 1, depth);
                    break;
                default: /* 6 */
                    r = sample_at(cur, (int)x * 4, depth); g = sample_at(cur, (int)x * 4 + 1, depth);
                    b = sample_at(cur, (int)x * 4 + 2, depth); a = sample_at(cur, (int)x * 4 + 3, depth);
                    break;
                }
                dst[passes[pi].x0 + x * passes[pi].dx] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            }
        }
        rp += (pstride + 1) * ph;
    }

    *out_w = (int)width;
    *out_h = (int)height;
    return 0;
}

/* Convenience wrapper: heap-allocated result (never freed: kfree is a no-op). */
uint32_t* decode_png(const uint8_t* png_data, size_t png_len, int* out_w, int* out_h) {
    *out_w = 0; *out_h = 0;
    if (!png_data || png_len < 33) return NULL;
    /* Peek at IHDR to size the buffers. */
    if (be32(png_data + 12) != 0x49484452u) return NULL;
    uint32_t w = be32(png_data + 16), h = be32(png_data + 20);
    if (w == 0 || h == 0 || w > 2048 || h > 2048) return NULL;
    size_t npix = (size_t)w * h;
    size_t scratch_len = png_len + (w * 4 + 1) * h + 64;
    uint32_t* pixels = (uint32_t*)kmalloc(npix * sizeof(uint32_t));
    uint8_t* scratch = (uint8_t*)kmalloc(scratch_len);
    if (!pixels || !scratch) return NULL;
    if (png_decode_buf(png_data, png_len, out_w, out_h, pixels, npix, scratch, scratch_len) != 0) return NULL;
    return pixels;
}
