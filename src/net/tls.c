/* TLS 1.3 client for SharkOS (RFC 8446).
 *
 * Scope: exactly one cipher suite, TLS_AES_128_GCM_SHA256, with X25519 key
 * exchange - the mandatory-to-implement combination that every TLS 1.3
 * server offers. Enough for https:// pages in the browser.
 *
 * Supported:  ClientHello with SNI / supported_versions / key_share /
 *             signature_algorithms; ServerHello; EncryptedExtensions;
 *             Certificate (parsed for the subject CN, shown in the status
 *             bar); CertificateVerify (skipped, see below); server Finished
 *             (verified with HMAC); client Finished; application data in
 *             both directions; record padding; NewSessionTicket / KeyUpdate
 *             handling; close_notify and fatal alerts.
 * Not done:   certificate chain validation (no root store, no RSA/ECDSA
 *             verification), HelloRetryRequest, 0-RTT, resumption, TLS 1.2.
 *             So this gives you confidentiality and integrity against
 *             passive attackers, not authentication against an active
 *             man-in-the-middle. The browser says so in the status bar.
 *
 * Transport is abstracted through two callbacks, so the same file runs
 * against a real socket in tools/tlstest_net.c on the host. */

#include "tls.h"

#ifdef TLS_HOST_TEST
#include <string.h>
#include <stdio.h>
#define TLS_LOG(...) fprintf(stderr, __VA_ARGS__)
#else
#include "kernel.h"
#define TLS_LOG(...) do { } while (0)
#endif

/* ---------------------------------------------------------- helpers */

static int tls_memcmp(const void* a, const void* b, size_t n) {
    const uint8_t* x = a; const uint8_t* y = b; int d = 0;
    for (size_t i = 0; i < n; i++) d |= x[i] ^ y[i];   /* constant time */
    return d;
}
static void tls_memmove_down(uint8_t* dst, const uint8_t* src, size_t n) {   /* dst < src only */
    for (size_t i = 0; i < n; i++) dst[i] = src[i];
}

static void put16(uint8_t* p, uint32_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put24(uint8_t* p, uint32_t v) { p[0] = (uint8_t)(v >> 16); p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)v; }
static uint32_t get16(const uint8_t* p) { return ((uint32_t)p[0] << 8) | p[1]; }
static uint32_t get24(const uint8_t* p) { return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2]; }

static int send_all(tls_t* t, const uint8_t* data, int len) {
    while (len > 0) {
        int n = t->send(data, len);
        if (n <= 0) return -1;
        data += n; len -= n;
    }
    return 0;
}

/* Read exactly n bytes from the transport (blocking with an overall timeout). */
static int recv_exact(tls_t* t, uint8_t* out, int n, int timeout_ms) {
    int got = 0;
    int waited = 0;
    while (got < n) {
        int r = t->recv(out + got, n - got, 50);
        if (r < 0) return -1;
        if (r == 0) {
            waited += 50;
            if (waited >= timeout_ms) return got;
            continue;
        }
        waited = 0;
        got += r;
    }
    return got;
}

/* HKDF-Expand-Label(secret, label, context, len) */
static void expand_label(const uint8_t secret[32], const char* label, const uint8_t* ctx, int ctx_len, uint8_t* out, int out_len) {
    uint8_t info[2 + 1 + 6 + 32 + 1 + 64];
    int lab_len = (int)strlen(label);
    int o = 0;
    put16(info + o, (uint32_t)out_len); o += 2;
    info[o++] = (uint8_t)(6 + lab_len);
    memcpy(info + o, "tls13 ", 6); o += 6;
    memcpy(info + o, label, lab_len); o += lab_len;
    info[o++] = (uint8_t)ctx_len;
    if (ctx_len) memcpy(info + o, ctx, ctx_len);
    o += ctx_len;
    hkdf_expand(secret, info, (size_t)o, out, (size_t)out_len);
}

static void derive_secret(const uint8_t secret[32], const char* label, const uint8_t* transcript_hash, uint8_t out[32]) {
    expand_label(secret, label, transcript_hash, 32, out, 32);
}

