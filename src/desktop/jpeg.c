/* Baseline JPEG decoder for SharkOS (ITU T.81 sequential DCT, Huffman).
 *
 *   - 8-bit precision, 1 (grey) or 3 (YCbCr) components, any sampling
 *     factors up to 2x2 (4:4:4, 4:2:2, 4:2:0, 4:4:0), restart intervals
 *   - integer AAN-style IDCT (no FPU), nearest chroma upsampling
 *   - progressive JPEGs are rejected (-2) and shown as a placeholder
 *
 * Integer only, no allocation: caller provides the pixel buffer and a
 * scratch area for the component planes. Output is 0xFFRRGGBB. */

#include "kernel.h"

typedef struct {
    uint8_t  bits[17];
    uint8_t  vals[256];
    /* fast lookup: code length + value for 9-bit prefixes */
    uint16_t maxcode[18];
    int32_t  valptr[17];
    int32_t  mincode[17];
    uint8_t  look_len[512];
    uint8_t  look_val[512];
} jhuff_t;

typedef struct {
    int id, h, v, tq, td, ta;
    int bw, bh;             /* blocks per line / column (padded to MCU) */
    uint8_t* plane;         /* decoded samples, bw*8 x bh*8 */
    int dc_pred;
} jcomp_t;

typedef struct {
    const uint8_t* d; size_t len, pos;
    uint32_t bitbuf; int bitcnt;
    int hit_marker;
    uint16_t qt[4][64];
    jhuff_t hdc[4], hac[4];
    jcomp_t comp[3]; int ncomp;
    int width, height;
    int hmax, vmax;
    int restart_interval;
    int progressive;
} jpeg_t;

static const uint8_t zigzag[64] = {
     0, 1, 8,16, 9, 2, 3,10,17,24,32,25,18,11, 4, 5,12,19,26,33,40,48,41,34,27,20,13, 6, 7,14,21,28,
    35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63 };

static void build_huff(jhuff_t* h) {
    int code = 0, k = 0;
    for (int l = 1; l <= 16; l++) {
        h->valptr[l] = k;
        h->mincode[l] = code;
        code += h->bits[l];
        k += h->bits[l];
        h->maxcode[l] = (uint16_t)(h->bits[l] ? code - 1 : 0);
        if (!h->bits[l]) h->mincode[l] = 0x7FFFFFFF;       /* no codes of this length */
        code <<= 1;
    }
    h->maxcode[17] = 0xFFFF;
    memset(h->look_len, 0, sizeof(h->look_len));
    /* 9-bit lookahead */
    code = 0; k = 0;
    for (int l = 1; l <= 9; l++) {
        for (int i = 0; i < h->bits[l]; i++, k++) {
            int c = code + i;
            int shift = 9 - l;
            for (int j = 0; j < (1 << shift); j++) {
                int idx = (c << shift) | j;
                if (idx < 512) { h->look_len[idx] = (uint8_t)l; h->look_val[idx] = h->vals[k]; }
            }
        }
        code = (code + h->bits[l]) << 1;
    }
}

static void fill_bits(jpeg_t* j) {
    while (j->bitcnt <= 24) {
        uint32_t b = 0;
        if (!j->hit_marker && j->pos < j->len) {
            b = j->d[j->pos];
            if (b == 0xFF) {
                uint8_t nxt = j->pos + 1 < j->len ? j->d[j->pos + 1] : 0;
                if (nxt == 0) { j->pos += 2; }
                else if (nxt >= 0xD0 && nxt <= 0xD7) { j->hit_marker = 1; b = 0; }   /* RST: stop here */
                else { j->hit_marker = 1; b = 0; }
            } else j->pos++;
        }
        j->bitbuf |= b << (24 - j->bitcnt);
        j->bitcnt += 8;
    }
}

static int get_bits(jpeg_t* j, int n) {
    if (n == 0) return 0;
    if (j->bitcnt < n) fill_bits(j);
    int v = (int)(j->bitbuf >> (32 - n));
    j->bitbuf <<= n; j->bitcnt -= n;
    return v;
}

static int decode_huff(jpeg_t* j, const jhuff_t* h) {
    if (j->bitcnt < 16) fill_bits(j);
    int look = (int)(j->bitbuf >> 23);
    int l = h->look_len[look];
    if (l) { j->bitbuf <<= l; j->bitcnt -= l; return h->look_val[look]; }
    /* slow path: lengths 10..16 */
    int code = (int)(j->bitbuf >> 22);           /* 10 bits */
    l = 10;
    while (l <= 16) {
        if (h->mincode[l] != 0x7FFFFFFF && code <= h->maxcode[l] && code >= h->mincode[l]) {
            j->bitbuf <<= l; j->bitcnt -= l;
            return h->vals[h->valptr[l] + code - h->mincode[l]];
        }
        l++;
        code = (int)(j->bitbuf >> (32 - l));
    }
    return 0;                                    /* corrupt: behave like EOB */
}

