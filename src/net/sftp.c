#include "kernel.h"
#include "sftp.h"

#define SFTP_MAX_PACKET 65536u
#define SFTP_SEND_MAX_PACKET 8192u
#define SFTP_FXF_READ 1u
#define SFTP_FXF_WRITE 2u
#define SFTP_FXF_APPEND 4u
#define SFTP_FXF_CREAT 8u
#define SFTP_FXF_TRUNC 16u
#define SFTP_FXF_EXCL 32u
#define SFTP_FX_OK 0u
#define SFTP_FX_EOF 1u
#define SFTP_FX_NO_SUCH_FILE 2u
#define SFTP_FX_PERMISSION_DENIED 3u
#define SFTP_FX_FAILURE 4u
#define SFTP_FX_BAD_MESSAGE 5u
#define SFTP_FX_OP_UNSUPPORTED 8u

struct sftp_reader {
    const uint8_t* data;
    uint32_t length;
    uint32_t position;
    uint8_t bad;
};

struct sftp_writer {
    uint8_t* data;
    uint32_t length;
    uint32_t capacity;
    uint8_t bad;
};

static uint32_t sftp_read_u32(struct sftp_reader* r) {
    if (r->position > r->length || r->length - r->position < 4) {
        r->bad = 1;
        return 0;
    }
    uint32_t v = ((uint32_t)r->data[r->position] << 24) | ((uint32_t)r->data[r->position + 1] << 16) |
                 ((uint32_t)r->data[r->position + 2] << 8) | r->data[r->position + 3];
    r->position += 4;
    return v;
}

static uint64_t sftp_read_u64(struct sftp_reader* r) {
    uint64_t hi = sftp_read_u32(r);
    uint64_t lo = sftp_read_u32(r);
    return (hi << 32) | lo;
}

static const uint8_t* sftp_read_string(struct sftp_reader* r, uint32_t* length) {
    uint32_t n = sftp_read_u32(r);
    if (r->bad || r->position > r->length || n > r->length - r->position) {
        r->bad = 1;
        *length = 0;
        return NULL;
    }
    const uint8_t* result = r->data + r->position;
    r->position += n;
    *length = n;
    return result;
}

static void sftp_write_bytes(struct sftp_writer* w, const void* data, uint32_t length) {
    if (w->bad || w->length > w->capacity || length > w->capacity - w->length) {
        w->bad = 1;
        return;
    }
    if (length) memcpy(w->data + w->length, data, length);
    w->length += length;
}

static void sftp_write_u32(struct sftp_writer* w, uint32_t v) {
    uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    sftp_write_bytes(w, b, 4);
}

static void sftp_write_u64(struct sftp_writer* w, uint64_t v) {
    sftp_write_u32(w, (uint32_t)(v >> 32));
    sftp_write_u32(w, (uint32_t)v);
}

static void sftp_write_string(struct sftp_writer* w, const void* data, uint32_t length) {
    sftp_write_u32(w, length);
    sftp_write_bytes(w, data, length);
}

static void sftp_write_text(struct sftp_writer* w, const char* text) {
    sftp_write_string(w, text, (uint32_t)strlen(text));
}

static int sftp_prefix(const char* text, const char* prefix) {
    while (*prefix) if (*text++ != *prefix++) return 0;
    return 1;
}

static void sftp_append(char* output, const char* text, uint32_t capacity) {
    uint32_t length = (uint32_t)strlen(output);
    while (*text && length + 1 < capacity) output[length++] = *text++;
    output[length] = 0;
}

static int sftp_send_packet(sftp_send_fn send, void* context, uint8_t type, const uint8_t* body, uint32_t body_length) {
    uint8_t packet[SFTP_SEND_MAX_PACKET + 4];
    uint32_t payload_length = body_length + 1;
    if (payload_length > SFTP_SEND_MAX_PACKET) return -1;
    packet[0] = (uint8_t)(payload_length >> 24);
    packet[1] = (uint8_t)(payload_length >> 16);
    packet[2] = (uint8_t)(payload_length >> 8);
    packet[3] = (uint8_t)payload_length;
    packet[4] = type;
    if (body_length) memcpy(packet + 5, body, body_length);
    return send(context, packet, payload_length + 4);
}

