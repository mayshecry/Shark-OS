/* Cryptographic primitives for SharkOS TLS: SHA-256, HMAC, HKDF, AES-128,
 * AES-GCM, X25519 and a hash-based DRBG.
 *
 * Plain portable integer C (no libc apart from memcpy/memset, no floating
 * point, no 64-bit division). Compiled both into the kernel and into the host
 * test harness (tools/tlstest.c), which checks every primitive against the
 * published test vectors:
 *   SHA-256 / HMAC   FIPS 180-4, RFC 4231
 *   HKDF             RFC 5869
 *   AES-128          FIPS 197 appendix C
 *   AES-GCM          NIST GCM spec test cases 2-4
 *   X25519           RFC 7748 section 5.2 / 6.1
 *
 * The field arithmetic for X25519 follows TweetNaCl (public domain): 16
 * limbs of 16 bits in int64, which needs nothing but 32x32->64 multiplies. */

#include "tls.h"

#ifdef TLS_HOST_TEST
#include <string.h>
#else
#include "kernel.h"
#endif

/* ----------------------------------------------------------- SHA-256 */

static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static void put_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static void put_be64(uint8_t* p, uint64_t v) {
    put_be32(p, (uint32_t)(v >> 32)); put_be32(p + 4, (uint32_t)v);
}

