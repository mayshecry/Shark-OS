

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define CONNECTIONS 8
#define SYN_TIMEOUT   4000u
#define IDLE_TIMEOUT  30000u
#define KEEPALIVE     3000u
#define KA_TRIES      3

typedef struct {
    int state;
    int accepted, peer_closed, local_closed, busy;
    uint16_t lport, pport;
    uint32_t last_rx, last_tx;
    int retries, keepalive;
} conn_t;

static conn_t conns[CONNECTIONS];
static uint32_t now;
static int sent_synack, sent_fin, sent_rst, sent_keepalive;

static void emit(int idx, int flag) {
    if (flag == 0x12) sent_synack++;
    else if (flag == 0x11) sent_fin++;
    else if (flag == 0x14) sent_rst++;
    else if (flag == 0x10) sent_keepalive++;
    conns[idx].last_tx = now;
}


static int on_syn(uint16_t sport, uint16_t dport) {
    int free_slot = -1, stale = -1;
    for (int i = 0; i < CONNECTIONS; i++) {
        conn_t* q = &conns[i];
        if (q->state && q->lport == dport && q->pport == sport) return i;
        if (!q->state && free_slot < 0) free_slot = i;
        if (q->state == 1 && stale < 0 && now - q->last_rx >= SYN_TIMEOUT) stale = i;
    }
    if (free_slot < 0 && stale >= 0) {
        memset(&conns[stale], 0, sizeof(conn_t));
        free_slot = stale;
    }
    if (free_slot < 0) return -1;
    conn_t* c = &conns[free_slot];
    memset(c, 0, sizeof(*c));
    c->lport = dport;
    c->pport = sport;
    c->state = 1;
    c->last_rx = c->last_tx = now;
    emit(free_slot, 0x12);
    return free_slot;
}

static int accept_conn(uint16_t port) {
    for (int i = 0; i < CONNECTIONS; i++)
        if (conns[i].state == 2 && !conns[i].accepted && conns[i].lport == port) {
            conns[i].accepted = 1;
            return i;
        }
    return -1;
}

static void establish(int i) {
    conns[i].state = 2;
    conns[i].last_rx = now;
}


static void close_conn(int i) {
    if (conns[i].state == 2 && !conns[i].local_closed) emit(i, 0x11);
    memset(&conns[i], 0, sizeof(conn_t));
}


static void reap(void) {
    for (int i = 0; i < CONNECTIONS; i++) {
        conn_t* c = &conns[i];
        if (!c->state || c->busy) continue;
        if (c->state == 1) {
            if (now - c->last_rx >= SYN_TIMEOUT) memset(c, 0, sizeof(conn_t));
            continue;
        }
        if (c->peer_closed) {
            if (now - c->last_rx >= 1000) memset(c, 0, sizeof(conn_t));
            continue;
        }
        if (now - c->last_rx >= IDLE_TIMEOUT) {
            emit(i, 0x14);
            memset(c, 0, sizeof(conn_t));
            continue;
        }
        if (now - c->last_tx >= KEEPALIVE) {
            if (c->retries >= KA_TRIES) {
                emit(i, 0x14);
                memset(c, 0, sizeof(conn_t));
                continue;
            }
            c->retries++;
            c->keepalive++;
            emit(i, 0x10);
        }
    }
}


static void peer_talks(int i) {
    conns[i].last_rx = now;
    conns[i].retries = 0;
    conns[i].keepalive = 0;
    emit(i, 0x10);
}

static int free_slots(void) {
    int n = 0;
    for (int i = 0; i < CONNECTIONS; i++) if (!conns[i].state) n++;
    return n;
}

int main(void) {
    int failures = 0;

    printf("1. abandoned half-open handshakes drain themselves\n");
    memset(conns, 0, sizeof(conns));
    now = 0;
    for (int i = 0; i < CONNECTIONS; i++) on_syn((uint16_t)(1000 + i), 80);
    printf("   table after %d SYNs: free=%d\n", CONNECTIONS, free_slots());
    now = SYN_TIMEOUT + 10;
    reap();
    printf("   after the SYN timeout: free=%d\n", free_slots());
    if (free_slots() != CONNECTIONS) { printf("   FAIL\n"); failures++; }
    else printf("   PASS\n");

    printf("2. a full table still answers a new SYN by evicting a stale slot\n");
    memset(conns, 0, sizeof(conns));
    now = 0;
    for (int i = 0; i < CONNECTIONS; i++) on_syn((uint16_t)(2000 + i), 80);
    now = SYN_TIMEOUT + 5;
    int slot = on_syn(2999, 80);
    printf("   new SYN while full -> slot %d\n", slot);
    if (slot < 0) { printf("   FAIL\n"); failures++; } else printf("   PASS\n");

    printf("3. a client that connects and never sends a request is reclaimed\n");
    memset(conns, 0, sizeof(conns));
    now = 0;
    int c = on_syn(3000, 80);
    establish(c);
    int h = accept_conn(80);
    for (now = 0; now <= IDLE_TIMEOUT + 5; now += 250) reap();
    printf("   connection was accepted as %d, slot now free: %d\n", h, !conns[h].state);
    printf("   keepalives sent: %d, resets: %d\n", sent_keepalive, sent_rst);
    if (conns[h].state) { printf("   FAIL\n"); failures++; } else printf("   PASS\n");

    printf("4. a live connection survives keepalives and stays usable\n");
    memset(conns, 0, sizeof(conns));
    sent_keepalive = sent_rst = 0;
    now = 0;
    c = on_syn(3001, 80);
    establish(c);
    h = accept_conn(80);
    for (now = 0; now <= 20000; now += 500) {
        reap();
        peer_talks(h);
    }
    printf("   live slot still open: %d, keepalives: %d, resets: %d\n",
           conns[h].state == 2, sent_keepalive, sent_rst);
    if (conns[h].state != 2 || sent_rst) { printf("   FAIL\n"); failures++; }
    else printf("   PASS\n");

    printf("5. closing after a response frees the slot immediately (no TIME_WAIT leak)\n");
    memset(conns, 0, sizeof(conns));
    now = 0;
    c = on_syn(3002, 80);
    establish(c);
    h = accept_conn(80);
    close_conn(h);
    printf("   slot free after close: %d, FIN sent: %d of 2 handshakes\n",
           free_slots() == CONNECTIONS, sent_fin);
    if (free_slots() != CONNECTIONS) { printf("   FAIL\n"); failures++; }
    else printf("   PASS\n");

    printf("6. repeated request bursts never exhaust the table\n");
    memset(conns, 0, sizeof(conns));
    now = 0;
    int refused = 0;
    for (int req = 0; req < 500; req++) {
        now += 7;
        int s = on_syn((uint16_t)(4000 + (req % 3000)), 80);
        if (s < 0) { refused++; reap(); continue; }
        establish(s);
        int hh = accept_conn(80);
        if (hh >= 0) close_conn(hh);
        reap();
    }
    printf("   500 requests, refused: %d, free slots at end: %d\n", refused, free_slots());
    if (refused || free_slots() != CONNECTIONS) { printf("   FAIL\n"); failures++; }
    else printf("   PASS\n");

    printf("\n%s\n", failures ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return failures ? 1 : 0;
}
