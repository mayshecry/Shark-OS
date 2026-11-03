#include "kernel.h"
#include "server_console.h"
#include "server_cli.h"
#include "containers.h"
#include "net.h"
#include "httpd.h"
#include "ssh.h"
#include "firewall.h"
#include "router.h"

#define SERVER_MULTIBOOT_MAGIC 0x2BADB002u
#define SERVER_MMAP_SNAPSHOT_SIZE 4096
#define SERVER_PMM_REGION_LIMIT 128

static int server_active_container = -1;
static struct fs_node* server_host_root;
static struct fs_node* server_host_cwd;
static char server_line[256];
static uint32_t server_line_len;
static char server_ssh_username[32] = "root";
static uint32_t server_boot_flags;
static struct server_multiboot_info* server_boot_info;
static uint32_t server_boot_mem_upper;
static uint32_t server_boot_mmap_original_address;
static uint32_t server_boot_mmap_original_length;
static uint8_t server_boot_mmap_snapshot[SERVER_MMAP_SNAPSHOT_SIZE];
static uint32_t server_boot_mmap_snapshot_length;
static pmm_memory_region_t server_pmm_regions[SERVER_PMM_REGION_LIMIT];
static uint32_t server_pmm_region_count;
static uint32_t server_boot_usable_type = 1;
static struct fs_node* server_editor_file;
static char server_editor_buffer[4096];
static uint32_t server_editor_length;
static uint32_t server_editor_cursor;
static uint8_t server_editor_dirty;

static uint32_t server_parse_uint(const char* text, uint32_t fallback) {
    if (!text || !*text) return fallback;
    uint32_t value = 0;
    for (uint32_t i = 0; text[i]; i++) {
        if (text[i] < '0' || text[i] > '9') return fallback;
        value = value * 10 + (uint32_t)(text[i] - '0');
        if (value > 65535) return fallback;
    }
    return value ? value : fallback;
}

static void server_putdec(uint32_t value) {
    char number[16];
    int_to_string(value, number);
    terminal_writestring(number);
}

static char* server_next_word(char** cursor) {
    if (!cursor || !*cursor) return NULL;
    while (**cursor == ' ' || **cursor == '\t') (*cursor)++;
    if (!**cursor) return NULL;
    char* word = *cursor;
    while (**cursor && **cursor != ' ' && **cursor != '\t') (*cursor)++;
    if (**cursor) *(*cursor)++ = 0;
    return word;
}

static void server_print_ip(const uint8_t* ip) {
    for (uint32_t i = 0; i < 4; i++) {
        server_putdec(ip[i]);
        if (i < 3) terminal_putchar('.');
    }
}

static void server_print_mac(const uint8_t* mac) {
    static const char digits[] = "0123456789abcdef";
    for (uint32_t i = 0; i < 6; i++) {
        terminal_putchar(digits[mac[i] >> 4]);
        terminal_putchar(digits[mac[i] & 15]);
        if (i < 5) terminal_putchar(':');
    }
}

void server_format_path(struct fs_node* node, char* output, uint32_t capacity) {
    if (!output || !capacity) return;
    output[0] = 0;
    if (!node) node = root;
    char names[16][32];
    uint32_t lengths[16];
    uint32_t count = 0;
    struct fs_node* cursor = node;
    while (cursor && cursor != root && count < 16) {
        uint32_t length = (uint32_t)strlen(cursor->name);
        if (length > 31) length = 31;
        memcpy(names[count], cursor->name, length);
        names[count][length] = 0;
        lengths[count] = length;
        count++;
        cursor = cursor->parent;
    }
    uint32_t out = 0;
    output[out++] = '/';
    while (count && out + 1 < capacity) {
        count--;
        for (uint32_t i = 0; i < lengths[count] && out + 1 < capacity; i++) output[out++] = names[count][i];
        if (count && out + 1 < capacity) output[out++] = '/';
    }
    output[out] = 0;
}

struct fs_node* server_webroot_node(void) {
    struct fs_node* saved = current_dir;
    current_dir = root;
    struct fs_node* node = fs_resolve_path("/User/www");
    current_dir = saved;
    return node;
}

static void server_prompt(void) {
    char path[256];
    server_format_path(current_dir, path, sizeof(path));
    terminal_writestring("root@shark");
    if (server_active_container >= 0) {
        terminal_putchar('[');
        server_putdec((uint32_t)server_active_container + 1);
        terminal_putchar(']');
    }
    terminal_putchar(':');
    terminal_writestring(path);
    terminal_writestring("# ");
}

