#include "kernel.h"
#include "net.h"
#include "httpd.h"
#include "ssh.h"
#include "server_console.h"
#include "server_cli.h"
#include "containers.h"

static char server_line[256];
static uint32_t server_line_len;
static struct fs_node* server_webroot;
static char server_ssh_username[32];
static uint8_t server_password_mode;
static struct fs_node* server_host_root;
static struct fs_node* server_host_cwd;
static int server_active_container = -1;
static int server_starts_with(const char* text, const char* prefix);
static uint64_t server_parse_memory_map(const struct server_multiboot_info* info, pmm_memory_region_t* regions, uint32_t capacity, uint32_t* count);
static void server_print_memory_map(void);
static const struct server_multiboot_info* server_boot_info;
static uint32_t server_boot_flags;
static uint32_t server_boot_mem_upper;
static uint32_t server_boot_mmap_original_length;
static uint32_t server_boot_mmap_original_address;
static uint32_t server_boot_usable_type = 1;
static uint8_t server_boot_mmap_snapshot[4096];
static uint32_t server_boot_mmap_snapshot_length;

void server_format_path(struct fs_node* node, char* output, uint32_t output_capacity) {
    if (!output || !output_capacity) return;
    char reverse[256];
    uint32_t used = 0;
    uint32_t path_length = 0;
    reverse[used++] = '/';
    while (node && node->parent && used + 2 < sizeof(reverse)) {
        uint32_t length = (uint32_t)strlen(node->name);
        if (used + length + 1 >= sizeof(reverse)) break;
        for (uint32_t i = length; i; i--) reverse[used++] = node->name[i - 1];
        reverse[used++] = '/';
        node = node->parent;
    }
    while (used && path_length + 1 < output_capacity) output[path_length++] = reverse[--used];
    if (path_length > 1 && output[path_length - 1] == '/') path_length--;
    output[path_length] = 0;
}

static void server_prompt(void) {
    char path[256];
    if (server_active_container >= 0) {
        const shark_container_t* container = shark_container_get((uint32_t)server_active_container);
        if (container) {
            server_format_path(container->cwd, path, sizeof(path));
            serial_puts("root@shark[");
            serial_puts(container->name);
            serial_puts("]:");
            serial_puts(path);
            serial_puts("# ");
            return;
        }
        server_active_container = -1;
    }
    server_format_path(server_host_cwd ? server_host_cwd : current_dir, path, sizeof(path));
    serial_puts("root@shark:");
    serial_puts(path);
    serial_puts("# ");
}

static uint32_t server_parse_uint(const char* text, uint32_t fallback) {
    if (!text) return fallback;
    while (*text == ' ') text++;
    if (!*text) return fallback;
    uint32_t value = 0;
    for (uint32_t i = 0; text[i]; i++) {
        if (text[i] < '0' || text[i] > '9') return fallback;
        value = value * 10 + (uint32_t)(text[i] - '0');
        if (value > 65535) return fallback;
    }
    return value ? value : fallback;
}

static void server_print_ip(const uint8_t* ip) {
    for (int i = 0; i < 4; i++) {
        serial_putdec(ip[i]);
        if (i != 3) serial_putc('.');
    }
}

static void server_print_mac(const uint8_t* mac) {
    static const char digits[] = "0123456789abcdef";
    for (uint32_t i = 0; i < 6; i++) {
        serial_putc(digits[mac[i] >> 4]);
        serial_putc(digits[mac[i] & 15]);
        if (i != 5) serial_putc(':');
    }
}

static void server_list(void) {
    if (!current_dir) return;
    for (int i = 0; i < current_dir->num_children; i++) {
        struct fs_node* node = current_dir->children[i];
        serial_puts(node->name);
        if (node->type == FS_DIRECTORY) serial_putc('/');
        serial_puts("\r\n");
    }
}

static void server_cat(const char* name) {
    if (!name || !*name) {
        serial_puts("usage: cat <file>\r\n");
        return;
    }
    struct fs_node* node = fs_resolve_path(name);
    if (!node && current_dir) node = find_node(current_dir, name);
    if (!node || node->type != FS_FILE) {
        serial_puts("file not found\r\n");
        return;
    }
    for (int i = 0; i < node->content_len; i++) serial_putc(node->content[i]);
    if (!node->content_len || node->content[node->content_len - 1] != '\n') serial_puts("\r\n");
}

static void server_write(const char* args) {
    if (!args || !*args) {
        serial_puts("usage: write <file> <single-line content>\r\n");
        return;
    }
    while (*args == ' ') args++;
    const char* split = args;
    while (*split && *split != ' ') split++;
    uint32_t name_len = (uint32_t)(split - args);
    if (!name_len || name_len >= 32 || !server_webroot) {
        serial_puts("invalid file name\r\n");
        return;
    }
    char name[32];
    for (uint32_t i = 0; i < name_len; i++) {
        if (args[i] == '/' || args[i] == '\\' || (args[i] == '.' && args[i + 1] == '.')) {
            serial_puts("invalid file name\r\n");
            return;
        }
        name[i] = args[i];
    }
    name[name_len] = 0;
    while (*split == ' ') split++;
    uint32_t content_len = (uint32_t)strlen(split);
    struct fs_node* node = find_node(server_webroot, name);
    if (node && node->type != FS_FILE) {
        serial_puts("not a regular file\r\n");
        return;
    }
    if (!node) node = create_node(name, FS_FILE, server_webroot);
    if (!node) {
        serial_puts("file table full\r\n");
        return;
    }
    if (content_len > FS_MAX_FILE_SIZE) content_len = FS_MAX_FILE_SIZE;
    if (fs_node_truncate(node, 0) != 0 || fs_node_write(node, 0, split, content_len) != 0) {
        serial_puts("write failed\r\n");
        return;
    }
    serial_puts("saved ");
    serial_puts(name);
    serial_puts("\r\n");
}

static void server_service_poll(void) {
    struct fs_node* saved_root = root;
    struct fs_node* saved_directory = current_dir;
    if (server_host_root) root = server_host_root;
    if (server_host_cwd) current_dir = server_host_cwd;
    net_poll();
    httpd_poll();
    ssh_server_poll();
    root = saved_root;
    current_dir = saved_directory;
}

