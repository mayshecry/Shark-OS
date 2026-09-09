/* GIF decoder (GIF87a/GIF89a) for SharkOS: first frame only, global/local
 * palettes, interlacing, transparency via the Graphic Control Extension.
 * LZW with the classic 12-bit code table; integer-only, no allocation
 * beyond caller buffers. Output is 0xAARRGGBB. */

#include "kernel.h"

typedef struct {
    const uint8_t* d;
    size_t len, pos;
    /* sub-block reader */
    int block_left;
    /* bit reader */
    uint32_t bitbuf;
    int bitcnt;
    int eof;
} gif_rd_t;

static int gif_byte(gif_rd_t* r) {
    while (r->block_left == 0) {
        if (r->pos >= r->len) { r->eof = 1; return 0; }
        r->block_left = r->d[r->pos++];
        if (r->block_left == 0) { r->eof = 1; return 0; }        /* block terminator */
    }
    if (r->pos >= r->len) { r->eof = 1; return 0; }
    r->block_left--;
    return r->d[r->pos++];
}

static int gif_bits(gif_rd_t* r, int n) {
    while (r->bitcnt < n) {
        if (r->eof) return -1;
        r->bitbuf |= (uint32_t)gif_byte(r) << r->bitcnt;
        r->bitcnt += 8;
    }
    int v = (int)(r->bitbuf & ((1u << n) - 1));
    r->bitbuf >>= n; r->bitcnt -= n;
    return v;
}

/* LZW tables: prefix/suffix/length, 4096 entries. Kept static (48 KB) so
 * decoding never touches the kernel stack. */
static uint16_t lzw_prefix[4096];
static uint8_t  lzw_suffix[4096];
static uint8_t  lzw_stack[4097];

/* Decodes into idx[w*h] (palette indices; 0xFF..? no: 256 = transparent
 * marker handled by caller via transparent index). Returns 0 on success. */
static int gif_lzw(gif_rd_t* r, uint8_t* out, uint32_t npix, int min_code) {
    if (min_code < 2 || min_code > 8) return -1;
    int clear = 1 << min_code, eoi = clear + 1;
    int code_size = min_code + 1, next = eoi + 1, prev = -1;
    uint32_t o = 0;
    for (int i = 0; i < clear; i++) { lzw_prefix[i] = 0xFFFF; lzw_suffix[i] = (uint8_t)i; }
    int guard = 0;
    while (o < npix && guard++ < 4000000) {
        int code = gif_bits(r, code_size);
        if (code < 0) break;
        if (code == clear) { code_size = min_code + 1; next = eoi + 1; prev = -1; continue; }
        if (code == eoi) break;
        int first;
        int sp = 0;
        int c = code;
        if (code >= next) {
            if (prev < 0 || code != next) return o > 0 ? 0 : -1;    /* corrupt: keep what we have */
            /* KwKwK case: output prev string + its first char */
            c = prev;
            int p = prev; int g2 = 0;
            while (lzw_prefix[p] != 0xFFFF && g2++ < 4096) p = lzw_prefix[p];
            lzw_stack[sp++] = lzw_suffix[p];
        }
        int g3 = 0;
        while (lzw_prefix[c] != 0xFFFF && g3++ < 4096) { lzw_stack[sp++] = lzw_suffix[c]; c = lzw_prefix[c]; }
        lzw_stack[sp++] = lzw_suffix[c];
        first = lzw_suffix[c];
        while (sp > 0 && o < npix) out[o++] = lzw_stack[--sp];
        if (prev >= 0 && next < 4096) {
            lzw_prefix[next] = (uint16_t)prev; lzw_suffix[next] = (uint8_t)first; next++;
            if (next == (1 << code_size) && code_size < 12) code_size++;
        }
        prev = code;
    }
    return 0;
}

