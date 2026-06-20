#include "kernel.h"

#define LOGO_LINES 8
static const char* logo_art[LOGO_LINES] = {
    "        \\     /",
    "         \\|/",
    "        --O--",
    "       /  |  \\",
    "      /   |   \\",
    "     ~~ shark ~~",
    "   +~~~~~~~~~~~~+",
    "   | The Sharkslayer |"
};

#define LOGO_COLOR VGA_COLOR_LIGHT_CYAN
#define INFO_LABEL_COLOR VGA_COLOR_LIGHT_CYAN
#define INFO_VALUE_COLOR VGA_COLOR_WHITE
#define INFO_ACCENT_COLOR VGA_COLOR_LIGHT_MAGENTA
#define INFO_HEADER_COLOR VGA_COLOR_LIGHT_BROWN
#define INFO_OK_COLOR VGA_COLOR_LIGHT_GREEN

void show_fastfetch(void) {
    char cpu_model[49];
    get_cpu_model(cpu_model);
    uint8_t old_color = terminal_color;
    char buf[32];

    uint8_t logo_color = LOGO_COLOR;
    for (int i = 0; i < LOGO_LINES; i++) {
        terminal_set_color(vga_entry_color(logo_color, VGA_COLOR_BLACK));
        terminal_writestring(logo_art[i]);
        switch (i) {
            case 0:
                terminal_set_color(vga_entry_color(INFO_HEADER_COLOR, VGA_COLOR_BLACK));
                terminal_writestring("  shark@SharkOS");
                break;
            case 1:
                terminal_set_color(vga_entry_color(INFO_ACCENT_COLOR, VGA_COLOR_BLACK));
                terminal_writestring("  -------------");
                break;
            case 2:
                terminal_set_color(vga_entry_color(INFO_LABEL_COLOR, VGA_COLOR_BLACK));
                terminal_writestring("  OS: ");
                terminal_set_color(vga_entry_color(INFO_VALUE_COLOR, VGA_COLOR_BLACK));
                terminal_writestring("SharkOS v0.1");
                break;
            case 3:
                terminal_set_color(vga_entry_color(INFO_LABEL_COLOR, VGA_COLOR_BLACK));
                terminal_writestring("  Host: ");
                terminal_set_color(vga_entry_color(INFO_VALUE_COLOR, VGA_COLOR_BLACK));
                terminal_writestring("The Sharkslayer");
                break;
            case 4:
                terminal_set_color(vga_entry_color(INFO_LABEL_COLOR, VGA_COLOR_BLACK));
                terminal_writestring("  Kernel: ");
                terminal_set_color(vga_entry_color(INFO_VALUE_COLOR, VGA_COLOR_BLACK));
                terminal_writestring("SharkOS v0.1 i686");
                break;
            case 5:
                terminal_set_color(vga_entry_color(INFO_LABEL_COLOR, VGA_COLOR_BLACK));
                terminal_writestring("  CPU: ");
                terminal_set_color(vga_entry_color(INFO_VALUE_COLOR, VGA_COLOR_BLACK));
                terminal_writestring(cpu_model);
                break;
            case 6:
                terminal_set_color(vga_entry_color(INFO_LABEL_COLOR, VGA_COLOR_BLACK));
                terminal_writestring("  Memory: ");
                terminal_set_color(vga_entry_color(INFO_VALUE_COLOR, VGA_COLOR_BLACK));
                int_to_string(total_system_memory / 1024 / 1024, buf);
                terminal_writestring(buf);
                terminal_writestring(" MB");
                break;
            case 7:
                terminal_set_color(vga_entry_color(INFO_LABEL_COLOR, VGA_COLOR_BLACK));
                terminal_writestring("  Display: ");
                terminal_set_color(vga_entry_color(INFO_VALUE_COLOR, VGA_COLOR_BLACK));
                int_to_string(screen_width, buf); terminal_writestring(buf); terminal_writestring("x");
                int_to_string(screen_height, buf); terminal_writestring(buf);
                break;
        }
        terminal_writestring("\n");
    }

    terminal_set_color(vga_entry_color(INFO_LABEL_COLOR, VGA_COLOR_BLACK));
    terminal_writestring("\n  Network: ");
    terminal_set_color(vga_entry_color(rtl_io_base != 0 ? INFO_OK_COLOR : INFO_VALUE_COLOR, VGA_COLOR_BLACK));
    terminal_writestring(rtl_io_base != 0 ? "RTL8139 Online" : "Loopback Mode");
    terminal_set_color(vga_entry_color(INFO_LABEL_COLOR, VGA_COLOR_BLACK));
    terminal_writestring("  |  Arch: ");
    terminal_set_color(vga_entry_color(INFO_VALUE_COLOR, VGA_COLOR_BLACK));
    terminal_writestring("i686\n");

    terminal_set_color(vga_entry_color(INFO_OK_COLOR, VGA_COLOR_BLACK));
    terminal_writestring("\n  Type 'help' for commands.  Press ? for FAQ.  Press + to split panes.\n\n");

    terminal_set_color(old_color);
}

void show_welcome_tour(void) {
    show_fastfetch();
}