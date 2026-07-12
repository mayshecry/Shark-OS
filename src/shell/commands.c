#include "kernel.h"
#include "sharkscript.h"
#include "plugin_manager.h"
#include "doom.h"
#include "net.h"
#include <stdint.h>

static int simple_atoi(const char* s) {
    int result = 0;
    while (*s >= '0' && *s <= '9') {
        result = result * 10 + (*s - '0');
        s++;
    }
    return result;
}

void print_prompt() {
    size_t pane_idx = active_pane;
    panes[pane_idx].col = panes[pane_idx].col_start;
    terminal_column = panes[pane_idx].col_start;

    if (terminal_row < content_first_row) {
        terminal_row = content_first_row;
    }

    uint8_t prompt_color = vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    terminal_set_color(prompt_color);
    terminal_writestring("Z;");

    char path_buf[128];
    char rev[128];
    struct fs_node* n = current_dir;
    int plen = 0;
    rev[plen++] = '/';
    while (n && n->parent) {
        int namelen = strlen(n->name);
        for (int k = namelen - 1; k >= 0; k--) rev[plen++] = n->name[k];
        rev[plen++] = '/';
        n = n->parent;
    }
    int j = 0;
    for (int k = plen - 1; k >= 0; k--) path_buf[j++] = rev[k];
    if (j > 0 && path_buf[j-1] != '/') path_buf[j++] = '/';
    path_buf[j] = '\0';

    terminal_writestring(path_buf);
    terminal_writestring(">");

    terminal_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    panes[pane_idx].prompt_end_col = terminal_column;
    draw_cursor();
}

static void print_tree(struct fs_node* node, int depth) {
    for (int k = 0; k < depth; k++) terminal_writestring("  ");
    terminal_writestring("|- ");
    terminal_writestring(node->name);
    if (node->type == FS_DIRECTORY) terminal_writestring("/");
    terminal_writestring("\n");
    if (node->type == FS_DIRECTORY) {
        for (int k = 0; k < node->num_children; k++) {
            if (k % 3 == 0) yield();
            print_tree(node->children[k], depth + 1);
        }
    }
}

typedef void (*cmd_handler_t)(const char* args);

typedef struct {
    const char* name;
    cmd_handler_t handler;
} cmd_entry_t;

static void cmd_ls(const char* args) {
    int long_format = 0;
    int show_all = 0;
    if (strcmp(args, "-l") == 0) long_format = 1;
    if (strcmp(args, "-a") == 0) show_all = 1;
    if (strcmp(args, "-la") == 0 || strcmp(args, "-al") == 0) { long_format = 1; show_all = 1; }
    if (long_format) {
        terminal_writestring("total ");
        char buf[16];
        int_to_string(current_dir->num_children, buf);
        terminal_writestring(buf);
        terminal_writestring("\n");
    }
    for (int i = 0; i < current_dir->num_children; i++) {
        if (!show_all && current_dir->children[i]->name[0] == '.') continue;
        if (long_format) {
            terminal_writestring(current_dir->children[i]->type == FS_DIRECTORY ? "d" : "-");
            terminal_writestring("rw-r--r--  ");
            terminal_writestring("1 ");
            terminal_writestring(current_user);
            terminal_writestring("  ");
            char sizebuf[16];
            int_to_string(current_dir->children[i]->content_len, sizebuf);
            terminal_writestring(sizebuf);
            terminal_writestring(" ");
        }
        terminal_writestring(current_dir->children[i]->name);
        if (current_dir->children[i]->type == FS_DIRECTORY) terminal_writestring("/");
        terminal_writestring("  ");
        if (i % 4 == 0) yield();
    }
}

static void cmd_cd(const char* args) {
    if (strcmp(args, "..") == 0) {
        if (current_dir->parent) current_dir = current_dir->parent;
    } else if (strcmp(args, "/") == 0 || strcmp(args, "root") == 0) {
        current_dir = root;
    } else {
        struct fs_node* target = find_node(current_dir, args);
        if (target && target->type == FS_DIRECTORY) current_dir = target;
        else terminal_writestring("Directory not found.");
    }
}

static void cmd_pwd(const char* args) {
    (void)args;
    char path[256];
    char rev[256];
    struct fs_node* n = current_dir;
    int plen = 0;
    rev[plen++] = '/';
    while (n && n->parent) {
        int namelen = strlen(n->name);
        for (int k = namelen - 1; k >= 0; k--) rev[plen++] = n->name[k];
        rev[plen++] = '/';
        n = n->parent;
    }
    int j = 0;
    for (int k = plen - 1; k >= 0; k--) path[j++] = rev[k];
    if (j > 0 && path[j-1] != '/') path[j++] = '/';
    path[j] = '\0';
    terminal_writestring(path);
    terminal_writestring("\n");
}

static void cmd_cat(const char* args) {
    if (strcasecmp(args, "cpuinfo") == 0 || (strchr(args, '/') && strstr(args, "cpuinfo"))) {
        char cpu_model[49];
        get_cpu_model(cpu_model);
        terminal_writestring("processor\t: 0\n");
        terminal_writestring("vendor_id\t: GenuineIntel\n");
        terminal_writestring("cpu family\t: 6\n");
        terminal_writestring("model\t\t: 42\n");
        terminal_writestring("model name\t: ");
        terminal_writestring(cpu_model);
        terminal_writestring("\n");
        terminal_writestring("stepping\t: 7\n");
        terminal_writestring("cpu MHz\t\t: 2394.466\n");
        terminal_writestring("cache size\t: 6144 KB\n");
        terminal_writestring("fpu\t\t: yes\n");
        terminal_writestring("fpu_exception\t: yes\n");
        terminal_writestring("cpuid level\t: 5\n");
        terminal_writestring("wp\t\t: yes\n");
        terminal_writestring("flags\t\t: fpu vme de pse tsc msr pae mce cx8 apic\n");
    } else if (strcasecmp(args, "meminfo") == 0 || (strchr(args, '/') && strstr(args, "meminfo"))) {
        char buf[64];
        uint32_t total_kb = (uint32_t)total_system_memory;
        uint32_t free_kb = (uint32_t)(total_system_memory * 3 / 4);
        uint32_t avail_kb = (uint32_t)(total_system_memory * 7 / 8);
        uint32_t buffers_kb = (uint32_t)(total_system_memory / 16);
        uint32_t cached_kb = (uint32_t)(total_system_memory / 8);
        terminal_writestring("MemTotal:\t");
        int_to_string(total_kb, buf); terminal_writestring(buf); terminal_writestring(" kB\n");
        terminal_writestring("MemFree:\t");
        int_to_string(free_kb, buf); terminal_writestring(buf); terminal_writestring(" kB\n");
        terminal_writestring("MemAvailable:\t");
        int_to_string(avail_kb, buf); terminal_writestring(buf); terminal_writestring(" kB\n");
        terminal_writestring("Buffers:\t");
        int_to_string(buffers_kb, buf); terminal_writestring(buf); terminal_writestring(" kB\n");
        terminal_writestring("Cached:\t");
        int_to_string(cached_kb, buf); terminal_writestring(buf); terminal_writestring(" kB\n");
        terminal_writestring("SwapTotal:\t0 kB\n");
        terminal_writestring("SwapFree:\t0 kB\n");
    } else {
        struct fs_node* target = find_node(current_dir, args);
        if (target && target->type == FS_FILE) {
            int yc = 0;
            for (int idx = 0; idx < target->content_len; idx++) {
                terminal_putchar(target->content[idx]);
                if (target->content[idx] == '\n') {
                    yc++;
                    if (yc % 4 == 0) yield();
                }
            }
        } else {
            terminal_writestring("File not found.");
        }
    }
}

static void cmd_clear(const char* args) {
    (void)args;
    terminal_clear();
}

static void cmd_echo(const char* args) {
    terminal_writestring(args);
    terminal_writestring("\n");
}

static void cmd_edit(const char* args) {
    if (strlen(args) == 0) {
        terminal_writestring("Usage: edit <filename>");
        return;
    }
    editor_target_file = find_node(current_dir, args);
    if (!editor_target_file) {
        terminal_writestring("File not found. Creating new file...\n");
        editor_target_file = create_node(args, FS_FILE, current_dir);
    }
    if (editor_target_file) {
        editor_buffer_idx = 0;
        current_kernel_mode = KERNEL_MODE_EDITOR;
        terminal_writestring("Editor: ");
        terminal_writestring(args);
        terminal_writestring(" (ESC to save and exit)\n");
        strcpy(editor_buffer, editor_target_file->content);
        editor_buffer_idx = editor_target_file->content_len;
    } else {
        terminal_writestring("Error: Cannot create file.\n");
    }
}

static void cmd_whoami(const char* args) {
    (void)args;
    terminal_writestring(current_user);
    terminal_writestring("\n");
}