static void transcript_hash(tls_t* t, uint8_t out[32]) {
    sha256_ctx_t c = t->transcript;       /* copy: the running hash continues */
    sha256_final(&c, out);
}

static void set_traffic_keys(tls_t* t, const uint8_t client_secret[32], const uint8_t server_secret[32]) {
    uint8_t key[16];
    expand_label(client_secret, "key", NULL, 0, key, 16);
    gcm_init(&t->wkey, key);
    expand_label(client_secret, "iv", NULL, 0, t->wiv, 12);
    expand_label(server_secret, "key", NULL, 0, key, 16);
    gcm_init(&t->rkey, key);
    expand_label(server_secret, "iv", NULL, 0, t->riv, 12);
    t->wseq = 0; t->rseq = 0;
}

static void make_nonce(const uint8_t iv[12], uint64_t seq, uint8_t nonce[12]) {
    memcpy(nonce, iv, 12);
    for (int i = 0; i < 8; i++) nonce[11 - i] ^= (uint8_t)(seq >> (8 * i));
}

/* ------------------------------------------------------------ records */

static uint8_t out_rec[TLS_MAX_RECORD];

/* Send one record. When encrypted, `type` becomes the inner content type. */
static int send_record(tls_t* t, uint8_t type, const uint8_t* data, int len) {
    if (len > 16384) return -1;
    if (!t->encrypted) {
        out_rec[0] = type; put16(out_rec + 1, 0x0303); put16(out_rec + 3, (uint32_t)len);
        memcpy(out_rec + 5, data, len);
        return send_all(t, out_rec, 5 + len);
    }
    static uint8_t inner[16384 + 1];
    memcpy(inner, data, len);
    inner[len] = type;
    int plen = len + 1;
    out_rec[0] = 23; put16(out_rec + 1, 0x0303); put16(out_rec + 3, (uint32_t)(plen + 16));
    uint8_t nonce[12];
    make_nonce(t->wiv, t->wseq, nonce);
    t->wseq++;
    gcm_crypt(&t->wkey, nonce, out_rec, 5, inner, out_rec + 5, (size_t)plen, out_rec + 5 + plen, 1);
    return send_all(t, out_rec, 5 + plen + 16);
}

static int send_alert(tls_t* t, uint8_t level, uint8_t desc) {
    uint8_t a[2] = { level, desc };
    return send_record(t, 21, a, 2);
}

/* Receive and (if needed) decrypt the next record into t->rec; sets
 * rec_type / rec_plain_len / rec_used. Returns 1, 0 on timeout, -1 on error. */
static int recv_record(tls_t* t, int timeout_ms) {
    int n = recv_exact(t, t->rec, 5, timeout_ms);
    if (n < 0) { t->peer_closed = 1; return -1; }
    if (n == 0) return 0;
    if (n < 5) { t->error = TLS_ERR_TRANSPORT; return -1; }
    uint8_t type = t->rec[0];
    int len = (int)get16(t->rec + 3);
    if (len > TLS_MAX_RECORD - 5) { t->error = TLS_ERR_TOOBIG; return -1; }
    n = recv_exact(t, t->rec + 5, len, timeout_ms > 5000 ? timeout_ms : 5000);
    if (n != len) { t->error = TLS_ERR_TRANSPORT; return -1; }
    t->rec_len = 5 + len;
    t->rec_used = 0;
    if (type == 20) {                       /* change_cipher_spec: middlebox compat, ignore */
        t->rec_plain_len = 0; t->rec_type = 20;
        return 1;
    }
    if (!t->encrypted || type != 23) {
        t->rec_type = type;
        t->rec_plain_len = len;
        tls_memmove_down(t->rec, t->rec + 5, (size_t)len);
        return 1;
    }
    if (len < 16) { t->error = TLS_ERR_DECRYPT; return -1; }
    uint8_t nonce[12];
    make_nonce(t->riv, t->rseq, nonce);
    t->rseq++;
    uint8_t hdr[5];
    memcpy(hdr, t->rec, 5);
    int plen = len - 16;
    uint8_t tag[16];
    memcpy(tag, t->rec + 5 + plen, 16);
    if (!gcm_crypt(&t->rkey, nonce, hdr, 5, t->rec + 5, t->rec, (size_t)plen, tag, 0)) {
        t->error = TLS_ERR_DECRYPT;
        return -1;
    }
    /* strip padding: content type is the last non-zero byte */
    while (plen > 0 && t->rec[plen - 1] == 0) plen--;
    if (plen == 0) { t->error = TLS_ERR_PROTOCOL; return -1; }
    t->rec_type = t->rec[plen - 1];
    t->rec_plain_len = plen - 1;
    return 1;
}