static void server_service_poll(void) {
    net_poll();
    httpd_poll();
    ssh_server_poll();
    server_htop_poll();
}

static void server_container_exit(void) {
    if (server_active_container < 0) return;
    shark_container_set_cwd((uint32_t)server_active_container, current_dir);
    root = server_host_root;
    current_dir = server_host_cwd;
    server_active_container = -1;
    terminal_writestring("returned to host shell\r\n");
}

static void server_container_list(void) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < SHARK_CONTAINER_LIMIT; i++) {
        const shark_container_t* container = shark_container_get(i);
        if (!container) continue;
        count++;
        terminal_writestring(container->name);
        terminal_writestring(container->state == SHARK_CONTAINER_RUNNING ? " running" : " stopped");
        terminal_writestring("  network ");
        terminal_writestring(container->network_mode == SHARK_CONTAINER_NETWORK_NAT ? "nat" : "bridge");
        if (container->state == SHARK_CONTAINER_RUNNING && net_vnic_is_attached(i)) {
            uint8_t mac[6], ip[4], mask[4], gateway[4], dns[4];
            uint32_t state = NET_VNIC_STATE_DOWN;
            net_vnic_get_info(i, mac, ip, mask, gateway, dns, &state);
            if (state == NET_VNIC_STATE_BOUND) {
                terminal_writestring("  ");
                server_print_ip(ip);
            } else if (state == NET_VNIC_STATE_FAILED) terminal_writestring("  dhcp failed");
            else terminal_writestring("  dhcp pending");
        }
        terminal_writestring("\r\n");
    }
    if (!count) terminal_writestring("no containers defined\r\n");
}

static void server_container_inspect(uint32_t index) {
    const shark_container_t* container = shark_container_get(index);
    if (!container) {
        terminal_writestring("container not found\r\n");
        return;
    }
    terminal_writestring("name: ");
    terminal_writestring(container->name);
    terminal_writestring("\nstate: ");
    terminal_writestring(container->state == SHARK_CONTAINER_RUNNING ? "running\n" : "stopped\n");
    terminal_writestring("rootfs: in-memory SharkOS Server template\nnetwork: ");
    terminal_writestring(container->network_mode == SHARK_CONTAINER_NETWORK_NAT ? "NAT\n" : "bridged\n");
    if (container->state == SHARK_CONTAINER_RUNNING && net_vnic_is_attached(index)) {
        uint8_t mac[6], ip[4], mask[4], gateway[4], dns[4];
        uint32_t state = NET_VNIC_STATE_DOWN;
        net_vnic_get_info(index, mac, ip, mask, gateway, dns, &state);
        terminal_writestring("virtual MAC: ");
        server_print_mac(mac);
        terminal_writestring("\n");
        if (state == NET_VNIC_STATE_BOUND) {
            terminal_writestring("DHCP: bound ");
            server_print_ip(ip);
            terminal_writestring("\nmask: ");
            server_print_ip(mask);
            terminal_writestring("\ngateway: ");
            server_print_ip(gateway);
            terminal_writestring("\ndns: ");
            server_print_ip(dns);
            terminal_writestring("\n");
        } else if (state == NET_VNIC_STATE_FAILED) terminal_writestring("DHCP: failed\n");
        else terminal_writestring("DHCP: pending\n");
    } else terminal_writestring("virtual adapter: not attached\n");
    terminal_writestring("services: per-container HTTP/SSH not attached\n");
}