static int server_read_line_blocking(const char* prompt, char* out, uint32_t capacity) {
    uint32_t used = 0;
    if (!capacity) return -1;
    out[0] = 0;
    terminal_writestring(prompt);
    for (;;) {
        server_service_poll();
        int c = serial_getchar_nonblocking();
        if (c < 0) c = server_console_getchar_nonblocking();
        if (c < 0) { asm volatile("hlt"); continue; }
        if (c == 27) { terminal_writestring("\n"); return -1; }
        if (c == '\r' || c == '\n') { terminal_writestring("\n"); out[used] = 0; return (int)used; }
        if (c == 8 || c == 127) {
            if (used) { used--; out[used] = 0; terminal_writestring("\b \b"); }
            continue;
        }
        if (c >= 32 && c <= 126 && used + 1 < capacity) {
            out[used++] = (char)c; out[used] = 0; terminal_putchar((char)c);
        }
    }
}

static char server_editor_buffer[MAX_FILE_CONTENT_SIZE];
static uint32_t server_editor_length;
static uint32_t server_editor_cursor;
static struct fs_node* server_editor_file;
static uint8_t server_editor_dirty;

static void server_editor_move(char* destination, const char* source, uint32_t length) {
    if (destination < source) for (uint32_t i = 0; i < length; i++) destination[i] = source[i];
    else if (destination > source) for (uint32_t i = length; i; i--) destination[i - 1] = source[i - 1];
}

static void server_nano_render(void) {
    terminal_clear();
    terminal_writestring("GNU nano-like editor   Ctrl+S Save   Ctrl+X Exit\n");
    terminal_writestring(server_editor_file ? server_editor_file->name : "untitled");
    terminal_writestring(server_editor_dirty ? "  [modified]\n\n" : "\n\n");
    uint32_t shown = 0;
    for (uint32_t i = 0; i <= server_editor_length && shown < 21; i++) {
        if (i == server_editor_cursor) terminal_putchar('|');
        if (i == server_editor_length) break;
        char c = server_editor_buffer[i];
        if (c == '\n') { terminal_writestring("\n"); shown++; }
        else if (c == '\t') terminal_writestring("    ");
        else if ((uint8_t)c >= 32 && (uint8_t)c <= 126) terminal_putchar(c);
    }
    terminal_writestring("\n\nLine ");
    uint32_t current_line = 1;
    for (uint32_t i = 0; i < server_editor_cursor; i++) if (server_editor_buffer[i] == '\n') current_line++;
    char number[16]; int_to_string(current_line, number); terminal_writestring(number);
    terminal_writestring(" | Ctrl+S writes changes; Ctrl+X exits\n");
}

static void server_nano_save(void) {
    if (!server_editor_file || fs_node_truncate(server_editor_file, 0) != 0 ||
        fs_node_write(server_editor_file, 0, server_editor_buffer, server_editor_length) != 0 ||
        fs_node_sync(server_editor_file) == 0) {
        terminal_writestring("\nSave failed\n");
        return;
    }
    server_editor_dirty = 0;
}

static struct fs_node* server_nano_open(const char* path) {
    if (!path || !*path) return NULL;
    struct fs_node* node = fs_resolve_path(path);
    if (node) return node->type == FS_FILE ? node : NULL;
    char parent_path[128];
    char name[32];
    uint32_t length = (uint32_t)strlen(path);
    if (length >= sizeof(parent_path)) return NULL;
    uint32_t slash = length;
    while (slash && path[slash - 1] != '/') slash--;
    const char* base = path + slash;
    uint32_t name_length = (uint32_t)strlen(base);
    if (!name_length || name_length >= sizeof(name)) return NULL;
    memcpy(name, base, name_length + 1);
    struct fs_node* parent = current_dir;
    if (slash) {
        uint32_t parent_length = slash > 1 ? slash - 1 : 1;
        memcpy(parent_path, path, parent_length);
        parent_path[parent_length] = 0;
        parent = fs_resolve_path(parent_path);
    }
    if (!parent || parent->type != FS_DIRECTORY) return NULL;
    return create_node(name, FS_FILE, parent);
}

static void server_nano(const char* path) {
    struct fs_node* file = server_nano_open(path);
    if (!file) { terminal_writestring("nano: cannot open file\n"); return; }
    server_editor_file = file;
    server_editor_length = file->content_len > 0 ? (uint32_t)file->content_len : 0;
    if (server_editor_length >= sizeof(server_editor_buffer)) server_editor_length = sizeof(server_editor_buffer) - 1;
    if (server_editor_length) memcpy(server_editor_buffer, fs_node_data(file), server_editor_length);
    server_editor_buffer[server_editor_length] = 0;
    server_editor_cursor = server_editor_length;
    server_editor_dirty = 0;
    server_nano_render();
    for (;;) {
        server_service_poll();
        int c = serial_getchar_nonblocking();
        if (c < 0) c = server_console_getchar_nonblocking();
        if (c < 0) { asm volatile("hlt"); continue; }
        if (c == 19 || c == 15) { server_nano_save(); server_nano_render(); continue; }
        if (c == 24) { if (server_editor_dirty) server_nano_save(); break; }
        if (c == 0x83) { if (server_editor_cursor) server_editor_cursor--; }
        else if (c == 0x82) { if (server_editor_cursor < server_editor_length) server_editor_cursor++; }
        else if (c == 0x80) { while (server_editor_cursor && server_editor_buffer[server_editor_cursor - 1] != '\n') server_editor_cursor--; }
        else if (c == 0x81) { while (server_editor_cursor < server_editor_length && server_editor_buffer[server_editor_cursor] != '\n') server_editor_cursor++; if (server_editor_cursor < server_editor_length) server_editor_cursor++; }
        else if (c == 8 || c == 127) {
            if (server_editor_cursor) { server_editor_move(server_editor_buffer + server_editor_cursor - 1, server_editor_buffer + server_editor_cursor, server_editor_length - server_editor_cursor); server_editor_cursor--; server_editor_length--; server_editor_dirty = 1; }
        } else if (c == 0x84) {
            if (server_editor_cursor < server_editor_length) { server_editor_move(server_editor_buffer + server_editor_cursor, server_editor_buffer + server_editor_cursor + 1, server_editor_length - server_editor_cursor - 1); server_editor_length--; server_editor_dirty = 1; }
        } else if (c == '\r' || c == '\n' || (c >= 32 && c <= 126) || c == '\t') {
            if (server_editor_length + 1 < sizeof(server_editor_buffer)) {
                char value = (c == '\r') ? '\n' : (char)c;
                server_editor_move(server_editor_buffer + server_editor_cursor + 1, server_editor_buffer + server_editor_cursor, server_editor_length - server_editor_cursor);
                server_editor_buffer[server_editor_cursor++] = value;
                server_editor_length++;
                server_editor_buffer[server_editor_length] = 0;
                server_editor_dirty = 1;
            }
        } else continue;
        server_nano_render();
    }
    terminal_clear();
    terminal_writestring("Editor closed\n");
}