static int sftp_send_status(sftp_send_fn send, void* context, uint32_t id, uint32_t code, const char* message) {
    uint8_t body[256];
    struct sftp_writer w = {body, 0, sizeof(body), 0};
    sftp_write_u32(&w, id);
    sftp_write_u32(&w, code);
    sftp_write_text(&w, message);
    sftp_write_text(&w, "");
    return w.bad ? -1 : sftp_send_packet(send, context, 101, body, w.length);
}

static void sftp_write_attrs(struct sftp_writer* w, const struct fs_node* node) {
    uint32_t flags = 1u | 4u;
    uint32_t permissions = node->type == FS_DIRECTORY ? 0040755u : 0100644u;
    sftp_write_u32(w, flags);
    sftp_write_u64(w, node->type == FS_FILE ? (uint64_t)node->content_len : 0);
    sftp_write_u32(w, permissions);
}

static int sftp_send_attrs(sftp_send_fn send, void* context, uint32_t id, const struct fs_node* node) {
    uint8_t body[32];
    struct sftp_writer w = {body, 0, sizeof(body), 0};
    sftp_write_u32(&w, id);
    sftp_write_attrs(&w, node);
    return w.bad ? -1 : sftp_send_packet(send, context, 105, body, w.length);
}

static int sftp_send_name(sftp_send_fn send, void* context, uint32_t id, const char* name, const char* longname, const struct fs_node* node) {
    uint8_t body[512];
    struct sftp_writer w = {body, 0, sizeof(body), 0};
    sftp_write_u32(&w, id);
    sftp_write_u32(&w, 1);
    sftp_write_text(&w, name);
    sftp_write_text(&w, longname);
    sftp_write_attrs(&w, node);
    return w.bad ? -1 : sftp_send_packet(send, context, 104, body, w.length);
}

static int sftp_path_copy(const uint8_t* data, uint32_t length, char* output, uint32_t capacity) {
    if (!data || !length || length >= capacity) return -1;
    for (uint32_t i = 0; i < length; i++) {
        if (!data[i] || data[i] < 32 || data[i] == '\\') return -1;
        output[i] = (char)data[i];
    }
    output[length] = 0;
    return 0;
}

static struct fs_node* sftp_resolve(sftp_session_t* s, const char* path) {
    if (!s || !s->root || !path) return NULL;
    struct fs_node* node = path[0] == '/' ? s->root : (s->cwd ? s->cwd : s->root);
    const char* p = path;
    if (path[0] == '/') {
        if (sftp_prefix(path, "/User/www") && (path[9] == 0 || path[9] == '/')) p = path + 9;
        else p = path + 1;
    }
    char part[32];
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        uint32_t n = 0;
        while (*p && *p != '/') {
            if (n >= sizeof(part) - 1) return NULL;
            part[n++] = *p++;
        }
        part[n] = 0;
        if (strcmp(part, ".") == 0) continue;
        if (strcmp(part, "..") == 0) {
            if (node != s->root && node->parent) node = node->parent;
            continue;
        }
        if (!node || node->type != FS_DIRECTORY) return NULL;
        node = find_node(node, part);
        if (!node) return NULL;
    }
    return node;
}

static int sftp_parent_leaf(sftp_session_t* s, const char* path, struct fs_node** parent, char* leaf) {
    uint32_t len = (uint32_t)strlen(path);
    if (!len || len >= 256 || path[len - 1] == '/') return -1;
    uint32_t split = len;
    while (split && path[split - 1] != '/') split--;
    const char* base = path + split;
    uint32_t base_len = len - split;
    if (!base_len || base_len >= 32 || strcmp(base, ".") == 0 || strcmp(base, "..") == 0) return -1;
    memcpy(leaf, base, base_len);
    leaf[base_len] = 0;
    if (!split) *parent = s->cwd ? s->cwd : s->root;
    else if (split == 1) *parent = s->root;
    else {
        char directory[256];
        uint32_t directory_len = split - 1;
        if (directory_len >= sizeof(directory)) return -1;
        memcpy(directory, path, directory_len);
        directory[directory_len] = 0;
        *parent = sftp_resolve(s, directory);
    }
    return *parent && (*parent)->type == FS_DIRECTORY ? 0 : -1;
}

static int sftp_is_descendant(const struct fs_node* node, const struct fs_node* ancestor) {
    while (node) {
        if (node == ancestor) return 1;
        node = node->parent;
    }
    return 0;
}