static void cmd_help(const char* args) {
    (void)args;
    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK));
    terminal_writestring("\nSharkOS Help\n------------------------------\n\n");
    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("FILESYSTEM  - ");
    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("ls, cd, cat, touch, edit, mkdir, rm, mv, cp, pwd, tree\n");
    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("SYSTEM      - ");
    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("whoami, sysinfo, kernelinfo, colors, lspci, ps, kill, exec\n");
    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("APPS        - ");
    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("clear, colors, credits, help, whatis, fortune, cowsay, sl, banner, doom\n");
    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("POWER       - ");
    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("poweroff, reboot\n");
    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("KEYS        - ");
    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("?, +, -, TAB, Ctrl+S, ESC\n\n");
    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK));
    terminal_writestring("Type 'help <category>' for details\n");
    terminal_set_color(vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
    terminal_writestring("Categories: Filesystem, System, Apps, Power, Keys\n\n");
    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

static void cmd_uname(const char* args) {
    terminal_writestring("SharkOS");
    if (strcmp(args, "-a") == 0) {
        terminal_writestring(" V1 i686 shark@SharkOS nemo");
    }
    terminal_writestring("\n");
}

static void cmd_sysinfo(const char* args) {
    (void)args;
    char buf[64];
    terminal_writestring("\033[0;36m");
    terminal_writestring("╔══════════════════════════════════╗\n");
    terminal_writestring("║       SharkOS System Info        ║\n");
    terminal_writestring("╚══════════════════════════════════╝\n");
    terminal_writestring("\033[0;37m");

    char cpu_model[49];
    get_cpu_model(cpu_model);
    terminal_writestring("CPU: ");
    terminal_writestring(cpu_model);
    terminal_writestring("\n");

    terminal_writestring("RAM: ");
    uint32_t total_mb = (uint32_t)(total_system_memory >> 20);
    int_to_string(total_mb, buf);
    terminal_writestring(buf);
    terminal_writestring(" MB\n");

    terminal_writestring("Resolution: ");
    int_to_string(screen_width, buf); terminal_writestring(buf);
    terminal_writestring("x");
    int_to_string(screen_height, buf); terminal_writestring(buf);
    terminal_writestring("\n");

    uint32_t up_secs = uptime_ticks / 100;
    uint32_t hours = up_secs / 3600;
    uint32_t mins = (up_secs % 3600) / 60;
    uint32_t secs = up_secs % 60;
    terminal_writestring("Uptime: ");
    int_to_string(hours, buf); terminal_writestring(buf); terminal_writestring("h ");
    int_to_string(mins, buf); terminal_writestring(buf); terminal_writestring("m ");
    int_to_string(secs, buf); terminal_writestring(buf); terminal_writestring("s\n");

    terminal_writestring("Tasks: ");
    int task_count = 0;
    task_t* t;
    for (t = task_list; t; t = t->next) task_count++;
    int_to_string(task_count, buf); terminal_writestring(buf);
    terminal_writestring("\n");

    terminal_writestring("\033[0;32m");
    terminal_writestring("SharkOS V2 - The Sharkslayer Edition\n");
    terminal_writestring("\033[0;37m");
}

static void cmd_hostname(const char* args) {
    (void)args;
    terminal_writestring("SharkOS");
    terminal_writestring("\n");
}

static void cmd_ps(const char* args) {
    (void)args;
    spin_lock(&task_list_lock);
    terminal_writestring("PID    NAME            STATE      CPU\n");
    task_t* t = task_list;
    while (t) {
        char buf[16];
        int_to_string(t->id, buf);
        terminal_writestring(buf);
        terminal_writestring("      ");
        terminal_writestring(t->name);
        terminal_writestring("          ");
        if (t->state == TASK_RUNNING) terminal_writestring("RUNNING    ");
        else terminal_writestring("READY      ");
        int_to_string(t->cpu_id, buf);
        terminal_writestring(buf);
        terminal_writestring("\n");
        t = t->next;
    }
    spin_unlock(&task_list_lock);
}

static void cmd_htop(const char* args) {
    (void)args;
    spin_lock(&task_list_lock);

    uint32_t total_syscalls = 0;
    task_t* t;
    for (t = task_list; t; t = t->next) total_syscalls += t->syscall_count;

    char buf[128];
    terminal_writestring("\n\033[0;32m");
    terminal_writestring("htop - SharkOS Task Manager\n");
    terminal_writestring("\033[0;37m");
    terminal_writestring("────────────────────────────────────────\n");

    uint32_t up_secs = uptime_ticks / 100;
    uint32_t hours = up_secs / 3600;
    uint32_t mins = (up_secs % 3600) / 60;
    uint32_t secs = up_secs % 60;
    terminal_writestring("Uptime: ");
    int_to_string(hours, buf); terminal_writestring(buf); terminal_writestring("h ");
    int_to_string(mins, buf); terminal_writestring(buf); terminal_writestring("m ");
    int_to_string(secs, buf); terminal_writestring(buf); terminal_writestring("s  |  ");

    int task_count = 0;
    for (t = task_list; t; t = t->next) task_count++;
    terminal_writestring("Tasks: ");
    int_to_string(task_count, buf);
    terminal_writestring(buf);
    terminal_writestring("  |  Syscalls: ");
    int_to_string(total_syscalls, buf);
    terminal_writestring(buf);
    terminal_writestring("\033[0;37m\n");

    terminal_writestring("\033[0;36mPID    NAME            STATE      CPU     SYSCALLS\033[0;37m\n");
    terminal_writestring("─────────────────────────────────────────────────\n");

    for (t = task_list; t; t = t->next) {
        uint32_t cpu_pct = total_syscalls > 0 ? (t->syscall_count * 100 / total_syscalls) : 0;

        terminal_writestring("\033[0;33m");
        int_to_string(t->id, buf);
        terminal_writestring(buf);
        terminal_writestring("      \033[0;36m");
        terminal_writestring(t->name);
        int len = strlen(t->name);
        for (int i = len; i < 15; i++) terminal_writestring(" ");
        terminal_writestring("\033[0;37m");

        if (t->state == TASK_RUNNING) terminal_writestring("RUNNING   ");
        else if (t->state == TASK_READY) terminal_writestring("READY     ");
        else if (t->state == TASK_SLEEPING) terminal_writestring("SLEEPING  ");
        else terminal_writestring("ZOMBIE    ");

        terminal_writestring("\033[0;32m");
        int_to_string(cpu_pct, buf);
        terminal_writestring(buf);
        for (int i = strlen(buf); i < 5; i++) terminal_writestring(" ");
        terminal_writestring("%  \033[0;35m");

        int_to_string(t->syscall_count, buf);
        terminal_writestring(buf);
        terminal_writestring("\033[0;37m\n");
    }

    terminal_writestring("────────────────────────────────────────\n");
    terminal_writestring("\033[0;37m");

    spin_unlock(&task_list_lock);
}

static void cmd_uptime(const char* args) {
    (void)args;
    uint32_t total_secs = uptime_ticks / 100;
    uint32_t hours = total_secs / 3600;
    uint32_t mins = (total_secs % 3600) / 60;
    uint32_t secs = total_secs % 60;
    char buf[32];
    terminal_writestring("Uptime: ");
    int_to_string(hours, buf); terminal_writestring(buf); terminal_writestring("h ");
    int_to_string(mins, buf); terminal_writestring(buf); terminal_writestring("m ");
    int_to_string(secs, buf); terminal_writestring(buf); terminal_writestring("s\n");
}

static void cmd_poweroff(const char* args) {
    (void)args;
    terminal_writestring("Shutting down...\n");
    shutdown();
}

static void cmd_reboot(const char* args) {
    (void)args;
    terminal_writestring("Rebooting...\n");
    outb(0x64, 0xFE);
    asm volatile("cli; hlt");
}

static void cmd_bokop(const char* args) {
    (void)args;
    terminal_writestring("bokop command executed.\n");
}

static void cmd_neofetch(const char* args) {
    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK));
    terminal_writestring("\n");
    terminal_writestring("       ⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⡠⢄⣀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀\n");
    terminal_writestring("       ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⢣⠀⠀⠉⣳⠤⠒⢊⠉⠉⠉⠉⡍⡉⠉⡐⠒⡲⢦⠀⠀\n");
    terminal_writestring("       ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣇⠔⠋⠀⠀⠀⡸⡄⠀⠀⠀⠠⣦⣇⡲⣪⣠⠋⠀⠀\n");
    terminal_writestring("       ⠀⠀⠀⠀⠀⠀⠀⠀⢀⡔⠁⠀⠀⠀⠀⠰⣃⣷⢆⠠⠒⢈⣵⣯⣿⠷⠃⠀⠀⠀\n");
    terminal_writestring("       ⠀⠀⠀⠀⠀⠀⠀⣠⠊⠀⠀⠀⠀⡀⡠⠀⠀⣹⠁⠀⠀⢀⣀⠴⠊⠀⠀⠀⠀⠀\n");
    terminal_writestring("       ⠀⠀⠀⠀⠀⠀⡰⠁⢀⠠⢄⣂⠥⢼⠀⠀⣠⠧⠒⠲⡊⢿⡆⠀⠀⠀⠀⠀⠀⠀\n");
    terminal_writestring("       ⠀⠀⣠⠴⠒⡲⣡⡴⠗⠋⠁⠀⠀⢸⠀⡴⠁⠀⠀⠀⠙⡼⠃⠀⠀⠀⠀⠀⠀⠀\n");
    terminal_writestring("       ⠀⠀⠉⠉⠒⡟⠉⡆⠀⠀⠀⠀⠀⠘⠼⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀\n");
    terminal_writestring("       ⠀⠀⠀⠀⠀⠸⣴⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀\n");
    terminal_writestring("\n");
    terminal_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    
    char buf[64];
    uint32_t total_mb = (uint32_t)(total_system_memory >> 20);
    uint32_t up_secs = uptime_ticks / 100;
    uint32_t hours = up_secs / 3600;
    uint32_t mins = (up_secs % 3600) / 60;
    uint32_t secs = up_secs % 60;
    
    terminal_writestring("\033[0;36m");
    terminal_writestring("  OS: \033[0;37mSharkOS V2 - The Sharkslayer Edition\n");
    terminal_writestring("\033[0;36m");
    terminal_writestring("  Host: \033[0;37mSharkOS PC\n");
    terminal_writestring("\033[0;36m");
    terminal_writestring("  Kernel: \033[0;37mSharkOS V2.2\n");
    terminal_writestring("\033[0;36m");
    terminal_writestring("  Shell: \033[0;37mnemo-shell\n");
    terminal_writestring("\033[0;36m");
    terminal_writestring("  Resolution: \033[0;37m");
    int_to_string(screen_width, buf); terminal_writestring(buf);
    terminal_writestring("x");
    int_to_string(screen_height, buf); terminal_writestring(buf);
    terminal_writestring("\n");
    terminal_writestring("\033[0;36m");
    terminal_writestring("  CPU: \033[0;37m");
    char cpu_model[49];
    get_cpu_model(cpu_model);
    terminal_writestring(cpu_model);
    terminal_writestring("\n");
    terminal_writestring("\033[0;36m");
    terminal_writestring("  Memory: \033[0;37m");
    int_to_string(total_mb, buf); terminal_writestring(buf);
    terminal_writestring(" MB\n");
    terminal_writestring("\033[0;36m");
    terminal_writestring("  Uptime: \033[0;37m");
    int_to_string(hours, buf); terminal_writestring(buf); terminal_writestring("h ");
    int_to_string(mins, buf); terminal_writestring(buf); terminal_writestring("m ");
    int_to_string(secs, buf); terminal_writestring(buf); terminal_writestring("s\n");
    terminal_writestring("\033[0;36m");
    terminal_writestring("  Theme: \033[0;37mSharkOS Blue\n");
    terminal_writestring("\033[0;37m");
}