static void server_container_command(char* text) {
    char* cursor = text;
    char* action = server_next_word(&cursor);
    if (!action || strcmp(action, "help") == 0) {
        terminal_writestring("container create <name> | start <name> [-br|-nat] | stop <name> | list | inspect <name> | enter <name> | exit | remove <name>\r\n");
        return;
    }
    if (strcmp(action, "list") == 0 || strcmp(action, "ls") == 0) {
        server_container_list();
        return;
    }
    if (strcmp(action, "exit") == 0) {
        server_container_exit();
        return;
    }
    char* name = server_next_word(&cursor);
    if (strcmp(action, "create") == 0) {
        if (!name) {
            terminal_writestring("container: missing name\r\n");
            return;
        }
        if (shark_container_create(name) < 0) terminal_writestring("container create failed\r\n");
        else {
            terminal_writestring("created SharkOS Server rootfs ");
            terminal_writestring(name);
            terminal_writestring("\r\n");
        }
        return;
    }
    if (!name) {
        terminal_writestring("container: missing name\r\n");
        return;
    }
    int index = shark_container_find(name);
    if (index < 0) {
        terminal_writestring("container not found\r\n");
        return;
    }
    if (strcmp(action, "inspect") == 0) {
        server_container_inspect((uint32_t)index);
        return;
    }
    if (strcmp(action, "start") == 0) {
        uint32_t mode = ((const shark_container_t*)shark_container_get((uint32_t)index))->network_mode;
        char* option = server_next_word(&cursor);
        if (option) {
            if (strcmp(option, "-br") == 0) mode = SHARK_CONTAINER_NETWORK_BRIDGE;
            else if (strcmp(option, "-nat") == 0) mode = SHARK_CONTAINER_NETWORK_NAT;
            else {
                terminal_writestring("usage: container start <name> [-br|-nat]\r\n");
                return;
            }
            if (server_next_word(&cursor)) {
                terminal_writestring("too many container start arguments\r\n");
                return;
            }
        }
        if (shark_container_start_mode((uint32_t)index, mode) != 0) {
            terminal_writestring("start failed: container is already running or virtual NIC setup failed\r\n");
            return;
        }
        if (net_vnic_is_attached((uint32_t)index)) {
            if (mode == SHARK_CONTAINER_NETWORK_NAT) terminal_writestring("container shell started with NAT; private DHCP address assigned\r\n");
            else terminal_writestring("container shell started with a bridged virtual adapter; LAN DHCP requested\r\n");
        } else terminal_writestring("container shell started without a virtual adapter; no physical NIC was detected\r\n");
        return;
    }
    if (strcmp(action, "stop") == 0) {
        if (server_active_container == index) {
            terminal_writestring("exit the container before stopping it\r\n");
            return;
        }
        if (shark_container_stop((uint32_t)index) != 0) terminal_writestring("container is not running\r\n");
        else terminal_writestring("container stopped\r\n");
        return;
    }
    if (strcmp(action, "enter") == 0) {
        const shark_container_t* container = shark_container_get((uint32_t)index);
        if (!container || container->state != SHARK_CONTAINER_RUNNING) {
            terminal_writestring("start the container before entering it\r\n");
            return;
        }
        if (server_active_container >= 0) server_container_exit();
        server_host_root = root;
        server_host_cwd = current_dir;
        server_active_container = index;
        root = container->root;
        current_dir = container->cwd ? container->cwd : container->root;
        if (!net_vnic_is_attached((uint32_t)index)) terminal_writestring("container shell started without a virtual adapter; no physical NIC was detected\r\n");
        else if (container->network_mode == SHARK_CONTAINER_NETWORK_NAT) terminal_writestring("container shell started with NAT; private DHCP address assigned\r\n");
        terminal_writestring("entered container shell; use 'container exit' to return\r\n");
        return;
    }
    if (strcmp(action, "remove") == 0 || strcmp(action, "rm") == 0) {
        if (server_active_container == index) {
            terminal_writestring("exit the container before removing it\r\n");
            return;
        }
        if (shark_container_remove((uint32_t)index) != 0) terminal_writestring("stop the container before removing it\r\n");
        else terminal_writestring("container removed\r\n");
        return;
    }
    terminal_writestring("unknown container action; use 'container help'\r\n");
}

