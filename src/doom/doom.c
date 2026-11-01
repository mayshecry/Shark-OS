#include "kernel.h"
#include "doom.h"
#include "desktop.h"
#include "doomgeneric.h"
#include "doomkeys.h"

static uint8_t doom_active;
static uint8_t doom_initialized;
static uint8_t doom_engine_error;
static uint8_t doom_saved_mode;
static int doom_window_x;
static int doom_window_y;
static int doom_window_w;
static int doom_window_h;
static uint16_t doom_key_queue[128];
static volatile uint32_t doom_key_write;
static volatile uint32_t doom_key_read;
static uint8_t doom_extended_key;
static uint64_t doom_exit_environment[8];
static volatile uint8_t doom_exit_armed;

extern int doom_setjmp(uint64_t* environment);
extern void doom_longjmp(uint64_t* environment, int value);

static const char* doom_argv[] = {"doom", "-iwad", "freedoom1.wad", "-nosound", "-nogui"};

static uint8_t doom_map_scancode(uint8_t code, uint8_t extended) {
    if (extended) {
        if (code == 0x48) return KEY_UPARROW;
        if (code == 0x50) return KEY_DOWNARROW;
        if (code == 0x4B) return KEY_LEFTARROW;
        if (code == 0x4D) return KEY_RIGHTARROW;
        if (code == 0x1D) return KEY_RCTRL;
        if (code == 0x38) return KEY_RALT;
        if (code == 0x53) return KEY_BACKSPACE;
        return 0;
    }
    if (code >= 0x02 && code <= 0x0B) return code == 0x0B ? '0' : (uint8_t)('1' + code - 0x02);
    if (code == 0x01) return KEY_ESCAPE;
    if (code == 0x0C) return KEY_MINUS;
    if (code == 0x0D) return KEY_EQUALS;
    if (code == 0x0E) return KEY_BACKSPACE;
    if (code == 0x0F) return KEY_TAB;
    if (code == 0x1C) return KEY_ENTER;
    if (code == 0x1D) return KEY_RCTRL;
    if (code == 0x2A || code == 0x36) return KEY_RSHIFT;
    if (code == 0x38) return KEY_RALT;
    if (code == 0x39) return KEY_USE;
    if (code == 0x3B) return KEY_F1;
    if (code == 0x3C) return KEY_F2;
    if (code == 0x3D) return KEY_F3;
    if (code == 0x3E) return KEY_F4;
    if (code == 0x3F) return KEY_F5;
    if (code == 0x40) return KEY_F6;
    if (code == 0x41) return KEY_F7;
    if (code == 0x42) return KEY_F8;
    if (code == 0x43) return KEY_F9;
    if (code == 0x44) return KEY_F10;
    if (code == 0x57) return KEY_F11;
    if (code == 0x58) return KEY_F12;
    if (code == 0x10) return 'q';
    if (code == 0x11) return 'w';
    if (code == 0x12) return 'e';
    if (code == 0x13) return 'r';
    if (code == 0x14) return 't';
    if (code == 0x15) return 'y';
    if (code == 0x16) return 'u';
    if (code == 0x17) return 'i';
    if (code == 0x18) return 'o';
    if (code == 0x19) return 'p';
    if (code == 0x1E) return 'a';
    if (code == 0x1F) return 's';
    if (code == 0x20) return 'd';
    if (code == 0x21) return 'f';
    if (code == 0x22) return 'g';
    if (code == 0x23) return 'h';
    if (code == 0x24) return 'j';
    if (code == 0x25) return 'k';
    if (code == 0x26) return 'l';
    if (code == 0x2C) return 'z';
    if (code == 0x2D) return 'x';
    if (code == 0x2E) return 'c';
    if (code == 0x2F) return 'v';
    if (code == 0x30) return 'b';
    if (code == 0x31) return 'n';
    if (code == 0x32) return 'm';
    return 0;
}

static void doom_queue_event(int pressed, uint8_t key) {
    if (!key) return;
    uint32_t next = (doom_key_write + 1) & 127;
    if (next == doom_key_read) return;
    doom_key_queue[doom_key_write] = (uint16_t)(((pressed ? 1 : 0) << 8) | key);
    doom_key_write = next;
}

void doom_keyboard_scancode(uint8_t scancode, uint8_t extended) {
    if (!doom_active || current_kernel_mode == KERNEL_MODE_DESKTOP) {
        if (!doom_active) return;
        window_t* focused = window_get_focused();
        if (!focused || focused->type != WINDOW_TYPE_DOOM) return;
    }
    if (scancode == 0xE0) {
        doom_extended_key = 1;
        return;
    }
    uint8_t use_extended = extended || doom_extended_key;
    doom_extended_key = 0;
    uint8_t key = doom_map_scancode(scancode & 0x7F, use_extended);
    doom_queue_event((scancode & 0x80) == 0, key);
}