static void cmd_ifconfig(const char* args) {
    (void)args;
    net_cmd_ifconfig();
}
static void cmd_dhcp(const char* args) {
    (void)args;
    net_cmd_dhcp();
}
static void cmd_ping(const char* args) {
    if (strlen(args) == 0) {
        terminal_writestring("Usage: ping <ip>\n");
        return;
    }
    net_cmd_ping(args);
}
static void cmd_wget(const char* args) {
    if (strlen(args) == 0) {
        terminal_writestring("Usage: wget <url>\n");
        return;
    }
    net_cmd_wget(args);
}
static void cmd_dns(const char* args) {
    if (strlen(args) == 0) {
        terminal_writestring("Usage: dns <hostname>\n");
        return;
    }
    net_cmd_dns(args);
}
static void cmd_netstat(const char* args) {
    (void)args;
    net_cmd_netstat();
}

static const cmd_entry_t cmd_table[] = {
    {"ls", cmd_ls},
    {"dir", cmd_ls},
    {"cd", cmd_cd},
    {"pwd", cmd_pwd},
    {"cat", cmd_cat},
    {"clear", cmd_clear},
    {"cls", cmd_clear},
    {"echo", cmd_echo},
    {"edit", cmd_edit},
    {"whoami", cmd_whoami},
    {"help", cmd_help},
    {"uname", cmd_uname},
    {"sysinfo", cmd_sysinfo},
    {"hostname", cmd_hostname},
    {"ps", cmd_ps},
    {"htop", cmd_htop},
    {"uptime", cmd_uptime},
    {"neofetch", cmd_neofetch},
    {"poweroff", cmd_poweroff},
    {"reboot", cmd_reboot},
    {"bokop", cmd_bokop},
    {"ifconfig", cmd_ifconfig},
    {"dhcp", cmd_dhcp},
    {"ping", cmd_ping},
    {"wget", cmd_wget},
    {"dns", cmd_dns},
    {"netstat", cmd_netstat},
};

#define CMD_TABLE_SIZE (sizeof(cmd_table) / sizeof(cmd_table[0]))

static cmd_handler_t find_command(const char* name) {
    for (unsigned int i = 0; i < CMD_TABLE_SIZE; i++) {
        if (strcmp(cmd_table[i].name, name) == 0) {
            return cmd_table[i].handler;
        }
    }
    return NULL;
}

