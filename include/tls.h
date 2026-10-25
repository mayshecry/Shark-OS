#ifndef TLS_H
#define TLS_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t h[8];
    uint8_t  buf[64];
    uint32_t buf_len;
    uint64_t total;
} sha256_ctx_t;

void sha256_init(sha256_ctx_t* c);
void sha256_update(sha256_ctx_t* c, const uint8_t* data, size_t len);
void sha256_final(sha256_ctx_t* c, uint8_t out[32]);
void sha256(const uint8_t* data, size_t len, uint8_t out[32]);
void hmac_sha256(const uint8_t* key, size_t key_len, const uint8_t* data, size_t len, uint8_t out[32]);
void hkdf_extract(const uint8_t* salt, size_t salt_len, const uint8_t* ikm, size_t ikm_len, uint8_t out[32]);
void hkdf_expand(const uint8_t prk[32], const uint8_t* info, size_t info_len, uint8_t* out, size_t out_len);

typedef struct { uint8_t rk[176]; } aes128_t;
void aes128_init(aes128_t* a, const uint8_t key[16]);
void aes128_encrypt_block(const aes128_t* a, const uint8_t in[16], uint8_t out[16]);

typedef struct {
    aes128_t aes;
    uint64_t hh[16], hl[16];
} gcm_t;
void gcm_init(gcm_t* g, const uint8_t key[16]);

int gcm_crypt(const gcm_t* g, const uint8_t nonce[12], const uint8_t* aad, size_t aad_len,
              const uint8_t* in, uint8_t* out, size_t len, uint8_t tag[16], int encrypt);

void x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]);
void x25519_base(uint8_t out[32], const uint8_t scalar[32]);

void tls_random_seed(const uint8_t* data, size_t len);
void tls_random(uint8_t* out, size_t len);

typedef int (*tls_send_fn)(const uint8_t* data, int len);
typedef int (*tls_recv_fn)(uint8_t* out, int max, int timeout_ms);

#define TLS_MAX_RECORD   (16384 + 256 + 5)
#define TLS_HS_BUF       (8 * 1024)

typedef struct {
    tls_send_fn send;
    tls_recv_fn recv;

    sha256_ctx_t transcript;

    uint8_t handshake_secret[32];
    uint8_t client_hs_secret[32], server_hs_secret[32];
    uint8_t client_app_secret[32], server_app_secret[32];
    gcm_t wkey, rkey;
    uint8_t wiv[12], riv[12];
    uint64_t wseq, rseq;
    int encrypted;

    uint8_t rec[TLS_MAX_RECORD];
    int rec_len;
    int rec_used;
    int rec_plain_len;
    uint8_t rec_type;

    uint8_t hs[TLS_HS_BUF];
    int hs_len;

    int handshake_done;
    int peer_closed;
    int error;
    char server_name[128];
    char peer_cn[64];
    int cert_verified;
    uint16_t cipher;
    int alert_received;
} tls_t;

enum {
    TLS_ERR_NONE = 0,
    TLS_ERR_TRANSPORT,
    TLS_ERR_VERSION,
    TLS_ERR_CIPHER,
    TLS_ERR_KEYSHARE,
    TLS_ERR_DECRYPT,
    TLS_ERR_FINISHED,
    TLS_ERR_ALERT,
    TLS_ERR_PROTOCOL,
    TLS_ERR_HRR,
    TLS_ERR_TOOBIG
};

int tls_connect(tls_t* t, const char* server_name, tls_send_fn send, tls_recv_fn recv);

int tls_write(tls_t* t, const uint8_t* data, int len);
int tls_read(tls_t* t, uint8_t* out, int max, int timeout_ms);
void tls_close(tls_t* t);
const char* tls_error_string(int err);

#endif
