#include "kernel.h"
#include "desktop.h"
#include "plugin_manager.h"
#include "doom.h"
#include "flappybird.h"
#include "smb.h"
#include "pong.h"
#include "geometrydash.h"
#include "net.h"

extern void ui_draw_chrome(void);
extern void ui_draw_footer(void);
extern void redraw_all_panes(void);
extern void mouse_update_cursor(void);
extern void mouse_draw_cursor(void);
extern void mouse_restore_under_cursor(void);

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

    asm volatile("sti; hlt");
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
    serial_init(); /* safe with no UART; enables pre-video diagnosis */
    if (magic != 0x2BADB002) {
        early_panic(0, "bad multiboot magic (not booted via GRUB multiboot).");
    }
    asm volatile("cli");
    serial_puts("SharkOS early boot: multiboot ok\n");

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
            char* legacy_pos = cmdline;
            while (*legacy_pos) {
                if (*legacy_pos == 'l' && *(legacy_pos+1) == 'e' && *(legacy_pos+2) == 'g' && *(legacy_pos+3) == 'a' && *(legacy_pos+4) == 'c' && *(legacy_pos+5) == 'y') {
                    legacy_mode = true;
                    break;
                }
                legacy_pos++;
            }
        }
    }

    /* Validate the framebuffer BEFORE the first screen write. The old code
       trusted these fields blindly: with no FB info (or a FB mapped above
       4 GB, standard for discrete GPUs with "Above 4G Decoding"), the boot
       drawing scribbled over random RAM with no IDT installed -> triple
       fault -> instant reboot with zero information (the Ryzen symptom). */
    if (!(mb_info->flags & (1u << 12))) {
        early_panic(1, "bootloader passed no framebuffer info (multiboot flags bit 12 clear).");
    }
    if (mb_info->framebuffer_addr_hi != 0) {
        serial_puts("FB phys: hi=");
        serial_puthex32(mb_info->framebuffer_addr_hi);
        serial_puts(" lo=");
        serial_puthex32(mb_info->framebuffer_addr_lo);
        serial_puts("\n");
        early_panic(2, "framebuffer is mapped ABOVE 4GB (discrete GPU + 'Above 4G Decoding' / Resizable BAR). "
                       "This 32-bit build cannot reach it. Workaround: disable 'Above 4G Decoding' and "
                       "'Resizable BAR'/'SAM' in BIOS setup, then boot again.");
    }
    if (mb_info->framebuffer_type != 1 || mb_info->framebuffer_bpp != 32) {
        early_panic(3, "framebuffer is not 32-bit RGB (need type=1, bpp=32).");
    }
    {
        uint32_t fb_w = mb_info->framebuffer_width;
        uint32_t fb_h = mb_info->framebuffer_height;
        uint32_t fb_p = mb_info->framebuffer_pitch;
        if (fb_w < 320 || fb_w > 4096 || fb_h < 200 || fb_h > 4096 ||
            fb_p < fb_w * 4u || fb_p > 65536u || (fb_p & 3)) {
            early_panic(4, "insane framebuffer geometry from bootloader.");
        }
    }

    lfbptr = (uint32_t*)(uintptr_t)mb_info->framebuffer_addr_lo;
    fb_addr_hi = mb_info->framebuffer_addr_hi; /* 0 here; kept for Task Manager */
    screen_width = mb_info->framebuffer_width;
    screen_height = mb_info->framebuffer_height;
    screen_pitch = mb_info->framebuffer_pitch;

    serial_puts("FB ok: ");
    serial_puthex32((uint32_t)(uintptr_t)lfbptr);
    serial_puts(" ");
    serial_putdec((uint32_t)screen_width);
    serial_puts("x");
    serial_putdec((uint32_t)screen_height);
    serial_puts(" pitch=");
    serial_putdec((uint32_t)screen_pitch);
    serial_puts("\n");

    if (!(mb_info->flags & ((1u << 6) | (1u << 0)))) {
        early_panic(5, "bootloader passed no memory info (need mmap or mem_lower/mem_upper).");
    }
    uint64_t mem_kb = 0;
    if (mb_info->flags & (1u << 0)) {
        mem_kb = (uint64_t)mb_info->mem_upper + (uint64_t)mb_info->mem_lower;
    }
    total_system_memory = mem_kb * 1024;

    if (mb_info->flags & (1 << 6)) {
        struct multiboot_mmap_entry* mmap = (struct multiboot_mmap_entry*)(uintptr_t)mb_info->mmap_addr;
        uint32_t mmap_len = mb_info->mmap_length;
        uint64_t highest_addr = 0;
        uint32_t entries = mmap_len / sizeof(struct multiboot_mmap_entry);
        for (uint32_t i = 0; i < entries; i++) {
            if (mmap[i].type == 1) {
                uint64_t region_start = ((uint64_t)mmap[i].addr_high << 32) | (uint64_t)mmap[i].addr_low;
                uint64_t region_len = ((uint64_t)mmap[i].len_high << 32) | (uint64_t)mmap[i].len_low;
                uint64_t region_end = region_start + region_len;
                if (region_start < 4294967296ULL && region_end > highest_addr) {
                    highest_addr = region_end;
                }
            }
        }
        if (highest_addr > 0) {
            total_system_memory = highest_addr;
        }
    }

    if (total_system_memory == 0) {
        early_panic(5, "bootloader reported 0 bytes of memory.");
    }
    if (total_system_memory > 2147483648) {
        total_system_memory = 2147483648;
    }
    serial_puts("RAM ok: ");
    serial_putdec((uint32_t)(total_system_memory >> 20));
    serial_puts(" MB\n");

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

    if (lite_mode || legacy_mode) {
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

    boot_screen_show();
    boot_screen_update("Loading kernel...", 5);

    terminal_initialize();
    boot_screen_update("Initializing descriptors...", 15);
    init_descriptor_tables();

    outb(0x21, 0x11);
    outb(0xA1, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    outb(0x21, 0x00);
    outb(0xA1, 0x00);
    outb(0x21, 0xFE);
    outb(0xA1, 0xFF);

    uint16_t divisor = 1193182 / 1000;
    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
    asm volatile("sti");
    serial_puts("IDT+PIC+PIT ok\n");
    boot_screen_update("Setting up filesystem...", 25);
    terminal_clear();
    strcpy(current_user, "sharkuser");
    tiling_enabled = 1;
    mouse_enabled = 1;
    selected_theme = 0;
    apply_theme(selected_theme);
    fs_initialize();
    boot_screen_update("Initializing input devices...", 40);

    outb(0x21, inb(0x21) & ~0x04);
    outb(0xA1, inb(0xA1) & ~0x10);
    outb(0x21, inb(0x21) & ~0x02);
    mouse_init();
    boot_screen_update("Loading plugins...", 55);

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
    boot_screen_update("Detecting hardware...", 70);

    net_init();
    boot_screen_update(net_has_nic() ? "Configuring network (DHCP)..."
                                     : "No network adapter found", 75);
    net_stack_init();
    struct fs_node* user_dir = find_node(root, "User");
    if (user_dir) strcpy(user_dir->name, current_user);
    while (keyboard_getchar() != 0);
    terminal_clear();
    boot_screen_update("Starting desktop environment...", 85);

    current_kernel_mode = KERNEL_MODE_DESKTOP;
    rtc_init();
    desktop_init();
    boot_screen_update("Ready!", 100);

    for (volatile int i = 0; i < 1000000; i++);
    boot_screen_hide();
    serial_puts("entering desktop\n");

    asm volatile("sti");

    hw_lfbptr = lfbptr;
    {

        framebuffer_enable_write_combining((uintptr_t)hw_lfbptr,
                                           (uint32_t)(screen_pitch * screen_height));
        size_t fb_bytes = (size_t)(screen_pitch * screen_height);
        uint32_t* shadow = (uint32_t*)kmalloc(fb_bytes);
        if (shadow) {
            memcpy(shadow, lfbptr, fb_bytes);
            lfbptr = shadow;
        }
    }

    desktop_render();

    uint32_t last_clock_tick = uptime_ticks;
    uint32_t last_blink_tick = uptime_ticks;
    uint32_t last_game_tick = uptime_ticks;
    uint32_t last_net_tick = uptime_ticks;
    int last_net_state = -1;
    uint32_t busy_ms = 0;
    uint32_t frames_this_sec = 0;
    int last_rtc_minute = (int)rtc_minutes;

    while (1) {

        yield();
        uint32_t work_start = uptime_ticks;

        bool pointer_moved = false;
        int old_mx = desktop_mouse_x;
        int old_my = desktop_mouse_y;

        if (mouse_enabled) {
            int mx = mouse_cursor_x;
            int my = mouse_cursor_y;
            int buttons = mouse_state.buttons;

            if (buttons != 0 || (buttons == 0 && desktop_mouse_down)) {
                desktop_handle_mouse(mx, my, buttons);
            }
            if (mx != desktop_mouse_x || my != desktop_mouse_y) {
                desktop_mouse_x = mx;
                desktop_mouse_y = my;
                pointer_moved = true;

                bool repaint = false;
                if (desktop.start_menu.visible) {
                    start_menu_handle_hover(mx, my);
                    int sx, sy, sw2, sh2;
                    start_menu_get_rect(&sx, &sy, &sw2, &sh2);
                    desktop_invalidate_rect(sx, sy, sx + sw2, sy + sh2);
                    repaint = true;
                } else if (desktop_mouse_down) {
                    if (desktop_drag_window < 0 && desktop_resize_window < 0) {
                        desktop.dirty = true;
                    } else {

                        repaint = true;
                    }
                } else {

                    for (int i = 0; i < desktop.window_count; i++) {
                        window_t* hw = &desktop.windows[i];
                        if (!hw->visible || hw->state == WINDOW_STATE_MINIMIZED)
                            continue;
                        if (hw->type != WINDOW_TYPE_SETTINGS &&
                            hw->type != WINDOW_TYPE_FILEMANAGER)
                            continue;
                        if (mx >= hw->rect.x && mx < hw->rect.x + hw->rect.width &&
                            my >= hw->rect.y && my < hw->rect.y + hw->rect.height) {
                            desktop_invalidate_rect(hw->rect.x, hw->rect.y,
                                                    hw->rect.x + hw->rect.width,
                                                    hw->rect.y + hw->rect.height);
                            repaint = true;
                        }
                    }
                }

                if (repaint) {

                    desktop_invalidate_rect(0, old_my - 16,
                                            (int)screen_width, old_my + 32);
                    desktop_invalidate_rect(0, my - 16,
                                            (int)screen_width, my + 32);
                }
            }
        }

        for (int k = 0; k < 16; k++) {
            char c = keyboard_getchar();
            if (c == 0) break;
            desktop_handle_keyboard(c);
        }

        if (uptime_ticks - last_net_tick >= 10) {
            last_net_tick = uptime_ticks;
            net_poll();

            int ns = net_dhcp_state() * 2 + net_has_link;
            if (ns != last_net_state) {
                last_net_state = ns;
                for (int i = 0; i < desktop.window_count; i++) {
                    if (desktop.windows[i].type == WINDOW_TYPE_NETWORK) {
                        desktop.windows[i].needs_redraw = true;
                        desktop_invalidate_rect(desktop.windows[i].rect.x,
                                                desktop.windows[i].rect.y,
                                                desktop.windows[i].rect.x + desktop.windows[i].rect.width,
                                                desktop.windows[i].rect.y + desktop.windows[i].rect.height);
                    }
                }
                /* Tray icon mirrors link/DHCP state: repaint taskbar rows only. */
                desktop_invalidate_rect(0, (int)screen_height - TASKBAR_HEIGHT,
                                        (int)screen_width, (int)screen_height);
            }
        }

        if (uptime_ticks - last_clock_tick >= 1000) {
            last_clock_tick = uptime_ticks;
            rtc_read_time();
            if ((int)rtc_minutes != last_rtc_minute) {
                last_rtc_minute = (int)rtc_minutes;
                /* Only the tray clock changed: flush the taskbar rows, not
                   the whole multi-megabyte framebuffer. */
                desktop_invalidate_rect(0, (int)screen_height - TASKBAR_HEIGHT,
                                        (int)screen_width, (int)screen_height);
            }

            sys_cpu_percent = busy_ms > 1000 ? 100 : busy_ms / 10;
            sys_cpu_history[sys_cpu_history_pos] = sys_cpu_percent;
            sys_cpu_history_pos = (sys_cpu_history_pos + 1) % SYS_CPU_HISTORY;
            sys_frames_per_sec = frames_this_sec;
            busy_ms = 0;
            frames_this_sec = 0;

            for (int i = 0; i < desktop.window_count; i++) {
                window_t* tw = &desktop.windows[i];
                if (tw->type == WINDOW_TYPE_TASKMANAGER && tw->visible &&
                    tw->state != WINDOW_STATE_MINIMIZED) {
                    tw->needs_redraw = true;
                    desktop_invalidate_rect(tw->rect.x, tw->rect.y,
                                            tw->rect.x + tw->rect.width,
                                            tw->rect.y + tw->rect.height);
                }
            }
        }

        window_t* focused = window_get_focused();
        if (focused && !focused->game_tick &&
            (focused->type == WINDOW_TYPE_TERMINAL || focused->type == WINDOW_TYPE_NOTEPAD) &&
            uptime_ticks - last_blink_tick >= 500) {
            last_blink_tick = uptime_ticks;
            focused->needs_redraw = true;
            desktop_invalidate_rect(focused->rect.x, focused->rect.y,
                                    focused->rect.x + focused->rect.width,
                                    focused->rect.y + focused->rect.height);
        }

        if (focused && focused->game_tick && uptime_ticks - last_game_tick >= 16) {
            last_game_tick = uptime_ticks;
            focused->game_tick();/
            desktop_invalidate_rect(focused->rect.x, focused->rect.y,
                                    focused->rect.x + focused->rect.width,
                                    focused->rect.y + focused->rect.height);
        }

        browser_tick();

        if (desktop.dirty) {
            desktop_render();
            frames_this_sec++;
            sys_frames_rendered++;
        } else if (pointer_moved) {
            desktop_render_cursor_only(old_mx, old_my);
        }

        busy_ms += uptime_ticks - work_start;
    }
}