int DG_GetKey(int* pressed, unsigned char* key) {
    if (!pressed || !key || doom_key_read == doom_key_write) return 0;
    uint16_t item = doom_key_queue[doom_key_read];
    doom_key_read = (doom_key_read + 1) & 127;
    *pressed = (item >> 8) & 1;
    *key = (unsigned char)item;
    return 1;
}

void DG_Init(void) {
    doom_window_x = 0;
    doom_window_y = 0;
    doom_window_w = (int)screen_width;
    doom_window_h = (int)screen_height;
}

void DG_SleepMs(uint32_t ms) {
    delay_ms(ms);
}

uint32_t DG_GetTicksMs(void) {
    return uptime_ticks;
}

void DG_SetWindowTitle(const char* title) {
    (void)title;
}

void DG_DrawFrame(void) {
}

void doom_engine_set_error(void) {
    doom_engine_error = 1;
    doom_active = 0;
}

void doom_engine_exit(void) {
    doom_engine_set_error();
    if (doom_exit_armed) doom_longjmp(doom_exit_environment, 1);
}

void doom_init(void) {
    if (doom_initialized) {
        doom_active = 1;
        doom_engine_error = 0;
        return;
    }
    doom_active = 1;
    doom_engine_error = 0;
    doom_key_write = 0;
    doom_key_read = 0;
    doom_window_x = 0;
    doom_window_y = 0;
    doom_window_w = (int)screen_width;
    doom_window_h = (int)screen_height;
    doom_exit_armed = 1;
    int exited = doom_setjmp(doom_exit_environment);
    if (!exited) doomgeneric_Create(5, (char**)doom_argv);
    doom_exit_armed = 0;
    if (!doom_engine_error) doom_initialized = 1;
}

void doom_cleanup(void) {
    doom_active = 0;
    if (current_kernel_mode == KERNEL_MODE_DESKTOP) desktop.dirty = true;
}

void doom_draw_frame(void) {
    if (!DG_ScreenBuffer || !lfbptr) return;
    uint32_t scale_x = doom_window_w > 0 ? (uint32_t)doom_window_w / DOOMGENERIC_RESX : 1;
    uint32_t scale_y = doom_window_h > 0 ? (uint32_t)doom_window_h / DOOMGENERIC_RESY : 1;
    uint32_t scale = scale_x < scale_y ? scale_x : scale_y;
    if (!scale) scale = 1;
    uint32_t width = DOOMGENERIC_RESX * scale;
    uint32_t height = DOOMGENERIC_RESY * scale;
    int x0 = doom_window_x + (doom_window_w - (int)width) / 2;
    int y0 = doom_window_y + (doom_window_h - (int)height) / 2;
    uint32_t stride = (uint32_t)(screen_pitch / 4);
    for (uint32_t y = 0; y < height; y++) {
        int py = y0 + (int)y;
        if (py < 0 || (uint32_t)py >= screen_height) continue;
        uint32_t sy = y / scale;
        uint32_t* dst = &lfbptr[(uint32_t)py * stride];
        const uint32_t* src = DG_ScreenBuffer + sy * DOOMGENERIC_RESX;
        for (uint32_t x = 0; x < width; x++) {
            int px = x0 + (int)x;
            if (px < 0 || (uint32_t)px >= screen_width) continue;
            dst[px] = 0xFF000000u | (src[x / scale] & 0x00FFFFFFu);
        }
    }
}

void doom_set_window_rect(int x, int y, int w, int h) {
    doom_window_x = x;
    doom_window_y = y;
    doom_window_w = w;
    doom_window_h = h;
}

void doom_draw_fullscreen(void) {
    doom_window_x = 0;
    doom_window_y = 0;
    doom_window_w = (int)screen_width;
    doom_window_h = (int)screen_height;
    doom_draw_frame();
}

void doom_run(void) {
    doom_init();
    doom_set_kernel_mode();
    while (doom_active && !doom_engine_error) {
        doom_tick();
        if (doom_active) doom_draw_fullscreen();
        yield();
    }
    doom_restore_kernel_mode();
}

void doom_tick(void) {
    if (!doom_active) return;
    doom_exit_armed = 1;
    int exited = doom_setjmp(doom_exit_environment);
    if (!exited) doomgeneric_Tick();
    doom_exit_armed = 0;
    if (exited) doom_engine_set_error();
    if (current_kernel_mode == KERNEL_MODE_DESKTOP) desktop.dirty = true;
}

void doom_handle_key(int key) {
    (void)key;
}

void doom_set_kernel_mode(void) {
    doom_saved_mode = (uint8_t)current_kernel_mode;
    doom_window_x = 0;
    doom_window_y = 0;
    doom_window_w = (int)screen_width;
    doom_window_h = (int)screen_height;
}

void doom_restore_kernel_mode(void) {
    if (current_kernel_mode == KERNEL_MODE_DESKTOP || doom_saved_mode == KERNEL_MODE_DESKTOP) {
        desktop.dirty = true;
        return;
    }
    terminal_initialize();
    redraw_all_panes();
    print_prompt();
}

bool doom_is_active(void) {
    return doom_active != 0;
}
