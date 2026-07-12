
#include "kernel.h"


#define PNG_SIG "\x89PNG\r\n\x1A\n"
#define PNG_IHDR 0x49484452
#define PNG_IDAT 0x49444154
#define PNG_PLTE 0x504C5445
#define PNG_IEND 0x49454E44


static const int length_base[29] = {
    3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 22, 28, 32, 40, 48, 56,
    64, 72, 80, 96, 112, 128, 160, 192, 256, 320, 448, 640, 1024
};
static const int length_extra[29] = {
    0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5, 5
};


static const int dist_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};
static const int dist_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4
};


typedef struct {
    const uint8_t* data;
    size_t len;
    size_t pos;
    uint32_t bit_buf;
    int bits;
} bitstream_t;

static inline void bs_init(bitstream_t* bs, const uint8_t* data, size_t len) {
    bs->data = data;
    bs->len = len;
    bs->pos = 0;
    bs->bit_buf = 0;
    bs->bits = 0;
}


static int decode_huffman_symbol(bitstream_t* bs) {
    while (bs->bits < 7 && bs->pos < bs->len) {
        bs->bit_buf |= ((uint32_t)bs->data[bs->pos++]) << bs->bits;
        bs->bits += 8;
    }
    
    if (bs->bits < 7) return -1;
    
    uint8_t bits7 = (uint8_t)(bs->bit_buf & 0x7F);
    if (bits7 <= 23) {
        int sym = (bits7 == 0) ? 256 : 256 + bits7;
        bs->bit_buf >>= 7;
        bs->bits -= 7;
        
        if (sym >= 257) {
            int idx = sym - 257;
            int length = length_base[idx];
            int extra = length_extra[idx];
            if (extra > 0) {
                while (bs->bits < extra && bs->pos < bs->len) {
                    bs->bit_buf |= ((uint32_t)bs->data[bs->pos++]) << bs->bits;
                    bs->bits += 8;
                }
                if (bs->bits >= extra) {
                    length += bs->bit_buf & ((1U << extra) - 1);
                    bs->bit_buf >>= extra;
                    bs->bits -= extra;
                }
            }
            return length + 0x100;
        }
        return sym;
    }
    
    while (bs->bits < 8 && bs->pos < bs->len) {
        bs->bit_buf |= ((uint32_t)bs->data[bs->pos++]) << bs->bits;
        bs->bits += 8;
    }
    
    if (bs->bits < 8) return -1;
    
    uint8_t byte = (uint8_t)(bs->bit_buf & 0xFF);
    
    if (byte <= 143) {
        bs->bit_buf >>= 8;
        bs->bits -= 8;
        return byte;
    }
    
    while (bs->bits < 9 && bs->pos < bs->len) {
        bs->bit_buf |= ((uint32_t)bs->data[bs->pos++]) << bs->bits;
        bs->bits += 8;
    }
    
    if (bs->bits < 9) return -1;
    
    uint16_t bits9 = (uint16_t)(bs->bit_buf & 0x1FF);
    
    if (bits9 >= 0x190 && bits9 <= 0x1FF) {
        int sym = 144 + (bits9 - 0x190);
        bs->bit_buf >>= 9;
        bs->bits -= 9;
        return sym;
    }
    
    if (byte >= 0xFC) {
        int sym = 280 + (byte - 0xFC);
        bs->bit_buf >>= 8;
        bs->bits -= 8;
        
        int length, extra_bits;
        if (sym <= 283) {
            length = length_base[sym - 257];
            extra_bits = length_extra[sym - 257];
        } else if (sym == 284) {
            length = 320;
            extra_bits = 4;
        } else {
            length = 1024;
            extra_bits = 5;
        }
        
        while (bs->bits < extra_bits && bs->pos < bs->len) {
            bs->bit_buf |= ((uint32_t)bs->data[bs->pos++]) << bs->bits;
            bs->bits += 8;
        }
        if (bs->bits >= extra_bits) {
            length += bs->bit_buf & ((1U << extra_bits) - 1);
            bs->bit_buf >>= extra_bits;
            bs->bits -= extra_bits;
        }
        
        return length + 0x100;
    }
    
    return -1;
}