static void server_httpd_tui(void) {
    char input[128];
    for (;;) {
        terminal_clear();
        if (server_output_capture_active()) {
            terminal_writestring("SharkOS HTTP server configuration\n\n");
            for (uint32_t i = 0; i < HTTPD_SERVER_LIMIT; i++) {
                char number[16];
                int_to_string(i + 1, number); terminal_writestring(number); terminal_writestring(". ");
                if (httpd_server_port(i)) {
                    terminal_writestring(httpd_server_active(i) ? "RUNNING  " : "STOPPED  ");
                    int_to_string(httpd_server_port(i), number); terminal_writestring(number);
                    terminal_writestring("  "); terminal_writestring(httpd_server_root(i)); terminal_writestring("\n");
                } else terminal_writestring("<empty>\n");
            }
            terminal_writestring("\nRemote controls:\n");
            terminal_writestring("  httpd tui add <slot 1-4> <port> <directory>\n");
            terminal_writestring("  httpd tui toggle <slot 1-4>\n");
            terminal_writestring("  httpd tui delete <slot 1-4>\n");
            terminal_writestring("  httpd tui\n");
            return;
        }
        terminal_writestring("SharkOS HTTP server configuration\n\n");
        for (uint32_t i = 0; i < HTTPD_SERVER_LIMIT; i++) {
            char number[16];
            int_to_string(i + 1, number); terminal_writestring(number); terminal_writestring(". ");
            if (httpd_server_port(i)) {
                terminal_writestring(httpd_server_active(i) ? "RUNNING  " : "STOPPED  ");
                int_to_string(httpd_server_port(i), number); terminal_writestring(number);
                terminal_writestring("  "); terminal_writestring(httpd_server_root(i)); terminal_writestring("\n");
            } else terminal_writestring("<empty>\n");
        }
        terminal_writestring("\n[a]dd or edit  [t]oggle  [d]elete  [q]uit\n");
        if (server_read_line_blocking("httpd> ", input, sizeof(input)) < 0) return;
        if (input[0] == 'q' || input[0] == 'Q') return;
        if (input[0] == 'a' || input[0] == 'A') {
            if (server_read_line_blocking("slot (1-4): ", input, sizeof(input)) < 0) continue;
            uint32_t slot = server_parse_uint(input, 0); if (!slot || slot > HTTPD_SERVER_LIMIT) continue; slot--;
            char port_text[32], root_path[128];
            if (server_read_line_blocking("port: ", port_text, sizeof(port_text)) < 0) continue;
            uint32_t port = server_parse_uint(port_text, 0); if (!port) { terminal_writestring("invalid port\n"); continue; }
            if (server_read_line_blocking("document directory: ", root_path, sizeof(root_path)) < 0) continue;
            if (!root_path[0]) strcpy(root_path, "/User/www");
            if (httpd_configure(slot, (uint16_t)port, root_path, 1) != 0) terminal_writestring("configuration failed; check port, directory and network adapter\n");
            else terminal_writestring("webserver configured and started\n");
        } else if (input[0] == 't' || input[0] == 'T') {
            if (server_read_line_blocking("slot (1-4): ", input, sizeof(input)) < 0) continue;
            uint32_t slot = server_parse_uint(input, 0); if (!slot || slot > HTTPD_SERVER_LIMIT) continue; slot--;
            uint16_t port = httpd_server_port(slot); const char* root_path = httpd_server_root(slot);
            if (!port) { terminal_writestring("empty slot\n"); continue; }
            if (httpd_configure(slot, port, root_path, !httpd_server_active(slot)) != 0) terminal_writestring("unable to change service state\n");
        } else if (input[0] == 'd' || input[0] == 'D') {
            if (server_read_line_blocking("slot (1-4): ", input, sizeof(input)) < 0) continue;
            uint32_t slot = server_parse_uint(input, 0); if (slot && slot <= HTTPD_SERVER_LIMIT) httpd_remove(slot - 1);
        }
    }
}

static char* server_next_word(char** cursor) {
    while (**cursor == ' ') (*cursor)++;
    if (!**cursor) return NULL;
    char* word = *cursor;
    while (**cursor && **cursor != ' ') (*cursor)++;
    if (**cursor) *(*cursor)++ = 0;
    return word;
}