static void server_container_network_command(char* command, char* args) {
    if (server_active_container < 0) return;
    uint32_t index = (uint32_t)server_active_container;
    if (strcmp(command, "ifconfig") == 0 || strcmp(command, "ip") == 0) {
        if (!net_vnic_is_attached(index)) {
            terminal_writestring("container virtual adapter is not attached; start the container with a physical NIC present\r\n");
            return;
        }
        uint8_t mac[6], ip[4], mask[4], gateway[4], dns[4];
        uint32_t state = NET_VNIC_STATE_DOWN;
        net_vnic_get_info(index, mac, ip, mask, gateway, dns, &state);
        terminal_writestring("interface: vnic");
        server_putdec(index);
        terminal_writestring("\nmac: ");
        server_print_mac(mac);
        terminal_writestring("\nip: ");
        server_print_ip(ip);
        terminal_writestring("\nmask: ");
        server_print_ip(mask);
        terminal_writestring("\ngateway: ");
        server_print_ip(gateway);
        terminal_writestring("\ndns: ");
        server_print_ip(dns);
        terminal_writestring("\nDHCP: ");
        terminal_writestring(state == NET_VNIC_STATE_BOUND ? "bound\r\n" : "pending\r\n");
        return;
    }
    if (strcmp(command, "ping") == 0) {
        char* cursor = args;
        char* address = server_next_word(&cursor);
        char* count_text = server_next_word(&cursor);
        uint8_t target[4];
        if (!address || !net_parse_ip4(address, target)) {
            terminal_writestring("usage: ping <ip> [count]\r\n");
            return;
        }
        int count = count_text ? (int)server_parse_uint(count_text, 1) : 4;
        if (net_vnic_is_attached(index)) net_vnic_ping(index, target, count);
        else terminal_writestring("container virtual adapter is not attached\r\n");
        return;
    }
}

static void server_httpd_tui(char* args) {
    if (args && *args) {
        char* cursor = args;
        char* action = server_next_word(&cursor);
        if (action && strcmp(action, "add") == 0) {
            uint32_t slot = server_parse_uint(server_next_word(&cursor), 0);
            uint32_t port = server_parse_uint(server_next_word(&cursor), 0);
            char* path = server_next_word(&cursor);
            char full_path[128];
            if (slot < HTTPD_SERVER_LIMIT && port && port <= 65535 && path) {
                if (path[0] != '/') {
                    char cwd[128];
                    server_format_path(current_dir, cwd, sizeof(cwd));
                    strcpy(full_path, cwd);
                    uint32_t used = (uint32_t)strlen(full_path);
                    if (used + 1 < sizeof(full_path) && used && full_path[used - 1] != '/') full_path[used++] = '/';
                    strcpy(full_path + used, path);
                    path = full_path;
                }
                if (httpd_configure(slot, (uint16_t)port, path, 1) == 0) terminal_writestring("HTTP service configured and started\r\n");
                else terminal_writestring("configuration failed; check port, directory and network adapter\r\n");
            } else terminal_writestring("usage: httpd tui add <slot> <port> <directory>\r\n");
            return;
        }
        if (action && strcmp(action, "delete") == 0) {
            uint32_t slot = server_parse_uint(server_next_word(&cursor), 0);
            if (slot < HTTPD_SERVER_LIMIT && httpd_remove(slot) == 0) terminal_writestring("HTTP service removed\r\n");
            else terminal_writestring("invalid HTTP service slot\r\n");
            return;
        }
        if (action && strcmp(action, "toggle") == 0) {
            uint32_t slot = server_parse_uint(server_next_word(&cursor), 0);
            if (slot < HTTPD_SERVER_LIMIT && httpd_server_root(slot)) {
                if (httpd_server_active(slot)) httpd_stop_server(slot);
                else httpd_configure(slot, httpd_server_port(slot), httpd_server_root(slot), 1);
            }
            return;
        }
    }
    terminal_writestring("httpd tui [add <slot> <port> <directory> | toggle <slot> | delete <slot>]\r\n");
    for (uint32_t i = 0; i < HTTPD_SERVER_LIMIT; i++) {
        if (!httpd_server_root(i)) continue;
        terminal_writestring("slot ");
        server_putdec(i);
        terminal_writestring(httpd_server_active(i) ? " active port " : " stopped port ");
        server_putdec(httpd_server_port(i));
        terminal_writestring(" root ");
        terminal_writestring(httpd_server_root(i));
        terminal_writestring("\r\n");
    }
}