int gif_decode_buf(const uint8_t* data, size_t len, int* out_w, int* out_h,
                   uint32_t* pixels, size_t max_pixels, uint8_t* scratch, size_t scratch_len) {
    *out_w = 0; *out_h = 0;
    if (!data || len < 13) return -1;
    if (!(data[0] == 'G' && data[1] == 'I' && data[2] == 'F' && data[3] == '8' && (data[4] == '7' || data[4] == '9') && data[5] == 'a')) return -1;
    uint8_t gpal[256][3]; int gpal_n = 0;
    uint8_t flags = data[10];
    int bg_index = data[11]; (void)bg_index;
    size_t pos = 13;
    if (flags & 0x80) {
        gpal_n = 2 << (flags & 7);
        if (pos + (size_t)gpal_n * 3 > len) return -1;
        for (int i = 0; i < gpal_n; i++) { gpal[i][0] = data[pos + i * 3]; gpal[i][1] = data[pos + i * 3 + 1]; gpal[i][2] = data[pos + i * 3 + 2]; }
        pos += (size_t)gpal_n * 3;
    }
    int transparent = -1;
    while (pos < len) {
        uint8_t b = data[pos++];
        if (b == 0x21) {                                   /* extension */
            if (pos >= len) return -1;
            uint8_t label = data[pos++];
            if (label == 0xF9 && pos + 5 < len && data[pos] == 4) {
                if (data[pos + 1] & 1) transparent = data[pos + 4];
            }
            /* skip sub-blocks */
            while (pos < len) { uint8_t sz = data[pos++]; if (sz == 0) break; pos += sz; }
            continue;
        }
        if (b == 0x3B) break;                              /* trailer */
        if (b != 0x2C) return -1;                          /* image descriptor expected */
        if (pos + 9 > len) return -1;
        int ix = data[pos] | (data[pos + 1] << 8), iy = data[pos + 2] | (data[pos + 3] << 8);
        int w = data[pos + 4] | (data[pos + 5] << 8), h = data[pos + 6] | (data[pos + 7] << 8);
        uint8_t iflags = data[pos + 8];
        pos += 9;
        (void)ix; (void)iy;
        if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return -1;
        if ((size_t)w * h > max_pixels) return -3;
        if ((size_t)w * h > scratch_len) return -3;
        uint8_t lpal[256][3]; int lpal_n = 0;
        if (iflags & 0x80) {
            lpal_n = 2 << (iflags & 7);
            if (pos + (size_t)lpal_n * 3 > len) return -1;
            for (int i = 0; i < lpal_n; i++) { lpal[i][0] = data[pos + i * 3]; lpal[i][1] = data[pos + i * 3 + 1]; lpal[i][2] = data[pos + i * 3 + 2]; }
            pos += (size_t)lpal_n * 3;
        }
        int interlaced = (iflags & 0x40) != 0;
        if (pos >= len) return -1;
        int min_code = data[pos++];
        gif_rd_t r; memset(&r, 0, sizeof(r));
        r.d = data; r.len = len; r.pos = pos;
        uint8_t* idx = scratch;
        memset(idx, 0, (size_t)w * h);
        if (gif_lzw(&r, idx, (uint32_t)w * h, min_code) != 0) return -1;
        uint8_t (*pal)[3] = lpal_n ? lpal : gpal;
        int pal_n = lpal_n ? lpal_n : gpal_n;
        /* de-interlace + palette */
        int row_map_pass[4] = { 0, 4, 2, 1 }, row_step[4] = { 8, 8, 4, 2 };
        int src_row = 0;
        if (interlaced) {
            for (int p = 0; p < 4; p++) {
                for (int y = row_map_pass[p]; y < h; y += row_step[p], src_row++) {
                    const uint8_t* s = idx + (size_t)src_row * w;
                    uint32_t* d = pixels + (size_t)y * w;
                    for (int x = 0; x < w; x++) {
                        int i = s[x];
                        if (i == transparent || i >= pal_n) d[x] = 0;
                        else d[x] = 0xFF000000u | ((uint32_t)pal[i][0] << 16) | ((uint32_t)pal[i][1] << 8) | pal[i][2];
                    }
                }
            }
        } else {
            for (size_t i = 0, n = (size_t)w * h; i < n; i++) {
                int c = idx[i];
                if (c == transparent || c >= pal_n) pixels[i] = 0;
                else pixels[i] = 0xFF000000u | ((uint32_t)pal[c][0] << 16) | ((uint32_t)pal[c][1] << 8) | pal[c][2];
            }
        }
        *out_w = w; *out_h = h;
        return 0;                                          /* first frame only */
    }
    return -1;
}