static void server_httpd_tui_command(char* text) {
    char* cursor = text;
    char* action = server_next_word(&cursor);
    if (!action) { server_httpd_tui(); return; }
    char* slot_text = server_next_word(&cursor);
    uint32_t slot = server_parse_uint(slot_text, 0);
    if (!slot || slot > HTTPD_SERVER_LIMIT) {
        terminal_writestring("slot must be 1 through 4\n");
        return;
    }
    slot--;
    if (strcmp(action, "toggle") == 0) {
        uint16_t port = httpd_server_port(slot);
        const char* root_path = httpd_server_root(slot);
        if (!port) { terminal_writestring("slot is empty\n"); return; }
        if (httpd_configure(slot, port, root_path, !httpd_server_active(slot)) == 0) terminal_writestring("HTTP service updated\n");
        else terminal_writestring("unable to change service state\n");
        return;
    }
    if (strcmp(action, "delete") == 0 || strcmp(action, "remove") == 0) {
        if (httpd_remove(slot) == 0) terminal_writestring("HTTP service removed\n");
        else terminal_writestring("slot is empty\n");
        return;
    }
    if (strcmp(action, "add") == 0 || strcmp(action, "set") == 0) {
        char* port_text = server_next_word(&cursor);
        uint32_t port = server_parse_uint(port_text, 0);
        while (*cursor == ' ') cursor++;
        if (!port || !*cursor) { terminal_writestring("usage: httpd tui add <slot> <port> <directory>\n"); return; }
        if (httpd_configure(slot, (uint16_t)port, cursor, 1) == 0) terminal_writestring("HTTP service configured and started\n");
        else terminal_writestring("configuration failed; check port, directory and network adapter\n");
        return;
    }
    terminal_writestring("usage: httpd tui [add <slot> <port> <directory> | toggle <slot> | delete <slot>]\n");
}

static void server_container_command(char* text) {
    char* cursor = text;
    char* action = server_next_word(&cursor);
    char* name = server_next_word(&cursor);
    if (!action || strcmp(action, "help") == 0) {
        serial_puts("container create <name> | start <name> [-br|-nat] | stop <name> | list | inspect <name> | enter <name> | exit | remove <name>\r\n");
        return;
    }
    if (strcmp(action, "list") == 0) {
        int found = 0;
        for (uint32_t i = 0; i < SHARK_CONTAINER_LIMIT; i++) {
            const shark_container_t* container = shark_container_get(i);
            if (!container) continue;
            found = 1;
            serial_puts(container->name);
            serial_puts(container->state == SHARK_CONTAINER_RUNNING ? "  running (shell)" : "  stopped");
            serial_puts("  network ");
            serial_puts(container->network_mode == SHARK_CONTAINER_NETWORK_NAT ? "nat" : "bridge");
            if (net_vnic_is_attached(i)) {
                uint8_t mac[6], ip[4], mask[4], gateway[4], dns[4];
                uint32_t network_state = NET_VNIC_STATE_DOWN;
                net_vnic_get_info(i, mac, ip, mask, gateway, dns, &network_state);
                serial_puts("  mac ");
                server_print_mac(mac);
                if (network_state == NET_VNIC_STATE_BOUND) {
                    serial_puts("  ip ");
                    server_print_ip(ip);
                } else if (network_state == NET_VNIC_STATE_FAILED) serial_puts("  dhcp failed");
                else serial_puts("  dhcp pending");
            } else serial_puts("  nic detached");
            serial_puts("\r\n");
        }
        if (!found) serial_puts("no containers defined\r\n");
        return;
    }
    if (!name && strcmp(action, "exit") != 0) {
        serial_puts("container: missing name\r\n");
        return;
    }
    if (strcmp(action, "create") == 0) {
        int index = shark_container_create(name);
        if (index < 0) serial_puts("create failed; check name, capacity and memory\r\n");
        else {
            serial_puts("created SharkOS Server rootfs ");
            serial_puts(name);
            serial_puts(" in RAM; default website: /User/www/index.html\r\n");
        }
        return;
    }
    if (strcmp(action, "exit") == 0) {
        if (server_active_container < 0) serial_puts("not inside a container\r\n");
        else {
            server_active_container = -1;
            serial_puts("returned to host shell\r\n");
        }
        return;
    }
    int index = shark_container_find(name);
    if (index < 0) {
        serial_puts("container not found\r\n");
        return;
    }
    const shark_container_t* container = shark_container_get((uint32_t)index);
    if (strcmp(action, "start") == 0) {
        char* mode_text = server_next_word(&cursor);
        uint32_t mode = container->network_mode;
        if (mode_text) {
            if (strcmp(mode_text, "-nat") == 0) mode = SHARK_CONTAINER_NETWORK_NAT;
            else if (strcmp(mode_text, "-br") == 0) mode = SHARK_CONTAINER_NETWORK_BRIDGE;
            else {
                serial_puts("usage: container start <name> [-br|-nat]\r\n");
                return;
            }
            if (server_next_word(&cursor)) {
                serial_puts("usage: container start <name> [-br|-nat]\r\n");
                return;
            }
        }
        if (shark_container_start_mode((uint32_t)index, mode) != 0) serial_puts("start failed; container is already running or virtual NIC setup failed\r\n");
        else if (!net_vnic_is_attached((uint32_t)index)) serial_puts("container shell started without a virtual adapter; no physical NIC was detected\r\n");
        else if (mode == SHARK_CONTAINER_NETWORK_NAT) serial_puts("container shell started with NAT; private DHCP address assigned\r\n");
        else serial_puts("container shell started with a bridged virtual adapter; LAN DHCP requested\r\n");
        return;
    }
    if (strcmp(action, "stop") == 0) {
        if (shark_container_stop((uint32_t)index) != 0) serial_puts("container is not running\r\n");
        else {
            if (server_active_container == index) server_active_container = -1;
            serial_puts("container stopped\r\n");
        }
        return;
    }
    if (strcmp(action, "enter") == 0) {
        if (container->state != SHARK_CONTAINER_RUNNING) serial_puts("start the container before entering it\r\n");
        else {
            server_active_container = index;
            serial_puts("entered container shell; use 'container exit' to return\r\n");
        }
        return;
    }
    if (strcmp(action, "inspect") == 0) {
        serial_puts("name: "); serial_puts(container->name); serial_puts("\r\nstate: ");
        serial_puts(container->state == SHARK_CONTAINER_RUNNING ? "running (shell context)\r\n" : "stopped\r\n");
        serial_puts("rootfs: in-memory SharkOS Server template\r\nnetwork: ");
        serial_puts(container->network_mode == SHARK_CONTAINER_NETWORK_NAT ? "NAT\r\n" : "bridged\r\n");
        if (net_vnic_is_attached((uint32_t)index)) {
            uint8_t mac[6], ip[4], mask[4], gateway[4], dns[4];
            uint32_t network_state = NET_VNIC_STATE_DOWN;
            net_vnic_get_info((uint32_t)index, mac, ip, mask, gateway, dns, &network_state);
            serial_puts("virtual MAC: "); server_print_mac(mac); serial_puts("\r\nDHCP: ");
            if (network_state == NET_VNIC_STATE_BOUND) { serial_puts("bound "); server_print_ip(ip); serial_puts("\r\n"); }
            else if (network_state == NET_VNIC_STATE_FAILED) serial_puts("failed\r\n");
            else serial_puts("pending\r\n");
        } else serial_puts("virtual NIC: detached\r\n");
        serial_puts("services: per-container HTTP/SSH not attached\r\n");
        return;
    }
    if (strcmp(action, "remove") == 0 || strcmp(action, "rm") == 0) {
        if (server_active_container == index) {
            serial_puts("exit the container before removing it\r\n");
            return;
        }
        if (shark_container_remove((uint32_t)index) != 0) serial_puts("stop the container before removing it\r\n");
        else { serial_puts("container removed\r\n"); }
        return;
    }
    serial_puts("unknown container action; use 'container help'\r\n");
}

