#include "kernel.h"

#define INFO_HEADER_COLOR VGA_COLOR_LIGHT_BROWN
#define INFO_LABEL_COLOR VGA_COLOR_LIGHT_CYAN
#define INFO_VALUE_COLOR VGA_COLOR_WHITE
#define INFO_ACCENT_COLOR VGA_COLOR_LIGHT_MAGENTA
#define INFO_OK_COLOR VGA_COLOR_LIGHT_GREEN
#define INFO_DIM_COLOR VGA_COLOR_DARK_GREY

void show_fastfetch(void) {
    char cpu_model[49];
    get_cpu_model(cpu_model);
    uint8_t old_color = terminal_color;
    char buf[32];
    int i;

    if (lite_mode) {
        terminal_writestring("\n");
        terminal_set_color(vga_entry_color(INFO_LABEL_COLOR, VGA_COLOR_BLACK));
        terminal_writestring("OS: ");
        terminal_set_color(vga_entry_color(INFO_VALUE_COLOR, VGA_COLOR_BLACK));
        terminal_writestring("SharkOS Lite\n");
        
        terminal_set_color(vga_entry_color(INFO_LABEL_COLOR, VGA_COLOR_BLACK));
        terminal_writestring("KERNEL: ");
        terminal_set_color(vga_entry_color(INFO_VALUE_COLOR, VGA_COLOR_BLACK));
        terminal_writestring("Nemo Kernel V0.0.6 powered by SharkOS\n");
        
        terminal_set_color(vga_entry_color(INFO_LABEL_COLOR, VGA_COLOR_BLACK));
        terminal_writestring("CPU: ");
        terminal_set_color(vga_entry_color(INFO_VALUE_COLOR, VGA_COLOR_BLACK));
        terminal_writestring(cpu_model);
        terminal_writestring("\n");
        
        terminal_set_color(vga_entry_color(INFO_LABEL_COLOR, VGA_COLOR_BLACK));
        terminal_writestring("MEMORY: ");
        terminal_set_color(vga_entry_color(INFO_VALUE_COLOR, VGA_COLOR_BLACK));
        int_to_string(total_system_memory / 1024, buf);
        terminal_writestring(buf);
        terminal_writestring(" MB\n");
        
        terminal_set_color(vga_entry_color(INFO_LABEL_COLOR, VGA_COLOR_BLACK));
        terminal_writestring("RES: ");
        terminal_set_color(vga_entry_color(INFO_VALUE_COLOR, VGA_COLOR_BLACK));
        int_to_string(screen_width, buf); terminal_writestring(buf); terminal_writestring("x");
        int_to_string(screen_height, buf); terminal_writestring(buf);
        terminal_writestring("\n");
        
        terminal_set_color(old_color);
        return;
    }

    terminal_set_color(vga_entry_color(INFO_HEADER_COLOR, VGA_COLOR_BLACK));
    terminal_writestring("  shark@SharkOS\n");

    terminal_set_color(vga_entry_color(INFO_DIM_COLOR, VGA_COLOR_BLACK));
    for (i = 0; i < 28; i++) terminal_writestring("-");
    terminal_writestring("\n");

    terminal_set_color(vga_entry_color(INFO_LABEL_COLOR, VGA_COLOR_BLACK));
    terminal_writestring("  OS       ");
    terminal_set_color(vga_entry_color(INFO_VALUE_COLOR, VGA_COLOR_BLACK));
    terminal_writestring("SharkOS V1\n");

    terminal_set_color(vga_entry_color(INFO_LABEL_COLOR, VGA_COLOR_BLACK));
    terminal_writestring("  Host     ");
    terminal_set_color(vga_entry_color(INFO_VALUE_COLOR, VGA_COLOR_BLACK));
    terminal_writestring("The Sharkslayer\n");

    terminal_set_color(vga_entry_color(INFO_LABEL_COLOR, VGA_COLOR_BLACK));
    terminal_writestring("  Kernel   ");
    terminal_set_color(vga_entry_color(INFO_VALUE_COLOR, VGA_COLOR_BLACK));
    terminal_writestring("Nemo Kernel V0.0.6 powered by SharkOS\n");

    terminal_set_color(vga_entry_color(INFO_LABEL_COLOR, VGA_COLOR_BLACK));
    terminal_writestring("  CPU      ");
    terminal_set_color(vga_entry_color(INFO_VALUE_COLOR, VGA_COLOR_BLACK));
    terminal_writestring(cpu_model);
    terminal_writestring("\n");

    terminal_set_color(vga_entry_color(INFO_LABEL_COLOR, VGA_COLOR_BLACK));
    terminal_writestring("  Memory   ");
    terminal_set_color(vga_entry_color(INFO_VALUE_COLOR, VGA_COLOR_BLACK));
    int_to_string(total_system_memory / 1024, buf);
    terminal_writestring(buf);
    terminal_writestring(" MB\n");

    terminal_set_color(vga_entry_color(INFO_LABEL_COLOR, VGA_COLOR_BLACK));
    terminal_writestring("  Display  ");
    terminal_set_color(vga_entry_color(INFO_VALUE_COLOR, VGA_COLOR_BLACK));
    int_to_string(screen_width, buf); terminal_writestring(buf); terminal_writestring("x");
    int_to_string(screen_height, buf); terminal_writestring(buf);
    terminal_writestring("\n");

    terminal_set_color(vga_entry_color(INFO_LABEL_COLOR, VGA_COLOR_BLACK));
    terminal_writestring("  Network  ");
    terminal_set_color(vga_entry_color(rtl_io_base != 0 ? INFO_OK_COLOR : INFO_VALUE_COLOR, VGA_COLOR_BLACK));
    terminal_writestring(rtl_io_base != 0 ? "RTL8139 Online" : "Loopback Mode");
    terminal_set_color(vga_entry_color(INFO_DIM_COLOR, VGA_COLOR_BLACK));
    terminal_writestring("  |  ");
    terminal_set_color(vga_entry_color(INFO_LABEL_COLOR, VGA_COLOR_BLACK));
    terminal_writestring("Arch: ");
    terminal_set_color(vga_entry_color(INFO_VALUE_COLOR, VGA_COLOR_BLACK));
    terminal_writestring("i686\n");

    terminal_set_color(vga_entry_color(INFO_DIM_COLOR, VGA_COLOR_BLACK));
    for (i = 0; i < 28; i++) terminal_writestring("-");
    terminal_writestring("\n");

    terminal_set_color(vga_entry_color(INFO_OK_COLOR, VGA_COLOR_BLACK));
    terminal_writestring("  help=Commands  ?=FAQ  +=Split  -=Close  TAB=Focus  s=Settings\n");

    terminal_set_color(old_color);
}

void show_welcome_tour(void) {
    show_fastfetch();
}