void execute_command(char* cmd) {
    if (strlen(cmd) == 0) return;
    uint8_t old_color = terminal_color;
    terminal_putchar('\n');

    char cmd_name[32];
    int i = 0;
    while(cmd[i] != ' ' && cmd[i] != '\0' && i < 31) {
        cmd_name[i] = cmd[i];
        i++;
    }
    cmd_name[i] = '\0';
    uint32_t plugin_id;
    char* args = (cmd[i] == ' ') ? &cmd[i+1] : "";
    while (*args == ' ') args++;

    cmd_handler_t handler = find_command(cmd_name);
    if (handler) {
        handler(args);
        terminal_set_color(old_color);
        return;
    }

    if (strcmp(cmd_name, "tree") == 0) {
        print_tree(current_dir, 0);
    } else if (strcmp(cmd_name, "find") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: find <name>");
        } else {
            struct fs_node* stack[64];
            int sp = 0;
            stack[sp++] = root;
            int found = 0;
            while (sp > 0) {
                yield();
                struct fs_node* n = stack[--sp];
                if (strstr(n->name, args)) {
                    terminal_writestring(n->name);
                    terminal_writestring("\n");
                    found++;
                }
                if (n->type == FS_DIRECTORY && n != root) {
                    for (int k = 0; k < n->num_children; k++) {
                        if (sp < 64) stack[sp++] = n->children[k];
                    }
                }
            }
            if (!found) terminal_writestring("No matches found.");
        }
    } else if (strcmp(cmd_name, "grep") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: grep <pattern> <file>");
        } else {
            char pattern[32];
            char filename[32];
            int s = 0, d = 0;
            char* p = args;
            while (*p && *p != ' ' && s < 31) pattern[s++] = *p++;
            pattern[s] = '\0';
            if (*p == ' ') p++;
            while (*p && d < 31) filename[d++] = *p++;
            filename[d] = '\0';
            if (s == 0 || d == 0) {
                terminal_writestring("Usage: grep <pattern> <file>");
            } else {
                struct fs_node* target = find_node(current_dir, filename);
                if (target && target->type == FS_FILE) {
                    int found = 0;
                    char line[128];
                    int li = 0;
                    for (int idx = 0; idx < target->content_len; idx++) {
                        char c = target->content[idx];
                        if (c == '\n' || idx == target->content_len - 1) {
                            if (idx == target->content_len - 1 && c != '\n') {
                                if (li < 127) line[li++] = c;
                            }
                            line[li] = '\0';
                            if (strstr(line, pattern)) {
                                terminal_writestring(line);
                                terminal_writestring("\n");
                                found++;
                            }
                            li = 0;
                        } else {
                            if (li < 127) line[li++] = c;
                        }
                    }
                    if (!found) terminal_writestring("No matches found.");
                } else {
                    terminal_writestring("File not found.");
                }
            }
        }
    } else if (strcmp(cmd_name, "stat") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: stat <file>");
        } else {
            struct fs_node* target = find_node(current_dir, args);
            if (target) {
                terminal_writestring("File: ");
                terminal_writestring(target->name);
                terminal_writestring("\nType: ");
                terminal_writestring(target->type == FS_DIRECTORY ? "directory" : "file");
                terminal_writestring("\nSize: ");
                char buf[16];
                int_to_string(target->content_len, buf);
                terminal_writestring(buf);
                terminal_writestring(" bytes\n");
                if (target->parent) {
                    terminal_writestring("Parent: ");
                    terminal_writestring(target->parent->name);
                    terminal_writestring("\n");
                }
            } else {
                terminal_writestring("File not found.");
            }
        }
    } else if (strcmp(cmd_name, "neofetch") == 0) {
        terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK));
        terminal_writestring("\n");
        terminal_writestring("       ⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⡠⢄⣀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀\n");
        terminal_writestring("       ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⢣⠀⠀⠉⣳⠤⠒⢊⠉⠉⠉⠉⡍⡉⠉⡐⠒⡲⢦⠀⠀\n");
        terminal_writestring("       ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣇⠔⠋⠀⠀⠀⡸⡄⠀⠀⠀⠠⣦⣇⡲⣪⣠⠋⠀⠀\n");
        terminal_writestring("       ⠀⠀⠀⠀⠀⠀⠀⠀⢀⡔⠁⠀⠀⠀⠀⠰⣃⣷⢆⠠⠒⢈⣵⣯⣿⠷⠃⠀⠀⠀\n");
        terminal_writestring("       ⠀⠀⠀⠀⠀⠀⠀⣠⠊⠀⠀⠀⠀⡀⡠⠀⠀⣹⠁⠀⠀⢀⣀⠴⠊⠀⠀⠀⠀⠀\n");
        terminal_writestring("       ⠀⠀⠀⠀⠀⠀⡰⠁⢀⠠⢄⣂⠥⢼⠀⠀⣠⠧⠒⠲⡊⢿⡆⠀⠀⠀⠀⠀⠀⠀\n");
        terminal_writestring("       ⠀⠀⣠⠴⠒⡲⣡⡴⠗⠋⠁⠀⠀⢸⠀⡴⠁⠀⠀⠀⠙⡼⠃⠀⠀⠀⠀⠀⠀⠀\n");
        terminal_writestring("       ⠀⠀⠉⠉⠒⡟⠉⡆⠀⠀⠀⠀⠀⠘⠼⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀\n");
        terminal_writestring("       ⠀⠀⠀⠀⠀⠸⣴⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀\n");
        terminal_writestring("\n");
        terminal_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        
        char buf[128];
        uint32_t total_mb = (uint32_t)(total_system_memory >> 20);
        uint32_t total_kb = (uint32_t)total_system_memory;
        uint32_t free_kb = (uint32_t)(total_system_memory * 3 / 4);
        uint32_t avail_kb = (uint32_t)(total_system_memory * 7 / 8);
        uint32_t up_secs = uptime_ticks / 100;
        uint32_t hours = up_secs / 3600;
        uint32_t mins = (up_secs % 3600) / 60;
        uint32_t secs = up_secs % 60;
        int task_count = 0;
        task_t* t;
        for (t = task_list; t; t = t->next) task_count++;
        
        char cpu_model[49];
        get_cpu_model(cpu_model);
        
        terminal_writestring("\033[0;36m");
        terminal_writestring("  OS: \033[0;37mSharkOS V2 - The Sharkslayer Edition\n");
        terminal_writestring("\033[0;36m");
        terminal_writestring("  Host: \033[0;37mSharkOS PC\n");
        terminal_writestring("\033[0;36m");
        terminal_writestring("  Kernel: \033[0;37mSharkOS V2.2\n");
        terminal_writestring("\033[0;36m");
        terminal_writestring("  Shell: \033[0;37mnemo-shell\n");
        terminal_writestring("\033[0;36m");
        terminal_writestring("  Resolution: \033[0;37m");
        int_to_string(screen_width, buf); terminal_writestring(buf);
        terminal_writestring("x");
        int_to_string(screen_height, buf); terminal_writestring(buf);
        terminal_writestring("\n");
        terminal_writestring("\033[0;36m");
        terminal_writestring("  CPU: \033[0;37m");
        terminal_writestring(cpu_model);
        terminal_writestring("\n");
        terminal_writestring("\033[0;36m");
        terminal_writestring("  CPU Speed: \033[0;37m2394.466 MHz\n");
        terminal_writestring("\033[0;36m");
        terminal_writestring("  Cache: \033[0;37m6144 KB\n");
        terminal_writestring("\033[0;36m");
        terminal_writestring("  Memory: \033[0;37m");
        int_to_string(total_mb, buf); terminal_writestring(buf);
        terminal_writestring(" MB\n");
        terminal_writestring("\033[0;36m");
        terminal_writestring("  Memory Free: \033[0;37m");
        int_to_string((uint32_t)(free_kb >> 10), buf); terminal_writestring(buf);
        terminal_writestring(" MB\n");
        terminal_writestring("\033[0;36m");
        terminal_writestring("  Memory Avail: \033[0;37m");
        int_to_string((uint32_t)(avail_kb >> 10), buf); terminal_writestring(buf);
        terminal_writestring(" MB\n");
        terminal_writestring("\033[0;36m");
        terminal_writestring("  Uptime: \033[0;37m");
        int_to_string(hours, buf); terminal_writestring(buf); terminal_writestring("h ");
        int_to_string(mins, buf); terminal_writestring(buf); terminal_writestring("m ");
        int_to_string(secs, buf); terminal_writestring(buf); terminal_writestring("s\n");
        terminal_writestring("\033[0;36m");
        terminal_writestring("  Tasks: \033[0;37m");
        int_to_string(task_count, buf); terminal_writestring(buf);
        terminal_writestring("\n");
        terminal_writestring("\033[0;36m");
        terminal_writestring("  Theme: \033[0;37mSharkOS Blue\n");
        terminal_writestring("\033[0;37m");
    } else if (strcmp(cmd_name, "fortune") == 0) {
        const char* fortunes[] = {
            "🦈 The shark is always hungry.",
            "Code is poetry written for machines.",
            "In SharkOS we trust.",
            "Segfault is just a feature.",
            "Keep swimming, keep coding.",
            "0xDEADBEEF is a tasty snack.",
            "Beware of off-by-one errors.",
            "The kernel is always watching.",
            "printf(\"Hello World\");\n",
            "rm -rf / --no-preserve-root\n"
        };
        int idx = (uptime_ticks / 7) % 10;
        terminal_writestring(fortunes[idx]);
        terminal_writestring("\n");
    } else if (strcmp(cmd_name, "cowsay") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: cowsay <text>");
        } else {
            terminal_writestring(" ");
            for (i = 0; i < (int)strlen(args) + 2; i++) terminal_writestring("_");
            terminal_writestring("\n< ");
            terminal_writestring(args);
            terminal_writestring(" >\n ");
            for (i = 0; i < (int)strlen(args) + 2; i++) terminal_writestring("-");
            terminal_writestring("\n        \\   ^__^\n         \\  (oo)\\_______\n            (__)\\       )\\/\\\n                ||----w |\n                ||     ||\n");
        }
    } else if (strcmp(cmd_name, "sl") == 0) {
        terminal_writestring("      ====        ________                ___________ \n  _/ _  \\_      /        \\   \\_    _/    /           \\ \n /    \\   \\    /    /\\    \\    \\  /     /    /\\    /\\ \n/     /    /  /    /  \\    \\    \\/     /    /  \\   \\ \\ \n\\     \\   /  /    /    \\    \\    /     /    /    \\   \\ \\ \n \\     \\/   /    /      \\    \\  /     /    /      \\   \\ \\ \n  \\        /    /        \\    \\/     /    /        \\   \\ \\ \n   \\______/____/__________\\____\\____/____/__________\\___\\ \\ \n");
    } else if (strcmp(cmd_name, "banner") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: banner <text>");
        } else {
            terminal_writestring("\n ##   ##  #####  ##   ## ######\n ### ### ##   ### ### ###   ##\n ## # ## ##   ## ## # ##   ##\n ##   ## #####  ##   ## ######\n ##   ## ##   ## ##   ## ##   ##\n ##   ## ##   ## ##   ## ##   ##\n ##   ##  #####  ##   ## ######\n\n");
            terminal_writestring(args);
            terminal_writestring("\n");
        }
    } else if (strcmp(cmd_name, "guess") == 0) {
        int secret = (uptime_ticks % 100) + 1;
        terminal_writestring("Guess the number (1-100): ");
        char numbuf[16];
        int ni = 0;
        while (ni < 15) {
            yield();
            char nc = keyboard_getchar();
            if (nc == '\n') break;
            if (nc == '\b') { if (ni > 0) ni--; continue; }
            if (nc >= '0' && nc <= '9') numbuf[ni++] = nc;
        }
        numbuf[ni] = '\0';
        int guess = 0;
        for (i = 0; i < ni; i++) guess = guess * 10 + (numbuf[i] - '0');
        if (guess == secret) terminal_writestring("\nCorrect! You win!");
        else if (guess < secret) terminal_writestring("\nToo low!");
        else terminal_writestring("\nToo high!");
        terminal_writestring(" (answer was ");
        char sbuf[16]; int_to_string(secret, sbuf); terminal_writestring(sbuf);
        terminal_writestring(")\n");
    } else if (strcmp(cmd_name, "tictactoe") == 0) {
        char board[9];
        for (i = 0; i < 9; i++) board[i] = ' ';
        int moves = 0;
        int player_score = 0, cpu_score = 0, draws = 0;
        int play_again = 1;
        while (play_again) {
            moves = 0;
            for (i = 0; i < 9; i++) board[i] = ' ';
            terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
            terminal_writestring("\n╔═══════════════════════════╗\n");
            terminal_writestring("║      TIC TAC TOE         ║\n");
            terminal_writestring("╚═══════════════════════════╝\n");
            terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
            terminal_writestring("You = X   CPU = O\n");
            terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK));
            terminal_writestring("Score: You ");
            char sbuf[16]; int_to_string(player_score, sbuf); terminal_writestring(sbuf);
            terminal_writestring(" - ");
            int_to_string(cpu_score, sbuf); terminal_writestring(sbuf);
            terminal_writestring(" CPU  (Draws: ");
            int_to_string(draws, sbuf); terminal_writestring(sbuf);
            terminal_writestring(")\n\n");
            while (moves < 9) {
                terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                terminal_writestring("  ");
                for (i = 0; i < 9; i++) {
                    if (board[i] == 'X') terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
                    else if (board[i] == 'O') terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
                    else terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                    terminal_putchar(board[i]);
                    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                    if (i % 3 != 2) terminal_writestring(" │ ");
                    else if (i != 8) terminal_writestring("\n  ───┼───┼───\n  ");
                }
                terminal_writestring("\n\n");
                int pos = 0;
                while (pos < 1 || pos > 9) {
                    yield();
                    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
                    terminal_writestring("Your move (1-9, 0=quit): ");
                    char nc = keyboard_getchar();
                    if (nc == 0) continue;
                    if (nc == '0') { moves = 9; break; }
                    if (nc >= '1' && nc <= '9') {
                        pos = nc - '0';
                        terminal_putchar(nc);
                        terminal_writestring("\n");
                    }
                }
                if (pos == 0) break;
                if (board[pos - 1] != ' ') {
                    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
                    terminal_writestring("Spot taken! Try again.\n\n");
                    continue;
                }
                board[pos - 1] = 'X';
                moves++;
                int win = 0;
                int wins[8][3] = {{0,1,2},{3,4,5},{6,7,8},{0,3,6},{1,4,7},{2,5,8},{0,4,8},{2,4,6}};
                for (int w = 0; w < 8; w++) {
                    if (board[wins[w][0]] == 'X' && board[wins[w][1]] == 'X' && board[wins[w][2]] == 'X') win = 1;
                }
                if (win) {
                    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
                    terminal_writestring("\n★ You win! ★\n");
                    player_score++;
                    break;
                }
                if (moves >= 9) {
                    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK));
                    terminal_writestring("\nDraw!\n");
                    draws++;
                    break;
                }
                int cpu_pos = -1;
                for (int w = 0; w < 8; w++) {
                    int a = wins[w][0], b = wins[w][1], c = wins[w][2];
                    if (board[a] == 'O' && board[b] == 'O' && board[c] == ' ') cpu_pos = c + 1;
                    if (board[a] == 'O' && board[c] == 'O' && board[b] == ' ') cpu_pos = b + 1;
                    if (board[b] == 'O' && board[c] == 'O' && board[a] == ' ') cpu_pos = a + 1;
                }
                if (cpu_pos == -1) {
                    for (int w = 0; w < 8; w++) {
                        int a = wins[w][0], b = wins[w][1], c = wins[w][2];
                        if (board[a] == 'X' && board[b] == 'X' && board[c] == ' ') cpu_pos = c + 1;
                        if (board[a] == 'X' && board[c] == 'X' && board[b] == ' ') cpu_pos = b + 1;
                        if (board[b] == 'X' && board[c] == 'X' && board[a] == ' ') cpu_pos = a + 1;
                    }
                }
                if (cpu_pos == -1 && board[4] == ' ') cpu_pos = 5;
                if (cpu_pos == -1) {
                    int corners[4] = {1, 3, 7, 9};
                    for (i = 0; i < 4; i++) {
                        if (board[corners[i] - 1] == ' ') { cpu_pos = corners[i]; break; }
                    }
                }
                if (cpu_pos == -1) {
                    for (i = 0; i < 9; i++) {
                        if (board[i] == ' ') { cpu_pos = i + 1; break; }
                    }
                }
                board[cpu_pos - 1] = 'O';
                moves++;
                terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
                terminal_writestring("CPU plays at position ");
                char pbuf[4]; int_to_string(cpu_pos, pbuf); terminal_writestring(pbuf);
                terminal_writestring("\n\n");
                win = 0;
                for (int w = 0; w < 8; w++) {
                    if (board[wins[w][0]] == 'O' && board[wins[w][1]] == 'O' && board[wins[w][2]] == 'O') win = 1;
                }
                if (win) {
                    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
                    terminal_writestring("\nCPU wins!\n");
                    cpu_score++;
                    break;
                }
                if (moves >= 9) {
                    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK));
                    terminal_writestring("\nDraw!\n");
                    draws++;
                    break;
                }
            }
            terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
            terminal_writestring("\nPlay again? (y/N): ");
            yield();
            char nc = keyboard_getchar();
            if (nc != 'y' && nc != 'Y') play_again = 0;
            terminal_writestring("\n");
        }
        terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        terminal_writestring("\nFinal Score: You ");
        char sbuf[16]; int_to_string(player_score, sbuf); terminal_writestring(sbuf);
        terminal_writestring(" - ");
        int_to_string(cpu_score, sbuf); terminal_writestring(sbuf);
        terminal_writestring(" CPU  (Draws: ");
        int_to_string(draws, sbuf); terminal_writestring(sbuf);
        terminal_writestring(")\n");
    } else if (strcmp(cmd_name, "touch") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: touch <file>");
        } else {
            struct fs_node* target = find_node(current_dir, args);
            if (!target) {
                struct fs_node* new_node = create_node(args, FS_FILE, current_dir);
                if (new_node) {
                    terminal_writestring("Created: ");
                    terminal_writestring(args);
                    terminal_writestring("\n");
                } else {
                    terminal_writestring("Error: Cannot create file (node pool full or directory full)\n");
                }
            } else {
                terminal_writestring("File already exists: ");
                terminal_writestring(args);
                terminal_writestring("\n");
            }
        }
    } else if (strcmp(cmd_name, "mkdir") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: mkdir <name>");
        } else if (find_node(current_dir, args)) {
            terminal_writestring("Directory already exists.");
        } else {
            create_node(args, FS_DIRECTORY, current_dir);
        }
    } else if (strcmp(cmd_name, "rm") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: rm <name>");
        } else {
            int use_force = 0;
            char rm_name[32];
            char* rp = args;
            if (rp[0] == 'f' && rp[1] == 'o' && rp[2] == 'r' && rp[3] == 'c' && rp[4] == 'e' && rp[5] == ' ') {
                use_force = 1;
                rp += 6;
            }
            int rs = 0;
            while (*rp && rs < 31) rm_name[rs++] = *rp++;
            rm_name[rs] = '\0';
            if (rs == 0) {
                terminal_writestring("Usage: rm [force] <name>");
            } else {
                struct fs_node* target = find_node(current_dir, rm_name);
                if (!target) {
                    terminal_writestring("Not found.");
                } else {
                    if (strcmp(target->name, "System") == 0 || strcmp(current_dir->name, "System") == 0) {
                        terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
                        terminal_writestring("\n\n");
                        terminal_writestring("╔══════════════════════════════════════════════════════════╗\n");
                        terminal_writestring("║                    KERNEL PANIC                         ║\n");
                        terminal_writestring("╚══════════════════════════════════════════════════════════╝\n\n");
                        terminal_writestring("FATAL: Attempted to delete protected system file/directory!\n");
                        terminal_writestring("System integrity compromised. Kernel halted.\n\n");
                        terminal_writestring("ACTION REQUIRED:\n");
                        terminal_writestring("  1. Power off the device immediately\n");
                        terminal_writestring("  2. Remove the USB drive / boot media\n");
                        terminal_writestring("  3. Do not boot from this media again\n\n");
                        terminal_writestring("System will now freeze. Please shut down manually.\n");
                        while (1) {
                            asm volatile("hlt");
                        }
                    }
                    if (!use_force) {
                        terminal_writestring("Protected: use rm force <name> to delete");
                    } else {
                        int found = 0;
                        for (i = 0; i < current_dir->num_children; i++) {
                            if (current_dir->children[i] == target) {
                                for (int j = i; j < current_dir->num_children - 1; j++) {
                                    current_dir->children[j] = current_dir->children[j + 1];
                                }
                                current_dir->num_children--;
                                found = 1;
                                break;
                            }
                        }
                        if (found) terminal_writestring("Removed.");
                        else terminal_writestring("Could not remove.");
                    }
                }
            }
        }
    } else if (strcmp(cmd_name, "mv") == 0 || strcmp(cmd_name, "rename") == 0) {
        char src[32], dst[32];
        int s = 0, d = 0;
        char* p = args;
        while (*p && *p != ' ' && s < 31) src[s++] = *p++;
        src[s] = '\0';
        if (*p == ' ') p++;
        while (*p && d < 31) dst[d++] = *p++;
        dst[d] = '\0';
        if (s == 0 || d == 0) {
            terminal_writestring("Usage: mv <src> <dst>");
        } else {
            struct fs_node* target = find_node(current_dir, src);
            if (target) {
                strcpy(target->name, dst);
                terminal_writestring("Renamed.");
            } else {
                terminal_writestring("Not found.");
            }
        }
    } else if (strcmp(cmd_name, "cp") == 0) {
        char src[32], dst[32];
        int s = 0, d = 0;
        char* p = args;
        while (*p && *p != ' ' && s < 31) src[s++] = *p++;
        src[s] = '\0';
        if (*p == ' ') p++;
        while (*p && d < 31) dst[d++] = *p++;
        dst[d] = '\0';
        if (s == 0 || d == 0) {
            terminal_writestring("Usage: cp <src> <dst>");
        } else {
            struct fs_node* source = find_node(current_dir, src);
            if (source && source->type == FS_FILE) {
                struct fs_node* copy = create_node(dst, FS_FILE, current_dir);
                if (copy) {
                    strcpy(copy->content, source->content);
                    copy->content_len = source->content_len;
                    terminal_writestring("Copied.");
                }
            } else {
                terminal_writestring("Source not found or is a directory.");
            }
        }
    } else if (strcmp(cmd_name, "head") == 0) {
        char filename[32];
        int n = 10;
        int s = 0;
        char* p = args;
        while (*p && *p != ' ' && s < 31) filename[s++] = *p++;
        filename[s] = '\0';
        if (*p == ' ') {
            p++;
            n = 0;
            while (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); p++; }
        }
        if (s == 0) {
            terminal_writestring("Usage: head <file> [lines]");
        } else {
            struct fs_node* target = find_node(current_dir, filename);
            if (target && target->type == FS_FILE) {
                int lines_shown = 0;
                int idx = 0;
                while (idx < target->content_len && lines_shown < n) {
                    terminal_putchar(target->content[idx]);
                    if (target->content[idx] == '\n') lines_shown++;
                    idx++;
                }
            } else {
                terminal_writestring("File not found.");
            }
        }
    } else if (strcmp(cmd_name, "tail") == 0) {
        char filename[32];
        int n = 10;
        int s = 0;
        char* p = args;
        while (*p && *p != ' ' && s < 31) filename[s++] = *p++;
        filename[s] = '\0';
        if (*p == ' ') {
            p++;
            n = 0;
            while (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); p++; }
        }
        if (s == 0) {
            terminal_writestring("Usage: tail <file> [lines]");
        } else {
            struct fs_node* target = find_node(current_dir, filename);
            if (target && target->type == FS_FILE) {
                int total_lines = 1;
                for (int idx = 0; idx < target->content_len; idx++) {
                    if (target->content[idx] == '\n') total_lines++;
                }
                int lines_to_skip = total_lines - n;
                if (lines_to_skip < 0) lines_to_skip = 0;
                int idx = 0;
                int line_count = 0;
                while (idx < target->content_len && line_count < lines_to_skip) {
                    if (target->content[idx] == '\n') line_count++;
                    idx++;
                }
                while (idx < target->content_len) {
                    terminal_putchar(target->content[idx]);
                    idx++;
                }
            } else {
                terminal_writestring("File not found.");
            }
        }
    } else if (strcmp(cmd_name, "wc") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: wc <file>");
        } else {
            struct fs_node* target = find_node(current_dir, args);
            if (target && target->type == FS_FILE) {
                int lines = 0, words = 0, chars = target->content_len;
                int in_word = 0;
                for (int idx = 0; idx < chars; idx++) {
                    char c = target->content[idx];
                    if (c == '\n') lines++;
                    if (c == ' ' || c == '\n' || c == '\t') {
                        in_word = 0;
                    } else if (!in_word) {
                        in_word = 1;
                        words++;
                    }
                }
                char buf[16];
                terminal_writestring("  ");
                int_to_string(lines, buf); terminal_writestring(buf);
                terminal_writestring("  ");
                int_to_string(words, buf); terminal_writestring(buf);
                terminal_writestring("  ");
                int_to_string(chars, buf); terminal_writestring(buf);
                terminal_writestring(" ");
                terminal_writestring(args);
            } else {
                terminal_writestring("File not found.");
            }
        }
    } else if (strcmp(cmd_name, "nl") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: nl <file>");
        } else {
            struct fs_node* target = find_node(current_dir, args);
            if (target && target->type == FS_FILE) {
                int line_num = 1;
                char buf[16];
                int_to_string(line_num, buf);
                int num_len = strlen(buf);
                for (int p = 0; p < 6 - num_len; p++) terminal_writestring(" ");
                terminal_writestring(buf);
                terminal_writestring("  ");
                line_num++;
                for (int idx = 0; idx < target->content_len; idx++) {
                    terminal_putchar(target->content[idx]);
                    if (target->content[idx] == '\n' && idx + 1 < target->content_len) {
                        int_to_string(line_num, buf);
                        num_len = strlen(buf);
                        for (int p = 0; p < 6 - num_len; p++) terminal_writestring(" ");
                        terminal_writestring(buf);
                        terminal_writestring("  ");
                        line_num++;
                    }
                }
            } else {
                terminal_writestring("File not found.");
            }
        }
    } else if (strcmp(cmd_name, "sort") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: sort <file>");
        } else {
            struct fs_node* target = find_node(current_dir, args);
            if (target && target->type == FS_FILE) {
                char lines[64][128];
                int line_count = 0;
                char line_buf[128];
                int line_idx = 0;
                for (int idx = 0; idx < target->content_len && line_count < 64; idx++) {
                    if (target->content[idx] == '\n' || idx == target->content_len - 1) {
                        if (idx == target->content_len - 1 && target->content[idx] != '\n') {
                            if (line_idx < 127) line_buf[line_idx++] = target->content[idx];
                        }
                        line_buf[line_idx] = '\0';
                        strcpy(lines[line_count], line_buf);
                        line_count++;
                        line_idx = 0;
                    } else {
                        if (line_idx < 127) line_buf[line_idx++] = target->content[idx];
                    }
                }
                int reverse = 0;
                if (strstr(args, "-r")) reverse = 1;
                int numeric = 0;
                if (strstr(args, "-n")) numeric = 1;
                for (i = 0; i < line_count - 1; i++) {
                    for (int j = 0; j < line_count - 1 - i; j++) {
                        int cmp = 0;
                        if (numeric) {
                            int a = simple_atoi(lines[j]);
                            int b = simple_atoi(lines[j+1]);
                            cmp = a - b;
                        } else {
                            cmp = strcmp(lines[j], lines[j+1]);
                        }
                        if ((!reverse && cmp > 0) || (reverse && cmp < 0)) {
                            char temp[128];
                            strcpy(temp, lines[j]);
                            strcpy(lines[j], lines[j+1]);
                            strcpy(lines[j+1], temp);
                        }
                    }
                }
                for (i = 0; i < line_count; i++) {
                    terminal_writestring(lines[i]);
                    terminal_writestring("\n");
                }
            } else {
                terminal_writestring("File not found.");
            }
        }
    } else if (strcmp(cmd_name, "which") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: which <command>");
        } else {
            struct fs_node* bin = search_path(args);
            if (bin) {
                terminal_writestring("/bin/");
                terminal_writestring(args);
            } else {
                terminal_writestring("Command not found in PATH.");
            }
        }
    } else if (strcmp(cmd_name, "yes") == 0) {
        const char* text = (strlen(args) > 0) ? args : "y";
        for (i = 0; i < 5; i++) {
            terminal_writestring(text);
            terminal_writestring("\n");
        }
    } else if (strcmp(cmd_name, "sleep") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: sleep <ms>");
        } else {
            int ms = 0;
            char* p = args;
            while (*p >= '0' && *p <= '9') { ms = ms * 10 + (*p - '0'); p++; }
            delay_ms(ms);
        }
    } else if (strcmp(cmd_name, "basename") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: basename <path>");
        } else {
            int last_slash = -1;
            int len = strlen(args);
            for (i = 0; i < len; i++) {
                if (args[i] == '/') last_slash = i;
            }
            terminal_writestring(args + last_slash + 1);
        }
    } else if (strcmp(cmd_name, "dirname") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: dirname <path>");
        } else {
            int last_slash = -1;
            int len = strlen(args);
            for (i = 0; i < len; i++) {
                if (args[i] == '/') last_slash = i;
            }
            if (last_slash == -1) {
                terminal_writestring(".");
            } else if (last_slash == 0) {
                terminal_writestring("/");
            } else {
                for (i = 0; i < last_slash; i++) {
                    terminal_putchar(args[i]);
                }
            }
        }
    } else if (strcmp(cmd_name, "env") == 0) {
        terminal_writestring("SHELL=sharkos\n");
        terminal_writestring("OS=SharkOS\n");
        terminal_writestring("USER=");
        terminal_writestring(current_user);
        terminal_writestring("\n");
        terminal_writestring("HOSTNAME=SharkOS\n");
        terminal_writestring("PWD=");
        struct fs_node* n = current_dir;
        char path[256];
        char rev[256];
        int plen = 0;
        rev[plen++] = '/';
        while (n && n->parent) {
            int namelen = strlen(n->name);
            for (int k = namelen - 1; k >= 0; k--) rev[plen++] = n->name[k];
            rev[plen++] = '/';
            n = n->parent;
        }
        int j = 0;
        for (int k = plen - 1; k >= 0; k--) path[j++] = rev[k];
        if (j > 0 && path[j-1] != '/') path[j++] = '/';
        path[j] = '\0';
        terminal_writestring(path);
        terminal_writestring("\n");
        terminal_writestring("HOME=/\n");
        terminal_writestring("TERM=sharkos-vga\n");
        terminal_writestring("PATH=/bin:/usr/bin\n");
        terminal_writestring("SHELL=/bin/shark\n");
        terminal_writestring("LOGNAME=");
        terminal_writestring(current_user);
        terminal_writestring("\n");
        terminal_writestring("USER=");
        terminal_writestring(current_user);
        terminal_writestring("\n");
    } else if (strcmp(cmd_name, "rev") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: rev <text>");
        } else {
            int len = strlen(args);
            for (i = len - 1; i >= 0; i--) {
                terminal_putchar(args[i]);
            }
            terminal_writestring("\n");
        }
    } else if (strcmp(cmd_name, "seq") == 0) {
        int start = 1, end = 10, step = 1;
        char* p = args;
        if (*p) {
            start = 0;
            while (*p >= '0' && *p <= '9') { start = start * 10 + (*p - '0'); p++; }
        }
        if (*p == ' ') {
            p++;
            end = 0;
            while (*p >= '0' && *p <= '9') { end = end * 10 + (*p - '0'); p++; }
        }
        if (*p == ' ') {
            p++;
            step = 0;
            while (*p >= '0' && *p <= '9') { step = step * 10 + (*p - '0'); p++; }
        }
        if (step == 0) step = 1;
        char buf[16];
        for (i = start; i <= end; i += step) {
            int_to_string(i, buf);
            terminal_writestring(buf);
            terminal_writestring("\n");
        }
    } else if (strcmp(cmd_name, "factor") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: factor <number>");
        } else {
            int n = 0;
            char* p = args;
            while (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); p++; }
            if (n < 2) {
                terminal_writestring("Number must be >= 2");
            } else {
                char buf[16];
                int first = 1;
                for (int d = 2; d * d <= n; d++) {
                    while (n % d == 0) {
                        if (!first) terminal_writestring(" ");
                        int_to_string(d, buf);
                        terminal_writestring(buf);
                        n /= d;
                        first = 0;
                    }
                }
                if (n > 1) {
                    if (!first) terminal_writestring(" ");
                    int_to_string(n, buf);
                    terminal_writestring(buf);
                }
                terminal_writestring("\n");
            }
        }
    } else if (strcmp(cmd_name, "true") == 0) {

    } else if (strcmp(cmd_name, "false") == 0) {

    } else if (strcmp(cmd_name, "tr") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: tr <set1> [set2]");
        } else {
            char set1[32], set2[32];
            int s1 = 0, s2 = 0;
            char* p = args;
            while (*p && *p != ' ' && s1 < 31) set1[s1++] = *p++;
            set1[s1] = '\0';
            if (*p == ' ') {
                p++;
                while (*p && s2 < 31) set2[s2++] = *p++;
                set2[s2] = '\0';
            }
            for (i = 0; args[i]; i++) {
                char c = args[i];
                if (s1 > 0) {
                    int found = 0;
                    for (int j = 0; j < s1; j++) {
                        if (c == set1[j]) {
                            if (s2 > 0 && j < s2) c = set2[j];
                            else if (s2 == 0) c = '\n';
                            found = 1;
                            break;
                        }
                    }
                    if (found) terminal_putchar(c);
                    else terminal_putchar(args[i]);
                } else {
                    terminal_putchar(c);
                }
            }
            terminal_writestring("\n");
        }
    } else if (strcmp(cmd_name, "cut") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: cut -c<range> or cut -f<field> -d<delim>");
        } else {
            if (args[0] == '-' && args[1] == 'c') {
                int pos = simple_atoi(args + 2) - 1;
                if (pos >= 0 && pos < (int)strlen(args)) terminal_putchar(args[pos]);
                terminal_writestring("\n");
            } else if (args[0] == '-' && args[1] == 'f') {
                int field = simple_atoi(args + 2) - 1;
                char* p = args + 3;
                while (*p && *p != ' ') p++;
                if (*p == ' ') p++;
                char delim = *p ? *p : ' ';
                int current = 0;
                int in_field = 0;
                for (i = 0; args[i]; i++) {
                    if (args[i] == delim) {
                        in_field = 0;
                    } else if (!in_field) {
                        in_field = 1;
                        if (current == field) {
                            terminal_putchar(args[i]);
                        }
                        current++;
                    } else if (current == field) {
                        terminal_putchar(args[i]);
                    }
                }
                terminal_writestring("\n");
            } else {
                terminal_writestring("Usage: cut -c<range> or cut -f<field> -d<delim>");
            }
        }
    } else if (strcmp(cmd_name, "uniq") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: uniq <file>");
        } else {
            struct fs_node* target = find_node(current_dir, args);
            if (target && target->type == FS_FILE) {
                char last_line[128] = "";
                char line[128];
                int li = 0;
                for (int idx = 0; idx < target->content_len; idx++) {
                    char c = target->content[idx];
                    if (c == '\n' || idx == target->content_len - 1) {
                        if (idx == target->content_len - 1 && c != '\n') {
                            if (li < 127) line[li++] = c;
                        }
                        line[li] = '\0';
                        if (strcmp(line, last_line) != 0) {
                            terminal_writestring(line);
                            terminal_writestring("\n");
                            strcpy(last_line, line);
                        }
                        li = 0;
                    } else {
                        if (li < 127) line[li++] = c;
                    }
                }
            } else {
                terminal_writestring("File not found.");
            }
        }
    } else if (strcmp(cmd_name, "tee") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: tee <file>");
        } else {
            terminal_writestring("tee: ");
            terminal_writestring(args);
            terminal_writestring(" (simulated - would write to file)\n");
        }
    } else if (strcmp(cmd_name, "time") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: time <command>");
        } else {
            uint32_t start = uptime_ticks;
            terminal_writestring("Timing: ");
            terminal_writestring(args);
            terminal_writestring("\n");
            execute_command(args);
            uint32_t end = uptime_ticks;
            uint32_t elapsed = end - start;
            terminal_writestring("Time: ");
            char buf[16];
            int_to_string(elapsed / 100, buf);
            terminal_writestring(buf);
            terminal_writestring(".");
            int_to_string(elapsed % 100, buf);
            terminal_writestring(buf);
            terminal_writestring("s\n");
        }
    } else if (strcmp(cmd_name, "id") == 0) {
        terminal_writestring("uid=0(root) gid=0(root) groups=0(root)\n");
    } else if (strcmp(cmd_name, "umask") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("0002\n");
        } else {
            terminal_writestring("umask set to: ");
            terminal_writestring(args);
            terminal_writestring("\n");
        }
    } else if (strcmp(cmd_name, "chmod") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: chmod <mode> <file>");
        } else {
            terminal_writestring("Permissions changed (simulated)\n");
        }
    } else if (strcmp(cmd_name, "ln") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: ln <target> <link>");
        } else {
            terminal_writestring("Link created (simulated)\n");
        }
    } else if (strcmp(cmd_name, "realpath") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: realpath <path>");
        } else {
            terminal_writestring("/");
            terminal_writestring(args);
            terminal_writestring("\n");
        }
    } else if (strcmp(cmd_name, "mktemp") == 0) {
        char temp_name[32];
        strcpy(temp_name, "tmp_");
        for (i = 4; i < 12; i++) {
            temp_name[i] = 'a' + (uptime_ticks % 26);
        }
        temp_name[12] = '\0';
        create_node(temp_name, FS_FILE, current_dir);
        terminal_writestring(temp_name);
        terminal_writestring("\n");
    } else if (strcmp(cmd_name, "cal") == 0) {
        uint32_t days = uptime_ticks / 8640000;
        int month = (days % 12) + 1;
        int year = 2024 + (days / 12);
        terminal_writestring("    ");
        const char* months[] = {"January","February","March","April","May","June","July","August","September","October","November","December"};
        terminal_writestring(months[month - 1]);
        terminal_writestring(" ");
        char ybuf[16]; int_to_string(year, ybuf); terminal_writestring(ybuf);
        terminal_writestring("\n");
        terminal_writestring("Su Mo Tu We Th Fr Sa\n");
        int start_day = (days + 2) % 7;
        int days_in_month = 31;
        if (month == 2) days_in_month = 28;
        else if (month == 4 || month == 6 || month == 9 || month == 11) days_in_month = 30;
        for (i = 0; i < start_day; i++) terminal_writestring("   ");
        for (int d = 1; d <= days_in_month; d++) {
            char dbuf[4]; int_to_string(d, dbuf);
            if (d < 10) terminal_writestring(" ");
            terminal_writestring(dbuf);
            terminal_writestring(" ");
            if ((start_day + d) % 7 == 0 && d != days_in_month) terminal_writestring("\n");
        }
        terminal_writestring("\n");
    } else if (strcmp(cmd_name, "who") == 0 || strcmp(cmd_name, "w") == 0) {
        terminal_writestring("USER     TTY      FROM             LOGIN@   IDLE   WHAT\n");
        terminal_writestring(current_user);
        terminal_writestring("     tty1     console          boot     ");
        uint32_t mins = (uptime_ticks / 100) / 60;
        char mbuf[16]; int_to_string(mins, mbuf); terminal_writestring(mbuf);
        terminal_writestring("     shark\n");
    } else if (strcmp(cmd_name, "dmesg") == 0) {
        terminal_writestring("[    0.000000] nemo (SharkOS V2 Kernel build 0.06) (gcc)\n");
        terminal_writestring("[    0.000001] BIOS-e820 memory map detected\n");
        terminal_writestring("[    0.000002] ");
        char buf[16]; hex_to_string((uint32_t)total_system_memory >> 20, buf); terminal_writestring(buf);
        terminal_writestring(" MB usable RAM\n");
        terminal_writestring("[    0.000003] Framebuffer initialized\n");
        terminal_writestring("[    0.000004] CPU features: FPU, PAE, PSE detected\n");
        terminal_writestring("[    0.000005] Local APIC timer: 100 Hz\n");
        terminal_writestring("[    0.000006] HZ: 1000\n");
        terminal_writestring("[    0.000007] PID max: 32768\n");
        terminal_writestring("[    0.000008] SHKRNL boot complete.\n");
        terminal_writestring("[    0.000009] Initializing filesystem\n");
        terminal_writestring("[    0.000010] Shell started\n");
    } else if (strcmp(cmd_name, "log") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: log <message>");
        } else {
            terminal_writestring("Logged: ");
            terminal_writestring(args);
            terminal_writestring("\n");
        }
    } else if (strcmp(cmd_name, "test") == 0 || strcmp(cmd_name, "[") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: test <expression>");
        } else {
            if (strcmp(args, "0") == 0 || strcmp(args, "false") == 0 || strcmp(args, "") == 0) {

            } else {

            }
        }
    } else if (strcmp(cmd_name, "printf") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: printf <format> [args...]");
        } else {
            terminal_writestring(args);
            terminal_writestring("\n");
        }
    } else if (strcmp(cmd_name, "xargs") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: xargs <command>");
        } else {
            terminal_writestring("xargs: ");
            terminal_writestring(args);
            terminal_writestring(" (simulated)\n");
        }
    } else if (strcmp(cmd_name, "comm") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: comm <file1> <file2>");
        } else {
            terminal_writestring("comm: compare files (simulated)\n");
        }
    } else if (strcmp(cmd_name, "expand") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: expand <file>");
        } else {
            struct fs_node* target = find_node(current_dir, args);
            if (target && target->type == FS_FILE) {
                for (int idx = 0; idx < target->content_len; idx++) {
                    if (target->content[idx] == '\t') {
                        terminal_writestring("    ");
                    } else {
                        terminal_putchar(target->content[idx]);
                    }
                }
            } else {
                terminal_writestring("File not found.");
            }
        }
    } else if (strcmp(cmd_name, "fold") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: fold -w<width> <file>");
        } else {
            terminal_writestring("fold: wrap lines (simulated)\n");
        }
    } else if (strcmp(cmd_name, "split") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: split <file>");
        } else {
            terminal_writestring("split: file split (simulated)\n");
        }
    } else if (strcmp(cmd_name, "paste") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: paste <file1> [file2...]");
        } else {
            terminal_writestring("paste: merge lines (simulated)\n");
        }
    } else if (strcmp(cmd_name, "join") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: join <file1> <file2>");
        } else {
            terminal_writestring("join: join lines (simulated)\n");
        }
    } else if (strcmp(cmd_name, "strings") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: strings <file>");
        } else {
            struct fs_node* target = find_node(current_dir, args);
            if (target && target->type == FS_FILE) {
                int in_string = 0;
                int str_start = 0;
                for (int idx = 0; idx < target->content_len; idx++) {
                    char c = target->content[idx];
                    if (c >= 32 && c < 127) {
                        if (!in_string) {
                            in_string = 1;
                            str_start = idx;
                        }
                    } else {
                        if (in_string && idx - str_start >= 4) {
                            for (int j = str_start; j < idx; j++) {
                                terminal_putchar(target->content[j]);
                            }
                            terminal_writestring("\n");
                        }
                        in_string = 0;
                    }
                }
            } else {
                terminal_writestring("File not found.");
            }
        }
    } else if (strcmp(cmd_name, "od") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: od <file>");
        } else {
            struct fs_node* target = find_node(current_dir, args);
            if (target && target->type == FS_FILE) {
                char hex[16];
                for (i = 0; i < target->content_len && i < 128; i += 16) {
                    hex_to_string(i, hex);
                    terminal_writestring(hex);
                    terminal_writestring("  ");
                    for (int j = 0; j < 16 && i + j < target->content_len; j++) {
                        unsigned char c = (unsigned char)target->content[i + j];
                        hex_to_string(c, hex);
                        terminal_writestring(&hex[2]);
                        terminal_writestring(" ");
                    }
                    terminal_writestring(" ");
                    for (int j = 0; j < 16 && i + j < target->content_len; j++) {
                        unsigned char c = (unsigned char)target->content[i + j];
                        if (c >= 32 && c < 127) terminal_putchar(c);
                        else terminal_putchar('.');
                    }
                    terminal_writestring("\n");
                }
            } else {
                terminal_writestring("File not found.");
            }
        }
    } else if (strcmp(cmd_name, "spkg") == 0) {
        int spkg_argc = 1;
        char* spkg_argv[16] = {(char*)"spkg"};
        char args_copy[256];
        strcpy(args_copy, args);

        int i = 0;
        int arg_start = 0;
        int len = strlen(args_copy);
        while (i <= len && spkg_argc < 15) {
            if (args_copy[i] == ' ' || args_copy[i] == '\0') {
                if (i > arg_start) {
                    args_copy[i] = '\0';
                    spkg_argv[spkg_argc++] = &args_copy[arg_start];
                }
                arg_start = i + 1;
            }
            i++;
        }

        extern int cmd_spkg(int argc, char** argv);
        cmd_spkg(spkg_argc, spkg_argv);
    } else if (plugin_is_plugin_command(cmd_name, &plugin_id)) {
        plugin_t* plugin = plugin_get_by_name(cmd_name);
        if (!plugin || !plugin->loaded) {
            terminal_writestring("Plugin '");
            terminal_writestring(cmd_name);
            terminal_writestring("' is not loaded. Use 'spkg install ");
            terminal_writestring(cmd_name);
            terminal_writestring("' to install it.\n");
        } else {
            char* plugin_argv[16] = {cmd_name};
            int plugin_argc = 1;
            if (strlen(args) > 0) {
                plugin_argv[plugin_argc++] = args;
            }
            plugin_execute(plugin->id, plugin_argc, plugin_argv);
        }
    } else if (strcmp(cmd_name, "microsoft") == 0) {
        terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        terminal_writestring("\n\n");
        terminal_writestring("╔══════════════════════════════════════════╗\n");
        terminal_writestring("║          KERNEL PANIC                    ║\n");
        terminal_writestring("╚══════════════════════════════════════════╝\n\n");
        terminal_writestring("Ewl get that slop out of here\n\n");
        terminal_writestring("FATAL: Unsupported operating system detected\n");
        terminal_writestring("SharkOS refuses to associate with proprietary software.\n\n");
        terminal_writestring("System will now halt to protect your freedom.\n\n");
        while (1) {
            asm volatile("hlt");
        }
    } else {
        struct fs_node* bin = search_path(cmd_name);
        if (bin) {
            execute_elf((uint8_t*)bin->content, args);
        } else {
            terminal_writestring("Unknown command: ");
            terminal_writestring(cmd_name);
            terminal_writestring("\n");
        }
    }
    terminal_set_color(old_color);
}