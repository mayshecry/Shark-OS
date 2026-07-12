#include "kernel.h"
#include "plugin_manager.h"

extern void ui_draw_chrome(void);
extern void ui_fill_content_bg(void);
extern void draw_pane_tabs(void);
extern void terminal_draw_scrollback(void);
extern void ui_draw_footer(void);

void lite_kmain(void) {
    terminal_color = vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    terminal_writestring("SharkOS Lite\n");
    tiling_enabled = false;
    mouse_enabled = false;
    init_descriptor_tables();
    outb(0x21, inb(0x21) & ~0x02);
    uint16_t divisor = 1193182 / 1000;
    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
    asm volatile("sti");
    fs_initialize();
    terminal_initialize();
    terminal_clear();
    
    
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
    extern int geometrydash_plugin_init(void);
    extern void geometrydash_plugin_cleanup(void);
    extern int geometrydash_plugin_command(int argc, char** argv);
    plugin_register_builtin("gdash", geometrydash_plugin_init, geometrydash_plugin_cleanup, geometrydash_plugin_command);
    
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
    
    apply_theme(THEME_SHARKOS);
    
    redraw_all_panes();
    print_prompt();

    while (1) {
        char c = keyboard_getchar();
        if (c == 0) { yield(); continue; }

        if (c == '\n') {
            command_buffer[command_index] = '\0';
            execute_command(command_buffer);
            command_index = 0;
            command_buffer[0] = '\0';
            terminal_writestring("\n");
            print_prompt();
        } else if (c == '\b') {
            if (command_index > 0) {
                command_index--;
                size_t old_col = terminal_column;
                size_t old_row = terminal_row;
                if (terminal_column > panes[active_pane].col_start) {
                    terminal_column--;
                } else if (terminal_row > content_first_row) {
                    terminal_row--;
                    terminal_column = panes[active_pane].col_end - 1;
                }
                draw_rect(col_px(old_col), row_px(old_row), font_cell_w, font_cell_h, UI_SURFACE);
                draw_rect(col_px(terminal_column), row_px(terminal_row), font_cell_w, font_cell_h, UI_SURFACE);
                draw_cursor();
            }
        } else if (c >= 32 && c < 127) {
            terminal_color = vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            if (command_index < sizeof(command_buffer) - 1) {
                command_buffer[command_index++] = c;
            }
            terminal_putentryat(c, terminal_color, terminal_column, terminal_row);
            if (++terminal_column >= panes[active_pane].col_end) {
                terminal_column = panes[active_pane].col_start;
                if (++terminal_row >= term_max_row) {
                    terminal_scroll();
                }
            }
            draw_cursor();
        }
    }
}