#include "kernel.h"
#include "plugin_manager.h"
#include "doom.h"
#include "flappybird.h"
#include "smb.h"
#include "pong.h"

extern void ui_draw_chrome(void);
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
void yield(void) {
    asm volatile("hlt");
}
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

    if (mb_info->flags & (1 << 2)) {
        char* cmdline = (char*)(uintptr_t)mb_info->cmdline;
        if (cmdline) {
            char* lite_pos = cmdline;
            while (*lite_pos) {
                if (*lite_pos == 'l' && *(lite_pos+1) == 'i' && *(lite_pos+2) == 't' && *(lite_pos+3) == 'e') {
                    lite_mode = true;
                    break;
                }
                lite_pos++;
            }
        }
    }

    lfbptr = (uint32_t*)(uintptr_t)mb_info->framebuffer_addr_lo;
    screen_width = mb_info->framebuffer_width;
    screen_height = mb_info->framebuffer_height;
    screen_pitch = mb_info->framebuffer_pitch;
    if (screen_width < 320) screen_width = 320;
    if (screen_height < 200) screen_height = 200;
    total_system_memory = ((uint64_t)mb_info->mem_upper + (uint64_t)mb_info->mem_lower);
    pmm_init(total_system_memory);
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

    if (lite_mode) {
        extern void lite_kmain(void);
        lite_kmain();
    }

    char buf[64];
    terminal_writestring("SharkOS V2 [");
    hex_to_string((uint32_t)total_system_memory >> 20, buf);
    terminal_writestring(buf);
    terminal_writestring(" MB RAM]\nBooting nemo...\n");
    boot_print("[    0.000000] nemo (SharkOS V2 Lite) (gcc)\n");
    char cpu_model[49];
    get_cpu_model(cpu_model);
    boot_print(cpu_model);
    boot_print("\n");
    boot_print("[    0.000001] ");
    hex_to_string((uint32_t)total_system_memory >> 20, buf);
    terminal_writestring(buf);
    boot_print(" MB RAM\n");
    boot_print("[    0.000002] Framebuffer: ");
    int_to_string(screen_width, buf); terminal_writestring(buf);
    terminal_writestring("x");
    int_to_string(screen_height, buf); terminal_writestring(buf);
    terminal_writestring("\n[    0.000003] SHKRNL boot complete.\n");
    terminal_initialize();
    init_descriptor_tables();
    outb(0x21, inb(0x21) & ~0x01 & ~0x02);

    uint16_t divisor = 1193182 / 1000;
    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
    asm volatile("sti");
    terminal_clear();
    strcpy(current_user, "sharkuser");
    tiling_enabled = 1;
    mouse_enabled = 1;
    selected_theme = 0;
    apply_theme(selected_theme);
    fs_initialize();

    outb(0x21, inb(0x21) & ~0x04);
    outb(0xA1, inb(0xA1) & ~0x04);
    outb(0x21, inb(0x21) & ~0x02);
    mouse_init();

    plugin_manager_init();
    extern int plugin_init_entry(void);
    extern void plugin_cleanup_entry(void);
    extern int plugin_command_entry(int argc, char** argv);
    plugin_register_builtin("python", plugin_init_entry, plugin_cleanup_entry, plugin_command_entry);
    extern int doom_plugin_init(void);
    extern void doom_plugin_cleanup(void);
    extern int doom_plugin_command(int argc, char** argv);
    plugin_register_builtin("doom", doom_plugin_init, doom_plugin_cleanup, doom_plugin_command);
    extern int flappybird_plugin_init(void);
    extern void flappybird_plugin_cleanup(void);
    extern int flappybird_plugin_command(int argc, char** argv);
    plugin_register_builtin("flappybird", flappybird_plugin_init, flappybird_plugin_cleanup, flappybird_plugin_command);
    extern int smb_plugin_init(void);
    extern void smb_plugin_cleanup(void);
    extern int smb_plugin_command(int argc, char** argv);
    plugin_register_builtin("smb", smb_plugin_init, smb_plugin_cleanup, smb_plugin_command);
    extern int pong_plugin_init(void);
    extern void pong_plugin_cleanup(void);
    extern int pong_plugin_command(int argc, char** argv);
    plugin_register_builtin("pong", pong_plugin_init, pong_plugin_cleanup, pong_plugin_command);

    detect_network_cards();
    struct fs_node* user_dir = find_node(root, "User");
    if (user_dir) strcpy(user_dir->name, current_user);
    while (keyboard_getchar() != 0);
    terminal_clear();
    redraw_all_panes();
    print_prompt();

    volatile uint32_t last_footer_tick = 0;

    while (1) {
        yield();

        if (uptime_ticks - last_footer_tick >= 1000) {
            last_footer_tick = uptime_ticks;
            ui_draw_footer();
        }

        char c = keyboard_getchar();
        if (c == 0) {
            continue;
        }

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
            terminal_column = panes[new_pane].col_start;
            print_prompt();
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
                 if (terminal_column != panes[active_pane].col_start) {
                     terminal_putchar('\n');
                 }
                 print_prompt();
             }
         }
    }
}