/* ---------------------------------------------------- handshake build */

static int build_client_hello(tls_t* t, uint8_t* out, const uint8_t pub[32], const uint8_t random[32]) {
    uint8_t* p = out;
    *p++ = 1;                                   /* handshake type: client_hello */
    uint8_t* len3 = p; p += 3;
    put16(p, 0x0303); p += 2;                   /* legacy_version */
    memcpy(p, random, 32); p += 32;
    *p++ = 32;                                  /* legacy_session_id: 32 random bytes (middlebox compat) */
    tls_random(p, 32); p += 32;
    put16(p, 2); p += 2;                        /* cipher_suites */
    put16(p, 0x1301); p += 2;                   /* TLS_AES_128_GCM_SHA256 */
    *p++ = 1; *p++ = 0;                         /* compression: null */
    uint8_t* ext_len = p; p += 2;
    /* server_name */
    int sn = (int)strlen(t->server_name);
    if (sn > 0) {
        put16(p, 0); p += 2;
        put16(p, (uint32_t)(sn + 5)); p += 2;
        put16(p, (uint32_t)(sn + 3)); p += 2;
        *p++ = 0;
        put16(p, (uint32_t)sn); p += 2;
        memcpy(p, t->server_name, sn); p += sn;
    }
    /* supported_versions: 1.3 only */
    put16(p, 43); p += 2; put16(p, 3); p += 2; *p++ = 2; put16(p, 0x0304); p += 2;
    /* supported_groups: x25519 */
    put16(p, 10); p += 2; put16(p, 4); p += 2; put16(p, 2); p += 2; put16(p, 0x001d); p += 2;
    /* signature_algorithms: we accept anything the server likes to sign with
     * (we do not verify), list the common ones so servers are happy. */
    static const uint16_t sigalgs[] = { 0x0403, 0x0804, 0x0401, 0x0503, 0x0805, 0x0501, 0x0806, 0x0601, 0x0807, 0x0808 };
    int ns = (int)(sizeof(sigalgs) / sizeof(sigalgs[0]));
    put16(p, 13); p += 2; put16(p, (uint32_t)(2 + 2 * ns)); p += 2; put16(p, (uint32_t)(2 * ns)); p += 2;
    for (int i = 0; i < ns; i++) { put16(p, sigalgs[i]); p += 2; }
    /* key_share: x25519 */
    put16(p, 51); p += 2; put16(p, 38); p += 2; put16(p, 36); p += 2;
    put16(p, 0x001d); p += 2; put16(p, 32); p += 2; memcpy(p, pub, 32); p += 32;
    /* psk_key_exchange_modes: psk_dhe_ke (required by some servers when tickets are sent) */
    put16(p, 45); p += 2; put16(p, 2); p += 2; *p++ = 1; *p++ = 1;
    put16(ext_len, (uint32_t)(p - ext_len - 2));
    put24(len3, (uint32_t)(p - len3 - 3));
    return (int)(p - out);
}

/* ------------------------------------------------- handshake parsing */

/* Extract the subject CN from a DER certificate (best effort, display only).
 * Walks TBSCertificate: version[0]? serial, sigalg, issuer, validity, subject.
 * Inside subject: SET { SEQUENCE { OID 2.5.4.3, string } } */