static void sha256_block(uint32_t h[8], const uint8_t p[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) w[i] = be32(p + 4 * i);
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR32(w[i - 15], 7) ^ ROR32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR32(w[i - 2], 17) ^ ROR32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = ROR32(e, 6) ^ ROR32(e, 11) ^ ROR32(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = hh + S1 + ch + K256[i] + w[i];
        uint32_t S0 = ROR32(a, 2) ^ ROR32(a, 13) ^ ROR32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

void sha256_init(sha256_ctx_t* c) {
    c->h[0] = 0x6a09e667; c->h[1] = 0xbb67ae85; c->h[2] = 0x3c6ef372; c->h[3] = 0xa54ff53a;
    c->h[4] = 0x510e527f; c->h[5] = 0x9b05688c; c->h[6] = 0x1f83d9ab; c->h[7] = 0x5be0cd19;
    c->buf_len = 0;
    c->total = 0;
}

void sha256_update(sha256_ctx_t* c, const uint8_t* data, size_t len) {
    c->total += len;
    while (len > 0) {
        uint32_t take = 64 - c->buf_len;
        if (take > len) take = (uint32_t)len;
        memcpy(c->buf + c->buf_len, data, take);
        c->buf_len += take; data += take; len -= take;
        if (c->buf_len == 64) { sha256_block(c->h, c->buf); c->buf_len = 0; }
    }
}

void sha256_final(sha256_ctx_t* c, uint8_t out[32]) {
    uint64_t bits = c->total * 8;
    uint8_t pad = 0x80;
    sha256_update(c, &pad, 1);
    uint8_t zero = 0;
    while (c->buf_len != 56) sha256_update(c, &zero, 1);
    uint8_t lenb[8];
    put_be64(lenb, bits);
    sha256_update(c, lenb, 8);
    for (int i = 0; i < 8; i++) put_be32(out + 4 * i, c->h[i]);
}

void sha256(const uint8_t* data, size_t len, uint8_t out[32]) {
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, data, len);
    sha256_final(&c, out);
}

/* -------------------------------------------------------- HMAC / HKDF */

void hmac_sha256(const uint8_t* key, size_t key_len, const uint8_t* data, size_t len, uint8_t out[32]) {
    uint8_t k[64], pad[64], inner[32];
    memset(k, 0, 64);
    if (key_len > 64) sha256(key, key_len, k);
    else memcpy(k, key, key_len);
    sha256_ctx_t c;
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
    sha256_init(&c); sha256_update(&c, pad, 64); sha256_update(&c, data, len); sha256_final(&c, inner);
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5c;
    sha256_init(&c); sha256_update(&c, pad, 64); sha256_update(&c, inner, 32); sha256_final(&c, out);
}

void hkdf_extract(const uint8_t* salt, size_t salt_len, const uint8_t* ikm, size_t ikm_len, uint8_t out[32]) {
    static const uint8_t zero_salt[32] = { 0 };
    if (!salt || salt_len == 0) { salt = zero_salt; salt_len = 32; }
    hmac_sha256(salt, salt_len, ikm, ikm_len, out);
}

void hkdf_expand(const uint8_t prk[32], const uint8_t* info, size_t info_len, uint8_t* out, size_t out_len) {
    uint8_t t[32], msg[32 + 256 + 1];
    size_t tlen = 0, done = 0;
    uint8_t counter = 1;
    if (info_len > 256) info_len = 256;
    while (done < out_len) {
        size_t o = 0;
        memcpy(msg, t, tlen); o += tlen;
        memcpy(msg + o, info, info_len); o += info_len;
        msg[o++] = counter++;
        hmac_sha256(prk, 32, msg, o, t);
        tlen = 32;
        size_t take = out_len - done < 32 ? out_len - done : 32;
        memcpy(out + done, t, take);
        done += take;
    }
}

/* ------------------------------------------------------------ AES-128 */

static uint8_t sbox[256];
static int sbox_ready = 0;

#define ROTL8(x, s) ((uint8_t)(((x) << (s)) | ((x) >> (8 - (s)))))

/* Generate the S-box from the GF(2^8) inverse + affine map instead of typing
 * 256 constants. Verified by the FIPS-197 test vector in the harness. */
static void aes_gen_sbox(void) {
    uint8_t p = 1, q = 1;
    do {
        p = (uint8_t)(p ^ (p << 1) ^ ((p & 0x80) ? 0x1B : 0));       /* p *= 3 */
        q ^= (uint8_t)(q << 1); q ^= (uint8_t)(q << 2); q ^= (uint8_t)(q << 4);
        if (q & 0x80) q ^= 0x09;                                       /* q /= 3 */
        uint8_t x = (uint8_t)(q ^ ROTL8(q, 1) ^ ROTL8(q, 2) ^ ROTL8(q, 3) ^ ROTL8(q, 4));
        sbox[p] = (uint8_t)(x ^ 0x63);
    } while (p != 1);
    sbox[0] = 0x63;
    sbox_ready = 1;
}

void aes128_init(aes128_t* a, const uint8_t key[16]) {
    if (!sbox_ready) aes_gen_sbox();
    uint8_t* rk = a->rk;
    memcpy(rk, key, 16);
    uint8_t rcon = 1;
    for (int i = 16; i < 176; i += 4) {
        uint8_t t0 = rk[i - 4], t1 = rk[i - 3], t2 = rk[i - 2], t3 = rk[i - 1];
        if ((i % 16) == 0) {
            uint8_t u = t0;                                            /* RotWord + SubWord + Rcon */
            t0 = (uint8_t)(sbox[t1] ^ rcon); t1 = sbox[t2]; t2 = sbox[t3]; t3 = sbox[u];
            rcon = (uint8_t)((rcon << 1) ^ ((rcon & 0x80) ? 0x1B : 0));
        }
        rk[i] = (uint8_t)(rk[i - 16] ^ t0); rk[i + 1] = (uint8_t)(rk[i - 15] ^ t1);
        rk[i + 2] = (uint8_t)(rk[i - 14] ^ t2); rk[i + 3] = (uint8_t)(rk[i - 13] ^ t3);
    }
}

static inline uint8_t xtime(uint8_t x) { return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1B : 0)); }