static int decode_distance(bitstream_t* bs) {
    while (bs->bits < 5 && bs->pos < bs->len) {
        bs->bit_buf |= ((uint32_t)bs->data[bs->pos++]) << bs->bits;
        bs->bits += 8;
    }
    
    if (bs->bits < 5) return -1;
    
    int dist_code = (int)(bs->bit_buf & 0x1F);
    bs->bit_buf >>= 5;
    bs->bits -= 5;
    
    if (dist_code < 30) {
        int dist = dist_base[dist_code];
        int extra = dist_extra[dist_code];
        
        if (extra > 0) {
            while (bs->bits < extra && bs->pos < bs->len) {
                bs->bit_buf |= ((uint32_t)bs->data[bs->pos++]) << bs->bits;
                bs->bits += 8;
            }
            if (bs->bits >= extra) {
                dist += bs->bit_buf & ((1U << extra) - 1);
                bs->bit_buf >>= extra;
                bs->bits -= extra;
            }
        }
        return dist;
    }
    
    return -1;
}


static bool inflate(const uint8_t* comp, size_t comp_len, uint8_t* out, size_t out_len) {
    bitstream_t bs;
    bs_init(&bs, comp, comp_len);
    
    bs.pos += 2; 
    
    uint8_t* out_ptr = out;
    uint8_t* out_end = out + out_len;
    
    while (out_ptr < out_end && bs.pos < bs.len + 10000) {
        while (bs.bits < 3 && bs.pos < bs.len) {
            bs.bit_buf |= ((uint32_t)bs.data[bs.pos++]) << bs.bits;
            bs.bits += 8;
        }
        
        if (bs.pos >= bs.len && bs.bits < 3) break;
        
        int bfinal = (int)(bs.bit_buf & 1);
        int btype = (int)((bs.bit_buf >> 1) & 3);
        bs.bit_buf >>= 3;
        bs.bits -= 3;
        
        if (btype == 0) {
            bs.bits = 0;
            bs.bit_buf = 0;
            
            if (bs.pos + 4 > bs.len) break;
            
            uint16_t len = bs.data[bs.pos] | (bs.data[bs.pos + 1] << 8);
            uint16_t nlen = bs.data[bs.pos + 2] | (bs.data[bs.pos + 3] << 8);
            bs.pos += 4;
            
            if (len == (uint16_t)~nlen) {
                for (int i = 0; i < len && out_ptr < out_end; i++) {
                    *out_ptr++ = bs.data[bs.pos++];
                }
            }
        } else if (btype == 1) {
            while (out_ptr < out_end) {
                int sym = decode_huffman_symbol(&bs);
                if (sym < 0) break;
                
                if (sym == 256) break;
                
                if (sym < 256) {
                    *out_ptr++ = (uint8_t)sym;
                } else {
                    int length = sym - 0x100;
                    
                    int dist = decode_distance(&bs);
                    if (dist < 1) dist = 1;
                    
                    for (int i = 0; i < length && out_ptr < out_end; i++) {
                        int src = (int)(out_ptr - out) - dist;
                        uint8_t val = (src >= 0) ? out[src] : 0;
                        *out_ptr++ = val;
                    }
                }
            }
        }
        
        if (bfinal) break;
    }
    
    return (out_ptr == out_end);
}


static inline uint8_t paeth_predictor(uint8_t a, uint8_t b, uint8_t c) {
    int pa = (a <= b) ? (b - a) : (a - b);
    int pb = (b <= c) ? (c - b) : (b - c);
    int pc = (a + b - c <= a - b) ? ((a + b - c) < 0 ? -(a + b - c) : (a + b - c)) : ((a - b) < 0 ? -(a - b) : (a - b));
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}