static int der_len(const uint8_t* p, int avail, int* hdr) {
    if (avail < 2) return -1;
    int l = p[1];
    if (l < 0x80) { *hdr = 2; return l; }
    int nb = l & 0x7f;
    if (nb < 1 || nb > 3 || avail < 2 + nb) return -1;
    l = 0;
    for (int i = 0; i < nb; i++) l = (l << 8) | p[2 + i];
    *hdr = 2 + nb;
    return l;
}

static void parse_cert_cn(const uint8_t* cert, int len, char* out, int out_max) {
    out[0] = 0;
    int h, l;
    const uint8_t* p = cert; int avail = len;
    if (*p != 0x30) return;
    l = der_len(p, avail, &h); if (l < 0) return; p += h; avail = l;            /* Certificate */
    if (*p != 0x30) return;
    l = der_len(p, avail, &h); if (l < 0) return; p += h; avail = l;            /* TBSCertificate */
    if (*p == 0xa0) { l = der_len(p, avail, &h); if (l < 0) return; p += h + l; avail -= h + l; }   /* version */
    for (int field = 0; field < 5; field++) {                                    /* serial, sigalg, issuer, validity -> subject */
        l = der_len(p, avail, &h); if (l < 0) return;
        if (field == 4) {
            /* subject: SEQUENCE OF SET OF SEQUENCE { OID, value } */
            const uint8_t* s = p + h; int sl = l;
            while (sl > 0) {
                int h2, l2 = der_len(s, sl, &h2); if (l2 < 0) return;
                const uint8_t* q = s + h2; int ql = l2;
                if (*q == 0x30) {
                    int h3, l3 = der_len(q, ql, &h3); if (l3 < 0) return;
                    const uint8_t* r = q + h3;
                    if (r[0] == 0x06 && r[1] == 3 && r[2] == 0x55 && r[3] == 0x04 && r[4] == 0x03) {
                        const uint8_t* v = r + 5; int h4, l4 = der_len(v, l3 - 5, &h4);
                        if (l4 < 0) return;
                        if (l4 > out_max - 1) l4 = out_max - 1;
                        memcpy(out, v + h4, l4); out[l4] = 0;
                        return;
                    }
                }
                s += h2 + l2; sl -= h2 + l2;
            }
            return;
        }
        p += h + l; avail -= h + l;
    }
}

/* Pull the next complete handshake message out of incoming records into
 * t->hs. Returns message type, or -1 on error. */
static int next_handshake_message(tls_t* t, int* msg_len) {
    int need = -1;
    t->hs_len = 0;
    for (;;) {
        if (t->hs_len >= 4) {
            need = 4 + (int)get24(t->hs + 1);
            if (need > TLS_HS_BUF) { t->error = TLS_ERR_TOOBIG; return -1; }
            if (t->hs_len >= need) break;
        }
        if (t->rec_used >= t->rec_plain_len) {
            int r = recv_record(t, 10000);
            if (r <= 0) { if (!t->error) t->error = TLS_ERR_TRANSPORT; return -1; }
            if (t->rec_type == 20) continue;
            if (t->rec_type == 21) {
                t->alert_received = t->rec[1];
                t->error = TLS_ERR_ALERT;
                TLS_LOG("tls: alert %d\n", t->rec[1]);
                return -1;
            }
            if (t->rec_type != 22) { t->error = TLS_ERR_PROTOCOL; return -1; }
        }
        int avail = t->rec_plain_len - t->rec_used;
        int want = need > 0 ? need - t->hs_len : (4 - t->hs_len);
        if (need < 0 && t->hs_len + avail >= 4) {
            /* take header first to learn the length, then as much body as present */
            want = 4 - t->hs_len;
            memcpy(t->hs + t->hs_len, t->rec + t->rec_used, want);
            t->hs_len += want; t->rec_used += want;
            continue;
        }
        if (want > avail) want = avail;
        if (t->hs_len + want > TLS_HS_BUF) { t->error = TLS_ERR_TOOBIG; return -1; }
        memcpy(t->hs + t->hs_len, t->rec + t->rec_used, want);
        t->hs_len += want; t->rec_used += want;
    }
    *msg_len = need;
    return t->hs[0];
}