static sftp_handle_t* sftp_handle_find(sftp_session_t* s, const uint8_t* id_data, uint32_t length) {
    if (!id_data || length != 4) return NULL;
    uint32_t id = ((uint32_t)id_data[0] << 24) | ((uint32_t)id_data[1] << 16) | ((uint32_t)id_data[2] << 8) | id_data[3];
    for (uint32_t i = 0; i < 8; i++) if (s->handles[i].active && s->handles[i].id == id) return &s->handles[i];
    return NULL;
}

static sftp_handle_t* sftp_handle_new(sftp_session_t* s, struct fs_node* node, uint8_t directory, uint8_t writable) {
    for (uint32_t i = 0; i < 8; i++) {
        if (!s->handles[i].active) {
            sftp_handle_t* h = &s->handles[i];
            memset(h, 0, sizeof(*h));
            h->active = 1;
            h->directory = directory;
            h->writable = writable;
            h->node = node;
            h->id = ++s->next_handle;
            if (!h->id) h->id = ++s->next_handle;
            return h;
        }
    }
    return NULL;
}

static int sftp_send_handle(sftp_send_fn send, void* context, uint32_t request_id, sftp_handle_t* h) {
    uint8_t body[16];
    struct sftp_writer w = {body, 0, sizeof(body), 0};
    uint8_t token[4] = {(uint8_t)(h->id >> 24), (uint8_t)(h->id >> 16), (uint8_t)(h->id >> 8), (uint8_t)h->id};
    sftp_write_u32(&w, request_id);
    sftp_write_string(&w, token, sizeof(token));
    return w.bad ? -1 : sftp_send_packet(send, context, 102, body, w.length);
}

static int sftp_attrs_skip(struct sftp_reader* r) {
    uint32_t flags = sftp_read_u32(r);
    if (r->bad) return -1;
    if (flags & 1u) sftp_read_u64(r);
    if (flags & 2u) { sftp_read_u32(r); sftp_read_u32(r); }
    if (flags & 4u) sftp_read_u32(r);
    if (flags & 8u) { sftp_read_u32(r); sftp_read_u32(r); }
    if (flags & 0x80000000u) {
        uint32_t count = sftp_read_u32(r);
        for (uint32_t i = 0; i < count && !r->bad; i++) {
            uint32_t name_len;
            sftp_read_string(r, &name_len);
            uint32_t data_len;
            sftp_read_string(r, &data_len);
        }
    }
    return r->bad ? -1 : 0;
}

