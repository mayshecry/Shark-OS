#include "kernel.h"
task_t* create_task(const char* name) {
    spin_lock(&task_list_lock);
    task_t* t = (task_t*)kmalloc(sizeof(task_t));
    if (!t) { spin_unlock(&task_list_lock); return NULL; }
    t->id = next_pid++; t->state = TASK_READY; t->cpu_id = 0;
    strcpy(t->name, name);
    t->next = task_list; task_list = t;
    spin_unlock(&task_list_lock);
    return t;
}
void yield() { }
struct multiboot_info {
    uint32_t flags, mem_lower, mem_upper, boot_device, cmdline;
    uint32_t mods_count, mods_addr, num, size, addr, shndx;
    uint32_t mmap_length, mmap_addr, drives_length, drives_addr;
    uint32_t config_table, boot_loader_name, apm_table;
    uint32_t vbe_control_info, vbe_mode_info;
    uint16_t vbe_mode, vbe_interface_seg, vbe_interface_off, vbe_interface_len;
    uint32_t framebuffer_addr_lo, framebuffer_addr_hi, framebuffer_pitch;
    uint32_t framebuffer_width, framebuffer_height;
    uint8_t framebuffer_bpp, framebuffer_type;
} __attribute__((packed));
struct multiboot_mmap_entry { uint32_t size, addr_low, addr_high, len_low, len_high, type; } __attribute__((packed));
static void boot_print(const char* s) { terminal_writestring(s); }
void kmain(uint32_t magic, struct multiboot_info* mb_info) {
    if (magic != 0x2BADB002) return;
    asm volatile("cli");
    lfbptr = (uint32_t*)(uintptr_t)mb_info->framebuffer_addr_lo;
    screen_width = mb_info->framebuffer_width;
    screen_height = mb_info->framebuffer_height;
    screen_pitch = mb_info->framebuffer_pitch;
    if (screen_width < 320) screen_width = 320;
    if (screen_height < 200) screen_height = 200;
    total_system_memory = (uint64_t)mb_info->mem_upper + (uint64_t)mb_info->mem_lower;
    ui_init_metrics();
    pane_count = 1;
    active_pane = 0;
    panes[0].col_start = 0;
    panes[0].col_end = term_cols;
    panes[0].row = content_first_row;
    panes[0].col = 0;
    panes[0].color = vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    panes[0].cmd_index = 0;
    terminal_row = content_first_row;
    terminal_column = 0;
    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    char buf[64];
    terminal_writestring("SharkOS V1 [");
    hex_to_string((uint32_t)total_system_memory >> 20, buf);
    terminal_writestring(buf);
    terminal_writestring(" MB RAM]\nBooting nemo...\n");
    boot_print("[    0.000000] nemo (SharkOS V1 Kernel build 0.06) (gcc)\n");
    char cpu_model[49];
    get_cpu_model(cpu_model);
    boot_print(cpu_model);
    boot_print("\n");
    boot_print("[    0.000001] BIOS-e820 memory map detected\n");
    boot_print("[    0.000002] ");
    hex_to_string((uint32_t)total_system_memory >> 20, buf);
    terminal_writestring(buf);
    boot_print(" MB usable RAM\n");
    boot_print("[    0.000003] Framebuffer: ");
    int_to_string(screen_width, buf); terminal_writestring(buf);
    terminal_writestring("x");
    int_to_string(screen_height, buf); terminal_writestring(buf);
    char bppbuf[8]; int_to_string(mb_info->framebuffer_bpp, bppbuf);
    terminal_writestring("@");
    terminal_writestring(bppbuf); terminal_writestring("bpp\n");
    boot_print("[    0.000004] CPU features: FPU, PAE, PSE, multicore detected\n");
    boot_print("[    0.000005] Local APIC timer: 100 Hz\n");
    boot_print("[    0.000006] HZ: 1000\n");
    boot_print("[    0.000007] PID max: 32768\n");
    boot_print("[    0.000008] Initializing cgroup subsys cpuset\n");
    boot_print("[    0.000009] Booting paravirtualized kernel on KVM\n");
    boot_print("[    0.000010] KVM setup done\n");
    boot_print("[    0.000011] kvm-clock: cpu 0, primary 0\n");
    boot_print("[    0.000012] TSC: PIT calibration matches HPET\n");
    boot_print("[    0.000013] Booting processor 1/1 APIC 0x0\n");
    boot_print("[    0.000014] x86/mm: Memory block size: 128MB\n");
    boot_print("[    0.000015] ACPI: Core revision 20240322\n");
    boot_print("[    0.000016] PM:  4.0.5\n");
    boot_print("[    0.000017] SLUB: HWalign=64, Order=0-3\n");
    boot_print("[    0.000018] rcu: Hierarchical RCU implementation\n");
    boot_print("[    0.000019] Memory: ");
    hex_to_string((uint32_t)total_system_memory >> 20, buf);
    terminal_writestring(buf);
    boot_print("M available\n");
    boot_print("[    0.000020] Built 1 zonelists, Total pages: ");
    hex_to_string((uint32_t)total_system_memory >> 12, buf);
    terminal_writestring(buf);
    boot_print("\n[    0.000021] Kernel command line: root=/dev/ram0 rw\n");
    boot_print("[    0.000022] PID hash table entries: 4096\n");
    boot_print("[    0.000023] Dentry cache hash table entries: 131072\n");
    boot_print("[    0.000024] Inode-cache hash table entries: 65536\n");
    boot_print("[    0.000025] Freeing SMP alternatives: 0frees\n");
    boot_print("[    0.000026] smpboot: CPU0: ");
    terminal_writestring(cpu_model);
    boot_print("\n[    0.000027] ACPI: 2 ACPI AML tables successfully acquired\n");
    boot_print("[    0.000028] ACPI: Setting up all available GPEs\n");
    boot_print("[    0.000029] Last level iTLB entries: 4KB 0, 2MB 0, 4MB 0\n");
    boot_print("[    0.000030] Last level dTLB entries: 4KB 0, 2MB 0, 4MB 0, 1GB 0\n");
    boot_print("[    0.000031] NMI watchdog: Enabled\n");
    boot_print("[    0.000032] SHKRNL boot complete.\n");
    delay_ms(500);
    terminal_initialize();
    init_descriptor_tables();
    outb(0x21, inb(0x21) & ~0x02);
    
    outb(0x43, 0x36);
    outb(0x40, 0x9C);
    outb(0x40, 0x2E);
    asm volatile("sti");
    terminal_clear();
    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK));
    terminal_writestring("   ╔═══════════════════════════════════╗\n");
    terminal_writestring("   ║          SETUP MODE              ║\n");
    terminal_writestring("   ╚═══════════════════════════════════╝\n\n");
    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("  Enter your username: ");
    terminal_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    int name_idx = 0;
    char name_buf[32];
    while (name_idx < 31) {
        char nc = keyboard_getchar();
        if (nc == 0) continue;
        if (nc == '\n') break;
        if (nc == '\b') {
            if (name_idx > 0) {
                name_idx--;
                if (terminal_column > panes[active_pane].prompt_end_col) {
                    terminal_column--;
                    terminal_putentryat(' ', terminal_color, terminal_column, terminal_row);
                }
            }
            continue;
        }
        if (nc >= 32 && nc < 127) {
            name_buf[name_idx++] = nc;
            terminal_putchar(nc);
        }
    }
    name_buf[name_idx] = '\0';
    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK));
    terminal_writestring("\n\n  ═══════════════════════════════════\n");
    terminal_writestring("  Customize your experience\n\n");
    int setup_tiling = 1;
    int setup_mouse = 0;
    int setup_theme = 0;
    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("  Enable tiling panes? (Y/n): ");
    terminal_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    while (1) {
        char nc = keyboard_getchar();
        if (nc == 0) continue;
        if (nc == '\n') break;
        if (nc == 'y' || nc == 'Y') { setup_tiling = 1; terminal_putchar('Y'); break; }
        if (nc == 'n' || nc == 'N') { setup_tiling = 0; terminal_putchar('N'); break; }
    }
    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("\n  Enable mouse support? (y/N): ");
    terminal_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    while (1) {
        char nc = keyboard_getchar();
        if (nc == 0) continue;
        if (nc == '\n') break;
        if (nc == 'y' || nc == 'Y') { setup_mouse = 1; terminal_putchar('Y'); break; }
        if (nc == 'n' || nc == 'N') { setup_mouse = 0; terminal_putchar('N'); break; }
    }
    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("\n  Enable retro TempleOS theme? (y/N): ");
    terminal_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    while (1) {
        char nc = keyboard_getchar();
        if (nc == 0) continue;
        if (nc == '\n') break;
        if (nc == 'y' || nc == 'Y') { setup_theme = 2; terminal_putchar('Y'); break; }
        if (nc == 'n' || nc == 'N') { setup_theme = 0; terminal_putchar('N'); break; }
    }
    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("\n\n  Applying settings...\n");
    if (name_idx > 0) strcpy(current_user, name_buf);
    tiling_enabled = setup_tiling;
    mouse_enabled = setup_mouse;
    selected_theme = setup_theme;
    apply_theme(selected_theme);
    delay_ms(500);
    fs_initialize();
    detect_network_cards();
    struct fs_node* user_dir = find_node(root, "User");
    if (user_dir) strcpy(user_dir->name, current_user);
    terminal_clear();
    print_prompt();

    while (1) {
        char c = keyboard_getchar();
        if (c == 0) continue;

        if (current_kernel_mode == KERNEL_MODE_SETTINGS) {
            if (c == 27) {
                settings_close();
            } else if (c == 0x10) {
                if (settings_selected > 0) settings_selected--;
                settings_draw();
            } else if (c == 0x11) {
                if (settings_selected < 2) settings_selected++;
                settings_draw();
            } else if (c == '\n') {
                if (settings_selected == 0) {
                    tiling_enabled = !tiling_enabled;
                } else if (settings_selected == 1) {
                    mouse_enabled = !mouse_enabled;
                } else if (settings_selected == 2) {
                    selected_theme = (selected_theme + 1) % MAX_THEMES;
                    apply_theme(selected_theme);
                }
                settings_draw();
            }
            continue;
        }

        if (current_kernel_mode == KERNEL_MODE_EDITOR) {
            if (c == 27) {
                if (editor_target_file) {
                    size_t si;
                    for (si = 0; si < editor_buffer_idx && si < MAX_FILE_CONTENT_SIZE - 1; si++) {
                        editor_target_file->content[si] = editor_buffer[si];
                    }
                    editor_target_file->content[si] = '\0';
                    editor_target_file->content_len = si;
                }
                current_kernel_mode = KERNEL_MODE_CLI;
                terminal_clear();
                print_prompt();
                continue;
            }
            terminal_putchar_editor(c);
            continue;
        }

        if (current_kernel_mode == KERNEL_MODE_FAQ) {
            if (c == 27 || c == '?') {
                faq_close();
            }
            continue;
        }

        if (ctrl_pressed && c == 's') {
            settings_open();
            continue;
        }
        if (c == '\t') {
            int new_pane = (active_pane + 1) % pane_count;
            active_pane = new_pane;
            draw_pane_tabs();
            terminal_row = panes[new_pane].row;
            terminal_column = panes[new_pane].prompt_end_col + panes[new_pane].cmd_index;
            continue;
        }
        if (command_index == 0) {
            if (c == '+') {
                split_active_pane();
                continue;
            }
            if (c == '-') {
                close_active_pane();
                continue;
            }
            if (c == '?') {
                faq_open();
                continue;
            }
        }
        
        if (mouse_state.wheel != 0 && current_kernel_mode == KERNEL_MODE_CLI) {
            scrollback_offset += mouse_state.wheel;
            if (scrollback_offset < 0) scrollback_offset = 0;
            if (scrollback_offset > scrollback.count) scrollback_offset = scrollback.count;
            terminal_draw_scrollback();
            draw_cursor();
            mouse_state.wheel = 0;
            continue;
        }

        
        if (c == 0x10 && history_count > 0 && command_index == 0 && current_kernel_mode == KERNEL_MODE_CLI) {
            if (history_index < history_count - 1) history_index++;
            else history_index = 0;
            
            draw_rect(col_px(terminal_column), row_px(terminal_row), font_cell_w, font_cell_h, UI_SURFACE);
            terminal_column = panes[active_pane].prompt_end_col;
            
            terminal_writestring(command_history[history_count - 1 - history_index]);
            command_index = strlen(command_history[history_count - 1 - history_index]);
            strcpy(command_buffer, command_history[history_count - 1 - history_index]);
            draw_cursor();
            continue;
        }
        if (c == 0x11 && history_count > 0 && command_index == 0 && current_kernel_mode == KERNEL_MODE_CLI) {
            if (history_index > 0) history_index--;
            else history_index = history_count - 1;
            draw_rect(col_px(terminal_column), row_px(terminal_row), font_cell_w, font_cell_h, UI_SURFACE);
            terminal_column = panes[active_pane].prompt_end_col;
            terminal_writestring(command_history[history_count - 1 - history_index]);
            command_index = strlen(command_history[history_count - 1 - history_index]);
            strcpy(command_buffer, command_history[history_count - 1 - history_index]);
            draw_cursor();
            continue;
        }

        terminal_putchar_cli(c);
        if (c == '\n') {
            command_buffer[command_index] = '\0';
            
            if (history_count < MAX_HISTORY) {
                strcpy(command_history[history_count], command_buffer);
                history_count++;
            } else {
                for (int h = 0; h < MAX_HISTORY - 1; h++) {
                    strcpy(command_history[h], command_history[h + 1]);
                }
                strcpy(command_history[MAX_HISTORY - 1], command_buffer);
            }
            history_index = -1;
            scrollback_offset = 0;
        execute_command(command_buffer);
        command_index = 0;
        command_buffer[0] = '\0';
        if (current_kernel_mode == KERNEL_MODE_CLI) {
            terminal_writestring("\n");
            print_prompt();
        }
        }
    }
}
