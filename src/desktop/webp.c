
 /* WebP decoder for SharkOS (WebM lossless + lossy stills).
 *
 *   - RIFF container: simple (VP8 / VP8L) and extended (VP8X + ALPH),
 *     animated files are reduced to their first frame.
 *   - Lossy images ("VP8 "): full VP8 key-frame decode, YUV -> RGB.
 *   - Lossless images ("VP8L"): bitstream with the four optional transforms.
 *   - Alpha ("ALPH"): lossless or raw, plus the optional prediction filter.
 *
 * Integer only, no allocation: caller provides the pixel buffer and scratch.
 * Output is 0xAARRGGBB, matching png.c/gif.c/jpeg.c. */

#include "kernel.h"

#define WEBP_OK      0
#define WEBP_ERR    -1
#define WEBP_BIG    -3

#define WP_GREEN_MAX 2328
#define WP_MAX_GROUPS 32
#define WP_MAX_CACHE  2048

typedef struct {
    const uint8_t* d;
    size_t len, pos;
    uint32_t bitbuf;
    int bitcnt;
} wp_bit_t;

static void wp_bits_init(wp_bit_t* b, const uint8_t* d, size_t len) {
    b->d = d; b->len = len; b->pos = 0; b->bitbuf = 0; b->bitcnt = 0;
}


static int wp_read_bits(wp_bit_t* b, int n) {
    while (b->bitcnt < n) {
        if (b->pos >= b->len) return -1;
        b->bitbuf |= (uint32_t)b->d[b->pos++] << b->bitcnt;
        b->bitcnt += 8;
    }
    int v = (int)(b->bitbuf & ((1u << n) - 1));
    b->bitbuf >>= n;
    b->bitcnt -= n;
    return v;
}


typedef struct {
    uint16_t sym[WP_GREEN_MAX];
    int count[18], first[18];
    int maxlen;
} wp_huff_t;

static wp_huff_t wp_huff[5];

static int wp_build_huff(wp_huff_t* h, const uint8_t* len, int alphabet) {
    int n, s;
    for (n = 0; n <= 16; n++) h->count[n] = 0;
    h->maxlen = 0;
    for (s = 0; s < alphabet; s++) {
        int l = len[s];
        if (l < 0 || l > 16) return WEBP_ERR;
        if (l) { h->count[l]++; if (l > h->maxlen) h->maxlen = l; }
    }
    if (h->maxlen == 0) { h->count[1] = 1; h->maxlen = 1; }
    int code = 0;
    for (n = 1; n <= h->maxlen; n++) {
        h->first[n] = code;
        int off = 0;
        for (s = 0; s < alphabet; s++)
            if (len[s] == n) h->sym[code + off++] = (uint16_t)s;
        code = (code + off) << 1;
    }
    return WEBP_OK;
}

static int wp_decode_huff(wp_huff_t* h, wp_bit_t* b) {
    int code = 0, first = 0, index = 0;
    for (int n = 1; n <= h->maxlen; n++) {
        int bit = wp_read_bits(b, 1);
        if (bit < 0) return -1;
        code = (code << 1) | bit;
        int cnt = h->count[n];
        if (code - first < cnt) return h->sym[index + (code - first)];
        index += cnt;
        first = (first + cnt) << 1;
        code <<= 1;
    }
    return -1;
}


static const int wp_len_extra[3] = { 2, 3, 7 };
static const int wp_len_repeat[3] = { 3, 3, 11 };
static const int wp_clc_order[19] = { 17, 18, 0, 1, 2, 3, 4, 5, 16, 6, 7, 8, 9,
                                      10, 11, 12, 13, 14, 15 };