static int parse_server_hello(tls_t* t, const uint8_t* m, int len, uint8_t server_pub[32]) {
    /* m points at the body (after 4-byte header) */
    static const uint8_t hrr_random[32] = { 0xCF, 0x21, 0xAD, 0x74, 0xE5, 0x9A, 0x61, 0x11, 0xBE, 0x1D, 0x8C, 0x02, 0x1E, 0x65, 0xB8, 0x91,
                                            0xC2, 0xA2, 0x11, 0x16, 0x7A, 0xBB, 0x8C, 0x5E, 0x07, 0x9E, 0x09, 0xE2, 0xC8, 0xA8, 0x33, 0x9C };
    if (len < 38) return TLS_ERR_PROTOCOL;
    if (tls_memcmp(m + 2, hrr_random, 32) == 0) return TLS_ERR_HRR;
    int p = 34;
    int sid = m[p++]; p += sid;
    if (p + 3 > len) return TLS_ERR_PROTOCOL;
    t->cipher = (uint16_t)get16(m + p); p += 2;
    if (t->cipher != 0x1301) return TLS_ERR_CIPHER;
    p++;                                                /* compression */
    if (p + 2 > len) return TLS_ERR_PROTOCOL;
    int ext_len = (int)get16(m + p); p += 2;
    int end = p + ext_len;
    if (end > len) return TLS_ERR_PROTOCOL;
    int got_version = 0, got_share = 0;
    while (p + 4 <= end) {
        int type = (int)get16(m + p), el = (int)get16(m + p + 2);
        p += 4;
        if (p + el > end) return TLS_ERR_PROTOCOL;
        if (type == 43 && el == 2 && get16(m + p) == 0x0304) got_version = 1;
        if (type == 51 && el >= 4) {
            int group = (int)get16(m + p), kl = (int)get16(m + p + 2);
            if (group == 0x001d && kl == 32 && el >= 36) { memcpy(server_pub, m + p + 4, 32); got_share = 1; }
        }
        p += el;
    }
    if (!got_version) return TLS_ERR_VERSION;
    if (!got_share) return TLS_ERR_KEYSHARE;
    return 0;
}

/* ---------------------------------------------------------- handshake */

