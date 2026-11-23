#include "kernel.h"
#include "net.h"
#include "httpd.h"
#include "ssh.h"

#define HTTPD_CLIENTS 8
#define HTTPD_REQUEST_SIZE 4096
#define HTTPD_HEADER_TIMEOUT_MS 5000u
#define HTTPD_STREAM_CHUNK 1024

typedef struct {
    int handle;
    uint8_t active;
    uint8_t server;
    uint32_t used;
    uint32_t opened;
    char request[HTTPD_REQUEST_SIZE];
} httpd_client_t;

static uint32_t httpd_accepted;
static uint32_t httpd_dropped;
static uint32_t httpd_slow;

typedef struct {
    uint16_t port;
    uint8_t configured;
    uint8_t active;
    uint8_t desired;
    char root[128];
} httpd_server_t;

static httpd_client_t httpd_clients[HTTPD_CLIENTS];
static httpd_server_t httpd_servers[HTTPD_SERVER_LIMIT];

static void httpd_append(char* out, uint32_t cap, uint32_t* used, const char* text) {
    while (*text && *used + 1 < cap) out[(*used)++] = *text++;
    out[*used] = 0;
}

static int httpd_equal(const char* a, const char* b) {
    return strcasecmp(a, b) == 0;
}

static const char* httpd_mime(const char* path) {
    const char* dot = NULL;
    for (const char* p = path; *p; p++) if (*p == '.') dot = p;
    if (!dot) return "application/octet-stream";
    if (httpd_equal(dot, ".html") || httpd_equal(dot, ".htm")) return "text/html; charset=utf-8";
    if (httpd_equal(dot, ".css")) return "text/css; charset=utf-8";
    if (httpd_equal(dot, ".js")) return "application/javascript; charset=utf-8";
    if (httpd_equal(dot, ".json")) return "application/json; charset=utf-8";
    if (httpd_equal(dot, ".txt")) return "text/plain; charset=utf-8";
    if (httpd_equal(dot, ".svg")) return "image/svg+xml";
    if (httpd_equal(dot, ".png")) return "image/png";
    if (httpd_equal(dot, ".jpg") || httpd_equal(dot, ".jpeg")) return "image/jpeg";
    if (httpd_equal(dot, ".gif")) return "image/gif";
    if (httpd_equal(dot, ".ico")) return "image/x-icon";
    if (httpd_equal(dot, ".xml")) return "application/xml";
    return "application/octet-stream";
}

static void httpd_send_response(httpd_client_t* client, int status, const char* reason, const char* mime, const uint8_t* body, uint32_t body_len, int head_only) {
    char header[512];
    char number[16];
    uint32_t used = 0;
    header[0] = 0;
    httpd_append(header, sizeof(header), &used, "HTTP/1.1 ");
    int_to_string((uint32_t)status, number);
    httpd_append(header, sizeof(header), &used, number);
    httpd_append(header, sizeof(header), &used, " ");
    httpd_append(header, sizeof(header), &used, reason);
    httpd_append(header, sizeof(header), &used, "\r\nContent-Type: ");
    httpd_append(header, sizeof(header), &used, mime);
    httpd_append(header, sizeof(header), &used, "\r\nContent-Length: ");
    int_to_string(body_len, number);
    httpd_append(header, sizeof(header), &used, number);
    httpd_append(header, sizeof(header), &used, "\r\nConnection: close\r\nServer: SharkOS\r\n\r\n");
    if (net_tcp_server_send(client->handle, (const uint8_t*)header, used) != (int)used) return;
    if (head_only || !body_len || !body) return;
    uint32_t sent = 0;
    while (sent < body_len) {
        uint32_t chunk = body_len - sent;
        if (chunk > HTTPD_STREAM_CHUNK) chunk = HTTPD_STREAM_CHUNK;
        if (net_tcp_server_send(client->handle, body + sent, chunk) != (int)chunk) return;
        sent += chunk;
    }
}