static void server_httpd_command(char* args) {
    char* cursor = args;
    char* action = server_next_word(&cursor);
    if (!action || strcmp(action, "status") == 0) {
        uint32_t active = 0;
        for (uint32_t i = 0; i < HTTPD_SERVER_LIMIT; i++) {
            if (!httpd_server_active(i)) continue;
            active++;
            terminal_writestring("HTTP service listening on port ");
            server_putdec(httpd_server_port(i));
            terminal_writestring(" root ");
            terminal_writestring(httpd_server_root(i));
            terminal_writestring("\r\n");
        }
        if (!active) terminal_writestring("no HTTP services running\r\n");
        return;
    }
    if (strcmp(action, "tui") == 0) {
        server_httpd_tui(cursor);
        return;
    }
    if (strcmp(action, "stop") == 0) {
        httpd_stop();
        terminal_writestring("HTTP services stopped\r\n");
        return;
    }
    if (strcmp(action, "start") == 0) {
        uint32_t port = server_parse_uint(server_next_word(&cursor), 80);
        char path[128];
        server_format_path(server_webroot_node(), path, sizeof(path));
        if (!net_has_nic()) {
            terminal_writestring("unable to start HTTP service; check adapter and port\r\n");
            return;
        }
        if (!port || port > 65535 || httpd_configure(0, (uint16_t)port, path, 1) != 0) terminal_writestring("unable to start HTTP service; check adapter and port\r\n");
        else terminal_writestring("HTTP service configured and started\r\n");
        return;
    }
    terminal_writestring("usage: httpd start [port] | stop | status | tui\r\n");
}

static void server_read_line_blocking(char* output, uint32_t capacity, int hidden) {
    uint32_t used = 0;
    if (!capacity) return;
    while (used + 1 < capacity) {
        int c = -1;
        while (c < 0) {
            c = serial_getchar_nonblocking();
            if (c < 0) c = server_console_getchar_nonblocking();
            server_service_poll();
        }
        if (c == '\r' || c == '\n') break;
        if (c == '\b' || c == 127) {
            if (used) {
                used--;
                if (!hidden) terminal_writestring("\b \b");
            }
            continue;
        }
        if (c < 32 || c > 126) continue;
        output[used++] = (char)c;
        if (!hidden) terminal_putchar((char)c);
    }
    output[used] = 0;
    terminal_writestring("\r\n");
}

static void server_sshd_command(char* args) {
    char* cursor = args;
    char* action = server_next_word(&cursor);
    if (!action || strcmp(action, "status") == 0) {
        if (ssh_server_is_active()) {
            terminal_writestring("OpenSSH SSHv2 listening on port ");
            server_putdec(ssh_server_listen_port());
            terminal_writestring("\r\n");
        } else terminal_writestring("SSH server stopped\r\n");
        return;
    }
    if (strcmp(action, "stop") == 0) {
        ssh_server_stop();
        terminal_writestring("SSH service stopped\r\n");
        return;
    }
    if (strcmp(action, "password") == 0) {
        char* username = server_next_word(&cursor);
        if (!username) username = "root";
        char password[64];
        terminal_writestring("enter SSH password (minimum 7 characters; input hidden): ");
        server_read_line_blocking(password, sizeof(password), 1);
        if (ssh_server_set_password(username, password) == 0) {
            strcpy(server_ssh_username, username);
            terminal_writestring("SSH password configured in RAM\r\n");
        } else terminal_writestring("password rejected; use 7 to 63 characters\r\n");
        return;
    }
    if (strcmp(action, "start") == 0) {
        uint32_t port = server_parse_uint(server_next_word(&cursor), 22);
        if (!ssh_server_has_password()) {
            terminal_writestring("configure the SSH password at the local console\r\n");
            return;
        }
        if (!ssh_server_has_host_key()) {
            terminal_writestring("SSH unavailable: CPU RDRAND is required for secure keys\r\n");
            return;
        }
        if (!net_has_nic() || !port || port > 65535 || ssh_server_start((uint16_t)port) != 0) terminal_writestring("cannot start SSH; configure a password and ensure RDRAND and a network adapter are available\r\n");
        else {
            terminal_writestring("OpenSSH SSHv2 listening on port ");
            server_putdec(port);
            terminal_writestring("\r\n");
        }
        return;
    }
    terminal_writestring("usage: sshd password <username> | start [port] | stop | status\r\n");
}

static void server_editor_save(void) {
    if (!server_editor_file) return;
    if (fs_node_truncate(server_editor_file, 0) != 0 || fs_node_write(server_editor_file, 0, server_editor_buffer, server_editor_length) != 0) {
        terminal_writestring("save failed\r\n");
        return;
    }
    fs_node_sync(server_editor_file);
    server_editor_dirty = 0;
    terminal_writestring("saved\r\n");
}