int tls_connect(tls_t* t, const char* server_name, tls_send_fn send, tls_recv_fn recv) {
    memset(t, 0, sizeof(*t));
    t->send = send; t->recv = recv;
    {
        int i = 0;
        while (server_name && server_name[i] && i < (int)sizeof(t->server_name) - 1) { t->server_name[i] = server_name[i]; i++; }
        t->server_name[i] = 0;
    }
    sha256_init(&t->transcript);

    /* ephemeral key */
    uint8_t priv[32], pub[32], random[32], server_pub[32];
    tls_random(priv, 32);
    tls_random(random, 32);
    x25519_base(pub, priv);

    /* ClientHello */
    static uint8_t hello[1024];
    int hl = build_client_hello(t, hello, pub, random);
    sha256_update(&t->transcript, hello, (size_t)hl);
    if (send_record(t, 22, hello, hl) < 0) return t->error = TLS_ERR_TRANSPORT;

    /* ServerHello */
    int ml;
    int mt = next_handshake_message(t, &ml);
    if (mt < 0) return t->error ? t->error : TLS_ERR_TRANSPORT;
    if (mt != 2) return t->error = TLS_ERR_PROTOCOL;
    int err = parse_server_hello(t, t->hs + 4, ml - 4, server_pub);
    if (err) { send_alert(t, 2, err == TLS_ERR_CIPHER ? 40 : 70); return t->error = err; }
    sha256_update(&t->transcript, t->hs, (size_t)ml);

    /* key schedule up to handshake traffic keys */
    uint8_t shared[32], early_secret[32], derived[32], empty_hash[32], th[32];
    x25519(shared, priv, server_pub);
    {
        uint8_t zeros[32]; memset(zeros, 0, 32);
        hkdf_extract(NULL, 0, zeros, 32, early_secret);
        sha256((const uint8_t*)"", 0, empty_hash);
        derive_secret(early_secret, "derived", empty_hash, derived);
        hkdf_extract(derived, 32, shared, 32, t->handshake_secret);
    }
    transcript_hash(t, th);
    derive_secret(t->handshake_secret, "c hs traffic", th, t->client_hs_secret);
    derive_secret(t->handshake_secret, "s hs traffic", th, t->server_hs_secret);
    set_traffic_keys(t, t->client_hs_secret, t->server_hs_secret);
    t->encrypted = 1;
    t->rec_used = t->rec_plain_len;               /* anything after ServerHello in that record is invalid anyway */

    /* Encrypted server flight: EncryptedExtensions, Certificate, CertificateVerify, Finished */
    int got_finished = 0;
    while (!got_finished) {
        mt = next_handshake_message(t, &ml);
        if (mt < 0) return t->error ? t->error : TLS_ERR_TRANSPORT;
        if (mt == 20) {
            /* verify: HMAC(finished_key, transcript_hash(up to CertificateVerify)) */
            uint8_t fkey[32], expect[32];
            expand_label(t->server_hs_secret, "finished", NULL, 0, fkey, 32);
            transcript_hash(t, th);
            hmac_sha256(fkey, 32, th, 32, expect);
            if (ml - 4 != 32 || tls_memcmp(expect, t->hs + 4, 32) != 0) { send_alert(t, 2, 51); return t->error = TLS_ERR_FINISHED; }
            got_finished = 1;
        } else if (mt == 11) {
            /* Certificate: context<0..255>, list length 3, then entries {len3, cert, ext len2, ext} */
            const uint8_t* m = t->hs + 4; int len = ml - 4;
            if (len > 4) {
                int p = 1 + m[0];
                int list_len = (int)get24(m + p); p += 3;
                if (p + 3 <= len && list_len >= 3) {
                    int cl = (int)get24(m + p); p += 3;
                    if (p + cl <= len) parse_cert_cn(m + p, cl, t->peer_cn, sizeof(t->peer_cn));
                }
            }
        } else if (mt == 8 || mt == 15 || mt == 13) {
            /* EncryptedExtensions / CertificateVerify / CertificateRequest: nothing to do */
        } else {
            send_alert(t, 2, 10);
            return t->error = TLS_ERR_PROTOCOL;
        }
        sha256_update(&t->transcript, t->hs, (size_t)ml);
    }

    /* application traffic secrets (transcript now includes server Finished) */
    uint8_t master[32];
    transcript_hash(t, th);
    {
        uint8_t zeros[32]; memset(zeros, 0, 32);
        derive_secret(t->handshake_secret, "derived", empty_hash, derived);
        hkdf_extract(derived, 32, zeros, 32, master);
    }
    derive_secret(master, "c ap traffic", th, t->client_app_secret);
    derive_secret(master, "s ap traffic", th, t->server_app_secret);

    /* client Finished (still under handshake keys) */
    {
        uint8_t fkey[32], fin[4 + 32];
        expand_label(t->client_hs_secret, "finished", NULL, 0, fkey, 32);
        hmac_sha256(fkey, 32, th, 32, fin + 4);
        fin[0] = 20; put24(fin + 1, 32);
        /* legacy change_cipher_spec first for middleboxes */
        uint8_t ccs = 1;
        int save = t->encrypted; t->encrypted = 0;
        send_record(t, 20, &ccs, 1);
        t->encrypted = save;
        if (send_record(t, 22, fin, sizeof(fin)) < 0) return t->error = TLS_ERR_TRANSPORT;
        sha256_update(&t->transcript, fin, sizeof(fin));
    }

    set_traffic_keys(t, t->client_app_secret, t->server_app_secret);
    t->encrypted = 2;
    t->handshake_done = 1;
    t->rec_used = t->rec_plain_len;
    memset(priv, 0, 32); memset(shared, 0, 32);
    return 0;
}

/* -------------------------------------------------- application data */

int tls_write(tls_t* t, const uint8_t* data, int len) {
    if (!t->handshake_done || t->error) return -1;
    int done = 0;
    while (done < len) {
        int n = len - done;
        if (n > 8192) n = 8192;
        if (send_record(t, 23, data + done, n) < 0) return -1;
        done += n;
    }
    return done;
}