static void server_execute_core(char* line) {
    while (*line == ' ') line++;
    if (!*line) return;
    char command_copy[256];
    strcpy(command_copy, line);
    char* args = line;
    while (*args && *args != ' ') args++;
    if (*args) *args++ = 0;
    while (*args == ' ') args++;
    if (strcmp(line, "container") == 0) { server_container_command(args); return; }
    if (strcmp(line, "help") == 0) {
        serial_puts("Server: container, httpd start|stop|status|tui, sshd start|stop|status, ip, dhcp, devices/lspci, nano <file>\r\n");
        serial_puts("Files: ls, cd, pwd, cat, nano, tree, find, grep, stat, touch, mkdir, rm, mv, cp, head, tail, wc, sort\r\n");
        serial_puts("Text/system: echo, whoami, uname, sysinfo, neofetch, uptime, ps, htop, memmap, dmesg, env, id, test, printf, time\r\n");
        serial_puts("Network/tools: ifconfig, ping, dns, netstat, curl, wget, websocket, gcc, spkg, audio\r\n");
        serial_puts("More: nl, which, basename, dirname, rev, seq, factor, tr, cut, uniq, tee, realpath, strings, od\r\n");
        serial_puts("Games and graphical apps are omitted. Use SFTP to transfer website files into /User/www\r\n");
        return;
    }
    if (strcmp(line, "nano") == 0) { if (!args[0]) serial_puts("usage: nano <file>\r\n"); else server_nano(args); return; }
    if (strcmp(line, "ip") == 0 || strcmp(line, "ifconfig") == 0) {
        if (server_active_container >= 0) {
            uint32_t index = (uint32_t)server_active_container;
            uint8_t mac[6], ip[4], mask[4], gateway[4], dns[4];
            uint32_t network_state = NET_VNIC_STATE_DOWN;
            if (net_vnic_get_info(index, mac, ip, mask, gateway, dns, &network_state) != 0) {
                serial_puts("container virtual adapter is not attached; start the container with a physical NIC present\r\n");
                return;
            }
            serial_puts("interface: vnic"); serial_putdec(index); serial_puts("\r\nmac: ");
            server_print_mac(mac); serial_puts("\r\nip: ");
            server_print_ip(ip); serial_puts("\nmask: "); server_print_ip(mask);
            serial_puts("\ngateway: "); server_print_ip(gateway); serial_puts("\ndns: ");
            server_print_ip(dns); serial_puts("\r\nDHCP: ");
            if (network_state == NET_VNIC_STATE_BOUND) serial_puts("bound\r\n");
            else if (network_state == NET_VNIC_STATE_FAILED) serial_puts("failed\r\n");
            else if (network_state == NET_VNIC_STATE_REQUESTING) serial_puts("requesting\r\n");
            else if (network_state == NET_VNIC_STATE_RENEWING) serial_puts("renewing\r\n");
            else serial_puts("discovering\r\n");
            return;
        }
        if (!net_has_nic()) {
            serial_puts("network adapter not detected\r\n");
            return;
        }
        serial_puts("driver: "); serial_puts(net_driver_name); serial_puts("\r\nmac: ");
        char mac[18]; net_format_mac(mac); serial_puts(mac); serial_puts("\r\nip: ");
        server_print_ip(net_ip); serial_puts("\nmask: "); server_print_ip(net_mask);
        serial_puts("\ngateway: "); server_print_ip(net_gw); serial_puts("\ndns: ");
        server_print_ip(net_dns); serial_puts("\r\n");
        return;
    }
    if (strcmp(line, "ping") == 0 && server_active_container >= 0) {
        char* cursor = args;
        char* address = server_next_word(&cursor);
        uint8_t destination[4];
        if (!address || !net_parse_ip4(address, destination)) {
            serial_puts("usage: ping <ip> [count]\r\n");
            return;
        }
        char* count_text = server_next_word(&cursor);
        uint32_t count = server_parse_uint(count_text, 4);
        net_vnic_ping((uint32_t)server_active_container, destination, (int)count);
        return;
    }
    if (strcmp(line, "dhcp") == 0) {
        if (server_active_container >= 0) {
            if (net_vnic_dhcp_start((uint32_t)server_active_container) == 0) {
                if (net_vnic_get_mode((uint32_t)server_active_container) == NET_VNIC_MODE_NAT) serial_puts("container NAT address assigned\r\n");
                else serial_puts("container DHCP discovery requested\r\n");
            } else serial_puts("container virtual adapter is not attached\r\n");
        } else {
            net_dhcp_start();
            serial_puts("DHCP requested\r\n");
        }
        return;
    }
    if (strcmp(line, "httpd") == 0) {
        if (strcmp(args, "tui") == 0 || strcmp(args, "config") == 0) { server_httpd_tui(); return; }
        if (server_starts_with(args, "tui ")) { server_httpd_tui_command(args + 4); return; }
        if (strcmp(args, "stop") == 0) { httpd_stop(); serial_puts("HTTP services stopped\r\n"); return; }
        if (strcmp(args, "status") == 0) {
            for (uint32_t i = 0; i < HTTPD_SERVER_LIMIT; i++) if (httpd_server_port(i)) {
                serial_puts("HTTP service "); serial_putdec(i + 1); serial_puts(": ");
                serial_puts(httpd_server_active(i) ? "listening on port " : "stopped on port ");
                serial_putdec(httpd_server_port(i)); serial_puts(" root "); serial_puts(httpd_server_root(i)); serial_puts("\r\n");
            }
            if (!httpd_is_active()) serial_puts("no HTTP services running\r\n");
            return;
        }
        if (args[0] == 's' && args[1] == 't' && args[2] == 'a' && args[3] == 'r' && args[4] == 't' && (args[5] == 0 || args[5] == ' ')) {
            uint16_t port = (uint16_t)server_parse_uint(args + 5, 80);
            if (httpd_start(port) == 0) {
                serial_puts("HTTP service listening on port "); serial_putdec(port); serial_puts("\r\n");
            } else serial_puts("unable to start HTTP service; check adapter and port\r\n");
            return;
        }
        serial_puts("usage: httpd start [port] | stop | status | tui\r\n");
        return;
    }
    if (strcmp(line, "sshd") == 0) {
        if (strcmp(args, "status") == 0) {
            serial_puts("host key: "); serial_puts(ssh_server_has_host_key() ? "ephemeral Ed25519 ready\r\n" : "unavailable; CPU RDRAND required\r\n");
            serial_puts("password: "); serial_puts(ssh_server_has_password() ? "configured in RAM\r\n" : "not configured\r\n");
            if (ssh_server_is_active()) { serial_puts("service: listening on port "); serial_putdec(ssh_server_listen_port()); serial_puts("\r\n"); }
            else serial_puts("service: stopped\r\n");
            return;
        }
        if (strcmp(args, "stop") == 0) { ssh_server_stop(); serial_puts("SSH service stopped\r\n"); return; }
        if (args[0] == 's' && args[1] == 't' && args[2] == 'a' && args[3] == 'r' && args[4] == 't' && (args[5] == 0 || args[5] == ' ')) {
            uint16_t port = (uint16_t)server_parse_uint(args + 5, 22);
            if (ssh_server_start(port) == 0) { serial_puts("OpenSSH SSHv2 listening on port "); serial_putdec(port); serial_puts("\r\n"); }
            else serial_puts("cannot start SSH; configure a password and ensure RDRAND and a network adapter are available\r\n");
            return;
        }
        if (args[0] == 'p' && args[1] == 'a' && args[2] == 's' && args[3] == 's' && args[4] == 'w' && args[5] == 'o' && args[6] == 'r' && args[7] == 'd' && args[8] == ' ') {
            const char* username = args + 9;
            uint32_t len = 0; while (username[len] && username[len] != ' ' && len < sizeof(server_ssh_username)) len++;
            if (!len || len >= sizeof(server_ssh_username) || username[len]) { serial_puts("usage: sshd password <username>\r\n"); return; }
            memcpy(server_ssh_username, username, len); server_ssh_username[len] = 0;
            server_password_mode = 1;
            serial_puts("enter SSH password (minimum 7 characters; input hidden): ");
            return;
        }
        serial_puts("usage: sshd password <username> | start [port] | stop | status\r\n");
        return;
    }
    if (strcmp(line, "ls") == 0) { server_list(); return; }
    if (strcmp(line, "cat") == 0) { server_cat(args); return; }
    if (strcmp(line, "write") == 0) { server_write(args); return; }
    if (strcmp(line, "uptime") == 0) {
        serial_putdec(uptime_ticks / 1000);
        serial_puts(" seconds\r\n");
        return;
    }
    if (strcmp(line, "memmap") == 0) { server_print_memory_map(); return; }
    if (strcmp(line, "clear") == 0) { serial_puts("\033[2J\033[H"); return; }
    if (strcmp(line, "reboot") == 0) {
        serial_puts("rebooting\r\n");
        asm volatile("cli");
        outb(0x64, 0xFE);
        for (;;) asm volatile("hlt");
    }
    if (strcmp(line, "halt") == 0) {
        serial_puts("system halted\r\n");
        for (;;) asm volatile("cli; hlt");
    }
    execute_command(command_copy);
}

