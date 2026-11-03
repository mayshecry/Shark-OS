#include "kernel.h"
#include "net.h"
#include "router.h"
#include "router_ui_html.h"

/* Management HTTP server for the SharkOS router. It uses the kernel's
 * lightweight TCP server API (the same one used by httpd/sshd) and is
 * polled from router_poll(), which is hooked into net_poll(). */

#define RHTTP_CLIENTS        4
#define RHTTP_REQUEST_MAX    4096
#define RHTTP_CLI_BODY_MAX   512
#define RHTTP_CLI_OUT_MAX    4096

typedef struct {
    int      handle;
    uint8_t  active;
    uint32_t used;
    char     request[RHTTP_REQUEST_MAX];
} rhttp_client_t;

static rhttp_client_t rhttp_clients[RHTTP_CLIENTS];
static char rhttp_status[ROUTER_STATUS_SIZE];
static char rhttp_cli_out[RHTTP_CLI_OUT_MAX];
static char rhttp_response[RHTTP_CLI_OUT_MAX + 128];
static uint16_t rhttp_port;
static int rhttp_active;
static int rhttp_busy;

static void rhttp_append(char* out, uint32_t cap, uint32_t* used, const char* text) {
    while (*text && *used + 1 < cap) out[(*used)++] = *text++;
    out[*used] = 0;
}

static void rhttp_num(char* out, uint32_t cap, uint32_t* used, uint32_t value) {
    char tmp[12];
    uint32_t n = 0;
    if (!value) tmp[n++] = '0';
    while (value && n < sizeof(tmp)) { tmp[n++] = (char)('0' + value % 10); value /= 10; }
    while (n && *used + 1 < cap) out[(*used)++] = tmp[--n];
    out[*used] = 0;
}