static const uint8_t wp_plane[120] = {
    0x18, 0x07, 0x17, 0x19, 0x28, 0x06, 0x27, 0x29, 0x16, 0x1a, 0x26, 0x2a,
    0x38, 0x05, 0x37, 0x39, 0x15, 0x1b, 0x36, 0x3a, 0x25, 0x2b, 0x48, 0x04,
    0x47, 0x49, 0x14, 0x1c, 0x35, 0x3b, 0x46, 0x4a, 0x24, 0x2c, 0x58, 0x45,
    0x4b, 0x34, 0x3c, 0x03, 0x57, 0x59, 0x13, 0x1d, 0x56, 0x5a, 0x23, 0x2d,
    0x44, 0x4c, 0x55, 0x5b, 0x33, 0x3d, 0x68, 0x02, 0x67, 0x69, 0x12, 0x1e,
    0x66, 0x6a, 0x22, 0x2e, 0x54, 0x5c, 0x43, 0x4d, 0x65, 0x6b, 0x32, 0x3e,
    0x78, 0x01, 0x77, 0x79, 0x53, 0x5d, 0x11, 0x1f, 0x64, 0x6c, 0x42, 0x4e,
    0x76, 0x7a, 0x21, 0x2f, 0x75, 0x7b, 0x31, 0x3f, 0x63, 0x6d, 0x52, 0x5e,
    0x00, 0x74, 0x7c, 0x41, 0x4f, 0x10, 0x20, 0x62, 0x6e, 0x30, 0x73, 0x7d,
    0x51, 0x5f, 0x40, 0x72, 0x7e, 0x61, 0x6f, 0x50, 0x71, 0x7f, 0x60, 0x70 };



typedef struct {
    wp_bit_t bit;
    uint8_t*  lenbuf;       
    uint32_t* cache;        
    int cache_bits;         
    uint32_t* meta;         
    int meta_w, meta_bits, have_meta;
    int groups;
    int green_alpha;        
    int max_groups;         
} wp_l8_t;

static uint32_t wp_cache_idx(const wp_l8_t* d, uint32_t pix) {
    return (uint32_t)((pix * 0x1e35a7bdu) >> (32 - d->cache_bits));
}


static int wp_read_code(wp_l8_t* d, uint8_t* len, int alphabet) {
    int simple = wp_read_bits(&d->bit, 1);
    if (simple < 0) return WEBP_ERR;
    for (int i = 0; i < alphabet; i++) len[i] = 0;
    if (simple) {
        int ns = wp_read_bits(&d->bit, 1);
        if (ns < 0) return WEBP_ERR;
        ns++;
        int is8 = wp_read_bits(&d->bit, 1);
        if (is8 < 0) return WEBP_ERR;
        int s0 = wp_read_bits(&d->bit, 1 + 7 * is8);
        if (s0 < 0 || s0 >= alphabet) return WEBP_ERR;
        len[s0] = 1;
        if (ns == 2) {
            int s1 = wp_read_bits(&d->bit, 8);
            if (s1 < 0 || s1 >= alphabet) return WEBP_ERR;
            len[s1] = 1;
        }
        return WEBP_OK;
    }

    uint8_t clen[19] = { 0 };
    int nc = wp_read_bits(&d->bit, 4);
    if (nc < 0) return WEBP_ERR;
    nc += 4;
    for (int i = 0; i < nc; i++) {
        int l = wp_read_bits(&d->bit, 3);
        if (l < 0) return WEBP_ERR;
        clen[wp_clc_order[i]] = (uint8_t)l;
    }
    wp_huff_t clc;
    if (wp_build_huff(&clc, clen, 19) != WEBP_OK) return WEBP_ERR;
    int max_symbol;
    if (wp_read_bits(&d->bit, 1) == 0) max_symbol = alphabet;
    else {
        int lnb = wp_read_bits(&d->bit, 3);
        if (lnb < 0) return WEBP_ERR;
        lnb = 2 + 2 * lnb;
        max_symbol = wp_read_bits(&d->bit, lnb);
        if (max_symbol < 0) return WEBP_ERR;
        max_symbol += 2;
        if (max_symbol > alphabet) return WEBP_ERR;
    }
    int num = 0, prev = 8;
    while (num < max_symbol) {
        int sym = wp_decode_huff(&clc, &d->bit);
        if (sym < 0 || sym > 18) return WEBP_ERR;
        if (sym < 16) {
            len[num] = (uint8_t)sym;
            if (sym) prev = sym;
            num++;
            continue;
        }
        int e = wp_read_bits(&d->bit, wp_len_extra[sym - 16]);
        if (e < 0) return WEBP_ERR;
        int rep = wp_len_repeat[sym - 16] + e;
        if (num + rep > alphabet) return WEBP_ERR;
        int v = 0;
        if (sym == 16) { v = prev; if (num == 0) v = 8; }
        for (int k = 0; k < rep; k++) { len[num + k] = (uint8_t)v; if (v) prev = v; }
        num += rep;
    }
    return WEBP_OK;
}