struct fs_node* server_webroot_node(void) {
    return server_webroot;
}

static int server_starts_with(const char* text, const char* prefix) {
    while (*prefix) {
        if (*text++ != *prefix++) return 0;
    }
    return 1;
}

struct server_multiboot_mmap_entry {
    uint32_t size;
    uint64_t address;
    uint64_t length;
    uint32_t type;
} __attribute__((packed));

static uint32_t server_detect_usable_memory_type(void) {
    const uint8_t* cursor = server_boot_mmap_snapshot;
    uint32_t remaining = server_boot_mmap_snapshot_length;
    while (remaining >= sizeof(uint32_t)) {
        const struct server_multiboot_mmap_entry* entry = (const struct server_multiboot_mmap_entry*)cursor;
        uint32_t record_size = entry->size;
        if (record_size < 20 || record_size > remaining - sizeof(uint32_t)) break;
        if (entry->type == 0 && entry->length) return 0;
        uint32_t advance = record_size + sizeof(uint32_t);
        cursor += advance;
        remaining -= advance;
    }
    return 1;
}

static uint64_t server_parse_memory_map(const struct server_multiboot_info* info, pmm_memory_region_t* regions, uint32_t capacity, uint32_t* count) {
    *count = 0;
    if (!info || !(server_boot_flags & (1u << 6)) || !server_boot_mmap_snapshot_length) return 0;
#ifdef SHARKOS_32BIT
    const uint64_t address_limit = 0xC0000000ULL;
#else
    const uint64_t address_limit = 137438953472ULL;
#endif
    const uint8_t* cursor = server_boot_mmap_snapshot;
    uint32_t remaining = server_boot_mmap_snapshot_length;
    uint64_t total = 0;
    uint32_t usable_type = server_boot_usable_type;
    while (remaining >= sizeof(uint32_t)) {
        const struct server_multiboot_mmap_entry* entry = (const struct server_multiboot_mmap_entry*)cursor;
        uint32_t record_size = entry->size;
        if (record_size < 20 || record_size > remaining - sizeof(uint32_t)) break;
        if (entry->type == usable_type && entry->length && entry->address < address_limit) {
            uint64_t start = entry->address;
            uint64_t end = start + entry->length;
            if (end < start) end = UINT64_MAX;
            if (end > address_limit) end = address_limit;
            if (end > start) {
                uint64_t bytes = end - start;
                total += bytes;
                if (regions && *count < capacity) {
                    regions[*count].start = start;
                    regions[*count].end = end;
                    (*count)++;
                }
            }
        }
        uint32_t advance = record_size + sizeof(uint32_t);
        cursor += advance;
        remaining -= advance;
    }
    return total;
}