static void key_update(tls_t* t, int request_back) {
    /* server updated its sending key: derive the next server application secret */
    uint8_t next[32];
    expand_label(t->server_app_secret, "traffic upd", NULL, 0, next, 32);
    memcpy(t->server_app_secret, next, 32);
    uint8_t key[16];
    expand_label(t->server_app_secret, "key", NULL, 0, key, 16);
    gcm_init(&t->rkey, key);
    expand_label(t->server_app_secret, "iv", NULL, 0, t->riv, 12);
    t->rseq = 0;
    if (request_back) {
        uint8_t msg[5] = { 24, 0, 0, 1, 0 };            /* KeyUpdate(update_not_requested) */
        send_record(t, 22, msg, 5);
        expand_label(t->client_app_secret, "traffic upd", NULL, 0, next, 32);
        memcpy(t->client_app_secret, next, 32);
        expand_label(t->client_app_secret, "key", NULL, 0, key, 16);
        gcm_init(&t->wkey, key);
        expand_label(t->client_app_secret, "iv", NULL, 0, t->wiv, 12);
        t->wseq = 0;
    }
}

int tls_read(tls_t* t, uint8_t* out, int max, int timeout_ms) {
    if (!t->handshake_done) return -1;
    for (;;) {
        if (t->rec_used < t->rec_plain_len && t->rec_type == 23) {
            int n = t->rec_plain_len - t->rec_used;
            if (n > max) n = max;
            memcpy(out, t->rec + t->rec_used, n);
            t->rec_used += n;
            return n;
        }
        if (t->peer_closed || t->error) return -1;
        int r = recv_record(t, timeout_ms);
        if (r == 0) return 0;
        if (r < 0) return -1;
        if (t->rec_type == 23) continue;
        if (t->rec_type == 21) {
            t->alert_received = t->rec[1];
            t->peer_closed = 1;                        /* close_notify or fatal: either way we are done */
            t->rec_used = t->rec_plain_len;
            return -1;
        }
        if (t->rec_type == 22) {
            /* post-handshake messages: NewSessionTicket (4) ignored, KeyUpdate (24) handled */
            int p = 0;
            while (p + 4 <= t->rec_plain_len) {
                int type = t->rec[p], l = (int)get24(t->rec + p + 1);
                if (type == 24 && l == 1) key_update(t, t->rec[p + 4] == 1);
                p += 4 + l;
            }
            t->rec_used = t->rec_plain_len;
            continue;
        }
        t->rec_used = t->rec_plain_len;               /* unknown type: skip */
    }
}

void tls_close(tls_t* t) {
    if (t->handshake_done && !t->peer_closed && !t->error) send_alert(t, 1, 0);   /* close_notify */
    memset(&t->wkey, 0, sizeof(t->wkey));
    memset(&t->rkey, 0, sizeof(t->rkey));
    memset(t->handshake_secret, 0, 32);
    memset(t->client_app_secret, 0, 32); memset(t->server_app_secret, 0, 32);
    memset(t->client_hs_secret, 0, 32); memset(t->server_hs_secret, 0, 32);
}

const char* tls_error_string(int err) {
    switch (err) {
    case TLS_ERR_NONE: return "ok";
    case TLS_ERR_TRANSPORT: return "connection failed or closed during the handshake";
    case TLS_ERR_VERSION: return "server does not speak TLS 1.3";
    case TLS_ERR_CIPHER: return "server refused TLS_AES_128_GCM_SHA256";
    case TLS_ERR_KEYSHARE: return "server sent no X25519 key share";
    case TLS_ERR_DECRYPT: return "record decryption failed (bad MAC)";
    case TLS_ERR_FINISHED: return "server Finished verification failed";
    case TLS_ERR_ALERT: return "server sent a fatal alert";
    case TLS_ERR_PROTOCOL: return "malformed handshake message";
    case TLS_ERR_HRR: return "server requested HelloRetryRequest (unsupported)";
    case TLS_ERR_TOOBIG: return "record too large";
    }
    return "unknown TLS error";
}