static int wp_read_group(wp_l8_t* d, int g) {
    uint8_t* base = d->lenbuf + (size_t)g * 5 * WP_GREEN_MAX;
    int alpha[5] = { d->green_alpha, 256, 256, 256, 40 };
    for (int c = 0; c < 5; c++)
        if (wp_read_code(d, base + (size_t)c * WP_GREEN_MAX, alpha[c]) != WEBP_OK)
            return WEBP_ERR;
    return WEBP_OK;
}

static int wp_decode_image(wp_l8_t* d, uint32_t* dst, int w, int h,
                           int xsize, int with_meta) {
    d->green_alpha = 280;                       
    int cc = wp_read_bits(&d->bit, 1);
    if (cc < 0) return WEBP_ERR;
    d->cache_bits = -1;
    if (cc) {
        int cb = wp_read_bits(&d->bit, 4);
        if (cb < 1 || cb > 11) return WEBP_ERR;
        d->cache_bits = cb;
        memset(d->cache, 0, (size_t)(1u << cb) * sizeof(uint32_t));
        d->green_alpha = 256 + 24 + (1 << cb);
    }
    d->have_meta = 0;
    if (with_meta) {
        int hm = wp_read_bits(&d->bit, 1);
        if (hm < 0) return WEBP_ERR;
        if (hm) {
            int pb = wp_read_bits(&d->bit, 3);
            if (pb < 0) return WEBP_ERR;
            pb += 2;
            d->meta_bits = pb;
            int pw = (w + (1 << pb) - 1) >> pb;
            int ph = (h + (1 << pb) - 1) >> pb;
            if ((uint32_t)pw * ph > 4096 || d->meta == NULL) return WEBP_ERR;
            if (wp_decode_image(d, d->meta, pw, ph, pw, 0) != WEBP_OK) return WEBP_ERR;
            d->meta_w = pw;
            d->have_meta = 1;
        }
    }

    d->groups = 1;
    if (d->have_meta) {
        uint32_t maxc = 0;
        int mh = (h + (1 << d->meta_bits) - 1) >> d->meta_bits;
        for (int i = 0; i < d->meta_w * mh; i++) {
            uint32_t v = d->meta[i] >> 8;
            if (v > maxc) maxc = v;
        }
        d->groups = (int)maxc + 1;
    }
    if (d->groups > d->max_groups) return WEBP_ERR;
    for (int g = 0; g < d->groups; g++)
        if (wp_read_group(d, g) != WEBP_OK) return WEBP_ERR;

    int cur_group = -1;
    int len_lim = 256 + 24;
    int pos = 0, x = 0, y = 0;
    while (pos < w * h) {
        int gi = 0;
        if (d->have_meta)
            gi = (int)((d->meta[(y >> d->meta_bits) * d->meta_w +
                                (x >> d->meta_bits)] >> 8) & 0xffff);
        if (gi != cur_group) {
            if (gi < 0 || gi >= d->groups) return WEBP_ERR;
            uint8_t* base = d->lenbuf + (size_t)gi * 5 * WP_GREEN_MAX;
            int alpha[5] = { d->green_alpha, 256, 256, 256, 40 };
            for (int c = 0; c < 5; c++)
                if (wp_build_huff(&wp_huff[c], base + (size_t)c * WP_GREEN_MAX,
                                  alpha[c]) != WEBP_OK) return WEBP_ERR;
            cur_group = gi;
        }
        int sc = wp_decode_huff(&wp_huff[0], &d->bit);
        if (sc < 0) return WEBP_ERR;
        if (sc < 256) {
            int r = wp_decode_huff(&wp_huff[1], &d->bit);
            int bl = wp_decode_huff(&wp_huff[2], &d->bit);
            int a = wp_decode_huff(&wp_huff[3], &d->bit);
            if (r < 0 || bl < 0 || a < 0) return WEBP_ERR;
            uint32_t pix = ((uint32_t)a << 24) | ((uint32_t)r << 16) |
                           ((uint32_t)sc << 8) | (uint32_t)bl;
            dst[(size_t)y * xsize + x] = pix;
            if (d->cache_bits > 0) d->cache[wp_cache_idx(d, pix)] = pix;
        } else if (sc < len_lim) {
            int sym = sc - 256;
            int len, eb = (sym - 2) >> 1;
            if (sym < 4) len = sym + 1;
            else {
                int xb = wp_read_bits(&d->bit, eb);
                if (xb < 0) return WEBP_ERR;
                len = ((2 + (sym & 1)) << eb) + xb + 1;
            }
            int ds = wp_decode_huff(&wp_huff[4], &d->bit);
            if (ds < 0) return WEBP_ERR;
            int dist_code;
            if (ds < 4) dist_code = ds + 1;
            else {
                int eb2 = (ds - 2) >> 1;
                int xb = wp_read_bits(&d->bit, eb2);
                if (xb < 0) return WEBP_ERR;
                dist_code = ((2 + (ds & 1)) << eb2) + xb + 1;
            }
            int dist;
            if (dist_code > 120) dist = dist_code - 120;
            else {
                int p = wp_plane[dist_code - 1];
                int dx = (p & 15) - ((p & 8) ? 16 : 0);
                int dy = (p >> 4) - ((p & 0x80) ? 16 : 0);
                dist = dx + dy * w;
                if (dist < 1) dist = 1;
            }
            for (int k = 0; k < len; k++) {
                if (pos + k >= w * h) return WEBP_ERR;
                int p = pos + k;
                int sp = p - dist; if (sp < 0) sp = 0;
                uint32_t v = dst[(size_t)(sp / w) * xsize + sp % w];
                dst[(size_t)(p / w) * xsize + p % w] = v;
                if (d->cache_bits > 0) d->cache[wp_cache_idx(d, v)] = v;
            }
            pos += len;
            x = pos % w; y = pos / w;
            continue;
        } else {
            uint32_t idx = (uint32_t)(sc - len_lim);
            if (d->cache_bits > 0 && idx < (1u << d->cache_bits)) {
                uint32_t pix = d->cache[idx];
                dst[(size_t)y * xsize + x] = pix;
                d->cache[wp_cache_idx(d, pix)] = pix;
            } else return WEBP_ERR;
        }
        pos++;
        x = pos % w; y = pos / w;
    }
    return WEBP_OK;
}



