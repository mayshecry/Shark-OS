#include "kernel.h"
#include "server_console.h"
#include "term_font16_data.h"

static volatile uint32_t* server_fb;
static volatile uint16_t* server_vga;
static uint32_t server_width;
static uint32_t server_height;
static uint32_t server_pitch;
static uint32_t server_columns;
static uint32_t server_rows;
static uint32_t server_column;
static uint32_t server_row;
static uint32_t server_bg;
static uint32_t server_fg;
static uint8_t server_mode;
static uint8_t server_escape_state;
static uint8_t server_keyboard_available;
static uint8_t server_shift;
static uint8_t server_caps_lock;
static uint8_t server_control;
static uint8_t server_extended;
static uint8_t server_red_pos;
static uint8_t server_red_size;
static uint8_t server_green_pos;
static uint8_t server_green_size;
static uint8_t server_blue_pos;
static uint8_t server_blue_size;

static const uint8_t server_keymap[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
    [0x0C] = '-', [0x0D] = '=', [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
    [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1A] = '[', [0x1B] = ']', [0x1C] = '\n',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l', [0x27] = ';',
    [0x28] = '\'', [0x29] = '`', [0x2B] = '\\', [0x2C] = 'z', [0x2D] = 'x',
    [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b', [0x31] = 'n', [0x32] = 'm',
    [0x33] = ',', [0x34] = '.', [0x35] = '/', [0x37] = '*', [0x39] = ' '
};

static const uint8_t server_keymap_shifted[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$', [0x06] = '%',
    [0x07] = '^', [0x08] = '&', [0x09] = '*', [0x0A] = '(', [0x0B] = ')',
    [0x0C] = '_', [0x0D] = '+', [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R', [0x14] = 'T',
    [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I', [0x18] = 'O', [0x19] = 'P',
    [0x1A] = '{', [0x1B] = '}', [0x1C] = '\n',
    [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G',
    [0x23] = 'H', [0x24] = 'J', [0x25] = 'K', [0x26] = 'L', [0x27] = ':',
    [0x28] = '"', [0x29] = '~', [0x2B] = '|', [0x2C] = 'Z', [0x2D] = 'X',
    [0x2E] = 'C', [0x2F] = 'V', [0x30] = 'B', [0x31] = 'N', [0x32] = 'M',
    [0x33] = '<', [0x34] = '>', [0x35] = '?', [0x37] = '*', [0x39] = ' '
};

static uint32_t server_component(uint8_t value, uint8_t position, uint8_t size) {
    if (!size || size > 8 || position >= 32 || position + size > 32) return 0;
    return ((uint32_t)(value >> (8 - size))) << position;
}

static uint32_t server_pixel(uint32_t color) {
    uint8_t red = (uint8_t)(color >> 16);
    uint8_t green = (uint8_t)(color >> 8);
    uint8_t blue = (uint8_t)color;
    return server_component(red, server_red_pos, server_red_size) |
           server_component(green, server_green_pos, server_green_size) |
           server_component(blue, server_blue_pos, server_blue_size);
}

static void server_fb_clear(void) {
    if (!server_fb) return;
    uint32_t color = server_pixel(server_bg);
    for (uint32_t y = 0; y < server_height; y++) {
        volatile uint32_t* row = server_fb + y * server_pitch;
        for (uint32_t x = 0; x < server_width; x++) row[x] = color;
    }
}

static void server_vga_clear(void) {
    if (!server_vga) return;
    for (uint32_t i = 0; i < 80 * 25; i++) server_vga[i] = 0x0F20;
}

static void server_console_clear(void) {
    if (server_mode == 1) server_fb_clear();
    if (server_mode == 2) server_vga_clear();
    server_column = 0;
    server_row = 0;
}

static void server_console_scroll(void) {
    if (server_mode == 1) {
        if (server_height <= 16) {
            server_fb_clear();
            server_row = 0;
            return;
        }
        for (uint32_t y = 0; y + 16 < server_height; y++) {
            volatile uint32_t* dest = server_fb + y * server_pitch;
            volatile uint32_t* src = server_fb + (y + 16) * server_pitch;
            for (uint32_t x = 0; x < server_width; x++) dest[x] = src[x];
        }
        uint32_t bg = server_pixel(server_bg);
        for (uint32_t y = server_height - 16; y < server_height; y++) {
            volatile uint32_t* row = server_fb + y * server_pitch;
            for (uint32_t x = 0; x < server_width; x++) row[x] = bg;
        }
    } else if (server_mode == 2) {
        for (uint32_t y = 0; y < 24; y++) {
            for (uint32_t x = 0; x < 80; x++) server_vga[y * 80 + x] = server_vga[(y + 1) * 80 + x];
        }
        for (uint32_t x = 0; x < 80; x++) server_vga[24 * 80 + x] = 0x0F20;
    }
    if (server_row) server_row--;
}

static void server_console_newline(void) {
    server_column = 0;
    server_row++;
    if (server_row >= server_rows) server_console_scroll();
}

static void server_console_draw_char(uint8_t c) {
    if (c < 32 || c > 126) c = '?';
    if (server_mode == 1) {
        uint32_t x0 = server_column * 8;
        uint32_t y0 = server_row * 16;
        const unsigned char* glyph = term_font8x16[c - 32];
        uint32_t fg = server_pixel(server_fg);
        uint32_t bg = server_pixel(server_bg);
        for (uint32_t y = 0; y < 16 && y0 + y < server_height; y++) {
            volatile uint32_t* row = server_fb + (y0 + y) * server_pitch;
            for (uint32_t x = 0; x < 8 && x0 + x < server_width; x++) {
                row[x0 + x] = (glyph[y] & (0x80u >> x)) ? fg : bg;
            }
        }
    } else if (server_mode == 2) {
        server_vga[server_row * 80 + server_column] = (uint16_t)(0x0F00 | c);
    }
    server_column++;
    if (server_column >= server_columns) server_console_newline();
}

static void server_console_handle_escape(uint8_t c) {
    if (server_escape_state == 1) {
        server_escape_state = c == '[' ? 2 : 0;
        return;
    }
    if (server_escape_state == 2) {
        if (c == '2') server_escape_state = 3;
        else {
            if (c == 'H') {
                server_column = 0;
                server_row = 0;
            }
            server_escape_state = 0;
        }
        return;
    }
    if (server_escape_state == 3) {
        if (c == 'J') server_console_clear();
        server_escape_state = 0;
    }
}

void server_console_init(const struct server_multiboot_info* info) {
    server_mode = 0;
    server_fb = 0;
    server_vga = 0;
    server_column = 0;
    server_row = 0;
    server_bg = 0x000000;
    server_fg = 0xDCEBFF;
    server_keyboard_available = 0;
    server_shift = 0;
    server_caps_lock = 0;
    server_control = 0;
    server_extended = 0;
    server_escape_state = 0;
    if (info && (info->flags & (1u << 12)) && info->framebuffer_bpp == 32 && info->framebuffer_type == 1) {
#ifdef SHARKOS_32BIT
        uint64_t address = info->framebuffer_addr_lo;
        uint64_t mapped_limit = 0x100000000ULL;
        if (info->framebuffer_addr_hi) address = mapped_limit;
#else
        uint64_t address = ((uint64_t)info->framebuffer_addr_hi << 32) | info->framebuffer_addr_lo;
        uint64_t mapped_limit = 137438953472ULL;
#endif
        if (address && address < mapped_limit && info->framebuffer_width >= 320 && info->framebuffer_height >= 200 &&
            info->framebuffer_pitch >= info->framebuffer_width * 4u && info->framebuffer_pitch / 4u <= 16384 &&
            (uint64_t)info->framebuffer_pitch * info->framebuffer_height <= mapped_limit - address) {
            server_fb = (volatile uint32_t*)(uintptr_t)address;
            server_width = info->framebuffer_width;
            server_height = info->framebuffer_height;
            server_pitch = info->framebuffer_pitch / 4;
            server_red_pos = info->red_field_position;
            server_red_size = info->red_mask_size;
            server_green_pos = info->green_field_position;
            server_green_size = info->green_mask_size;
            server_blue_pos = info->blue_field_position;
            server_blue_size = info->blue_mask_size;
            if (!server_red_size || !server_green_size || !server_blue_size) {
                server_red_pos = 16;
                server_red_size = 8;
                server_green_pos = 8;
                server_green_size = 8;
                server_blue_pos = 0;
                server_blue_size = 8;
            }
            server_columns = server_width / 8;
            server_rows = server_height / 16;
            server_mode = 1;
        }
    }
    if (!server_mode) {
        server_vga = (volatile uint16_t*)(uintptr_t)0xB8000;
        server_columns = 80;
        server_rows = 25;
        server_mode = 2;
    }
    server_console_clear();
    if (inb(0x64) != 0xFF) {
        for (uint32_t i = 0; i < 100000; i++) {
            if (!(inb(0x64) & 2)) {
                outb(0x64, 0xAE);
                server_keyboard_available = 1;
                break;
            }
        }
    }
}

void server_console_putchar(char c) {
    if (!server_mode) return;
    if (server_escape_state) {
        server_console_handle_escape((uint8_t)c);
        return;
    }
    if ((uint8_t)c == 27) {
        server_escape_state = 1;
        return;
    }
    if (c == '\r') {
        server_column = 0;
        return;
    }
    if (c == '\n') {
        server_console_newline();
        return;
    }
    if (c == '\b') {
        if (server_column) server_column--;
        server_console_draw_char(' ');
        if (server_column) server_column--;
        return;
    }
    if ((uint8_t)c < 32 || (uint8_t)c > 126) return;
    server_console_draw_char((uint8_t)c);
}

int server_console_getchar_nonblocking(void) {
    if (!server_keyboard_available || !(inb(0x64) & 1)) return -1;
    uint8_t scancode = inb(0x60);
    if (scancode == 0xE0) {
        server_extended = 1;
        return -1;
    }
    if (server_extended) {
        server_extended = 0;
        if (scancode == 0x1D) { server_control = 1; return -1; }
        if (scancode == 0x9D) { server_control = 0; return -1; }
        if (scancode & 0x80) return -1;
        if (scancode == 0x48) return 0x80;
        if (scancode == 0x50) return 0x81;
        if (scancode == 0x4D) return 0x82;
        if (scancode == 0x4B) return 0x83;
        if (scancode == 0x53) return 0x84;
        return -1;
    }
    if (scancode == 0x2A || scancode == 0x36) {
        server_shift |= scancode == 0x2A ? 1 : 2;
        return -1;
    }
    if (scancode == 0xAA) {
        server_shift &= (uint8_t)~1;
        return -1;
    }
    if (scancode == 0xB6) {
        server_shift &= (uint8_t)~2;
        return -1;
    }
    if (scancode == 0x3A) {
        server_caps_lock ^= 1;
        return -1;
    }
    if (scancode == 0x1D) { server_control = 1; return -1; }
    if (scancode == 0x9D) { server_control = 0; return -1; }
    if (scancode & 0x80) return -1;
    uint8_t base = server_keymap[scancode];
    uint8_t c = server_shift ? server_keymap_shifted[scancode] : base;
    if (base >= 'a' && base <= 'z') {
        c = (server_caps_lock ^ (server_shift != 0)) ? (uint8_t)(base - 'a' + 'A') : base;
    }
    if (server_control && c >= 'a' && c <= 'z') return c - 'a' + 1;
    if (server_control && c >= 'A' && c <= 'Z') return c - 'A' + 1;
    return c ? c : -1;
}