static int httpd_finish_request(httpd_client_t* client) {
    httpd_server_t* server = &httpd_servers[client->server];
    char method[8];
    char target[256];
    char path[512];
    uint32_t line_end = 0;
    while (line_end + 1 < client->used && !(client->request[line_end] == '\r' && client->request[line_end + 1] == '\n')) line_end++;
    if (line_end + 1 >= client->used) return 0;
    uint32_t first_space = 0;
    while (first_space < line_end && client->request[first_space] != ' ') first_space++;
    if (!first_space || first_space >= line_end || first_space >= sizeof(method)) {
        static const uint8_t bad[] = "Bad Request\n";
        httpd_send_response(client, 400, "Bad Request", "text/plain; charset=utf-8", bad, sizeof(bad) - 1, 0);
        return 1;
    }
    uint32_t target_start = first_space + 1;
    uint32_t target_end = target_start;
    while (target_end < line_end && client->request[target_end] != ' ') target_end++;
    uint32_t method_len = first_space;
    uint32_t target_len = target_end - target_start;
    if (!target_len || target_len >= sizeof(target)) {
        static const uint8_t bad[] = "Bad Request\n";
        httpd_send_response(client, 400, "Bad Request", "text/plain; charset=utf-8", bad, sizeof(bad) - 1, 0);
        return 1;
    }
    for (uint32_t i = 0; i < method_len; i++) method[i] = client->request[i];
    method[method_len] = 0;
    for (uint32_t i = 0; i < target_len; i++) target[i] = client->request[target_start + i];
    target[target_len] = 0;
    char* query = strchr(target, '?');
    if (query) *query = 0;
    if (target[0] != '/') {
        static const uint8_t bad[] = "Bad Request\n";
        httpd_send_response(client, 400, "Bad Request", "text/plain; charset=utf-8", bad, sizeof(bad) - 1, 0);
        return 1;
    }
    if (!httpd_equal(method, "HEAD") && !httpd_equal(method, "GET")) {
        static const uint8_t body[] = "Method Not Allowed\n";
        httpd_send_response(client, 405, "Method Not Allowed", "text/plain; charset=utf-8", body, sizeof(body) - 1, 0);
        return 1;
    }
    int head_only = httpd_equal(method, "HEAD");
    for (uint32_t i = 0; target[i]; i++) {
        if (target[i] == '%' || target[i] == '\\' || (uint8_t)target[i] < 32) {
            static const uint8_t bad[] = "Bad Request\n";
            httpd_send_response(client, 400, "Bad Request", "text/plain; charset=utf-8", bad, sizeof(bad) - 1, 0);
            return 1;
        }
        if (target[i] == '.' && target[i + 1] == '.') {
            static const uint8_t bad[] = "Bad Request\n";
            httpd_send_response(client, 400, "Bad Request", "text/plain; charset=utf-8", bad, sizeof(bad) - 1, 0);
            return 1;
        }
    }
    uint32_t path_used = 0;
    path[0] = 0;
    httpd_append(path, sizeof(path), &path_used, server->root);
    if (path_used && path[path_used - 1] == '/' && target[0] == '/') path_used--;
    httpd_append(path, sizeof(path), &path_used, target);
    uint32_t plen = strlen(path);
    if (plen && path[plen - 1] == '/') httpd_append(path, sizeof(path), &path_used, "index.html");
    struct fs_node* node = fs_resolve_path(path);
    if (!node || node->type != FS_FILE) {
        static const uint8_t missing[] = "Not Found\n";
        httpd_send_response(client, 404, "Not Found", "text/plain; charset=utf-8", missing, sizeof(missing) - 1, head_only);
        return 1;
    }
    const uint8_t* body = (const uint8_t*)node->content;
    uint32_t body_len = node->content_len > 0 ? (uint32_t)node->content_len : 0;
    httpd_send_response(client, 200, "OK", httpd_mime(path), body, body_len, head_only);
    return 1;
}