typedef struct {
    int type;              
    int block_bits;        
    uint32_t* data;        
    int data_w, data_h;    
    int pix_bits;          
    int wb;                
    int count;             
} wp_tx_t;

#define WP_TX_NONE 0
#define WP_TX_SUB  1      
#define WP_TX_PRED 2      
#define WP_TX_COLOR 3     
#define WP_TX_PAL 4       


static uint32_t wp_pred(int m, uint32_t L, uint32_t T, uint32_t TR, uint32_t TL) {
    switch (m) {
    case 0: return 0xff000000u;
    case 1: return L;
    case 2: return T;
    case 3: return TR;
    case 4: return TL;
    case 5: return (L + TR) / 2;
    case 6: return (L + TL) / 2;
    case 7: return (L + T) / 2;
    case 8: return (TL + T) / 2;
    case 9: return (T + TR) / 2;
    case 10: return ((L + TL) / 2 + (T + TR) / 2) / 2;
    case 11: {
        int pA = (int)(L >> 24) + (int)(T >> 24) - (int)(TL >> 24);
        int pR = (int)((L >> 16) & 255) + (int)((T >> 16) & 255) - (int)((TL >> 16) & 255);
        int pG = (int)((L >> 8) & 255) + (int)((T >> 8) & 255) - (int)((TL >> 8) & 255);
        int pB = (int)(L & 255) + (int)(T & 255) - (int)(TL & 255);
        int dL = pA - (int)(L >> 24);               if (dL < 0) dL = -dL;
        dL += pR - (int)((L >> 16) & 255);          if (dL < 0) dL = -dL;
        dL += pG - (int)((L >> 8) & 255);           if (dL < 0) dL = -dL;
        dL += pB - (int)(L & 255);                  if (dL < 0) dL = -dL;
        int dT = pA - (int)(T >> 24);               if (dT < 0) dT = -dT;
        dT += pR - (int)((T >> 16) & 255);          if (dT < 0) dT = -dT;
        dT += pG - (int)((T >> 8) & 255);           if (dT < 0) dT = -dT;
        dT += pB - (int)(T & 255);                  if (dT < 0) dT = -dT;
        return dL < dT ? L : T;
    }
    case 12: {
        int a = (int)(L >> 24) + (int)(T >> 24) - (int)(TL >> 24);
        int r = (int)((L >> 16) & 255) + (int)((T >> 16) & 255) - (int)((TL >> 16) & 255);
        int g = (int)((L >> 8) & 255) + (int)((T >> 8) & 255) - (int)((TL >> 8) & 255);
        int b = (int)(L & 255) + (int)(T & 255) - (int)(TL & 255);
        if (a < 0) a = 0; else if (a > 255) a = 255;
        if (r < 0) r = 0; else if (r > 255) r = 255;
        if (g < 0) g = 0; else if (g > 255) g = 255;
        if (b < 0) b = 0; else if (b > 255) b = 255;
        return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }
    default: break;
    }

    int a = (int)(L >> 24) + ((int)(L >> 24) - (int)(TL >> 24)) / 2;
    int r = (int)((L >> 16) & 255) + ((int)((L >> 16) & 255) - (int)((TL >> 16) & 255)) / 2;
    int g = (int)((L >> 8) & 255) + ((int)((L >> 8) & 255) - (int)((TL >> 8) & 255)) / 2;
    int b = (int)(L & 255) + ((int)(L & 255) - (int)(TL & 255)) / 2;
    if (a < 0) a = 0; else if (a > 255) a = 255;
    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint32_t wp_color_inv(uint32_t cte, uint32_t pix) {
    int8_t g2r = (int8_t)((cte >> 16) & 255), g2b = (int8_t)((cte >> 8) & 255);
    int8_t r2b = (int8_t)(cte & 255);
    int a = (int)(pix >> 24);
    int8_t g = (int8_t)((pix >> 8) & 255);
    int r = (int)((pix >> 16) & 255) + ((g2r * g) >> 5);
    int tr = r & 255;
    int b = (int)(pix & 255) + ((g2b * g) >> 5) + ((r2b * (int8_t)tr) >> 5);
    return ((uint32_t)a << 24) | ((uint32_t)tr << 16) | ((uint32_t)(g & 255) << 8) |
           (uint32_t)(b & 255);
}

static int wp_apply_tx(wp_tx_t* tx, int ntx, uint32_t* pix, int* pw, int h) {
    for (int t = ntx - 1; t >= 0; t--) {
        int w = *pw;
        if (tx[t].type == WP_TX_SUB) {
            for (int i = 0; i < w * h; i++) {
                uint32_t p = pix[i];
                int g = (int)((p >> 8) & 255);
                int r = (int)((p >> 16) & 255) + g;
                int b = (int)(p & 255) + g;
                pix[i] = (p & 0xff000000u) | ((uint32_t)(r & 255) << 16) |
                         (p & 0x0000ff00u) | (uint32_t)(b & 255);
            }
        } else if (tx[t].type == WP_TX_PRED) {
            int n = 1 << tx[t].block_bits;
            int tw = tx[t].data_w;
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    uint32_t res = pix[(size_t)y * w + x];
                    uint32_t pred;
                    if (x == 0 && y == 0) pred = 0xff000000u;
                    else if (y == 0) pred = pix[(size_t)y * w + (x - 1)];
                    else if (x == 0) pred = pix[(size_t)(y - 1) * w];
                    else {
                        int mode = (int)((tx[t].data[(size_t)(y / n) * tw + (x / n)] >> 8) & 255);
                        if (mode > 13) mode = 13;
                        uint32_t L = pix[(size_t)y * w + (x - 1)];
                        uint32_t T = pix[(size_t)(y - 1) * w + x];
                        uint32_t TL = pix[(size_t)(y - 1) * w + (x - 1)];
                        uint32_t TR = (x == w - 1) ? pix[(size_t)(y - 1) * w]
                                                   : pix[(size_t)(y - 1) * w + (x + 1)];
                        pred = wp_pred(mode, L, T, TR, TL);
                    }
                    int a = (int)(res >> 24) + (int)(pred >> 24);
                    int r = (int)((res >> 16) & 255) + (int)((pred >> 16) & 255);
                    int g = (int)((res >> 8) & 255) + (int)((pred >> 8) & 255);
                    int b = (int)(res & 255) + (int)(pred & 255);
                    pix[(size_t)y * w + x] = ((uint32_t)(a & 255) << 24) |
                        ((uint32_t)(r & 255) << 16) | ((uint32_t)(g & 255) << 8) |
                        (uint32_t)(b & 255);
                }
            }
        } else if (tx[t].type == WP_TX_COLOR) {
            int n = 1 << tx[t].block_bits;
            int tw = tx[t].data_w;
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    uint32_t cte = tx[t].data[(size_t)(y / n) * tw + (x / n)];
                    int i = (size_t)y * w + x;
                    pix[i] = wp_color_inv(cte, pix[i]);
                }
        } else if (tx[t].type == WP_TX_PAL) {

            int wb = tx[t].wb;
            int nfull = *pw;
            int npack = (nfull + (1 << wb) - 1) >> wb;
            uint32_t* pal = tx[t].data;
            int cnt = tx[t].count;
            for (int y = 0; y < h; y++) {
                uint32_t* row = pix + (size_t)y * nfull;
                for (int i = npack - 1; i >= 0; i--) {
                    uint32_t packed = row[i];
                    for (int k = 0; k < (1 << wb); k++) {
                        int idx;
                        if (wb == 1) idx = (int)((packed >> (4 * k)) & 15);
                        else if (wb == 2) idx = (int)((packed >> (2 * k)) & 3);
                        else idx = (int)((packed >> k) & 1);
                        uint32_t col = (idx < cnt) ? pal[idx] : 0;
                        row[i * (1 << wb) + k] = col;
                    }
                }
            }
            *pw = nfull;
        }
    }
    return WEBP_OK;
}