uint32_t* decode_png(const uint8_t* png_data, size_t png_len, int* out_w, int* out_h) {
    *out_w = 0;
    *out_h = 0;
    
    if (!png_data || png_len < 33) return NULL;
    
    
    static const uint8_t sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    for (int i = 0; i < 8; i++) {
        if (png_data[i] != sig[i]) return NULL;
    }
    
    size_t pos = 8;
    uint32_t width = 0, height = 0;
    uint8_t bit_depth = 8, color_type = 0;
    uint8_t* idat_buffer = NULL;
    size_t idat_alloc = 0;
    size_t idat_len = 0;
    uint8_t palette[256][3] = {0};
    bool has_palette = false;
    bool alloc_failed = false;
    
    while (pos + 8 < png_len && !alloc_failed) {
        if (pos + 16 > png_len) break;
        
        uint32_t chunk_len = png_data[pos] | (png_data[pos + 1] << 8) | 
                             (png_data[pos + 2] << 16) | (png_data[pos + 3] << 24);
        uint32_t chunk_type = png_data[pos + 4] | (png_data[pos + 5] << 8) | 
                              (png_data[pos + 6] << 16) | (png_data[pos + 7] << 24);
        
        if (pos + 12 + chunk_len > png_len) break;
        pos += 8;
        
        if (chunk_type == PNG_IHDR && chunk_len >= 13) {
            width = png_data[pos] | (png_data[pos + 1] << 8) | 
                    (png_data[pos + 2] << 16) | (png_data[pos + 3] << 24);
            height = png_data[pos + 4] | (png_data[pos + 5] << 8) | 
                     (png_data[pos + 6] << 16) | (png_data[pos + 7] << 24);
            bit_depth = png_data[pos + 8];
            color_type = png_data[pos + 9];
        } else if (chunk_type == PNG_IDAT) {
            size_t needed = idat_len + chunk_len;
            if (needed > idat_alloc) {
                size_t new_alloc = needed + 65536;
                if (new_alloc > 1048576) { alloc_failed = true; continue; }
                uint8_t* new_buf = (uint8_t*)kmalloc(new_alloc);
                if (!new_buf) { alloc_failed = true; continue; }
                if (idat_buffer) {
                    memcpy(new_buf, idat_buffer, idat_len);
                    kfree(idat_buffer);
                }
                idat_buffer = new_buf;
                idat_alloc = new_alloc;
            }
            memcpy(idat_buffer + idat_len, png_data + pos, chunk_len);
            idat_len += chunk_len;
        } else if (chunk_type == PNG_PLTE && chunk_len >= 3) {
            int entries = chunk_len / 3;
            if (entries > 256) entries = 256;
            for (int i = 0; i < entries; i++) {
                palette[i][0] = png_data[pos + i * 3];
                palette[i][1] = png_data[pos + i * 3 + 1];
                palette[i][2] = png_data[pos + i * 3 + 2];
            }
            has_palette = true;
        }
        pos += chunk_len;
    }
    
    if (!width || !height || idat_len == 0 || alloc_failed) {
        if (idat_buffer) { kfree(idat_buffer); idat_buffer = NULL; }
        return NULL;
    }
    
    if (bit_depth != 8) {
        return NULL;
    }
    
    
    uint32_t* pixels = (uint32_t*)kmalloc(width * height * sizeof(uint32_t));
    if (!pixels) { if (idat_buffer) kfree(idat_buffer); return NULL; }
    
    
    int bytes_per_pixel;
    switch (color_type) {
        case 2: bytes_per_pixel = 3; break; 
        case 6: bytes_per_pixel = 4; break; 
        case 0: bytes_per_pixel = 1; break; 
        case 3: bytes_per_pixel = 1; break; 
        case 4: bytes_per_pixel = 2; break; 
        default: {
            kfree(pixels);
            return NULL;
        }
    }
    
    size_t row_size = width * bytes_per_pixel;
    size_t raw_size = row_size * height + height;
    
    uint8_t* raw = (uint8_t*)kmalloc(raw_size + 1024);
    if (!raw) {
        kfree(pixels);
        return NULL;
    }
    
    
    for (size_t i = 0; i < raw_size + 1024; i++) raw[i] = 0;
    
    
    bool success = inflate(idat_buffer, idat_len, raw, raw_size);
    
    
    if (!success) {
        size_t raw_pos = 0;
        size_t search_pos = 2;
        
        while (search_pos < idat_len - 4 && raw_pos < raw_size) {
            if ((idat_buffer[search_pos] & 0x0F) == 0) {
                int bfinal = idat_buffer[search_pos] & 1;
                search_pos++;
                
                while (search_pos < idat_len && (search_pos & 1)) search_pos++;
                
                if (search_pos + 4 > idat_len) break;
                
                uint16_t block_len = idat_buffer[search_pos] | (idat_buffer[search_pos + 1] << 8);
                uint16_t nlen = idat_buffer[search_pos + 2] | (idat_buffer[search_pos + 3] << 8);
                search_pos += 4;
                
                if (block_len == (uint16_t)~nlen && block_len > 0) {
                    for (int i = 0; i < block_len && raw_pos < raw_size; i++) {
                        raw[raw_pos++] = idat_buffer[search_pos + i];
                    }
                }
                search_pos += block_len;
                
                if (bfinal) break;
            } else {
                search_pos++;
            }
        }
        success = (raw_pos > 0);
    }
    
    
    for (uint32_t y = 0; y < height; y++) {
        uint8_t filter = raw[y * (row_size + 1)];
        uint8_t* row_data = raw + y * (row_size + 1) + 1;
        
        for (uint32_t x = 0; x < width; x++) {
            uint8_t r = 0, g = 0, b = 0, a = 0xFF;
            
            
            switch (color_type) {
                case 2: 
                    r = row_data[x * 3];
                    g = row_data[x * 3 + 1];
                    b = row_data[x * 3 + 2];
                    a = 0xFF;
                    break;
                case 6: 
                    r = row_data[x * 4];
                    g = row_data[x * 4 + 1];
                    b = row_data[x * 4 + 2];
                    a = row_data[x * 4 + 3];
                    break;
                case 0: 
                    r = g = b = row_data[x];
                    a = 0xFF;
                    break;
                case 3: 
                    if (has_palette) {
                        r = palette[row_data[x]][0];
                        g = palette[row_data[x]][1];
                        b = palette[row_data[x]][2];
                    }
                    a = 0xFF;
                    break;
                case 4: 
                    r = g = b = row_data[x * 2];
                    a = row_data[x * 2 + 1];
                    break;
            }
            
            
            uint32_t* out_row = pixels + y * width;
            
            if (filter == 1 && x > 0) { 
                uint32_t left = out_row[x - 1];
                r = (r + ((left >> 16) & 0xFF)) & 0xFF;
                g = (g + ((left >> 8) & 0xFF)) & 0xFF;
                b = (b + (left & 0xFF)) & 0xFF;
                a = (a + ((left >> 24) & 0xFF)) & 0xFF;
            } else if (filter == 2 && y > 0) { 
                uint32_t up = pixels[(y - 1) * width + x];
                r = (r + ((up >> 16) & 0xFF)) & 0xFF;
                g = (g + ((up >> 8) & 0xFF)) & 0xFF;
                b = (b + (up & 0xFF)) & 0xFF;
                a = (a + ((up >> 24) & 0xFF)) & 0xFF;
            } else if (filter == 3 && y > 0) { 
                uint32_t left = (x > 0) ? out_row[x - 1] : 0;
                uint32_t up = pixels[(y - 1) * width + x];
                r = ((r + ((left >> 16) & 0xFF) + ((up >> 16) & 0xFF)) / 2) & 0xFF;
                g = ((g + ((left >> 8) & 0xFF) + ((up >> 8) & 0xFF)) / 2) & 0xFF;
                b = ((b + (left & 0xFF) + (up & 0xFF)) / 2) & 0xFF;
                a = ((a + ((left >> 24) & 0xFF) + ((up >> 24) & 0xFF)) / 2) & 0xFF;
            } else if (filter == 4 && y > 0) { 
                uint32_t left = (x > 0) ? out_row[x - 1] : 0;
                uint32_t up = pixels[(y - 1) * width + x];
                uint32_t up_left = (x > 0 && y > 0) ? pixels[(y - 1) * width + x - 1] : 0;
                r = (r + paeth_predictor((uint8_t)(left & 0xFF), (uint8_t)(up & 0xFF), (uint8_t)(up_left & 0xFF))) & 0xFF;
                g = (g + paeth_predictor((uint8_t)((left >> 8) & 0xFF), (uint8_t)((up >> 8) & 0xFF), (uint8_t)((up_left >> 8) & 0xFF))) & 0xFF;
                b = (b + paeth_predictor((uint8_t)((left >> 16) & 0xFF), (uint8_t)((up >> 16) & 0xFF), (uint8_t)((up_left >> 16) & 0xFF))) & 0xFF;
                a = (a + paeth_predictor((uint8_t)((left >> 24) & 0xFF), (uint8_t)((up >> 24) & 0xFF), (uint8_t)((up_left >> 24) & 0xFF))) & 0xFF;
            }
            
            out_row[x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    
    kfree(raw);
    if (idat_buffer) kfree(idat_buffer);
    
    *out_w = (int)width;
    *out_h = (int)height;
    return pixels;
}