static int extend(int v, int t) { return (t == 0) ? 0 : (v < (1 << (t - 1)) ? v - (1 << t) + 1 : v); }

/* Integer IDCT (Chen-Wang style, 13-bit constants). */
#define W1 2841
#define W2 2676
#define W3 2408
#define W5 1609
#define W6 1108
#define W7 565

static void idct_row(int* blk) {
    int x0, x1, x2, x3, x4, x5, x6, x7, x8;
    if (!((x1 = blk[4] << 11) | (x2 = blk[6]) | (x3 = blk[2]) | (x4 = blk[1]) | (x5 = blk[7]) | (x6 = blk[5]) | (x7 = blk[3]))) {
        int v = blk[0] << 3;
        for (int i = 0; i < 8; i++) blk[i] = v;
        return;
    }
    x0 = (blk[0] << 11) + 128;
    x8 = W7 * (x4 + x5); x4 = x8 + (W1 - W7) * x4; x5 = x8 - (W1 + W7) * x5;
    x8 = W3 * (x6 + x7); x6 = x8 - (W3 - W5) * x6; x7 = x8 - (W3 + W5) * x7;
    x8 = x0 + x1; x0 -= x1;
    x1 = W6 * (x3 + x2); x2 = x1 - (W2 + W6) * x2; x3 = x1 + (W2 - W6) * x3;
    x1 = x4 + x6; x4 -= x6; x6 = x5 + x7; x5 -= x7;
    x7 = x8 + x3; x8 -= x3; x3 = x0 + x2; x0 -= x2;
    x2 = (181 * (x4 + x5) + 128) >> 8; x4 = (181 * (x4 - x5) + 128) >> 8;
    blk[0] = (x7 + x1) >> 8; blk[1] = (x3 + x2) >> 8; blk[2] = (x0 + x4) >> 8; blk[3] = (x8 + x6) >> 8;
    blk[4] = (x8 - x6) >> 8; blk[5] = (x0 - x4) >> 8; blk[6] = (x3 - x2) >> 8; blk[7] = (x7 - x1) >> 8;
}

