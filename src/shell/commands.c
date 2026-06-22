#include "kernel.h"
#include "sharkscript.h"

void print_prompt() {
    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK));
    terminal_writestring("shark:");
    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring(current_dir->name);
    terminal_writestring("> ");
    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    panes[active_pane].prompt_end_col = terminal_column;
}

static void print_tree(struct fs_node* node, int depth) {
    for (int k = 0; k < depth; k++) terminal_writestring("  ");
    terminal_writestring("|- ");
    terminal_writestring(node->name);
    if (node->type == FS_DIRECTORY) terminal_writestring("/");
    terminal_writestring("\n");
    if (node->type == FS_DIRECTORY) {
        for (int k = 0; k < node->num_children; k++) {
            print_tree(node->children[k], depth + 1);
        }
    }
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
    char* args = (cmd[i] == ' ') ? &cmd[i+1] : "";

    if (strcmp(cmd_name, "ls") == 0 || strcmp(cmd_name, "dir") == 0) {
        for (i = 0; i < current_dir->num_children; i++) {
            terminal_writestring(current_dir->children[i]->name);
            if (current_dir->children[i]->type == FS_DIRECTORY) terminal_writestring("/");
            terminal_writestring("  ");
        }
    } else if (strcmp(cmd_name, "cd") == 0) {
        if (strcmp(args, "..") == 0) {
            if (current_dir->parent) current_dir = current_dir->parent;
        } else if (strcmp(args, "/") == 0 || strcmp(args, "root") == 0) {
            current_dir = root;
        } else {
            struct fs_node* target = find_node(current_dir, args);
            if (target && target->type == FS_DIRECTORY) current_dir = target;
            else terminal_writestring("Directory not found.");
        }
    } else if (strcmp(cmd_name, "cat") == 0) {
        struct fs_node* target = find_node(current_dir, args);
        if (target && target->type == FS_FILE) terminal_writestring(target->content);
        else terminal_writestring("File not found.");
    } else if (strcmp(cmd_name, "touch") == 0) {
        if (find_node(current_dir, args)) terminal_writestring("File already exists.");
        else create_node(args, FS_FILE, current_dir);
    } else if (strcmp(cmd_name, "edit") == 0) {
        struct fs_node* target = find_node(current_dir, args);
        if (target && target->type == FS_FILE) {
            editor_target_file = target;
            current_kernel_mode = KERNEL_MODE_EDITOR;
            terminal_clear();
            terminal_writestring("Editing: ");
            terminal_writestring(editor_target_file->name);
            terminal_writestring("\nPress ESC to save and exit.\n\n");
            editor_buffer_idx = 0;
        } else terminal_writestring("File not found.");
    } else if (strcmp(cmd_name, "whoami") == 0) {
        terminal_writestring("sharkuser");
    } else if (strcmp(cmd_name, "ping") == 0) {
        if (!network_initialized) {
            terminal_writestring("Network system failure.");
        } else {
            const char* target = (strlen(args) > 0) ? args : "127.0.0.1";
            send_icmp_ping(target);
        }
    } else if (strcmp(cmd_name, "clear") == 0 || strcmp(cmd_name, "cls") == 0) {
        terminal_clear();
        return;
    } else if (strcmp(cmd_name, "help") == 0) {
        uint8_t m = VGA_COLOR_LIGHT_MAGENTA;
        uint8_t c = VGA_COLOR_LIGHT_CYAN;
        uint8_t g = VGA_COLOR_LIGHT_GREEN;
        uint8_t w = VGA_COLOR_WHITE;
        uint8_t d = VGA_COLOR_DARK_GREY;
        uint8_t y = VGA_COLOR_LIGHT_BROWN;
        uint8_t r = VGA_COLOR_LIGHT_RED;
        uint8_t b = VGA_COLOR_LIGHT_BLUE;
        int col;

        terminal_set_color(vga_entry_color(m, VGA_COLOR_BLACK));
        terminal_writestring("\n  ");
        terminal_set_color(vga_entry_color(y, VGA_COLOR_BLACK));
        terminal_writestring("SharkOS Help");
        terminal_set_color(vga_entry_color(m, VGA_COLOR_BLACK));
        terminal_writestring("  ");
        terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK));
        for (col = 0; col < 20; col++) terminal_writestring("-");
        terminal_set_color(vga_entry_color(y, VGA_COLOR_BLACK));

        terminal_writestring("\n\n");
        terminal_set_color(vga_entry_color(m, VGA_COLOR_BLACK));
        terminal_writestring("  ");
        terminal_set_color(vga_entry_color(c, VGA_COLOR_BLACK));
        terminal_writestring("FILESYSTEM");
        terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK));
        terminal_writestring("  ");
        for (col = 0; col < 25; col++) terminal_writestring("-");
        terminal_set_color(vga_entry_color(w, VGA_COLOR_BLACK));
        terminal_writestring("\n");
        terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("    ");
        terminal_writestring("ls          "); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("List files\n"); terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("    cd <dir>  "); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("Change directory (.. / to go up/root)\n"); terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("    cat <f>   "); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("View file contents\n"); terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("    touch <f> "); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("Create empty file\n"); terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("    edit <f>  "); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("Open file editor\n"); terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("    mkdir <n> "); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("Create directory\n"); terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("    rm <n>    "); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("Remove file/dir\n"); terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("    mv <s> <d>"); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("Rename item\n"); terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("    cp <s> <d>"); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("Copy file\n"); terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("    pwd       "); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("Print working directory\n"); terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("    tree      "); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("Show directory tree\n"); terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));

        terminal_set_color(vga_entry_color(m, VGA_COLOR_BLACK));
        terminal_writestring("  ");
        terminal_set_color(vga_entry_color(c, VGA_COLOR_BLACK));
        terminal_writestring("SYSTEM");
        terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK));
        terminal_writestring("  ");
        for (col = 0; col < 29; col++) terminal_writestring("-");
        terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("\n");
        terminal_writestring("    whoami    "); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("Display current user\n"); terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("    sysinfo   "); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("Show system information\n"); terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("    colors    "); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("Display color palette\n"); terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("    lspci     "); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("List PCI devices\n"); terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("    ps        "); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("List processes\n"); terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("    kill <p>  "); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("Kill process by PID\n"); terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("    exec <p>  "); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("Execute a binary\n"); terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("    uname     "); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("Print OS name (-a for details)\n"); terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("    hostname  "); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("Print hostname\n"); terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("    echo <t>  "); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("Print text\n"); terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("    calc <e>  "); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("Simple calculator (2+3)\n"); terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("    ping <h>  "); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("Ping a host\n"); terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("    shs <f>   "); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("Run .shx script\n");

        terminal_set_color(vga_entry_color(m, VGA_COLOR_BLACK));
        terminal_writestring("  ");
        terminal_set_color(vga_entry_color(c, VGA_COLOR_BLACK));
        terminal_writestring("APPS & UTILITIES");
        terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK));
        terminal_writestring("  ");
        for (col = 0; col < 21; col++) terminal_writestring("-");
        terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("\n");
        terminal_writestring("    clear/cls "); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("Clear the terminal\n"); terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("    colors    "); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("Show 16-color palette\n"); terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("    help      "); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("Show this menu\n"); terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));

        terminal_set_color(vga_entry_color(m, VGA_COLOR_BLACK));
        terminal_writestring("  ");
        terminal_set_color(vga_entry_color(c, VGA_COLOR_BLACK));
        terminal_writestring("POWER");
        terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK));
        terminal_writestring("  ");
        for (col = 0; col < 30; col++) terminal_writestring("-");
        terminal_set_color(vga_entry_color(r, VGA_COLOR_BLACK));
        terminal_writestring("\n");
        terminal_writestring("    poweroff  "); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("Shut down the system\n"); terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("    reboot    "); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("Reboot the system\n"); terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));

        terminal_set_color(vga_entry_color(m, VGA_COLOR_BLACK));
        terminal_writestring("  ");
        terminal_set_color(vga_entry_color(c, VGA_COLOR_BLACK));
        terminal_writestring("KEYBOARD SHORTCUTS");
        terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK));
        terminal_writestring("  ");
        for (col = 0; col < 19; col++) terminal_writestring("-");
        terminal_set_color(vga_entry_color(b, VGA_COLOR_BLACK));
        terminal_writestring("\n");
        terminal_writestring("    "); terminal_writestring("?           "); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("Open FAQ\n"); terminal_set_color(vga_entry_color(b, VGA_COLOR_BLACK));
        terminal_writestring("    "); terminal_writestring("+           "); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("Split pane\n"); terminal_set_color(vga_entry_color(b, VGA_COLOR_BLACK));
        terminal_writestring("    "); terminal_writestring("-           "); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("Close pane\n"); terminal_set_color(vga_entry_color(b, VGA_COLOR_BLACK));
        terminal_writestring("    "); terminal_writestring("TAB         "); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("Switch focus between panes\n"); terminal_set_color(vga_entry_color(b, VGA_COLOR_BLACK));
        terminal_writestring("    "); terminal_writestring("Ctrl+S      "); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("Open settings\n"); terminal_set_color(vga_entry_color(b, VGA_COLOR_BLACK));
        terminal_writestring("    "); terminal_writestring("ESC         "); terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK)); terminal_writestring("Close FAQ/Settings/Editor\n");

        terminal_set_color(vga_entry_color(y, VGA_COLOR_BLACK));
        terminal_writestring("\n  ");
        for (col = 0; col < 36; col++) terminal_writestring("-");
        terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK));
        terminal_writestring("\n  For more info visit github.com/mayshecry/sharkos\n\n");
        terminal_set_color(old_color);
    } else if (strcmp(cmd_name, "lspci") == 0) {
        pci_list_devices();
    } else if (strcmp(cmd_name, "bokop") == 0 || strcmp(cmd_name, "poweroff") == 0) {
        terminal_writestring("Shutting down...");
        shutdown();
    } else if (strcmp(cmd_name, "reboot") == 0) {
        terminal_writestring("Rebooting...");
        outb(0x64, 0xFE);
    } else if (strcmp(cmd_name, "sysinfo") == 0 || strcmp(cmd_name, "neofetch") == 0) {
        show_fastfetch();
    } else if (strcmp(cmd_name, "colors") == 0) {
        terminal_writestring("SharkOS 16-Color Palette:\n");
        for (i = 0; i < 16; i++) {
            terminal_set_color(vga_entry_color(i, VGA_COLOR_BLACK));
            terminal_writestring("Color ");
        }
        terminal_set_color(old_color);
    } else if (strcmp(cmd_name, "ps") == 0) {
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
    } else if (strcmp(cmd_name, "shs") == 0) {
        if (strcmp(args, "") == 0) {
            terminal_writestring("Usage: shs <script.shx>\n");
        } else {
            shs_init_engine();
            shs_run_file(args);
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
            struct fs_node* target = find_node(current_dir, args);
            if (!target) {
                terminal_writestring("Not found.");
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
    } else if (strcmp(cmd_name, "pwd") == 0) {
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
    } else if (strcmp(cmd_name, "tree") == 0) {
        print_tree(current_dir, 0);
    } else if (strcmp(cmd_name, "date") == 0) {
        terminal_writestring("Current date/time not available (no RTC driver).");
    } else if (strcmp(cmd_name, "uptime") == 0) {
        terminal_writestring("Uptime tracking not implemented.");
    } else if (strcmp(cmd_name, "uname") == 0) {
        terminal_writestring("SharkOS");
        if (strcmp(args, "-a") == 0) {
            terminal_writestring(" v0.1 i686 shark@SharkOS");
        }
    } else if (strcmp(cmd_name, "hostname") == 0) {
        terminal_writestring("SharkOS");
    } else if (strcmp(cmd_name, "echo") == 0) {
        terminal_writestring(args);
    } else if (strcmp(cmd_name, "calc") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: calc <expr>  e.g. calc 2+3");
        } else {
            int a = 0, b = 0;
            char op = 0;
            char* p = args;
            while (*p >= '0' && *p <= '9') { a = a * 10 + (*p - '0'); p++; }
            if (*p) { op = *p; p++; }
            while (*p >= '0' && *p <= '9') { b = b * 10 + (*p - '0'); p++; }
            char buf[32];
            if (op == '+') { int_to_string(a + b, buf); terminal_writestring(buf); }
            else if (op == '-') { int_to_string(a - b, buf); terminal_writestring(buf); }
            else if (op == '*') { int_to_string(a * b, buf); terminal_writestring(buf); }
            else if (op == '/') {
                if (b == 0) terminal_writestring("Division by zero");
                else { int_to_string(a / b, buf); terminal_writestring(buf); }
            }
            else terminal_writestring("Unsupported expression. Use + - * /");
        }
    } else if (strcmp(cmd_name, "kill") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: kill <pid>");
        } else {
            int pid = 0;
            char* p = args;
            while (*p >= '0' && *p <= '9') { pid = pid * 10 + (*p - '0'); p++; }
            spin_lock(&task_list_lock);
            task_t* t = task_list;
            while (t) {
                if (t->id == pid) {
                    t->state = TASK_ZOMBIE;
                    terminal_writestring("Killed.");
                    break;
                }
                t = t->next;
            }
            if (!t) terminal_writestring("PID not found.");
            spin_unlock(&task_list_lock);
        }
    } else if (strcmp(cmd_name, "exec") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: exec <program>");
        } else {
            struct fs_node* bin = search_path(args);
            if (bin) {
                execute_elf((uint8_t*)bin->content, "");
            } else {
                terminal_writestring("Program not found in PATH.");
            }
        }
    } else {
        struct fs_node* bin = search_path(cmd_name);
        if (bin) {
            execute_elf((uint8_t*)bin->content, args);
        } else {
            terminal_writestring("Unknown command: ");
            terminal_writestring(cmd_name);
        }
    }
}