static void server_nano(char* path) {
    if (!path || !*path) {
        terminal_writestring("usage: nano <file>\r\n");
        return;
    }
    server_editor_file = fs_resolve_path(path);
    if (!server_editor_file) {
        char parent[128];
        strcpy(parent, path);
        uint32_t length = (uint32_t)strlen(parent);
        while (length && parent[length - 1] != '/') length--;
        if (!length) strcpy(parent, ".");
        else if (length > 1) parent[length - 1] = 0;
        struct fs_node* saved = current_dir;
        struct fs_node* directory = fs_resolve_path(parent);
        if (directory) current_dir = directory;
        const char* name = path + length;
        if (!*name) name = path;
        server_editor_file = directory ? create_node(name, FS_FILE, directory) : NULL;
        current_dir = saved;
    }
    if (!server_editor_file || server_editor_file->type != FS_FILE) {
        terminal_writestring("cannot open file\r\n");
        return;
    }
    const char* content = fs_node_data(server_editor_file);
    server_editor_length = server_editor_file->content_len > 0 ? (uint32_t)server_editor_file->content_len : 0;
    if (server_editor_length >= sizeof(server_editor_buffer)) server_editor_length = sizeof(server_editor_buffer) - 1;
    if (server_editor_length && content) memcpy(server_editor_buffer, content, server_editor_length);
    server_editor_buffer[server_editor_length] = 0;
    server_editor_cursor = server_editor_length;
    server_editor_dirty = 0;
    terminal_writestring("nano: enter text; Ctrl-S saves, Ctrl-X exits\r\n");
    while (1) {
        int c = -1;
        while (c < 0) {
            c = serial_getchar_nonblocking();
            if (c < 0) c = server_console_getchar_nonblocking();
            server_service_poll();
        }
        if (c == 19) {
            server_editor_save();
            continue;
        }
        if (c == 24 || c == 27) break;
        if (c == '\r') c = '\n';
        if (c == '\b' || c == 127) {
            if (server_editor_cursor) {
                server_editor_cursor--;
                if (server_editor_cursor < server_editor_length) {
                    for (uint32_t i = server_editor_cursor; i < server_editor_length; i++) server_editor_buffer[i] = server_editor_buffer[i + 1];
                }
                server_editor_length--;
                server_editor_buffer[server_editor_length] = 0;
                server_editor_dirty = 1;
                terminal_writestring("\b \b");
            }
            continue;
        }
        if (c < 32 && c != '\n' && c != '\t') continue;
        if (server_editor_length + 1 >= sizeof(server_editor_buffer)) continue;
        server_editor_buffer[server_editor_cursor++] = (char)c;
        server_editor_length++;
        server_editor_buffer[server_editor_length] = 0;
        server_editor_dirty = 1;
        terminal_putchar((char)c);
    }
    if (server_editor_dirty) {
        terminal_writestring("\nsave changes? Ctrl-S to save, any other key to discard\r\n");
        int c = serial_getchar_nonblocking();
        if (c == 19) server_editor_save();
    }
    server_editor_file = NULL;
}