static int httpd_rearm_slot(uint32_t slot) {
    if (slot >= HTTPD_SERVER_LIMIT || !httpd_servers[slot].configured) return -1;
    if (!httpd_servers[slot].desired) return -1;
    if (ssh_server_is_active() && ssh_server_listen_port() == httpd_servers[slot].port) return -1;
    if (net_tcp_listen(httpd_servers[slot].port) != 0) {
        httpd_servers[slot].active = 0;
        return -1;
    }
    for (int i = 0; i < HTTPD_CLIENTS; i++) {
        if (httpd_clients[i].active && httpd_clients[i].server == slot) {
            net_tcp_server_close(httpd_clients[i].handle);
            httpd_clients[i].active = 0;
        }
    }
    httpd_servers[slot].active = 1;
    httpd_servers[slot].desired = 1;
    return 0;
}

int httpd_rearm(void) {
    int started = 0;
    for (uint32_t i = 0; i < HTTPD_SERVER_LIMIT; i++) {
        if (httpd_rearm_slot(i) == 0) started++;
    }
    return started;
}

int httpd_configure(uint32_t slot, uint16_t port, const char* root, int start) {
    if (slot >= HTTPD_SERVER_LIMIT || !port || !root || !root[0] || strlen(root) >= sizeof(httpd_servers[slot].root)) return -1;
    char root_copy[128];
    strcpy(root_copy, root);
    struct fs_node* directory = fs_resolve_path(root_copy);
    if (!directory || directory->type != FS_DIRECTORY) return -1;
    if (ssh_server_is_active() && ssh_server_listen_port() == port) return -1;
    for (uint32_t i = 0; i < HTTPD_SERVER_LIMIT; i++) {
        if (i != slot && httpd_servers[i].configured && httpd_servers[i].port == port) return -1;
    }
    if (httpd_servers[slot].active) httpd_stop_server(slot);
    httpd_servers[slot].port = port;
    strcpy(httpd_servers[slot].root, root_copy);
    httpd_servers[slot].configured = 1;
    if (start) {
        if (!net_has_nic() || net_tcp_listen(port) != 0) {
            httpd_servers[slot].active = 0;
            httpd_servers[slot].desired = 0;
            return -1;
        }
        httpd_servers[slot].active = 1;
        httpd_servers[slot].desired = 1;
    } else {
        httpd_servers[slot].desired = 0;
    }
    return 0;
}

int httpd_start(uint16_t port) {
    if (!port || !net_has_nic()) return -1;
    for (uint32_t i = 0; i < HTTPD_SERVER_LIMIT; i++) {
        if (httpd_servers[i].configured && httpd_servers[i].port == port) return httpd_configure(i, port, httpd_servers[i].root, 1);
    }
    for (uint32_t i = 0; i < HTTPD_SERVER_LIMIT; i++) {
        if (!httpd_servers[i].configured) return httpd_configure(i, port, "/User/www", 1);
    }
    return -1;
}

void httpd_stop_server(uint32_t slot) {
    if (slot >= HTTPD_SERVER_LIMIT) return;
    httpd_servers[slot].desired = 0;
    if (!httpd_servers[slot].active) return;
    uint16_t port = httpd_servers[slot].port;
    net_tcp_server_stop(port);
    for (int i = 0; i < HTTPD_CLIENTS; i++) {
        if (httpd_clients[i].active && httpd_clients[i].server == slot) {
            net_tcp_server_close(httpd_clients[i].handle);
            httpd_clients[i].active = 0;
        }
    }
    httpd_servers[slot].active = 0;
}

int httpd_remove(uint32_t slot) {
    if (slot >= HTTPD_SERVER_LIMIT || !httpd_servers[slot].configured) return -1;
    httpd_stop_server(slot);
    memset(&httpd_servers[slot], 0, sizeof(httpd_servers[slot]));
    return 0;
}