static int rhttp_ieq(const char* a, const char* b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

static int rhttp_prefix(const char* text, const char* prefix) {
    while (*prefix) {
        char ca = *text, cb = *prefix;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return 0;
        text++; prefix++;
    }
    return 1;
}

static uint32_t rhttp_parse_uint(const char* text) {
    uint32_t value = 0;
    if (!text) return 0;
    while (*text == ' ' || *text == '\t') text++;
    if (*text < '0' || *text > '9') return 0;
    while (*text >= '0' && *text <= '9') { value = value * 10 + (uint32_t)(*text - '0'); text++; }
    return value;
}

/* Find a header value within the request head (case-insensitive name). */
static int rhttp_header(const char* request, uint32_t head_end, const char* name,
                        char* out, uint32_t cap) {
    uint32_t pos = 0;
    /* skip request line */
    while (pos + 1 < head_end && !(request[pos] == '\r' && request[pos + 1] == '\n')) pos++;
    if (pos + 1 >= head_end) return 0;
    pos += 2;
    uint32_t name_len = strlen(name);
    while (pos < head_end) {
        uint32_t line_end = pos;
        while (line_end + 1 < head_end &&
               !(request[line_end] == '\r' && request[line_end + 1] == '\n')) line_end++;
        uint32_t line_len = line_end - pos;
        if (line_len > name_len) {
            uint32_t colon = pos;
            while (colon < line_end && request[colon] != ':') colon++;
            if (colon > pos && (uint32_t)(colon - pos) == name_len) {
                char field[32];
                uint32_t i = 0;
                for (; i < name_len && i + 1 < sizeof(field); i++) field[i] = request[pos + i];
                field[i] = 0;
                if (rhttp_ieq(field, name)) {
                    uint32_t value_start = colon + 1;
                    while (value_start < line_end && (request[value_start] == ' ' || request[value_start] == '\t')) value_start++;
                    uint32_t n = 0;
                    while (value_start < line_end && n + 1 < cap) out[n++] = request[value_start++];
                    out[n] = 0;
                    return 1;
                }
            }
        }
        if (line_end + 1 >= head_end) break;
        pos = line_end + 2;
    }
    return 0;
}

static int rhttp_token_ok(const char* supplied) {
    const char* expected = router_get_config()->token;
    uint32_t expected_len = strlen(expected);
    uint32_t supplied_len = supplied ? strlen(supplied) : 0;
    unsigned int diff = expected_len ^ supplied_len;
    for (uint32_t i = 0; i < ROUTER_TOKEN_SIZE; i++) {
        unsigned char a = i < supplied_len ? (unsigned char)supplied[i] : 0;
        unsigned char b = i < expected_len ? (unsigned char)expected[i] : 0;
        diff |= (unsigned int)(a ^ b);
    }
    return diff == 0 && expected_len != 0;
}

static void rhttp_json_escape(char* out, uint32_t cap, uint32_t* used, const char* text) {
    while (*text && *used + 2 < cap) {
        char c = *text++;
        if (c == '"' || c == '\\') { out[(*used)++] = '\\'; out[(*used)++] = c; }
        else if (c == '\n') { out[(*used)++] = '\\'; out[(*used)++] = 'n'; }
        else if (c == '\r') { out[(*used)++] = '\\'; out[(*used)++] = 'r'; }
        else if (c == '\t') { out[(*used)++] = '\\'; out[(*used)++] = 't'; }
        else if ((unsigned char)c < 32) out[(*used)++] = ' ';
        else out[(*used)++] = c;
    }
    out[*used] = 0;
}

static void rhttp_send(rhttp_client_t* client, int status, const char* reason,
                       const char* mime, const uint8_t* body, uint32_t body_len,
                       int head_only) {
    char header[256];
    uint32_t used = 0;
    header[0] = 0;
    rhttp_append(header, sizeof(header), &used, "HTTP/1.1 ");
    rhttp_num(header, sizeof(header), &used, (uint32_t)status);
    rhttp_append(header, sizeof(header), &used, " ");
    rhttp_append(header, sizeof(header), &used, reason);
    rhttp_append(header, sizeof(header), &used, "\r\nContent-Type: ");
    rhttp_append(header, sizeof(header), &used, mime);
    rhttp_append(header, sizeof(header), &used, "\r\nContent-Length: ");
    rhttp_num(header, sizeof(header), &used, body_len);
    rhttp_append(header, sizeof(header), &used,
                 "\r\nConnection: close\r\nCache-Control: no-store\r\n"
                 "X-Content-Type-Options: nosniff\r\nServer: SharkOS-Router\r\n\r\n");
    if (net_tcp_server_send(client->handle, (const uint8_t*)header, used) != (int)used) return;
    if (!head_only && body_len && body) net_tcp_server_send(client->handle, body, body_len);
}

static void rhttp_json_response(rhttp_client_t* client, int status, const char* reason,
                                const char* json, int head_only) {
    rhttp_send(client, status, reason, "application/json; charset=utf-8",
               (const uint8_t*)json, (uint32_t)strlen(json), head_only);
}

static void rhttp_text(rhttp_client_t* client, int status, const char* reason,
                       const char* text, int head_only) {
    rhttp_send(client, status, reason, "text/plain; charset=utf-8",
               (const uint8_t*)text, (uint32_t)strlen(text), head_only);
}

static uint32_t rhttp_head_end(const char* request, uint32_t used) {
    for (uint32_t i = 0; i + 3 < used; i++) {
        if (request[i] == '\r' && request[i + 1] == '\n' &&
            request[i + 2] == '\r' && request[i + 3] == '\n') return i + 4;
    }
    return 0;
}

static void rhttp_handle(rhttp_client_t* client, uint32_t head_end) {
    char method[8];
    char path[192];
    const char* request = client->request;
    uint32_t first_space = 0;
    while (first_space < head_end && request[first_space] != ' ' && first_space + 1 < sizeof(method)) first_space++;
    if (!first_space || first_space >= head_end) { rhttp_text(client, 400, "Bad Request", "bad request\n", 0); return; }
    memcpy(method, request, first_space);
    method[first_space] = 0;
    uint32_t target_start = first_space + 1;
    uint32_t target_end = target_start;
    while (target_end < head_end && request[target_end] != ' ') target_end++;
    uint32_t target_len = target_end - target_start;
    if (!target_len || target_len >= sizeof(path)) { rhttp_text(client, 400, "Bad Request", "bad request\n", 0); return; }
    memcpy(path, request + target_start, target_len);
    path[target_len] = 0;
    char* query = strchr(path, '?');
    if (query) *query = 0;
    if (path[0] != '/' || strstr(path, "..")) { rhttp_text(client, 400, "Bad Request", "bad request\n", 0); return; }
    int head_only = rhttp_ieq(method, "HEAD");
    if (!head_only && !rhttp_ieq(method, "GET") && !rhttp_ieq(method, "POST")) {
        rhttp_text(client, 405, "Method Not Allowed", "method not allowed\n", head_only);
        return;
    }
    int is_api = rhttp_prefix(path, "/api/");
    char token_header[64];
    char auth_header[96];
    int has_token = rhttp_header(request, head_end, "X-Shark-Token", token_header, sizeof(token_header));
    int has_auth = rhttp_header(request, head_end, "Authorization", auth_header, sizeof(auth_header));
    const char* supplied = 0;
    if (has_token) supplied = token_header;
    else if (has_auth && rhttp_prefix(auth_header, "Bearer ")) supplied = auth_header + 7;
    if (is_api && !rhttp_token_ok(supplied)) {
        rhttp_json_response(client, 401, "Unauthorized",
                            "{\"ok\":false,\"error\":\"invalid or missing API token\"}", head_only);
        return;
    }
    if (rhttp_ieq(method, "GET") || head_only) {
        if (rhttp_ieq(path, "/") || rhttp_ieq(path, "/index.html")) {
            rhttp_send(client, 200, "OK", "text/html; charset=utf-8",
                       (const uint8_t*)ROUTER_UI_HTML, ROUTER_UI_HTML_LEN, head_only);
            return;
        }
        if (rhttp_ieq(path, "/api/status")) {
            router_build_status(rhttp_status, sizeof(rhttp_status));
            rhttp_json_response(client, 200, "OK", rhttp_status, head_only);
            return;
        }
        if (rhttp_ieq(path, "/api/fw")) {
            router_build_firewall_status(rhttp_status, sizeof(rhttp_status));
            rhttp_json_response(client, 200, "OK", rhttp_status, head_only);
            return;
        }
        if (rhttp_ieq(path, "/api/log")) {
            router_build_log(rhttp_status, sizeof(rhttp_status));
            rhttp_json_response(client, 200, "OK", rhttp_status, head_only);
            return;
        }
        rhttp_text(client, 404, "Not Found", "not found\n", head_only);
        return;
    }

    /* POST */
    char length_text[16];
    uint32_t content_length = 0;
    if (rhttp_header(request, head_end, "Content-Length", length_text, sizeof(length_text)))
        content_length = rhttp_parse_uint(length_text);
    if (content_length > RHTTP_CLI_BODY_MAX || head_end + content_length > client->used) {
        rhttp_json_response(client, 400, "Bad Request",
                            "{\"ok\":false,\"error\":\"invalid body\"}", 0);
        return;
    }
    char body[RHTTP_CLI_BODY_MAX + 1];
    uint32_t body_len = content_length;
    memcpy(body, request + head_end, body_len);
    body[body_len] = 0;
    if (rhttp_ieq(path, "/api/cli")) {
        /* Accept plain text commands, or {"cmd":"..."} JSON for clients. */
        char command[RHTTP_CLI_BODY_MAX + 1];
        const char* source = body;
        if (body[0] == '{') {
            const char* key = strstr(body, "\"cmd\"");
            if (key) {
                const char* colon = strchr(key, ':');
                const char* quote = colon ? strchr(colon, '"') : 0;
                if (quote) {
                    uint32_t n = 0;
                    const char* p = quote + 1;
                    while (*p && *p != '"' && n < sizeof(command) - 1) {
                        if (*p == '\\' && p[1]) p++;
                        command[n++] = *p++;
                    }
                    command[n] = 0;
                    source = command;
                }
            }
        }
        while (*source == ' ' || *source == '\t') source++;
        if (!*source) {
            rhttp_json_response(client, 400, "Bad Request",
                                "{\"ok\":false,\"error\":\"empty command\"}", 0);
            return;
        }
        rhttp_cli_out[0] = 0;
        router_cli_exec(0, source, rhttp_cli_out, sizeof(rhttp_cli_out));
        char* response = rhttp_response;
        uint32_t used = 0;
        rhttp_append(response, sizeof(rhttp_response), &used, "{\"ok\":true,\"output\":\"");
        rhttp_json_escape(response, sizeof(rhttp_response), &used, rhttp_cli_out);
        rhttp_append(response, sizeof(rhttp_response), &used, "\"}");
        rhttp_json_response(client, 200, "OK", response, 0);
        return;
    }
    if (rhttp_ieq(path, "/api/token")) {
        int result = router_set_token(body);
        rhttp_json_response(client, result == 0 ? 200 : 400, result == 0 ? "OK" : "Bad Request",
                            result == 0 ? "{\"ok\":true,\"output\":\"API token updated\"}"
                                        : "{\"ok\":false,\"error\":\"token must be 1-31 printable characters\"}", 0);
        return;
    }
    rhttp_text(client, 404, "Not Found", "not found\n", 0);
}

int router_http_start(uint16_t port) {
    if (!port || !net_has_nic()) return -1;
    if (rhttp_active && rhttp_port == port) return 0;
    if (net_tcp_is_listening(port)) return -1;   /* httpd/sshd/other listener owns it */
    if (rhttp_active) router_http_stop();
    if (net_tcp_listen(port) != 0) return -1;
    rhttp_port = port;
    rhttp_active = 1;
    router_build_status(rhttp_status, sizeof(rhttp_status));
    return 0;
}

void router_http_stop(void) {
    if (rhttp_active) net_tcp_server_stop(rhttp_port);
    for (uint32_t i = 0; i < RHTTP_CLIENTS; i++) {
        if (rhttp_clients[i].active) {
            net_tcp_server_close(rhttp_clients[i].handle);
            rhttp_clients[i].active = 0;
        }
    }
    rhttp_active = 0;
}

int router_http_is_active(void) { return rhttp_active; }
uint16_t router_http_port(void) { return rhttp_active ? rhttp_port : 0; }

void router_http_poll(void) {
    if (rhttp_busy || !rhttp_active) return;
    rhttp_busy = 1;
    for (;;) {
        int handle = net_tcp_server_accept(rhttp_port);
        if (handle < 0) break;
        int stored = 0;
        for (uint32_t i = 0; i < RHTTP_CLIENTS; i++) {
            if (rhttp_clients[i].active) continue;
            memset(&rhttp_clients[i], 0, sizeof(rhttp_clients[i]));
            rhttp_clients[i].handle = handle;
            rhttp_clients[i].active = 1;
            stored = 1;
            break;
        }
        if (!stored) net_tcp_server_close(handle);
    }
    uint8_t temp[512];
    for (uint32_t i = 0; i < RHTTP_CLIENTS; i++) {
        rhttp_client_t* client = &rhttp_clients[i];
        if (!client->active) continue;
        int got = net_tcp_server_recv(client->handle, temp, sizeof(temp), 0);
        if (got > 0) {
            if ((uint32_t)got > sizeof(client->request) - 1 - client->used) {
                rhttp_text(client, 413, "Payload Too Large", "request too large\n", 0);
                net_tcp_server_close(client->handle);
                client->active = 0;
                continue;
            }
            memcpy(client->request + client->used, temp, (uint32_t)got);
            client->used += (uint32_t)got;
            client->request[client->used] = 0;
        } else if (got < 0 || net_tcp_server_closed(client->handle)) {
            net_tcp_server_close(client->handle);
            client->active = 0;
            continue;
        }
        uint32_t head_end = rhttp_head_end(client->request, client->used);
        if (!head_end) continue;
        char length_text[16];
        uint32_t content_length = 0;
        if (rhttp_header(client->request, head_end, "Content-Length",
                         length_text, sizeof(length_text)))
            content_length = rhttp_parse_uint(length_text);
        if (content_length > RHTTP_CLI_BODY_MAX) {
            rhttp_text(client, 413, "Payload Too Large", "body too large\n", 0);
            net_tcp_server_close(client->handle);
            client->active = 0;
            continue;
        }
        if (client->used < head_end + content_length) continue;
        router_note_http_request();
        rhttp_handle(client, head_end);
        router_build_status(rhttp_status, sizeof(rhttp_status));
        net_tcp_server_close(client->handle);
        client->active = 0;
    }
    rhttp_busy = 0;
}