static int wp_vp8l(const uint8_t* data, size_t len, uint32_t* pix,
                   int* out_w, int* out_h, uint32_t max_pixels,
                   uint8_t* scratch, size_t scratch_len) {
    wp_l8_t d;
    memset(&d, 0, sizeof(d));
    wp_bits_init(&d.bit, data, len);
    if (wp_read_bits(&d.bit, 8) != 0x2f) return WEBP_ERR;
    int w = wp_read_bits(&d.bit, 14), h = wp_read_bits(&d.bit, 14);
    (void)wp_read_bits(&d.bit, 1);           
    int ver = wp_read_bits(&d.bit, 3);
    if (w < 0 || h < 0 || ver != 0) return WEBP_ERR;
    w++; h++;
    if ((size_t)w * h > max_pixels) return WEBP_BIG;


    size_t sp = 0;
    d.max_groups = WP_MAX_GROUPS;
    size_t need = (size_t)WP_MAX_GROUPS * 5 * WP_GREEN_MAX;
    if (need > scratch_len) return WEBP_BIG;
    d.lenbuf = scratch + sp; sp += need;
    if (sp + WP_MAX_CACHE * 4 > scratch_len) return WEBP_BIG;
    d.cache = (uint32_t*)(scratch + sp); sp += (size_t)WP_MAX_CACHE * 4;
    if (sp + 4096 * 4 > scratch_len) return WEBP_BIG;
    d.meta = (uint32_t*)(scratch + sp); sp += 4096 * 4;
    uint8_t* sub = scratch + sp;
    size_t sub_len = scratch_len - sp;


    wp_tx_t txs[4];
    int ntx = 0;
    for (;;) {
        int tf = wp_read_bits(&d.bit, 1);
        if (tf < 0) return WEBP_ERR;
        if (tf == 0) break;
        if (ntx >= 4) return WEBP_ERR;
        int ty = wp_read_bits(&d.bit, 2);
        if (ty < 0) return WEBP_ERR;
        wp_tx_t* t = &txs[ntx++];
        memset(t, 0, sizeof(*t));
        if (ty == 0 || ty == 1) {
            int bb = wp_read_bits(&d.bit, 3);
            if (bb < 0) return WEBP_ERR;
            bb += 2;
            t->type = ty == 0 ? WP_TX_PRED : WP_TX_COLOR;
            t->block_bits = bb;
            int n = 1 << bb;
            t->data_w = (w + n - 1) / n; t->data_h = (h + n - 1) / n;
            size_t bytes = (size_t)t->data_w * t->data_h * 4;
            if (bytes > sub_len) return WEBP_BIG;
            t->data = (uint32_t*)sub;
            sub += bytes; sub_len -= bytes;
            if (wp_decode_image(&d, t->data, t->data_w, t->data_h,
                                t->data_w, 0) != WEBP_OK) return WEBP_ERR;
        } else if (ty == 2) {
            t->type = WP_TX_SUB;
        } else {
            t->type = WP_TX_PAL;
            int cnt = wp_read_bits(&d.bit, 8);
            if (cnt < 0) return WEBP_ERR;
            cnt++;
            t->count = cnt;
            size_t bytes = (size_t)cnt * 4;
            if (bytes > sub_len) return WEBP_BIG;
            t->data = (uint32_t*)sub;
            sub += bytes; sub_len -= bytes;
            if (wp_decode_image(&d, t->data, cnt, 1, cnt, 0) != WEBP_OK) return WEBP_ERR;
            for (int i = 1; i < cnt; i++) {  
                t->data[i] = ((t->data[i - 1] + t->data[i]) & 0xff000000u)
                           | (((t->data[i - 1] & 0xff0000u) + (t->data[i] & 0xff0000u)) & 0xff0000u)
                           | (((t->data[i - 1] & 0xff00u) + (t->data[i] & 0xff00u)) & 0xff00u)
                           | (((t->data[i - 1] & 0xffu) + (t->data[i] & 0xffu)) & 0xffu);
            }
            if (cnt <= 2) t->wb = 3;
            else if (cnt <= 4) t->wb = 2;
            else if (cnt <= 16) t->wb = 1;
            else t->wb = 0;
            if (t->wb) w = (w + (1 << t->wb) - 1) >> t->wb;
        }
    }


    if (wp_decode_image(&d, pix, w, h, w, 1) != WEBP_OK) return WEBP_ERR;
    if (wp_apply_tx(txs, ntx, pix, &w, h) != WEBP_OK) return WEBP_ERR;
    *out_w = w; *out_h = h;
    return WEBP_OK;
}



