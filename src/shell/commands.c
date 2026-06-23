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
            if (target && target->type == FS_FILE) terminal_writestring(target->content);
            else terminal_writestring("File not found.");
        }
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
            if (editor_target_file->content_len > 0) {
                for (int ei = 0; ei < editor_target_file->content_len; ei++) {
                    char c = editor_target_file->content[ei];
                    if (editor_buffer_idx < sizeof(editor_buffer) - 1) {
                        editor_buffer[editor_buffer_idx++] = c;
                    }
                    terminal_write_char_internal(c);
                }
            }
        } else terminal_writestring("File not found.");
    } else if (strcmp(cmd_name, "whoami") == 0) {
        terminal_writestring(current_user);
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
    } else if (strcmp(cmd_name, "credits") == 0) {
        uint8_t m = VGA_COLOR_LIGHT_MAGENTA;
        uint8_t c = VGA_COLOR_LIGHT_CYAN;
        uint8_t g = VGA_COLOR_LIGHT_GREEN;
        uint8_t y = VGA_COLOR_LIGHT_BROWN;
        uint8_t d = VGA_COLOR_DARK_GREY;

        terminal_set_color(vga_entry_color(m, VGA_COLOR_BLACK));
        terminal_writestring("\n        ╔══════════════════════════════╗\n");
        terminal_writestring("        ║      ");
        terminal_set_color(vga_entry_color(y, VGA_COLOR_BLACK));
        terminal_writestring("SharkOS Credits");
        terminal_set_color(vga_entry_color(m, VGA_COLOR_BLACK));
        terminal_writestring("      ║\n");
        terminal_writestring("        ╚══════════════════════════════╝\n\n");

        terminal_set_color(vga_entry_color(c, VGA_COLOR_BLACK));
        terminal_writestring("    ╔─────── ");
        terminal_set_color(vga_entry_color(y, VGA_COLOR_BLACK));
        terminal_writestring("Developer");
        terminal_set_color(vga_entry_color(c, VGA_COLOR_BLACK));
        terminal_writestring(" ───────╗\n");
        terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK));
        terminal_writestring("    │                                          │\n");
        terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("           🦈  Mayshecry");
        terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK));
        terminal_writestring("                              │\n");
        terminal_writestring("    │          Lead Developer & Creator");
        terminal_writestring("          │\n");
        terminal_writestring("    ╚══════════════════════════════════════════╝\n\n");

        terminal_set_color(vga_entry_color(c, VGA_COLOR_BLACK));
        terminal_writestring("    ╔─────── ");
        terminal_set_color(vga_entry_color(y, VGA_COLOR_BLACK));
        terminal_writestring("Bug Hunters");
        terminal_set_color(vga_entry_color(c, VGA_COLOR_BLACK));
        terminal_writestring(" ───────╗\n");
        terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK));
        terminal_writestring("    │                                          │\n");
        terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("           🐛  staxx.cc");
        terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK));
        terminal_writestring("                              │\n");
        terminal_writestring("    │          Finding the editor bugs");
        terminal_writestring("          │\n");
        terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("           🐛  eclipsehq");
        terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK));
        terminal_writestring("                              │\n");
        terminal_writestring("    │          Minor bug reports");
        terminal_writestring("                │\n");
        terminal_writestring("    ╚══════════════════════════════════════════╝\n\n");

        terminal_set_color(vga_entry_color(c, VGA_COLOR_BLACK));
        terminal_writestring("    ╔─────── ");
        terminal_set_color(vga_entry_color(y, VGA_COLOR_BLACK));
        terminal_writestring("Thanks for using!");
        terminal_set_color(vga_entry_color(c, VGA_COLOR_BLACK));
        terminal_writestring(" ───────╗\n");
        terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK));
        terminal_writestring("    │                                          │\n");
        terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("           github.com/mayshecry/sharkos");
        terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK));
        terminal_writestring("           │\n");
        terminal_writestring("    ╚══════════════════════════════════════════╝\n");
        terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK));
    } else if (strcmp(cmd_name, "kernelinfo") == 0) {
        uint8_t m = VGA_COLOR_LIGHT_MAGENTA;
        uint8_t c = VGA_COLOR_LIGHT_CYAN;
        uint8_t g = VGA_COLOR_LIGHT_GREEN;
        uint8_t y = VGA_COLOR_LIGHT_BROWN;
        uint8_t d = VGA_COLOR_DARK_GREY;

        terminal_set_color(vga_entry_color(m, VGA_COLOR_BLACK));
        (void)m; (void)c; (void)g; (void)y; (void)d;
        terminal_writestring("\n  ╔══════════════════════════════════════╗\n");
        terminal_writestring("  ║         SharkOS Kernel Info          ║\n");
        terminal_writestring("  ╚══════════════════════════════════════╝\n\n");

        terminal_set_color(vga_entry_color(c, VGA_COLOR_BLACK));
        terminal_writestring("  Architecture\n");
        terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK));
        terminal_writestring("  ────────────\n");
        terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("   Monolithic x86 (32-bit i686)\n");
        terminal_writestring("   Protected mode with paging\n");
        terminal_writestring("   Custom bootloader (GRUB multiboot)\n\n");

        terminal_set_color(vga_entry_color(c, VGA_COLOR_BLACK));
        terminal_writestring("  How It Works\n");
        terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK));
        terminal_writestring("  ────────────\n");
        terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("   Boot: GRUB loads kernel into memory\n");
        terminal_writestring("   Init: kmain() sets up framebuffer, memory\n");
        terminal_writestring("   CPU:  Descriptor tables, interrupts (IDT)\n");
        terminal_writestring("   Mem:  Physical memory manager (PMM)\n");
        terminal_writestring("   FS:   In-memory virtual filesystem\n");
        terminal_writestring("   UI:   Direct framebuffer graphics\n");
        terminal_writestring("   Shell: Command interpreter with editor\n\n");

        terminal_set_color(vga_entry_color(c, VGA_COLOR_BLACK));
        terminal_writestring("  Components\n");
        terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK));
        terminal_writestring("  ──────────\n");
        terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("   SHKRNL    — Kernel core & boot\n");
        terminal_writestring("   sharkfs   — Virtual filesystem\n");
        terminal_writestring("   shproc    — Process management\n");
        terminal_writestring("   shnet     — Network stack (RTL8139)\n");
        terminal_writestring("   shinput   — Keyboard & mouse input\n");
        terminal_writestring("   shsound   — Audio subsystem\n\n");

        terminal_set_color(vga_entry_color(c, VGA_COLOR_BLACK));
        terminal_writestring("  Features\n");
        terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK));
        terminal_writestring("  ────────\n");
        terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
        terminal_writestring("   [x] Graphical terminal with themes\n");
        terminal_writestring("   [x] Mouse support & tiling panes\n");
        terminal_writestring("   [x] Built-in text editor\n");
        terminal_writestring("   [x] ELF executable loader\n");
        terminal_writestring("   [x] SharkScript interpreter (.shx)\n");
        terminal_writestring("   [x] PCI device enumeration\n");
        terminal_writestring("   [x] Network ping (ICMP)\n\n");

        terminal_set_color(vga_entry_color(y, VGA_COLOR_BLACK));
        terminal_writestring("  ═══════════════════════════════════════\n");
        terminal_set_color(vga_entry_color(m, VGA_COLOR_BLACK));
        terminal_writestring("  Version: nemo (SharkOS V1 Kernel build 0.06)\n\n");
        terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK));
    } else if (strcmp(cmd_name, "help") == 0) {
        uint8_t m = VGA_COLOR_LIGHT_MAGENTA;
        uint8_t c = VGA_COLOR_LIGHT_CYAN;
        uint8_t g = VGA_COLOR_LIGHT_GREEN;
        uint8_t d = VGA_COLOR_DARK_GREY;
        uint8_t y = VGA_COLOR_LIGHT_BROWN;

        if (strlen(args) > 0) {
            if (strcasecmp(args, "filesystem") == 0 || strcasecmp(args, "fs") == 0) {
                terminal_set_color(vga_entry_color(m, VGA_COLOR_BLACK));
                terminal_writestring("\nFILESYSTEM\n------------------------------\n");
                terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
                terminal_writestring("ls, cd, cat, touch, edit, mkdir, rm, mv, cp, pwd, tree, head, tail, wc, nl, sort\n");
            } else if (strcasecmp(args, "system") == 0 || strcasecmp(args, "sys") == 0) {
                terminal_set_color(vga_entry_color(m, VGA_COLOR_BLACK));
                terminal_writestring("\nSYSTEM\n------------------------------\n");
                terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
                terminal_writestring("whoami, sysinfo, kernelinfo, colors, lspci, ps, kill, exec, uname, hostname\n");
                terminal_writestring("echo, calc, ping, shs, which, env, basename, dirname, df, free, hexdump\n");
                terminal_writestring("uptime, history, find, grep, stat\n");
            } else if (strcasecmp(args, "apps") == 0 || strcasecmp(args, "utilities") == 0 || strcasecmp(args, "util") == 0) {
                terminal_set_color(vga_entry_color(m, VGA_COLOR_BLACK));
                terminal_writestring("\nAPPS & UTILITIES\n------------------------------\n");
                terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
                terminal_writestring("clear, colors, credits, help, whatis, fortune, cowsay, sl, banner\n");
                terminal_writestring("yes, sleep, guess, tictactoe\n");
            } else if (strcasecmp(args, "power") == 0) {
                terminal_set_color(vga_entry_color(m, VGA_COLOR_BLACK));
                terminal_writestring("\nPOWER\n------------------------------\n");
                terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
                terminal_writestring("poweroff, reboot\n");
            } else if (strcasecmp(args, "keys") == 0 || strcasecmp(args, "shortcuts") == 0 || strcasecmp(args, "keyboard") == 0) {
                terminal_set_color(vga_entry_color(m, VGA_COLOR_BLACK));
                terminal_writestring("\nKEYBOARD SHORTCUTS\n------------------------------\n");
                terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
                terminal_writestring("? = FAQ, + = split pane, - = close pane, TAB = switch pane\n");
                terminal_writestring("Ctrl+S = settings, ESC = close FAQ/settings/editor\n");
            } else {
                terminal_writestring("Unknown category. Try: Filesystem, System, Apps, Power, Keys\n");
            }
        } else {
            terminal_set_color(vga_entry_color(m, VGA_COLOR_BLACK));
            terminal_writestring("\nSharkOS Help\n------------------------------\n\n");
            terminal_set_color(vga_entry_color(c, VGA_COLOR_BLACK));
            terminal_writestring("FILESYSTEM  - ");
            terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
            terminal_writestring("ls, cd, cat, touch, edit, mkdir, rm, mv, cp, pwd, tree\n");
            terminal_set_color(vga_entry_color(c, VGA_COLOR_BLACK));
            terminal_writestring("SYSTEM      - ");
            terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
            terminal_writestring("whoami, sysinfo, kernelinfo, colors, lspci, ps, kill, exec\n");
            terminal_set_color(vga_entry_color(c, VGA_COLOR_BLACK));
            terminal_writestring("APPS        - ");
            terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
            terminal_writestring("clear, colors, credits, help, whatis, fortune, cowsay, sl, banner\n");
            terminal_set_color(vga_entry_color(c, VGA_COLOR_BLACK));
            terminal_writestring("POWER       - ");
            terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
            terminal_writestring("poweroff, reboot\n");
            terminal_set_color(vga_entry_color(c, VGA_COLOR_BLACK));
            terminal_writestring("KEYS        - ");
            terminal_set_color(vga_entry_color(g, VGA_COLOR_BLACK));
            terminal_writestring("?, +, -, TAB, Ctrl+S, ESC\n\n");
            terminal_set_color(vga_entry_color(y, VGA_COLOR_BLACK));
            terminal_writestring("Type 'help <category>' for details\n");
            terminal_set_color(vga_entry_color(d, VGA_COLOR_BLACK));
            terminal_writestring("Categories: Filesystem, System, Apps, Power, Keys\n\n");
            terminal_set_color(old_color);
        }
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
    } else if (strcmp(cmd_name, "uname") == 0) {
        terminal_writestring("SharkOS");
        if (strcmp(args, "-a") == 0) {
            terminal_writestring(" V1 i686 shark@SharkOS nemo");
        }
    } else if (strcmp(cmd_name, "hostname") == 0) {
        terminal_writestring("SharkOS");
    } else if (strcmp(cmd_name, "df") == 0) {
        char buf[16];
        int_to_string(pool_index, buf);
        terminal_writestring("Filesystem nodes: ");
        terminal_writestring(buf);
        terminal_writestring("/64 used\n");
    } else if (strcmp(cmd_name, "free") == 0) {
        char buf[64];
        uint32_t total = (uint32_t)(free_memory_end - (uintptr_t)&stack_top);
        uint32_t used = (uint32_t)(free_memory_start - (uintptr_t)&stack_top);
        uint32_t free_kb = (total - used) / 1024;
        uint32_t used_kb = used / 1024;
        terminal_writestring("             total        used        free\n");
        terminal_writestring("Heap:        ");
        int_to_string(total / 1024, buf); terminal_writestring(buf); terminal_writestring(" KB");
        terminal_writestring("       ");
        int_to_string(used_kb, buf); terminal_writestring(buf); terminal_writestring(" KB");
        terminal_writestring("       ");
        int_to_string(free_kb, buf); terminal_writestring(buf); terminal_writestring(" KB\n");
    } else if (strcmp(cmd_name, "hexdump") == 0 || strcmp(cmd_name, "hex") == 0) {
        struct fs_node* target = find_node(current_dir, args);
        if (target && target->type == FS_FILE) {
            int len = target->content_len;
            char hex[16];
            for (int i = 0; i < len; i += 16) {
                hex_to_string(i, hex);
                terminal_writestring(hex);
                terminal_writestring("  ");
                for (int j = 0; j < 16 && i + j < len; j++) {
                    unsigned char c = (unsigned char)target->content[i + j];
                    hex_to_string(c, hex);
                    terminal_writestring(&hex[2]);
                    if (j == 7) terminal_writestring("  ");
                    else terminal_writestring(" ");
                }
                int pad = (len - i < 16) ? (16 - (len - i)) * 3 + ((len - i) <= 7 ? 1 : 0) : 0;
                for (int p = 0; p < pad; p++) terminal_writestring(" ");
                terminal_writestring(" ");
                for (int j = 0; j < 16 && i + j < len; j++) {
                    unsigned char c = (unsigned char)target->content[i + j];
                    if (c >= 32 && c < 127) terminal_putchar(c);
                    else terminal_putchar('.');
                }
                terminal_writestring("\n");
            }
        } else {
            terminal_writestring("File not found.");
        }
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
                for (int i = 0; i < line_count - 1; i++) {
                    for (int j = 0; j < line_count - 1 - i; j++) {
                        if (strcmp(lines[j], lines[j+1]) > 0) {
                            char temp[128];
                            strcpy(temp, lines[j]);
                            strcpy(lines[j], lines[j+1]);
                            strcpy(lines[j+1], temp);
                        }
                    }
                }
                for (int i = 0; i < line_count; i++) {
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
        for (int yc = 0; yc < 5; yc++) {
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
            for (int bi = 0; bi < len; bi++) {
                if (args[bi] == '/') last_slash = bi;
            }
            terminal_writestring(args + last_slash + 1);
        }
    } else if (strcmp(cmd_name, "dirname") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: dirname <path>");
        } else {
            int last_slash = -1;
            int len = strlen(args);
            for (int di = 0; di < len; di++) {
                if (args[di] == '/') last_slash = di;
            }
            if (last_slash == -1) {
                terminal_writestring(".");
            } else if (last_slash == 0) {
                terminal_writestring("/");
            } else {
                for (int di = 0; di < last_slash; di++) {
                    terminal_putchar(args[di]);
                }
            }
        }
    } else if (strcmp(cmd_name, "uptime") == 0) {
        uint32_t total_secs = uptime_ticks / 100;
        uint32_t hours = total_secs / 3600;
        uint32_t mins = (total_secs % 3600) / 60;
        uint32_t secs = total_secs % 60;
        char buf[32];
        terminal_writestring("Uptime: ");
        int_to_string(hours, buf); terminal_writestring(buf); terminal_writestring("h ");
        int_to_string(mins, buf); terminal_writestring(buf); terminal_writestring("m ");
        int_to_string(secs, buf); terminal_writestring(buf); terminal_writestring("s\n");
    } else if (strcmp(cmd_name, "history") == 0) {
        for (int hi = 0; hi < history_count; hi++) {
            char buf[16];
            int_to_string(hi + 1, buf);
            terminal_writestring(buf);
            terminal_writestring("  ");
            terminal_writestring(command_history[hi]);
            terminal_writestring("\n");
        }
    } else if (strcmp(cmd_name, "find") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: find <name>");
        } else {
            struct fs_node* stack[64];
            int sp = 0;
            stack[sp++] = root;
            int found = 0;
            while (sp > 0) {
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
            "printf(\"Hello World\"); // the beginning",
            "rm -rf / --no-preserve-root // just kidding"
        };
        int idx = (uptime_ticks / 7) % 10;
        terminal_writestring(fortunes[idx]);
        terminal_writestring("\n");
    } else if (strcmp(cmd_name, "cowsay") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: cowsay <text>");
        } else {
            terminal_writestring(" ");
            for (int i = 0; i < strlen(args) + 2; i++) terminal_writestring("_");
            terminal_writestring("\n< ");
            terminal_writestring(args);
            terminal_writestring(" >\n ");
            for (int i = 0; i < strlen(args) + 2; i++) terminal_writestring("-");
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
            char nc = keyboard_getchar();
            if (nc == '\n') break;
            if (nc == '\b') { if (ni > 0) ni--; continue; }
            if (nc >= '0' && nc <= '9') numbuf[ni++] = nc;
        }
        numbuf[ni] = '\0';
        int guess = 0;
        for (int i = 0; i < ni; i++) guess = guess * 10 + (numbuf[i] - '0');
        if (guess == secret) terminal_writestring("\nCorrect! You win!");
        else if (guess < secret) terminal_writestring("\nToo low!");
        else terminal_writestring("\nToo high!");
        terminal_writestring(" (answer was ");
        char sbuf[16]; int_to_string(secret, sbuf); terminal_writestring(sbuf);
        terminal_writestring(")\n");
    } else if (strcmp(cmd_name, "tictactoe") == 0) {
        char board[9];
        for (int i = 0; i < 9; i++) board[i] = '1' + i;
        int moves = 0;
        terminal_writestring("\n=== TIC TAC TOE ===\n");
        terminal_writestring("You=X  CPU=O\n");
        terminal_writestring("Type number 1-9 then Enter\n\n");
        while (moves < 9) {
            terminal_writestring("Board: ");
            for (int i = 0; i < 9; i++) {
                terminal_putchar(board[i]);
                if (i % 3 != 2) terminal_writestring("|");
            }
            terminal_writestring("\n");
            terminal_writestring("Move (1-9): ");
            int pos = 0;
            while (pos < 1 || pos > 9) {
                char nc = keyboard_getchar();
                if (nc == 0) continue;
                if (nc >= '1' && nc <= '9') {
                    pos = nc - '0';
                    terminal_putchar(nc);
                }
            }
            terminal_writestring("\n");
            if (board[pos - 1] == 'X' || board[pos - 1] == 'O') {
                terminal_writestring("Spot taken!\n");
                continue;
            }
            board[pos - 1] = 'X';
            moves++;
            int win = 0;
            int wins[8][3] = {{0,1,2},{3,4,5},{6,7,8},{0,3,6},{1,4,7},{2,5,8},{0,4,8},{2,4,6}};
            for (int w = 0; w < 8; w++) {
                if (board[wins[w][0]] == 'X' && board[wins[w][1]] == 'X' && board[wins[w][2]] == 'X') win = 1;
            }
            if (win) { terminal_writestring("\nYou win!\n"); break; }
            if (moves >= 9) { terminal_writestring("\nDraw!\n"); break; }
            for (int i = 0; i < 9; i++) {
                if (board[i] != 'X' && board[i] != 'O') { board[i] = 'O'; moves++; break; }
            }
            win = 0;
            for (int w = 0; w < 8; w++) {
                if (board[wins[w][0]] == 'O' && board[wins[w][1]] == 'O' && board[wins[w][2]] == 'O') win = 1;
            }
            if (win) { terminal_writestring("\nCPU wins!\n"); break; }
        }
    } else if (strcmp(cmd_name, "whatis") == 0) {
        if (strlen(args) == 0) {
            terminal_writestring("Usage: whatis <command>");
        } else if (strcmp(args, "ls") == 0 || strcmp(args, "dir") == 0) {
            terminal_writestring("ls - list files in the current directory");
        } else if (strcmp(args, "cd") == 0) {
            terminal_writestring("cd <dir> - change current directory (use .. for parent, / for root)");
        } else if (strcmp(args, "cat") == 0) {
            terminal_writestring("cat <file> - display the contents of a file");
        } else if (strcmp(args, "touch") == 0) {
            terminal_writestring("touch <file> - create an empty file");
        } else if (strcmp(args, "edit") == 0) {
            terminal_writestring("edit <file> - open the built-in text editor");
        } else if (strcmp(args, "mkdir") == 0) {
            terminal_writestring("mkdir <name> - create a new directory");
        } else if (strcmp(args, "rm") == 0) {
            terminal_writestring("rm [force] <name> - remove a file or directory. Use 'force' prefix inside /System/");
        } else if (strcmp(args, "mv") == 0 || strcmp(args, "rename") == 0) {
            terminal_writestring("mv <src> <dst> - rename a file or directory");
        } else if (strcmp(args, "cp") == 0) {
            terminal_writestring("cp <src> <dst> - copy a file");
        } else if (strcmp(args, "pwd") == 0) {
            terminal_writestring("pwd - print the current working directory path");
        } else if (strcmp(args, "tree") == 0) {
            terminal_writestring("tree - display the directory structure as a tree");
        } else if (strcmp(args, "head") == 0) {
            terminal_writestring("head <file> [n] - show the first n lines of a file (default 10)");
        } else if (strcmp(args, "tail") == 0) {
            terminal_writestring("tail <file> [n] - show the last n lines of a file (default 10)");
        } else if (strcmp(args, "wc") == 0) {
            terminal_writestring("wc <file> - count lines, words, and characters in a file");
        } else if (strcmp(args, "nl") == 0) {
            terminal_writestring("nl <file> - number the lines of a file");
        } else if (strcmp(args, "sort") == 0) {
            terminal_writestring("sort <file> - sort the lines of a file alphabetically");
        } else if (strcmp(args, "echo") == 0) {
            terminal_writestring("echo <text> - print the given text to the terminal");
        } else if (strcmp(args, "calc") == 0) {
            terminal_writestring("calc <expr> - evaluate a simple expression (e.g. 2+3)");
        } else if (strcmp(args, "clear") == 0 || strcmp(args, "cls") == 0) {
            terminal_writestring("clear/cls - clear the terminal screen");
        } else if (strcmp(args, "whoami") == 0) {
            terminal_writestring("whoami - display the current user name");
        } else if (strcmp(args, "sysinfo") == 0 || strcmp(args, "neofetch") == 0) {
            terminal_writestring("sysinfo/neofetch - display system information");
        } else if (strcmp(args, "kernelinfo") == 0) {
            terminal_writestring("kernelinfo - show kernel architecture and version details");
        } else if (strcmp(args, "colors") == 0) {
            terminal_writestring("colors - display the 16-color VGA palette");
        } else if (strcmp(args, "lspci") == 0) {
            terminal_writestring("lspci - enumerate and list PCI devices");
        } else if (strcmp(args, "ps") == 0) {
            terminal_writestring("ps - list all running processes");
        } else if (strcmp(args, "kill") == 0) {
            terminal_writestring("kill <pid> - terminate a process by its PID");
        } else if (strcmp(args, "exec") == 0) {
            terminal_writestring("exec <program> - execute a binary from the PATH");
        } else if (strcmp(args, "uname") == 0) {
            terminal_writestring("uname - print OS name. Use -a for full details");
        } else if (strcmp(args, "hostname") == 0) {
            terminal_writestring("hostname - print the system hostname");
        } else if (strcmp(args, "ping") == 0) {
            terminal_writestring("ping <host> - send ICMP echo request to a host");
        } else if (strcmp(args, "shs") == 0) {
            terminal_writestring("shs <script.shx> - run a SharkScript file");
        } else if (strcmp(args, "which") == 0) {
            terminal_writestring("which <command> - locate a command in the system PATH");
        } else if (strcmp(args, "yes") == 0) {
            terminal_writestring("yes [text] - repeatedly print text (default 'y', limited to 5 lines)");
        } else if (strcmp(args, "sleep") == 0) {
            terminal_writestring("sleep <ms> - pause execution for the given milliseconds");
        } else if (strcmp(args, "basename") == 0) {
            terminal_writestring("basename <path> - strip directory from a path, return filename only");
        } else if (strcmp(args, "dirname") == 0) {
            terminal_writestring("dirname <path> - extract the directory portion from a path");
        } else if (strcmp(args, "env") == 0) {
            terminal_writestring("env - display the runtime environment variables");
        } else if (strcmp(args, "hexdump") == 0 || strcmp(args, "hex") == 0) {
            terminal_writestring("hexdump/hex <file> - display file contents in hexadecimal format");
        } else if (strcmp(args, "df") == 0) {
            terminal_writestring("df - show filesystem node usage");
        } else if (strcmp(args, "free") == 0) {
            terminal_writestring("free - display memory usage statistics");
        } else if (strcmp(args, "date") == 0) {
            terminal_writestring("date - show current date/time (RTC driver required)");
        } else if (strcmp(args, "uptime") == 0) {
            terminal_writestring("uptime - show system uptime");
        } else if (strcmp(args, "poweroff") == 0 || strcmp(args, "bokop") == 0) {
            terminal_writestring("poweroff/bokop - shut down the system");
        } else if (strcmp(args, "reboot") == 0) {
            terminal_writestring("reboot - restart the system");
        } else if (strcmp(args, "credits") == 0) {
            terminal_writestring("credits - display SharkOS developer credits");
        } else if (strcmp(args, "help") == 0) {
            terminal_writestring("help [category] - show this help menu. Categories: Filesystem, System, Apps, Power, Keys");
        } else if (strcmp(args, "whatis") == 0) {
            terminal_writestring("whatis <command> - this command. Shows what a command does and how to use it");
        } else {
            terminal_writestring("No information available for: ");
            terminal_writestring(args);
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