static void server_execute_core(char* line) {
    while (*line == ' ' || *line == '\t') line++;
    if (!*line) return;
    char command_copy[256];
    strcpy(command_copy, line);
    char* args = line;
    while (*args && *args != ' ' && *args != '\t') args++;
    if (*args) *args++ = 0;
    while (*args == ' ' || *args == '\t') args++;
    if (strcmp(line, "container") == 0) { server_container_command(args); return; }
    if (strcmp(line, "fw") == 0 || strcmp(line, "firewall") == 0) { fw_command(args); return; }
    if (strcmp(line, "httpd") == 0) {
        if (server_active_container >= 0) terminal_writestring("per-container HTTP service contexts are not attached\r\n");
        else server_httpd_command(args);
        return;
    }
    if (strcmp(line, "sshd") == 0) {
        if (server_active_container >= 0) terminal_writestring("per-container SSH service contexts are not attached\r\n");
        else server_sshd_command(args);
        return;
    }
    if (strcmp(line, "nano") == 0) { server_nano(args); return; }
    if (strcmp(line, "exit") == 0 && server_active_container >= 0) { server_container_exit(); return; }
    if (strcmp(line, "ping") == 0 || strcmp(line, "ip") == 0 || strcmp(line, "ifconfig") == 0) {
        if (server_active_container >= 0) {
            server_container_network_command(line, args);
            return;
        }
    }
    if (strcmp(line, "dhcp") == 0) {
        net_dhcp_start();
        terminal_writestring("DHCP discovery requested\r\n");
        return;
    }
    if (strcmp(line, "reboot") == 0) {
        terminal_writestring("rebooting\r\n");
        outb(0x64, 0xFE);
        return;
    }
    if (strcmp(line, "halt") == 0) {
        terminal_writestring("system halted\r\n");
        for (;;) asm volatile("cli; hlt");
    }
    if (strcmp(line, "help") == 0) {
        terminal_writestring("Server: container, fw docs/status/rate/allow/deny/policy/delete/clear, httpd, sshd\r\n");
        terminal_writestring("Router: rtr start [port], rtr status, rtr help, rtr stop, rtr wan, rtr dhcp, rtr dns\r\n");
        terminal_writestring("Files: ls [-la] [dir], cd <dir>, pwd, cat <file>, nano <file>, tree, find, grep, stat\r\n");
        terminal_writestring("File operations: touch, mkdir, rm [force], mv <src> <dst>, cp <src> <dst>, head, tail, wc\r\n");
        terminal_writestring("System/network: help, whoami, uname, sysinfo, neofetch, uptime, devices, ifconfig, dhcp, ping, dns, netstat\r\n");
        terminal_writestring("Web/network tools: httpd, sshd, curl, wget, gcc\r\n");
        return;
    }
    if (server_active_container >= 0) shark_container_set_cwd((uint32_t)server_active_container, current_dir);
    execute_command(command_copy);
}

static void server_main_loop(void) {
    while (1) {
        server_service_poll();
        if (server_htop_is_active()) {
            int key = serial_getchar_nonblocking();
            if (key < 0) key = server_console_getchar_nonblocking();
            if (key < 0) {
                server_idle_waiting = 1;
                asm volatile("sti; hlt");
                server_idle_waiting = 0;
                continue;
            }
            server_htop_handle_key(key);
            if (!server_htop_is_active()) server_prompt();
            continue;
        }
        int c = serial_getchar_nonblocking();
        if (c < 0) c = server_console_getchar_nonblocking();
        if (c < 0) {
            server_idle_waiting = 1;
            asm volatile("sti; hlt");
            server_idle_waiting = 0;
            continue;
        }
        if (c == '\r' || c == '\n') {
            terminal_writestring("\r\n");
            server_line[server_line_len] = 0;
            server_execute_core(server_line);
            server_line_len = 0;
            if (!server_htop_is_active()) server_prompt();
            continue;
        }
        if (c == '\b' || c == 127) {
            if (server_line_len) {
                server_line[--server_line_len] = 0;
                terminal_writestring("\b \b");
            }
            continue;
        }
        if (c < 32 || c > 126 || server_line_len + 1 >= sizeof(server_line)) continue;
        server_line[server_line_len++] = (char)c;
        server_line[server_line_len] = 0;
        terminal_putchar((char)c);
    }
}