void aes128_encrypt_block(const aes128_t* a, const uint8_t in[16], uint8_t out[16]) {
    uint8_t s[16], t[16];
    const uint8_t* rk = a->rk;
    for (int i = 0; i < 16; i++) s[i] = in[i] ^ rk[i];
    for (int round = 1; round <= 10; round++) {
        rk += 16;
        /* SubBytes + ShiftRows: byte at column c, row r comes from column (c+r)%4 */
        for (int c = 0; c < 4; c++)
            for (int r = 0; r < 4; r++)
                t[c * 4 + r] = sbox[s[((c + r) & 3) * 4 + r]];
        if (round != 10) {
            for (int c = 0; c < 4; c++) {                              /* MixColumns */
                uint8_t a0 = t[c * 4], a1 = t[c * 4 + 1], a2 = t[c * 4 + 2], a3 = t[c * 4 + 3];
                uint8_t all = (uint8_t)(a0 ^ a1 ^ a2 ^ a3);
                s[c * 4]     = (uint8_t)(a0 ^ all ^ xtime((uint8_t)(a0 ^ a1)));
                s[c * 4 + 1] = (uint8_t)(a1 ^ all ^ xtime((uint8_t)(a1 ^ a2)));
                s[c * 4 + 2] = (uint8_t)(a2 ^ all ^ xtime((uint8_t)(a2 ^ a3)));
                s[c * 4 + 3] = (uint8_t)(a3 ^ all ^ xtime((uint8_t)(a3 ^ a0)));
            }
        } else {
            memcpy(s, t, 16);
        }
        for (int i = 0; i < 16; i++) s[i] ^= rk[i];
    }
    memcpy(out, s, 16);
}

/* ---------------------------------------------------------------- GCM */

/* Shoup's 4-bit table method (as in mbed TLS / Cifra). */
static const uint64_t gcm_last4[16] = {
    0x0000, 0x1c20, 0x3840, 0x2460, 0x7080, 0x6ca0, 0x48c0, 0x54e0,
    0xe100, 0xfd20, 0xd940, 0xc560, 0x9180, 0x8da0, 0xa9c0, 0xb5e0
};

void gcm_init(gcm_t* g, const uint8_t key[16]) {
    uint8_t h[16] = { 0 };
    aes128_init(&g->aes, key);
    aes128_encrypt_block(&g->aes, h, h);
    uint64_t vh = ((uint64_t)be32(h) << 32) | be32(h + 4);
    uint64_t vl = ((uint64_t)be32(h + 8) << 32) | be32(h + 12);
    g->hl[8] = vl; g->hh[8] = vh;
    g->hh[0] = 0; g->hl[0] = 0;
    for (int i = 4; i > 0; i >>= 1) {
        uint32_t T = (uint32_t)(vl & 1) * 0xe1000000u;
        vl = (vh << 63) | (vl >> 1);
        vh = (vh >> 1) ^ ((uint64_t)T << 32);
        g->hl[i] = vl; g->hh[i] = vh;
    }
    for (int i = 2; i <= 8; i *= 2) {
        uint64_t* hil = g->hl + i; uint64_t* hih = g->hh + i;
        vh = *hih; vl = *hil;
        for (int j = 1; j < i; j++) { hih[j] = vh ^ g->hh[j]; hil[j] = vl ^ g->hl[j]; }
    }
}

static void gcm_mult(const gcm_t* g, uint8_t x[16]) {
    uint8_t lo = x[15] & 0xf, hi, rem;
    uint64_t zh = g->hh[lo], zl = g->hl[lo];
    for (int i = 15; i >= 0; i--) {
        lo = x[i] & 0xf;
        hi = (x[i] >> 4) & 0xf;
        if (i != 15) {
            rem = (uint8_t)zl & 0xf;
            zl = (zh << 60) | (zl >> 4);
            zh = (zh >> 4);
            zh ^= gcm_last4[rem] << 48;
            zh ^= g->hh[lo]; zl ^= g->hl[lo];
        }
        rem = (uint8_t)zl & 0xf;
        zl = (zh << 60) | (zl >> 4);
        zh = (zh >> 4);
        zh ^= gcm_last4[rem] << 48;
        zh ^= g->hh[hi]; zl ^= g->hl[hi];
    }
    put_be32(x, (uint32_t)(zh >> 32)); put_be32(x + 4, (uint32_t)zh);
    put_be32(x + 8, (uint32_t)(zl >> 32)); put_be32(x + 12, (uint32_t)zl);
}