static void server_print_memory_map(void) {
    const struct server_multiboot_info* info = server_boot_info;
    if (!info) {
        serial_puts("memory map unavailable\r\n");
        return;
    }
    serial_puts("Multiboot flags: ");
    serial_puthex32(server_boot_flags);
    serial_puts("  mem_upper: ");
    serial_putdec(server_boot_mem_upper);
    serial_puts(" KiB  map length: ");
    serial_putdec(server_boot_mmap_original_length);
    serial_puts(" bytes  mmap address: ");
    serial_puthex32(server_boot_mmap_original_address);
    serial_puts("  usable type: ");
    serial_putdec(server_boot_usable_type);
    serial_puts("\r\n");
    if (!(server_boot_flags & (1u << 6)) || !server_boot_mmap_snapshot_length) {
        serial_puts("Multiboot memory map is absent\r\n");
        return;
    }
    if (server_boot_mmap_snapshot_length < server_boot_mmap_original_length) serial_puts("memory-map snapshot truncated\r\n");
    const uint8_t* cursor = server_boot_mmap_snapshot;
    uint32_t remaining = server_boot_mmap_snapshot_length;
    uint32_t offset = 0;
    while (remaining >= sizeof(uint32_t)) {
        const struct server_multiboot_mmap_entry* entry = (const struct server_multiboot_mmap_entry*)cursor;
        uint32_t record_size = entry->size;
        if (record_size < 20 || record_size > remaining - sizeof(uint32_t)) {
            serial_puts("invalid record at byte ");
            serial_putdec(offset);
            serial_puts(" size ");
            serial_putdec(record_size);
            serial_puts(" remaining ");
            serial_putdec(remaining);
            serial_puts("\r\n");
            break;
        }
        serial_puts("offset ");
        serial_putdec(offset);
        serial_puts(" size ");
        serial_putdec(record_size);
        serial_puts(" type ");
        serial_putdec(entry->type);
        serial_puts(" base ");
        serial_puthex32((uint32_t)(entry->address >> 32));
        serial_puthex32((uint32_t)entry->address);
        serial_puts(" length ");
        serial_puthex32((uint32_t)(entry->length >> 32));
        serial_puthex32((uint32_t)entry->length);
        serial_puts("\r\n");
        uint32_t advance = record_size + sizeof(uint32_t);
        cursor += advance;
        remaining -= advance;
        offset += advance;
    }
    serial_puts("reported usable total: ");
    serial_putdec((uint32_t)(total_system_memory >> 20));
    serial_puts(" MiB\r\n");
}

static void server_execute(char* line) {
    struct fs_node* saved_root = root;
    struct fs_node* saved_directory = current_dir;
    int selected = server_active_container;
    const shark_container_t* container = selected >= 0 ? shark_container_get((uint32_t)selected) : NULL;
    if (container && container->state == SHARK_CONTAINER_RUNNING) {
        root = container->root;
        current_dir = container->cwd;
        server_execute_core(line);
        shark_container_set_cwd((uint32_t)selected, current_dir);
    } else {
        if (selected >= 0) server_active_container = -1;
        server_host_root = root;
        server_host_cwd = current_dir;
        server_execute_core(line);
        server_host_root = root;
        server_host_cwd = current_dir;
    }
    root = saved_root;
    current_dir = saved_directory;
}

uint32_t server_execute_remote(const char* command, char* output, uint32_t output_capacity, struct fs_node** working_directory) {
    if (!command || !output || !output_capacity) return 0;
    uint32_t command_length = (uint32_t)strlen(command);
    struct fs_node* saved_root = root;
    struct fs_node* saved_directory = current_dir;
    if (server_host_root) root = server_host_root;
    if (working_directory) {
        if (!*working_directory) *working_directory = root;
        current_dir = *working_directory;
    }
    server_output_capture_begin(output, output_capacity);
    if (command_length >= sizeof(server_line)) {
        serial_puts("command too long\r\n");
    } else if (server_starts_with(command, "sshd password") && (command[13] == 0 || command[13] == ' ')) {
        serial_puts("configure the SSH password at the local console\r\n");
    } else if (server_starts_with(command, "container ") || strcmp(command, "container") == 0) {
        serial_puts("container management is available at the local console\r\n");
    } else {
        char line[256];
        memcpy(line, command, command_length);
        line[command_length] = 0;
        server_execute_core(line);
    }
    uint32_t output_length = server_output_capture_end();
    if (working_directory) *working_directory = current_dir;
    root = saved_root;
    current_dir = saved_directory;
    return output_length;
}

