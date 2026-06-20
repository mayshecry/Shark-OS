#include "kernel.h"

void print_prompt() {
    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK));
    terminal_writestring("shark> ");
    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
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
        for (int i = 0; i < current_dir->num_children; i++) {
            terminal_writestring(current_dir->children[i]->name);
            if (current_dir->children[i]->type == FS_DIRECTORY) terminal_writestring("/");
            terminal_writestring("  ");
        }
    } else if (strcmp(cmd_name, "cd") == 0) {
        if (strcmp(args, "..") == 0) {
            if (current_dir->parent) current_dir = current_dir->parent;
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
        print_prompt();
        return;
    } else if (strcmp(cmd_name, "help") == 0) {
        terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK));
        terminal_writestring("=== SharkOS Command Reference ===");
        terminal_writestring("\n");
        terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
        terminal_writestring("Filesystem: ");
        terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        terminal_writestring("ls, dir, cd, cat, touch, edit\n");
        terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
        terminal_writestring("System:     ");
        terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        terminal_writestring("whoami, ping, sysinfo, colors, lspci, clear, cls, help\n");
        terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
        terminal_writestring("UI:         ");
        terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        terminal_writestring("+ = split pane, TAB = focus, - = close pane\n");
        terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
        terminal_writestring("Apps:       ");
        terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        terminal_writestring("shs, gcc, go\n");
        terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
        terminal_writestring("Power:      ");
        terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        terminal_writestring("bokop, poweroff\n");
        terminal_set_color(old_color);
    } else if (strcmp(cmd_name, "lspci") == 0) {
        pci_list_devices();
    } else if (strcmp(cmd_name, "bokop") == 0 || strcmp(cmd_name, "poweroff") == 0) {
        terminal_writestring("Shutting down...");
        shutdown();
    } else if (strcmp(cmd_name, "sysinfo") == 0) {
        show_fastfetch();
    } else if (strcmp(cmd_name, "colors") == 0) {
        uint8_t old_color = terminal_color;
        terminal_writestring("SharkOS 16-Color Palette:\n");
        for (int i = 0; i < 16; i++) {
            terminal_set_color(vga_entry_color(i, VGA_COLOR_BLACK));
            terminal_writestring("Color Code ");
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
    } else {
        struct fs_node* bin = search_path(cmd_name);
        if (bin) {
            execute_elf((uint8_t*)bin->content, args);
        } else {
            terminal_writestring("Unknown command: ");
            terminal_writestring(cmd_name);
        }
    }

    terminal_writestring("\n");
    print_prompt();
}