static void ghash_update(const gcm_t* g, uint8_t y[16], const uint8_t* data, size_t len) {
    while (len > 0) {
        size_t n = len < 16 ? len : 16;
        for (size_t i = 0; i < n; i++) y[i] ^= data[i];
        gcm_mult(g, y);
        data += n; len -= n;
    }
}

int gcm_crypt(const gcm_t* g, const uint8_t nonce[12], const uint8_t* aad, size_t aad_len,
              const uint8_t* in, uint8_t* out, size_t len, uint8_t tag[16], int encrypt) {
    uint8_t j0[16], ctr[16], ks[16], y[16], lens[16], t[16];
    memcpy(j0, nonce, 12); j0[12] = 0; j0[13] = 0; j0[14] = 0; j0[15] = 1;
    memset(y, 0, 16);
    ghash_update(g, y, aad, aad_len);
    if (!encrypt) ghash_update(g, y, in, len);           /* hash the ciphertext before it is overwritten */
    memcpy(ctr, j0, 16);
    size_t off = 0;
    while (off < len) {
        /* inc32 */
        for (int i = 15; i >= 12; i--) { if (++ctr[i]) break; }
        aes128_encrypt_block(&g->aes, ctr, ks);
        size_t n = len - off < 16 ? len - off : 16;
        for (size_t i = 0; i < n; i++) out[off + i] = in[off + i] ^ ks[i];
        off += n;
    }
    if (encrypt) ghash_update(g, y, out, len);
    put_be64(lens, (uint64_t)aad_len * 8);
    put_be64(lens + 8, (uint64_t)len * 8);
    ghash_update(g, y, lens, 16);
    aes128_encrypt_block(&g->aes, j0, t);
    for (int i = 0; i < 16; i++) t[i] ^= y[i];
    if (encrypt) { memcpy(tag, t, 16); return 1; }
    uint8_t diff = 0;
    for (int i = 0; i < 16; i++) diff |= (uint8_t)(t[i] ^ tag[i]);
    return diff == 0;
}

/* ------------------------------------------------------------- X25519 */

typedef int64_t gf[16];
static const gf gf_121665 = { 0xDB41, 1 };

static void car25519(gf o) {
    for (int i = 0; i < 16; i++) {
        o[i] += (1LL << 16);
        int64_t c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c << 16;
    }
}

static void sel25519(gf p, gf q, int b) {
    int64_t c = ~(int64_t)(b - 1);
    for (int i = 0; i < 16; i++) {
        int64_t t = c & (p[i] ^ q[i]);
        p[i] ^= t; q[i] ^= t;
    }
}

static void pack25519(uint8_t* o, const gf n) {
    gf m, t;
    for (int i = 0; i < 16; i++) t[i] = n[i];
    car25519(t); car25519(t); car25519(t);
    for (int j = 0; j < 2; j++) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; i++) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        int b = (int)((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        sel25519(t, m, 1 - b);
    }
    for (int i = 0; i < 16; i++) { o[2 * i] = (uint8_t)(t[i] & 0xff); o[2 * i + 1] = (uint8_t)(t[i] >> 8); }
}

static void unpack25519(gf o, const uint8_t* n) {
    for (int i = 0; i < 16; i++) o[i] = n[2 * i] + ((int64_t)n[2 * i + 1] << 8);
    o[15] &= 0x7fff;
}

static void gf_add(gf o, const gf a, const gf b) { for (int i = 0; i < 16; i++) o[i] = a[i] + b[i]; }
static void gf_sub(gf o, const gf a, const gf b) { for (int i = 0; i < 16; i++) o[i] = a[i] - b[i]; }

static void gf_mul(gf o, const gf a, const gf b) {
    int64_t t[31];
    for (int i = 0; i < 31; i++) t[i] = 0;
    for (int i = 0; i < 16; i++) for (int j = 0; j < 16; j++) t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; i++) t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; i++) o[i] = t[i];
    car25519(o); car25519(o);
}