static uint8_t clamp8(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

static void idct_col(int* blk, uint8_t* out, int stride) {
    int x0, x1, x2, x3, x4, x5, x6, x7, x8;
    if (!((x1 = blk[8 * 4] << 8) | (x2 = blk[8 * 6]) | (x3 = blk[8 * 2]) | (x4 = blk[8 * 1]) | (x5 = blk[8 * 7]) | (x6 = blk[8 * 5]) | (x7 = blk[8 * 3]))) {
        uint8_t v = clamp8(((blk[0] + 32) >> 6) + 128);
        for (int i = 0; i < 8; i++) out[i * stride] = v;
        return;
    }
    x0 = (blk[0] << 8) + 8192;
    x8 = W7 * (x4 + x5) + 4; x4 = (x8 + (W1 - W7) * x4) >> 3; x5 = (x8 - (W1 + W7) * x5) >> 3;
    x8 = W3 * (x6 + x7) + 4; x6 = (x8 - (W3 - W5) * x6) >> 3; x7 = (x8 - (W3 + W5) * x7) >> 3;
    x8 = x0 + x1; x0 -= x1;
    x1 = W6 * (x3 + x2) + 4; x2 = (x1 - (W2 + W6) * x2) >> 3; x3 = (x1 + (W2 - W6) * x3) >> 3;
    x1 = x4 + x6; x4 -= x6; x6 = x5 + x7; x5 -= x7;
    x7 = x8 + x3; x8 -= x3; x3 = x0 + x2; x0 -= x2;
    x2 = (181 * (x4 + x5) + 128) >> 8; x4 = (181 * (x4 - x5) + 128) >> 8;
    out[0 * stride] = clamp8(((x7 + x1) >> 14) + 128); out[1 * stride] = clamp8(((x3 + x2) >> 14) + 128);
    out[2 * stride] = clamp8(((x0 + x4) >> 14) + 128); out[3 * stride] = clamp8(((x8 + x6) >> 14) + 128);
    out[4 * stride] = clamp8(((x8 - x6) >> 14) + 128); out[5 * stride] = clamp8(((x0 - x4) >> 14) + 128);
    out[6 * stride] = clamp8(((x3 - x2) >> 14) + 128); out[7 * stride] = clamp8(((x7 - x1) >> 14) + 128);
}

static void decode_block(jpeg_t* j, jcomp_t* c, uint8_t* out, int stride) {
    int blk[64];
    memset(blk, 0, sizeof(blk));
    const uint16_t* q = j->qt[c->tq];
    int t = decode_huff(j, &j->hdc[c->td]);
    int diff = t ? extend(get_bits(j, t), t) : 0;
    c->dc_pred += diff;
    blk[0] = c->dc_pred * q[0];
    int k = 1;
    while (k < 64) {
        int rs = decode_huff(j, &j->hac[c->ta]);
        int r = rs >> 4, s = rs & 15;
        if (s == 0) {
            if (r == 15) { k += 16; continue; }
            break;                                       /* EOB */
        }
        k += r;
        if (k > 63) break;
        blk[zigzag[k]] = extend(get_bits(j, s), s) * q[k];
        k++;
    }
    for (int i = 0; i < 8; i++) idct_row(blk + i * 8);
    for (int i = 0; i < 8; i++) idct_col(blk + i, out + i, stride);
}

static uint32_t rd16be(const uint8_t* p) { return ((uint32_t)p[0] << 8) | p[1]; }

int jpeg_decode_buf(const uint8_t* data, size_t len, int* out_w, int* out_h,
                    uint32_t* pixels, size_t max_pixels, uint8_t* scratch, size_t scratch_len) {
    static jpeg_t J;                                    /* ~10 KB: keep off the stack */
    jpeg_t* j = &J;
    *out_w = 0; *out_h = 0;
    if (!data || len < 4 || data[0] != 0xFF || data[1] != 0xD8) return -1;
    memset(j, 0, sizeof(*j));
    j->d = data; j->len = len; j->pos = 2;
    int got_sof = 0, got_sos = 0;
    while (j->pos + 4 <= len && !got_sos) {
        if (data[j->pos] != 0xFF) { j->pos++; continue; }
        uint8_t m = data[j->pos + 1];
        if (m == 0xFF) { j->pos++; continue; }
        j->pos += 2;
        if (m == 0xD8 || (m >= 0xD0 && m <= 0xD7) || m == 0x01) continue;
        if (j->pos + 2 > len) return -1;
        uint32_t seglen = rd16be(data + j->pos);
        if (seglen < 2 || j->pos + seglen > len) return -1;
        const uint8_t* s = data + j->pos + 2;
        uint32_t sl = seglen - 2;
        switch (m) {
        case 0xC0: case 0xC1: {                          /* SOF0 baseline / SOF1 extended sequential */
            if (sl < 6) return -1;
            if (s[0] != 8) return -1;
            j->height = (int)rd16be(s + 1); j->width = (int)rd16be(s + 3);
            j->ncomp = s[5];
            if (j->ncomp != 1 && j->ncomp != 3) return -1;
            if (sl < 6 + (uint32_t)j->ncomp * 3) return -1;
            j->hmax = j->vmax = 1;
            for (int i = 0; i < j->ncomp; i++) {
                jcomp_t* c = &j->comp[i];
                c->id = s[6 + i * 3]; c->h = s[7 + i * 3] >> 4; c->v = s[7 + i * 3] & 15; c->tq = s[8 + i * 3] & 3;
                if (c->h < 1 || c->h > 2 || c->v < 1 || c->v > 2) return -1;
                if (c->h > j->hmax) j->hmax = c->h;
                if (c->v > j->vmax) j->vmax = c->v;
            }
            got_sof = 1;
            break;
        }
        case 0xC2: case 0xC6: case 0xCA: case 0xCE:
            return -2;                                   /* progressive: unsupported */
        case 0xC3: case 0xC5: case 0xC7: case 0xC9: case 0xCB: case 0xCD: case 0xCF:
            return -1;                                   /* lossless / hierarchical / arithmetic */
        case 0xC4: {                                     /* DHT */
            uint32_t p = 0;
            while (p + 17 <= sl) {
                int tc = s[p] >> 4, th = s[p] & 15;
                if (th > 3) return -1;
                jhuff_t* h = tc ? &j->hac[th] : &j->hdc[th];
                int total = 0;
                h->bits[0] = 0;
                for (int i = 1; i <= 16; i++) { h->bits[i] = s[p + i]; total += s[p + i]; }
                if (total > 256 || p + 17 + (uint32_t)total > sl) return -1;
                for (int i = 0; i < total; i++) h->vals[i] = s[p + 17 + i];
                build_huff(h);
                p += 17 + (uint32_t)total;
            }
            break;
        }
        case 0xDB: {                                     /* DQT */
            uint32_t p = 0;
            while (p < sl) {
                int pq = s[p] >> 4, tq = s[p] & 3;
                p++;
                if (pq) { if (p + 128 > sl) return -1; for (int i = 0; i < 64; i++) j->qt[tq][i] = (uint16_t)rd16be(s + p + i * 2); p += 128; }
                else { if (p + 64 > sl) return -1; for (int i = 0; i < 64; i++) j->qt[tq][i] = s[p + i]; p += 64; }
            }
            break;
        }
        case 0xDD:                                       /* DRI */
            if (sl >= 2) j->restart_interval = (int)rd16be(s);
            break;
        case 0xDA: {                                     /* SOS */
            if (!got_sof) return -1;
            int ns = s[0];
            if (ns != j->ncomp) return -1;               /* non-interleaved baseline: unsupported */
            for (int i = 0; i < ns; i++) {
                int cid = s[1 + i * 2], tt = s[2 + i * 2];
                for (int k = 0; k < j->ncomp; k++) if (j->comp[k].id == cid) { j->comp[k].td = tt >> 4; j->comp[k].ta = tt & 15; }
            }
            got_sos = 1;
            break;
        }
        case 0xD9: return -1;                            /* EOI before SOS */
        default: break;                                  /* APPn, COM, ... */
        }
        j->pos += seglen;
    }
    if (!got_sof || !got_sos) return -1;
    int W = j->width, H = j->height;
    if (W <= 0 || H <= 0 || W > 4096 || H > 4096) return -1;
    if ((size_t)W * H > max_pixels) return -3;
    int mcuw = 8 * j->hmax, mcuh = 8 * j->vmax;
    int mcux = (W + mcuw - 1) / mcuw, mcuy = (H + mcuh - 1) / mcuh;
    /* allocate planes from scratch */
    size_t used = 0;
    for (int i = 0; i < j->ncomp; i++) {
        jcomp_t* c = &j->comp[i];
        c->bw = mcux * c->h; c->bh = mcuy * c->v;
        size_t need = (size_t)c->bw * 8 * c->bh * 8;
        if (used + need > scratch_len) return -3;
        c->plane = scratch + used; used += need;
        c->dc_pred = 0;
    }
    /* entropy-coded data */
    j->bitbuf = 0; j->bitcnt = 0; j->hit_marker = 0;
    int mcu_count = 0, total_mcus = mcux * mcuy;
    for (int my = 0; my < mcuy; my++) {
        for (int mx = 0; mx < mcux; mx++) {
            for (int ci = 0; ci < j->ncomp; ci++) {
                jcomp_t* c = &j->comp[ci];
                int stride = c->bw * 8;
                for (int by = 0; by < c->v; by++) for (int bx = 0; bx < c->h; bx++) {
                    uint8_t* out = c->plane + (size_t)((my * c->v + by) * 8) * stride + (mx * c->h + bx) * 8;
                    decode_block(j, c, out, stride);
                }
            }
            mcu_count++;
            if (j->restart_interval && (mcu_count % j->restart_interval) == 0 && mcu_count < total_mcus) {
                /* byte-align and consume the RSTn marker */
                j->bitbuf = 0; j->bitcnt = 0; j->hit_marker = 0;
                while (j->pos + 1 < len && !(data[j->pos] == 0xFF && data[j->pos + 1] >= 0xD0 && data[j->pos + 1] <= 0xD7)) j->pos++;
                if (j->pos + 1 < len) j->pos += 2;
                for (int ci = 0; ci < j->ncomp; ci++) j->comp[ci].dc_pred = 0;
            }
        }
    }
    /* colour conversion + upsampling */
    for (int y = 0; y < H; y++) {
        uint32_t* dst = pixels + (size_t)y * W;
        if (j->ncomp == 1) {
            const uint8_t* yp = j->comp[0].plane + (size_t)y * j->comp[0].bw * 8;
            for (int x = 0; x < W; x++) { uint32_t v = yp[x]; dst[x] = 0xFF000000u | (v << 16) | (v << 8) | v; }
            continue;
        }
        jcomp_t* cy = &j->comp[0]; jcomp_t* cb = &j->comp[1]; jcomp_t* cr = &j->comp[2];
        const uint8_t* yp = cy->plane + (size_t)(y * cy->v / j->vmax) * cy->bw * 8;
        const uint8_t* bp = cb->plane + (size_t)(y * cb->v / j->vmax) * cb->bw * 8;
        const uint8_t* rp = cr->plane + (size_t)(y * cr->v / j->vmax) * cr->bw * 8;
        for (int x = 0; x < W; x++) {
            int Y = yp[x * cy->h / j->hmax];
            int Cb = bp[x * cb->h / j->hmax] - 128;
            int Cr = rp[x * cr->h / j->hmax] - 128;
            /* R = Y + 1.402 Cr; G = Y - 0.344 Cb - 0.714 Cr; B = Y + 1.772 Cb  (16.16 fixed) */
            int r = Y + ((91881 * Cr) >> 16);
            int g = Y - ((22554 * Cb + 46802 * Cr) >> 16);
            int b = Y + ((116130 * Cb) >> 16);
            dst[x] = 0xFF000000u | ((uint32_t)clamp8(r) << 16) | ((uint32_t)clamp8(g) << 8) | clamp8(b);
        }
    }
    *out_w = W; *out_h = H;
    return 0;
}