int httpd_server_active(uint32_t slot) { return slot < HTTPD_SERVER_LIMIT && httpd_servers[slot].active; }
uint16_t httpd_server_port(uint32_t slot) { return slot < HTTPD_SERVER_LIMIT && httpd_servers[slot].configured ? httpd_servers[slot].port : 0; }
const char* httpd_server_root(uint32_t slot) { return slot < HTTPD_SERVER_LIMIT && httpd_servers[slot].configured ? httpd_servers[slot].root : ""; }

void httpd_stop(void) {
    for (uint32_t i = 0; i < HTTPD_SERVER_LIMIT; i++) httpd_stop_server(i);
}

int httpd_is_active(void) {
    for (uint32_t i = 0; i < HTTPD_SERVER_LIMIT; i++) if (httpd_servers[i].active) return 1;
    return 0;
}

uint16_t httpd_listen_port(void) {
    for (uint32_t i = 0; i < HTTPD_SERVER_LIMIT; i++) if (httpd_servers[i].active) return httpd_servers[i].port;
    return 0;
}

void httpd_stats(uint32_t* accepted, uint32_t* dropped, uint32_t* slow) {
    if (accepted) *accepted = httpd_accepted;
    if (dropped) *dropped = httpd_dropped;
    if (slow) *slow = httpd_slow;
}

static void httpd_relisten(uint32_t slot) {
    httpd_server_t* server = &httpd_servers[slot];
    if (!server->active || !net_has_nic()) return;
    if (net_tcp_is_listening(server->port)) return;
    if (ssh_server_is_active() && ssh_server_listen_port() == server->port) return;
    net_tcp_listen(server->port);
}

void httpd_poll(void) {
    for (uint32_t server = 0; server < HTTPD_SERVER_LIMIT; server++) {
        if (!httpd_servers[server].active) continue;
        httpd_relisten(server);
        for (;;) {
            int handle = net_tcp_server_accept(httpd_servers[server].port);
            if (handle < 0) break;
            int stored = 0;
            for (int i = 0; i < HTTPD_CLIENTS; i++) {
                if (!httpd_clients[i].active) {
                    httpd_clients[i].handle = handle;
                    httpd_clients[i].server = (uint8_t)server;
                    httpd_clients[i].used = 0;
                    httpd_clients[i].opened = uptime_ticks;
                    httpd_clients[i].request[0] = 0;
                    httpd_clients[i].active = 1;
                    httpd_accepted++;
                    stored = 1;
                    break;
                }
            }
            if (!stored) {
                httpd_dropped++;
                net_tcp_server_close(handle);
            }
        }
    }
    uint8_t temp[512];
    for (int i = 0; i < HTTPD_CLIENTS; i++) {
        httpd_client_t* client = &httpd_clients[i];
        if (!client->active) continue;

        if (uptime_ticks - client->opened > HTTPD_HEADER_TIMEOUT_MS) {
            httpd_slow++;
            net_tcp_server_close(client->handle);
            client->active = 0;
            continue;
        }

        int got = net_tcp_server_recv(client->handle, temp, sizeof(temp), 0);
        if (got > 0) {
            if ((uint32_t)got > sizeof(client->request) - 1 - client->used) {
                static const uint8_t too_big[] = "Request Header Fields Too Large\n";
                httpd_send_response(client, 431, "Request Header Fields Too Large",
                                    "text/plain; charset=utf-8", too_big, sizeof(too_big) - 1, 0);
                net_tcp_server_close(client->handle);
                client->active = 0;
                continue;
            }
            for (int j = 0; j < got; j++) client->request[client->used++] = (char)temp[j];
            client->request[client->used] = 0;
            if (strstr(client->request, "\r\n\r\n")) {
                httpd_finish_request(client);
                net_tcp_server_close(client->handle);
                client->active = 0;
            }
        } else if (got < 0 || net_tcp_server_closed(client->handle)) {
            net_tcp_server_close(client->handle);
            client->active = 0;
        }
    }
}