static void gf_sq(gf o, const gf a) { gf_mul(o, a, a); }

static void gf_inv(gf o, const gf i) {
    gf c;
    for (int a = 0; a < 16; a++) c[a] = i[a];
    for (int a = 253; a >= 0; a--) {
        gf_sq(c, c);
        if (a != 2 && a != 4) gf_mul(c, c, i);
    }
    for (int a = 0; a < 16; a++) o[a] = c[a];
}

void x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]) {
    uint8_t z[32];
    int64_t x[80];
    gf a, b, c, d, e, f;
    for (int i = 0; i < 31; i++) z[i] = scalar[i];
    z[31] = (uint8_t)((scalar[31] & 127) | 64);
    z[0] &= 248;
    unpack25519(x, point);
    for (int i = 0; i < 16; i++) { b[i] = x[i]; d[i] = a[i] = c[i] = 0; }
    a[0] = d[0] = 1;
    for (int i = 254; i >= 0; --i) {
        int r = (z[i >> 3] >> (i & 7)) & 1;
        sel25519(a, b, r);
        sel25519(c, d, r);
        gf_add(e, a, c);
        gf_sub(a, a, c);
        gf_add(c, b, d);
        gf_sub(b, b, d);
        gf_sq(d, e);
        gf_sq(f, a);
        gf_mul(a, c, a);
        gf_mul(c, b, e);
        gf_add(e, a, c);
        gf_sub(a, a, c);
        gf_sq(b, a);
        gf_sub(c, d, f);
        gf_mul(a, c, gf_121665);
        gf_add(a, a, d);
        gf_mul(c, c, a);
        gf_mul(a, d, f);
        gf_mul(d, b, x);
        gf_sq(b, e);
        sel25519(a, b, r);
        sel25519(c, d, r);
    }
    for (int i = 0; i < 16; i++) { x[i + 16] = a[i]; x[i + 32] = c[i]; x[i + 48] = b[i]; x[i + 64] = d[i]; }
    gf_inv(x + 32, x + 32);
    gf_mul(x + 16, x + 16, x + 32);
    pack25519(out, x + 16);
}

void x25519_base(uint8_t out[32], const uint8_t scalar[32]) {
    static const uint8_t nine[32] = { 9 };
    x25519(out, scalar, nine);
}

/* --------------------------------------------------------------- DRBG */

static uint8_t drbg_state[32];
static uint32_t drbg_counter = 0;

void tls_random_seed(const uint8_t* data, size_t len) {
    sha256_ctx_t c;
    uint8_t ctr[4] = { (uint8_t)drbg_counter, (uint8_t)(drbg_counter >> 8), (uint8_t)(drbg_counter >> 16), (uint8_t)(drbg_counter >> 24) };
    drbg_counter++;
    sha256_init(&c);
    sha256_update(&c, drbg_state, 32);
    sha256_update(&c, (const uint8_t*)"seed", 4);
    sha256_update(&c, ctr, 4);
    sha256_update(&c, data, len);
    sha256_final(&c, drbg_state);
}

void tls_random(uint8_t* out, size_t len) {
    while (len > 0) {
        sha256_ctx_t c;
        uint8_t block[32];
        uint8_t ctr[4] = { (uint8_t)drbg_counter, (uint8_t)(drbg_counter >> 8), (uint8_t)(drbg_counter >> 16), (uint8_t)(drbg_counter >> 24) };
        drbg_counter++;
        sha256_init(&c);
        sha256_update(&c, drbg_state, 32);
        sha256_update(&c, (const uint8_t*)"out", 3);
        sha256_update(&c, ctr, 4);
        sha256_final(&c, block);
        size_t n = len < 32 ? len : 32;
        memcpy(out, block, n);
        out += n; len -= n;
        /* ratchet so earlier outputs cannot be recovered from the state */
        sha256_init(&c);
        sha256_update(&c, drbg_state, 32);
        sha256_update(&c, (const uint8_t*)"next", 4);
        sha256_final(&c, drbg_state);
    }
}