static int sftp_handle_request(sftp_session_t* s, const uint8_t* packet, uint32_t length, sftp_send_fn send, void* context) {
    if (!length) return -1;
    uint8_t type = packet[0];
    struct sftp_reader r = {packet + 1, length - 1, 0, 0};
    if (type == 1) {
        uint32_t version = sftp_read_u32(&r);
        if (r.bad || r.position != r.length || version < 3) return -1;
        s->version = 3;
        s->initialized = 1;
        uint8_t body[4] = {0, 0, 0, 3};
        return sftp_send_packet(send, context, 2, body, sizeof(body));
    }
    if (!s->initialized) return -1;
    uint32_t id = sftp_read_u32(&r);
    if (r.bad) return -1;
    if (type == 3) {
        uint32_t path_length;
        const uint8_t* path_bytes = sftp_read_string(&r, &path_length);
        uint32_t flags = sftp_read_u32(&r);
        if (sftp_attrs_skip(&r) != 0 || r.position != r.length) return sftp_send_status(send, context, id, SFTP_FX_BAD_MESSAGE, "bad open request");
        char path[256];
        if (sftp_path_copy(path_bytes, path_length, path, sizeof(path)) != 0) return sftp_send_status(send, context, id, SFTP_FX_FAILURE, "invalid path");
        struct fs_node* node = sftp_resolve(s, path);
        if (!node && (flags & SFTP_FXF_CREAT)) {
            struct fs_node* parent;
            char leaf[32];
            if (sftp_parent_leaf(s, path, &parent, leaf) != 0 || find_node(parent, leaf)) return sftp_send_status(send, context, id, SFTP_FX_FAILURE, "cannot create file");
            node = create_node(leaf, FS_FILE, parent);
        } else if (node && (flags & SFTP_FXF_CREAT) && (flags & SFTP_FXF_EXCL)) {
            return sftp_send_status(send, context, id, SFTP_FX_FAILURE, "file exists");
        }
        if (!node || node->type != FS_FILE) return sftp_send_status(send, context, id, SFTP_FX_NO_SUCH_FILE, "file not found");
        if (flags & SFTP_FXF_TRUNC) {
            if (!(flags & SFTP_FXF_WRITE) || fs_node_truncate(node, 0) != 0) return sftp_send_status(send, context, id, SFTP_FX_FAILURE, "cannot truncate");
        }
        sftp_handle_t* handle = sftp_handle_new(s, node, 0, (flags & SFTP_FXF_WRITE) != 0);
        return handle ? sftp_send_handle(send, context, id, handle) : sftp_send_status(send, context, id, SFTP_FX_FAILURE, "too many open files");
    }
    if (type == 4) {
        uint32_t n;
        const uint8_t* token = sftp_read_string(&r, &n);
        sftp_handle_t* h = sftp_handle_find(s, token, n);
        if (r.bad || r.position != r.length || !h) return sftp_send_status(send, context, id, SFTP_FX_FAILURE, "invalid handle");
        h->active = 0;
        return sftp_send_status(send, context, id, SFTP_FX_OK, "");
    }
    if (type == 5) {
        uint32_t n;
        const uint8_t* token = sftp_read_string(&r, &n);
        uint64_t offset = sftp_read_u64(&r);
        uint32_t requested = sftp_read_u32(&r);
        sftp_handle_t* h = sftp_handle_find(s, token, n);
        if (r.bad || r.position != r.length || !h || h->directory || offset > FS_MAX_FILE_SIZE) return sftp_send_status(send, context, id, SFTP_FX_FAILURE, "invalid read");
        if (offset >= (uint64_t)h->node->content_len) return sftp_send_status(send, context, id, SFTP_FX_EOF, "eof");
        uint32_t available = (uint32_t)h->node->content_len - (uint32_t)offset;
        uint32_t count = requested;
        if (count > available) count = available;
        if (count > 4096) count = 4096;
        uint8_t body[4112];
        struct sftp_writer w = {body, 0, sizeof(body), 0};
        sftp_write_u32(&w, id);
        sftp_write_string(&w, h->node->content + (uint32_t)offset, count);
        return w.bad ? -1 : sftp_send_packet(send, context, 103, body, w.length);
    }
    if (type == 6) {
        uint32_t n;
        const uint8_t* token = sftp_read_string(&r, &n);
        uint64_t offset = sftp_read_u64(&r);
        uint32_t data_length;
        const uint8_t* data = sftp_read_string(&r, &data_length);
        sftp_handle_t* h = sftp_handle_find(s, token, n);
        if (r.bad || r.position != r.length || !h || h->directory || !h->writable || offset > FS_MAX_FILE_SIZE || data_length > FS_MAX_FILE_SIZE - (uint32_t)offset)
            return sftp_send_status(send, context, id, SFTP_FX_PERMISSION_DENIED, "write denied");
        if (fs_node_write(h->node, (uint32_t)offset, data, data_length) != 0) return sftp_send_status(send, context, id, SFTP_FX_FAILURE, "write failed");
        return sftp_send_status(send, context, id, SFTP_FX_OK, "");
    }
    if (type == 7 || type == 17) {
        uint32_t n;
        const uint8_t* path_bytes = sftp_read_string(&r, &n);
        char path[256];
        if (r.bad || r.position != r.length || sftp_path_copy(path_bytes, n, path, sizeof(path)) != 0) return sftp_send_status(send, context, id, SFTP_FX_BAD_MESSAGE, "bad path");
        struct fs_node* node = sftp_resolve(s, path);
        return node ? sftp_send_attrs(send, context, id, node) : sftp_send_status(send, context, id, SFTP_FX_NO_SUCH_FILE, "not found");
    }
    if (type == 8 || type == 10) {
        uint32_t n;
        const uint8_t* token = sftp_read_string(&r, &n);
        sftp_handle_t* h = sftp_handle_find(s, token, n);
        if (type == 10 && sftp_attrs_skip(&r) != 0) return sftp_send_status(send, context, id, SFTP_FX_BAD_MESSAGE, "bad attributes");
        if (r.bad || r.position != r.length || !h) return sftp_send_status(send, context, id, SFTP_FX_FAILURE, "invalid handle");
        return sftp_send_attrs(send, context, id, h->node);
    }
    if (type == 9) {
        if (sftp_attrs_skip(&r) != 0 || r.position != r.length) return sftp_send_status(send, context, id, SFTP_FX_BAD_MESSAGE, "bad attributes");
        return sftp_send_status(send, context, id, SFTP_FX_OK, "");
    }
    if (type == 11) {
        uint32_t n;
        const uint8_t* path_bytes = sftp_read_string(&r, &n);
        char path[256];
        if (r.bad || r.position != r.length || sftp_path_copy(path_bytes, n, path, sizeof(path)) != 0) return sftp_send_status(send, context, id, SFTP_FX_BAD_MESSAGE, "bad path");
        struct fs_node* node = sftp_resolve(s, path);
        if (!node || node->type != FS_DIRECTORY) return sftp_send_status(send, context, id, SFTP_FX_NO_SUCH_FILE, "not a directory");
        sftp_handle_t* h = sftp_handle_new(s, node, 1, 0);
        return h ? sftp_send_handle(send, context, id, h) : sftp_send_status(send, context, id, SFTP_FX_FAILURE, "too many open directories");
    }
    if (type == 12) {
        uint32_t n;
        const uint8_t* token = sftp_read_string(&r, &n);
        sftp_handle_t* h = sftp_handle_find(s, token, n);
        if (r.bad || r.position != r.length || !h || !h->directory) return sftp_send_status(send, context, id, SFTP_FX_FAILURE, "invalid directory handle");
        if (h->index >= (uint32_t)h->node->num_children) return sftp_send_status(send, context, id, SFTP_FX_EOF, "eof");
        struct fs_node* child = h->node->children[h->index++];
        char longname[96];
        char size[16];
        int_to_string(child->type == FS_FILE ? (uint32_t)child->content_len : 0, size);
        strcpy(longname, child->type == FS_DIRECTORY ? "drwxr-xr-x 1 shark shark " : "-rw-r--r-- 1 shark shark ");
        sftp_append(longname, size, sizeof(longname));
        sftp_append(longname, " ", sizeof(longname));
        sftp_append(longname, child->name, sizeof(longname));
        return sftp_send_name(send, context, id, child->name, longname, child);
    }
    if (type == 13 || type == 15) {
        uint32_t n;
        const uint8_t* path_bytes = sftp_read_string(&r, &n);
        char path[256];
        if (r.bad || r.position != r.length || sftp_path_copy(path_bytes, n, path, sizeof(path)) != 0) return sftp_send_status(send, context, id, SFTP_FX_BAD_MESSAGE, "bad path");
        struct fs_node* node = sftp_resolve(s, path);
        if (!node || node == s->root || !node->parent) return sftp_send_status(send, context, id, SFTP_FX_NO_SUCH_FILE, "not found");
        if (type == 13 && node->type != FS_FILE) return sftp_send_status(send, context, id, SFTP_FX_FAILURE, "not a file");
        if (type == 15 && (node->type != FS_DIRECTORY || node->num_children)) return sftp_send_status(send, context, id, SFTP_FX_FAILURE, "directory not empty");
        if (fs_node_remove(node) != 0) return sftp_send_status(send, context, id, SFTP_FX_FAILURE, "remove failed");
        return sftp_send_status(send, context, id, SFTP_FX_OK, "");
    }
    if (type == 14) {
        uint32_t n;
        const uint8_t* path_bytes = sftp_read_string(&r, &n);
        if (sftp_attrs_skip(&r) != 0 || r.position != r.length) return sftp_send_status(send, context, id, SFTP_FX_BAD_MESSAGE, "bad mkdir request");
        char path[256], leaf[32];
        struct fs_node* parent;
        if (sftp_path_copy(path_bytes, n, path, sizeof(path)) != 0 || sftp_parent_leaf(s, path, &parent, leaf) != 0 || find_node(parent, leaf))
            return sftp_send_status(send, context, id, SFTP_FX_FAILURE, "cannot create directory");
        struct fs_node* node = create_node(leaf, FS_DIRECTORY, parent);
        return node ? sftp_send_status(send, context, id, SFTP_FX_OK, "") : sftp_send_status(send, context, id, SFTP_FX_FAILURE, "directory create failed");
    }
    if (type == 16) {
        uint32_t n;
        const uint8_t* path_bytes = sftp_read_string(&r, &n);
        char path[256];
        if (r.bad || r.position != r.length || sftp_path_copy(path_bytes, n, path, sizeof(path)) != 0) return sftp_send_status(send, context, id, SFTP_FX_BAD_MESSAGE, "bad path");
        struct fs_node* node = sftp_resolve(s, path);
        if (!node) return sftp_send_status(send, context, id, SFTP_FX_NO_SUCH_FILE, "not found");
        char canonical[256];
        struct fs_node* parts[64];
        uint32_t count = 0;
        struct fs_node* walk = node;
        while (walk && walk != s->root && count < sizeof(parts) / sizeof(parts[0])) {
            parts[count++] = walk;
            walk = walk->parent;
        }
        if (walk != s->root) return sftp_send_status(send, context, id, SFTP_FX_FAILURE, "path too deep");
        strcpy(canonical, "/User/www");
        for (int32_t i = (int32_t)count - 1; i >= 0; i--) {
            sftp_append(canonical, "/", sizeof(canonical));
            sftp_append(canonical, parts[i]->name, sizeof(canonical));
        }
        uint8_t body[600];
        struct sftp_writer w = {body, 0, sizeof(body), 0};
        sftp_write_u32(&w, id);
        sftp_write_u32(&w, 1);
        sftp_write_text(&w, canonical);
        sftp_write_text(&w, canonical);
        sftp_write_attrs(&w, node);
        return w.bad ? -1 : sftp_send_packet(send, context, 104, body, w.length);
    }
    if (type == 18) {
        uint32_t old_length, new_length;
        const uint8_t* old_bytes = sftp_read_string(&r, &old_length);
        const uint8_t* new_bytes = sftp_read_string(&r, &new_length);
        char old_path[256], new_path[256], leaf[32];
        struct fs_node* new_parent;
        struct fs_node* node;
        if (r.bad || r.position != r.length || sftp_path_copy(old_bytes, old_length, old_path, sizeof(old_path)) != 0 ||
            sftp_path_copy(new_bytes, new_length, new_path, sizeof(new_path)) != 0 ||
            !(node = sftp_resolve(s, old_path)) || node == s->root || !node->parent ||
            sftp_parent_leaf(s, new_path, &new_parent, leaf) != 0 || sftp_resolve(s, new_path) ||
            (node->type == FS_DIRECTORY && sftp_is_descendant(new_parent, node)))
            return sftp_send_status(send, context, id, SFTP_FX_FAILURE, "rename failed");
        struct fs_node* old_parent = node->parent;
        for (uint32_t i = 0; i < (uint32_t)old_parent->num_children; i++) {
            if (old_parent->children[i] == node) {
                for (uint32_t j = i; j + 1 < (uint32_t)old_parent->num_children; j++) old_parent->children[j] = old_parent->children[j + 1];
                old_parent->num_children--;
                break;
            }
        }
        if (new_parent->num_children >= MAX_CHILDREN) {
            node->parent = old_parent;
            old_parent->children[old_parent->num_children++] = node;
            return sftp_send_status(send, context, id, SFTP_FX_FAILURE, "destination directory full");
        }
        strcpy(node->name, leaf);
        node->parent = new_parent;
        new_parent->children[new_parent->num_children++] = node;
        fs_cache_clear();
        return sftp_send_status(send, context, id, SFTP_FX_OK, "");
    }
    return sftp_send_status(send, context, id, SFTP_FX_OP_UNSUPPORTED, "operation unsupported");
}

void sftp_session_init(sftp_session_t* session, struct fs_node* root) {
    memset(session, 0, sizeof(*session));
    session->root = root;
    session->cwd = root;
    session->next_handle = 1;
}

int sftp_session_feed(sftp_session_t* session, const uint8_t* data, uint32_t length, sftp_send_fn send, void* context) {
    if (!session || !send || length > sizeof(session->input) - session->input_length) return -1;
    if (length) memcpy(session->input + session->input_length, data, length);
    session->input_length += length;
    while (session->input_length >= 4) {
        uint32_t packet_length = ((uint32_t)session->input[0] << 24) | ((uint32_t)session->input[1] << 16) |
                                 ((uint32_t)session->input[2] << 8) | session->input[3];
        if (!packet_length || packet_length > SFTP_MAX_PACKET) return -1;
        if (session->input_length < packet_length + 4) break;
        if (sftp_handle_request(session, session->input + 4, packet_length, send, context) != 0) return -1;
        uint32_t remaining = session->input_length - packet_length - 4;
        for (uint32_t i = 0; i < remaining; i++) session->input[i] = session->input[i + packet_length + 4];
        session->input_length = remaining;
    }
    return 0;
}