static void server_input_char(char c) {
    if (server_password_mode) {
        if (c == '\r' || c == '\n') {
            serial_puts("\r\n");
            server_line[server_line_len] = 0;
            if (ssh_server_set_password(server_ssh_username, server_line) == 0) serial_puts("SSH password configured in RAM\r\n");
            else serial_puts("password rejected; use 7 to 63 characters\r\n");
            memset(server_line, 0, sizeof(server_line)); server_line_len = 0; server_password_mode = 0; server_prompt();
            return;
        }
        if (c == 8 || c == 127) { if (server_line_len) server_line[--server_line_len] = 0; return; }
        if ((uint8_t)c < 32 || server_line_len + 1 >= 64) return;
        server_line[server_line_len++] = c; server_line[server_line_len] = 0;
        return;
    }
    if (c == '\r' || c == '\n') {
        serial_puts("\r\n");
        server_line[server_line_len] = 0;
        server_execute(server_line);
        server_line_len = 0;
        server_line[0] = 0;
        if (!server_password_mode && !server_htop_is_active()) server_prompt();
        return;
    }
    if (c == 8 || c == 127) {
        if (server_line_len) {
            server_line[--server_line_len] = 0;
            serial_puts("\b \b");
        }
        return;
    }
    if ((uint8_t)c < 32 || server_line_len + 1 >= sizeof(server_line)) return;
    server_line[server_line_len++] = c;
    server_line[server_line_len] = 0;
    serial_putc(c);
}

void kmain(uint32_t magic, struct server_multiboot_info* info) {
    asm volatile("cli");
    serial_init();
    if (magic != 0x2BADB002 || !info) {
        serial_puts("invalid multiboot handoff\r\n");
        for (;;) asm volatile("cli; hlt");
    }
    server_boot_info = info;
    server_boot_flags = info->flags;
    server_boot_mem_upper = info->mem_upper;
    server_boot_mmap_original_length = info->mmap_length;
    server_boot_mmap_original_address = info->mmap_addr;
    if ((info->flags & (1u << 6)) && info->mmap_addr && info->mmap_length) {
        server_boot_mmap_snapshot_length = info->mmap_length;
        if (server_boot_mmap_snapshot_length > sizeof(server_boot_mmap_snapshot)) server_boot_mmap_snapshot_length = sizeof(server_boot_mmap_snapshot);
        memcpy(server_boot_mmap_snapshot, (const void*)(uintptr_t)info->mmap_addr, server_boot_mmap_snapshot_length);
    }
    server_boot_usable_type = server_detect_usable_memory_type();
    server_console_init(info);
    screen_width = (info->flags & (1u << 12)) ? info->framebuffer_width : 640;
    screen_height = (info->flags & (1u << 12)) ? info->framebuffer_height : 400;
    serial_puts("SharkOS Server\r\n");
    uint64_t memory_kb = (uint64_t)server_boot_mem_upper + 1024;
    total_system_memory = memory_kb * 1024;
    pmm_memory_region_t memory_regions[64];
    uint32_t memory_region_count = 0;
    uint64_t mapped_memory = server_parse_memory_map(info, memory_regions, 64, &memory_region_count);
    if (mapped_memory) total_system_memory = mapped_memory;
#ifdef SHARKOS_32BIT
    if (total_system_memory > 0xC0000000ULL) total_system_memory = 0xC0000000ULL;
#else
    if (total_system_memory > 137438953472ULL) total_system_memory = 137438953472ULL;
#endif
    init_descriptor_tables();
    if (memory_region_count) pmm_init_regions(memory_regions, memory_region_count);
    else pmm_init(total_system_memory);
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
    outb(0xA1, 0xFF); outb(0x21, 0xFE);
    uint16_t divisor = 1193182 / 1000;
    outb(0x43, 0x36); outb(0x40, (uint8_t)divisor); outb(0x40, (uint8_t)(divisor >> 8));
    asm volatile("sti");
    fs_initialize();
    shark_containers_init();
    struct fs_node* user = find_node(root, "User");
    server_webroot = create_node("www", FS_DIRECTORY, user);
    if (server_webroot) current_dir = root;
    server_host_root = root;
    server_host_cwd = current_dir;
    net_init();
    net_stack_init();
    int ssh_key_ready = ssh_server_init() == 0;
    int ssh_started = 0;
    if (ssh_key_ready && net_has_nic() &&
        ssh_server_set_password("root", "root123") == 0 && ssh_server_start(22) == 0) ssh_started = 1;
    if (net_has_nic()) {
        serial_puts("network adapter: "); serial_puts(net_driver_name); serial_puts("\r\n");
        serial_puts("network configuration: DHCP requested\r\n");
    } else serial_puts("network adapter not detected\r\n");
    serial_puts(ssh_key_ready ? "ephemeral Ed25519 SSH host key generated for this boot\r\n" : "SSH unavailable: CPU RDRAND is required for secure keys\r\n");
    serial_puts("\r\nSharkOS Server quick start\r\n");
    serial_puts("  Web files live in /User/www. The first page served at / is /User/www/index.html.\r\n");
    serial_puts("  Create or edit it with: nano /User/www/index.html\r\n");
    serial_puts("  Start one website: httpd start [port]\r\n");
    serial_puts("  Configure up to four ports and document folders: httpd tui\r\n");
    serial_puts("  Check website services: httpd status\r\n");
    serial_puts("  List hardware: devices  (or lspci)\r\n");
    serial_puts("  System summary: neofetch    Help: help\r\n");
    if (ssh_started) {
        serial_puts("  SSH is listening on port 22. Default login: root / root123\r\n");
        serial_puts("  Connect with: ssh root@<server-ip>\r\n");
        serial_puts("  Change the default: sshd stop, then sshd password root, then sshd start\r\n");
        serial_puts("  Warning: the default SSH password is intentionally weak; change it before exposing this host.\r\n");
    } else serial_puts("  SSH is not active; check the network adapter, port 22, and CPU RDRAND support.\r\n");
    serial_puts("\r\n");
    server_prompt();
    for (;;) {
        net_poll();
        httpd_poll();
        ssh_server_poll();
        int c = serial_getchar_nonblocking();
        if (c < 0) c = server_console_getchar_nonblocking();
        int htop_was_active = server_htop_is_active();
        if (htop_was_active) {
            server_htop_handle_key(c);
            if (!server_htop_is_active()) server_prompt();
        } else if (c >= 0) server_input_char((char)c);
        server_htop_poll();
        if (c < 0) {
            server_idle_waiting = 1;
            asm volatile("hlt");
            server_idle_waiting = 0;
        }
    }
}