static uint32_t server_read32(const volatile uint8_t* data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint64_t server_read64(const volatile uint8_t* data) {
    return (uint64_t)server_read32(data) | ((uint64_t)server_read32(data + 4) << 32);
}

static void server_init_memory(struct server_multiboot_info* info) {
    server_pmm_region_count = 0;
    total_system_memory = 0;
    if (info && (info->flags & (1u << 6)) && info->mmap_addr && info->mmap_length) {
        uint32_t offset = 0;
        uint32_t remaining = info->mmap_length;
        uint8_t* map = (uint8_t*)(uintptr_t)info->mmap_addr;
        server_boot_mmap_original_address = info->mmap_addr;
        server_boot_mmap_original_length = info->mmap_length;
        if (remaining > SERVER_MMAP_SNAPSHOT_SIZE) remaining = SERVER_MMAP_SNAPSHOT_SIZE;
        memcpy(server_boot_mmap_snapshot, map, remaining);
        server_boot_mmap_snapshot_length = remaining;
        while (offset + 4 <= remaining && server_pmm_region_count < SERVER_PMM_REGION_LIMIT) {
            uint32_t size = server_read32(map + offset);
            if (size < 20 || size > remaining - offset - 4) break;
            uint64_t base = server_read64(map + offset + 4);
            uint64_t length = server_read64(map + offset + 12);
            uint32_t type = server_read32(map + offset + 20);
            if (type == server_boot_usable_type && length && base <= 0x1FFFFFFFFFULL && length <= 0x2000000000ULL - base) {
                server_pmm_regions[server_pmm_region_count].start = base;
                server_pmm_regions[server_pmm_region_count].end = base + length;
                server_pmm_region_count++;
                total_system_memory += length;
            }
            offset += size + 4;
        }
    }
    if (server_pmm_region_count) pmm_init_regions(server_pmm_regions, server_pmm_region_count);
    else {
        uint64_t memory = ((uint64_t)server_boot_mem_upper + 1024ULL) * 1024ULL;
        if (memory < 64ULL * 1024 * 1024) memory = 128ULL * 1024 * 1024;
        total_system_memory = memory;
        pmm_init(memory);
    }
}

uint32_t server_execute_remote(const char* command, char* output, uint32_t output_capacity, struct fs_node** working_directory) {
    if (!command || !output || !output_capacity) return 0;
    struct fs_node* saved_root = root;
    struct fs_node* saved_cwd = current_dir;
    if (working_directory && *working_directory) current_dir = *working_directory;
    server_output_capture_begin(output, output_capacity);
    char buffer[256];
    uint32_t length = (uint32_t)strlen(command);
    if (length >= sizeof(buffer)) length = sizeof(buffer) - 1;
    memcpy(buffer, command, length);
    buffer[length] = 0;
    server_execute_core(buffer);
    uint32_t produced = server_output_capture_end();
    if (working_directory) *working_directory = current_dir;
    root = saved_root;
    current_dir = saved_cwd;
    return produced;
}

void kmain(uint32_t magic, struct server_multiboot_info* info) {
    asm volatile("cli");
    serial_init();
    if (magic != SERVER_MULTIBOOT_MAGIC || !info) {
        terminal_writestring("invalid multiboot handoff\r\n");
        for (;;) asm volatile("cli; hlt");
    }
    server_boot_info = info;
    server_boot_flags = info->flags;
    server_boot_mem_upper = info->mem_upper;
    total_system_memory = ((uint64_t)info->mem_upper + 1024ULL) * 1024ULL;
    server_console_init(info);
    if (info->flags & (1u << 12)) {
        screen_width = info->framebuffer_width;
        screen_height = info->framebuffer_height;
    } else {
        screen_width = 640;
        screen_height = 400;
    }
    init_descriptor_tables();
    outb(0x43, 0x36);
    uint16_t pit_divisor = 1193;
    outb(0x40, (uint8_t)(pit_divisor & 0xFF));
    outb(0x40, (uint8_t)(pit_divisor >> 8));
    asm volatile("sti");
    server_init_memory(info);
    fs_initialize();
    server_host_root = root;
    server_host_cwd = current_dir;
    strcpy(current_user, "root");
    shark_containers_init();
    firewall_init();
    net_init();
    net_stack_init();
    router_init();
    ssh_server_init();
    ssh_server_set_password("root", "root123");
    current_dir = root;
    terminal_clear();
    terminal_writestring("SharkOS Server\r\n");
    terminal_writestring("network adapter: ");
    if (net_has_nic()) {
        terminal_writestring(net_driver_name);
        terminal_writestring("\r\nnetwork configuration: DHCP requested\r\n");
    } else terminal_writestring("not detected\r\n");
    terminal_writestring("SharkOS Server quick start\r\n");
    terminal_writestring("  Web files live in /User/www. The first page served at / is /User/www/index.html.\r\n");
    terminal_writestring("  Create or edit it with: nano /User/www/index.html\r\n");
    terminal_writestring("  Start one website: httpd start [port]\r\n");
    terminal_writestring("  Configure up to four ports and document folders: httpd tui\r\n");
    terminal_writestring("  Check website services: httpd status\r\n");
    terminal_writestring("  System summary: neofetch    Firewall rules: fw docs\r\n");
    terminal_writestring("  Router: rtr start (DHCP + DNS + WAN + NAT + web console on port 69)\r\n");
    terminal_writestring("  SSH default login: root / root123\r\n");
    terminal_writestring("  Warning: the default SSH password is weak; change it before exposing this host.\r\n");
    if (!ssh_server_has_host_key()) terminal_writestring("  SSH is not active; check adapter and CPU RDRAND support.\r\n");
    server_prompt();
    asm volatile("sti");
    server_main_loop();
}
