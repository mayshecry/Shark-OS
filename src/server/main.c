#include "kernel.h"
#include "net.h"
#include "httpd.h"
#include "ssh.h"

struct server_multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
} __attribute__((packed));

static char server_line[256];
static uint32_t server_line_len;
static struct fs_node* server_webroot;
static char server_ssh_username[32];
static uint8_t server_password_mode;

static void server_prompt(void) {
    serial_puts("server:/User/www> ");
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

static void server_list(void) {
    if (!server_webroot) return;
    for (int i = 0; i < server_webroot->num_children; i++) {
        struct fs_node* node = server_webroot->children[i];
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
    if (!node && server_webroot) node = find_node(server_webroot, name);
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
    if (content_len >= MAX_FILE_CONTENT_SIZE) content_len = MAX_FILE_CONTENT_SIZE - 1;
    memcpy(node->content, split, content_len);
    node->content[content_len] = 0;
    node->content_len = (int)content_len;
    node->disk_backed = 0;
    serial_puts("saved ");
    serial_puts(name);
    serial_puts("\r\n");
}

static void server_execute(char* line) {
    while (*line == ' ') line++;
    if (!*line) return;
    char* args = line;
    while (*args && *args != ' ') args++;
    if (*args) *args++ = 0;
    while (*args == ' ') args++;
    if (strcmp(line, "help") == 0) {
        serial_puts("help  ip  dhcp  httpd start [port]|stop|status  sshd password <user>|start [port]|stop|status  ls  cat  write  uptime  clear  reboot  halt\r\n");
        serial_puts("website files are kept in RAM under /User/www; write stores one line of text\r\n");
        return;
    }
    if (strcmp(line, "ip") == 0 || strcmp(line, "ifconfig") == 0) {
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
    if (strcmp(line, "dhcp") == 0) {
        net_dhcp_start();
        serial_puts("DHCP requested\r\n");
        return;
    }
    if (strcmp(line, "httpd") == 0) {
        if (strcmp(args, "stop") == 0) {
            httpd_stop();
            serial_puts("HTTP service stopped\r\n");
            return;
        }
        if (strcmp(args, "status") == 0) {
            if (httpd_is_active()) {
                serial_puts("HTTP service listening on port ");
                serial_putdec(httpd_listen_port());
                serial_puts("\r\n");
            } else serial_puts("HTTP service stopped\r\n");
            return;
        }
        if (args[0] == 's' && args[1] == 't' && args[2] == 'a' && args[3] == 'r' && args[4] == 't' && (args[5] == 0 || args[5] == ' ')) {
            uint16_t port = (uint16_t)server_parse_uint(args + 5, 80);
            if (httpd_start(port) == 0) {
                serial_puts("HTTP service listening on port "); serial_putdec(port); serial_puts("\r\n");
            } else serial_puts("unable to start HTTP service; check adapter and port\r\n");
            return;
        }
        serial_puts("usage: httpd start [port] | stop | status\r\n");
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
            serial_puts("enter SSH password (minimum 8 characters; input hidden): ");
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
    serial_puts("unknown command: "); serial_puts(line); serial_puts("\r\n");
}

static void server_input_char(char c) {
    if (server_password_mode) {
        if (c == '\r' || c == '\n') {
            serial_puts("\r\n");
            server_line[server_line_len] = 0;
            if (ssh_server_set_password(server_ssh_username, server_line) == 0) serial_puts("SSH password configured in RAM\r\n");
            else serial_puts("password rejected; use 8 to 63 characters\r\n");
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
        if (!server_password_mode) server_prompt();
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
    serial_init();
    serial_puts("SharkOS Server\r\n");
    if (magic != 0x2BADB002 || !info) {
        serial_puts("invalid multiboot handoff\r\n");
        for (;;) asm volatile("cli; hlt");
    }
    uint64_t memory_kb = (uint64_t)info->mem_upper + 1024;
    total_system_memory = memory_kb * 1024;
    if (total_system_memory > 137438953472ULL) total_system_memory = 137438953472ULL;
    init_descriptor_tables();
    pmm_init(total_system_memory);
    outb(0x21, 0x11); outb(0xA1, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
    outb(0xA1, 0xFF); outb(0x21, 0xFE);
    uint16_t divisor = 1193182 / 1000;
    outb(0x43, 0x36); outb(0x40, (uint8_t)divisor); outb(0x40, (uint8_t)(divisor >> 8));
    asm volatile("sti");
    fs_initialize();
    struct fs_node* user = find_node(root, "User");
    server_webroot = create_node("www", FS_DIRECTORY, user);
    if (server_webroot) {
        struct fs_node* index = create_node("index.html", FS_FILE, server_webroot);
        if (index) {
            const char* page = "<!doctype html><html><head><meta charset=\"utf-8\"><title>SharkOS Server</title></head><body><h1>SharkOS Server</h1><p>HTTP service is running.</p></body></html>\n";
            index->content_len = (int)strlen(page);
            memcpy(index->content, page, (size_t)index->content_len + 1);
        }
        current_dir = server_webroot;
    }
    net_init();
    net_stack_init();
    int ssh_key_ready = ssh_server_init() == 0;
    if (net_has_nic()) {
        serial_puts("network adapter: "); serial_puts(net_driver_name); serial_puts("\r\n");
        serial_puts("network configuration: DHCP requested\r\n");
    } else serial_puts("network adapter not detected\r\n");
    serial_puts(ssh_key_ready ? "ephemeral Ed25519 SSH host key generated for this boot\r\n" : "SSH unavailable: CPU RDRAND is required for secure keys\r\n");
    serial_puts("services are stopped; use 'httpd start' or configure a password then run 'sshd start'\r\n");
    server_prompt();
    for (;;) {
        net_poll();
        httpd_poll();
        ssh_server_poll();
        int c = serial_getchar_nonblocking();
        if (c >= 0) server_input_char((char)c);
        else asm volatile("hlt");
    }
}