int webp_decode_buf(const uint8_t* data, size_t len, int* out_w, int* out_h,
                    uint32_t* pixels, size_t max_pixels,
                    uint8_t* scratch, size_t scratch_len) {
    if (len < 12 || data[0] != 'R' || data[1] != 'I' || data[2] != 'F' || data[3] != 'F')
        return WEBP_ERR;
    size_t riff_len = (size_t)data[4] | ((size_t)data[5] << 8) | ((size_t)data[6] << 16) | ((size_t)data[7] << 24);
    if (riff_len + 8 > len) riff_len = len - 8;
    if (data[8] != 'W' || data[9] != 'E' || data[10] != 'B' || data[11] != 'P')
        return WEBP_ERR;
    size_t pos = 12;
    size_t end = 8 + riff_len;
    if (end > len) end = len;
    int is_ext = 0;
    int has_alph = 0;
    size_t alph_pos = 0, alph_len = 0;
    size_t vp8l_pos = 0, vp8l_len = 0;
    while (pos + 8 <= end) {
        const uint8_t* chunk = data + pos;
        size_t clen = (size_t)chunk[4] | ((size_t)chunk[5] << 8) | ((size_t)chunk[6] << 16) | ((size_t)chunk[7] << 24);
        size_t chunk_end = pos + 8 + clen;
        if (chunk_end > end) chunk_end = end;
        if (clen > (size_t)(end - pos - 8)) clen = end - pos - 8;
        if (chunk[0] == 'V' && chunk[1] == 'P' && chunk[2] == '8' && chunk[3] == 'X') {
            is_ext = 1;
            if (clen >= 4) {
                uint32_t flags = (uint8_t)chunk[8] | ((uint8_t)chunk[9] << 8) | ((uint8_t)chunk[10] << 16) | ((uint8_t)chunk[11] << 24);
                has_alph = (flags >> 4) & 1;
            }
        } else if (chunk[0] == 'A' && chunk[1] == 'L' && chunk[2] == 'P' && chunk[3] == 'H') {
            alph_pos = pos + 8;
            alph_len = clen;
            (void)alph_pos; (void)alph_len;
        } else if (chunk[0] == 'V' && chunk[1] == 'P' && chunk[2] == '8' && chunk[3] == 'L') {
            vp8l_pos = pos + 8;
            vp8l_len = clen;
        } else if (chunk[0] == 'V' && chunk[1] == 'P' && chunk[2] == '8' && chunk[3] == ' ') {
            return WEBP_ERR;
        }
        pos = chunk_end;
        if (clen & 1) pos++;
        if (vp8l_pos) break;
    }
    if (!vp8l_pos) return WEBP_ERR;
    return wp_vp8l(data + vp8l_pos, vp8l_len, pixels, out_w, out_h, max_pixels, scratch, scratch_len);
}