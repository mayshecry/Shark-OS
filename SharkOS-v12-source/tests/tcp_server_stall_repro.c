

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define TCP_SERVER_CONNECTIONS 8

typedef struct {
    uint8_t state;
    uint8_t accepted;
    uint8_t peer_closed;
    uint8_t local_closed;
    uint16_t local_port, peer_port;
    uint32_t send_seq, send_acked, recv_seq;
} conn_t;

static conn_t connections[TCP_SERVER_CONNECTIONS];
static int stat_free = 0, stat_handshake_wait = 0, stat_established = 0, stat_wait = 0;

static int is_listening(uint16_t port) { return port == 80; }


static int accept_conn(uint16_t port) {
    for (int i = 0; i < TCP_SERVER_CONNECTIONS; i++)
        if (connections[i].state == 2 && !connections[i].accepted &&
            connections[i].local_port == port) {
            connections[i].accepted = 1;
            return i;
        }
    return -1;
}


static int on_syn(uint16_t sport, uint16_t dport) {
    int free_slot = -1;
    for (int i = 0; i < TCP_SERVER_CONNECTIONS; i++) {
        conn_t* q = &connections[i];
        if (q->state && q->local_port == dport && q->peer_port == sport) return -2;
        if (!q->state && free_slot < 0) free_slot = i;
    }
    if (!is_listening(dport) || free_slot < 0) return -1;
    conn_t* c = &connections[free_slot];
    memset(c, 0, sizeof(*c));
    c->local_port = dport;
    c->peer_port = sport;
    c->send_seq = 1000;
    c->recv_seq = 5000;
    c->state = 1;
    return free_slot;
}

static void on_ack_handshake(int i) {
    conn_t* c = &connections[i];
    if (c->state == 1) { c->state = 2; c->send_acked = c->send_seq + 1; }
}


static void httpd_closes(int handle) {
    conn_t* c = &connections[handle];
    memset(c, 0, sizeof(*c));
}

static void census(void) {
    stat_free = stat_handshake_wait = stat_established = stat_wait = 0;
    for (int i = 0; i < TCP_SERVER_CONNECTIONS; i++) {
        conn_t* c = &connections[i];
        if (!c->state) stat_free++;
        else if (c->state == 1) stat_handshake_wait++;
        else if (c->peer_closed) stat_wait++;
        else stat_established++;
    }
}

static int stale_established(void) {
    int n = 0;
    for (int i = 0; i < TCP_SERVER_CONNECTIONS; i++)
        if (connections[i].state == 2 && !connections[i].peer_closed) n++;
    return n;
}

int main(void) {
    memset(connections, 0, sizeof(connections));
    uint16_t client_port = 40000;

    printf("=== 1. normal request/response cycle (what happens today) ===\n");
    for (int req = 1; req <= 3; req++) {
        int slot = on_syn(client_port++, 80);
        on_ack_handshake(slot);
        int handle = accept_conn(80);
        census();
        printf("  request %d: slot=%d free=%d established=%d timewait=%d accept=%d\n",
               req, slot, stat_free, stat_established, stat_wait, handle);
        httpd_closes(handle);
        census();
        printf("             after close: free=%d established=%d timewait=%d\n",
               stat_free, stat_established, stat_wait);
    }

    printf("\n=== 2. a port that is rebound while connections are still in TIME_WAIT ===\n");
    printf("    (httpd_rearm() / ssh re-arm after a link flap does exactly this)\n");
    memset(connections, 0, sizeof(connections));
    for (int i = 0; i < TCP_SERVER_CONNECTIONS; i++) {
        connections[i].state = 2;
        connections[i].peer_closed = 1;
        connections[i].local_port = 80;
        connections[i].peer_port = (uint16_t)(41000 + i);
    }
    census();
    printf("    before rearm: free=%d timewait=%d\n", stat_free, stat_wait);


    printf("    net_tcp_listen(80) -> %s (listener table still holds 80)\n",
           is_listening(80) ? "0 already listening" : "re-created");
    int slot = on_syn(42000, 80);
    printf("    next SYN: slot=%d  => %s\n", slot,
           slot < 0 ? "DROPPED, no SYN-ACK: browser shows 'connection timed out'"
                    : "accepted");
    census();
    printf("    free=%d timewait=%d\n", stat_free, stat_wait);

    printf("\n=== 3. a client that never finishes its handshake ===\n");
    printf("    (port scanner, health probe, or a NAT'd guest during a link flap)\n");
    memset(connections, 0, sizeof(connections));
    for (int i = 0; i < TCP_SERVER_CONNECTIONS; i++)
        if (on_syn((uint16_t)(43000 + i), 80) < 0) { printf("    SYN %d dropped\n", i); break; }
    int refused = on_syn(44000, 80);
    printf("    after %d half-open handshakes: next SYN slot=%d (%s), stale established=%d\n",
           TCP_SERVER_CONNECTIONS, refused, refused < 0 ? "dropped, no RST" : "ok",
           stale_established());
    printf("    accept() keeps returning: %d (nothing reaps state==1)\n", accept_conn(80));

    printf("\n=== 4. two servers on the same port (httpd + router console) ===\n");
    memset(connections, 0, sizeof(connections));

    printf("    both held 80: 'port in use' for whoever rebinds second\n");

    printf("\n=== 5. a request accepted twice ===\n");
    memset(connections, 0, sizeof(connections));
    int a = on_syn(45000, 80);
    on_ack_handshake(a);
    int h1 = accept_conn(80);
    int h2 = accept_conn(80);
    printf("    first accept=%d second accept=%d\n", h1, h2);
    printf("    (accepted flag guards this case)\n");

    printf("\n=== 6. connection closed by peer without a client FIN reaching us ===\n");
    memset(connections, 0, sizeof(connections));
    int b = on_syn(46000, 80);
    on_ack_handshake(b);
    int h = accept_conn(80);
    printf("    slot %d: handshake done, request never arrives, no FIN, no timer\n", h);
    printf("    slot stays: state=%d peer_closed=%d accepted=%d\n",
           connections[h].state, connections[h].peer_closed, connections[h].accepted);
    return 0;
}
