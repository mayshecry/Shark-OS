#include "kernel.h"
#include "desktop.h"
#include "plugin_manager.h"
#include "doom.h"
#include "flappybird.h"
#include "smb.h"
#include "pong.h"
#include "geometrydash.h"
#include "net.h"
#include "firewall.h"
#include "router.h"
#include "ata.h"
#include "install.h"
#include "store.h"
#include "audio.h"

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


static char boot_url[256];

static void boot_trace(const char* stage) {
    serial_puts("[boot] ");
    serial_puts(stage);
    serial_puts(" ticks=");
    serial_putdec(uptime_ticks);
    serial_puts(" mem_mb=");
    serial_putdec((uint32_t)(total_system_memory >> 20));
    serial_puts("\n");
}

void kmain(uint32_t magic, struct multiboot_info* mb_info) {
    if (magic != 0x2BADB002) return;
    asm volatile("cli");
    serial_init();
    boot_trace("kmain entered");

    if (mb_info->flags & (1 << 2)) {
        char* cmdline = (char*)(uintptr_t)mb_info->cmdline;
        store_boot_option(cmdline);
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
                                                                               
                                                                             
                                                                             
            for (char* up = cmdline; *up; up++) {
                if ((up == cmdline || up[-1] == ' ') && up[0] == 'u' && up[1] == 'r' && up[2] == 'l' && up[3] == '=') {
                    int o = 0;
                    for (char* v = up + 4; *v && *v != ' ' && o < (int)sizeof(boot_url) - 1; v++) boot_url[o++] = *v;
                    boot_url[o] = 0;
                    break;
                }
            }
            char* kawaii_pos = cmdline;
            while (*kawaii_pos) {
                if (*kawaii_pos == 'k' && *(kawaii_pos+1) == 'a' && *(kawaii_pos+2) == 'w' && *(kawaii_pos+3) == 'a' && *(kawaii_pos+4) == 'i' && *(kawaii_pos+5) == 'i') {
                    kawaii_mode = true;
                    break;
                }
                kawaii_pos++;
            }
        }
    }

    lfbptr = (uint32_t*)(uintptr_t)(((uint64_t)mb_info->framebuffer_addr_hi << 32) | (uint64_t)mb_info->framebuffer_addr_lo);
    if (!lfbptr) lfbptr = (uint32_t*)(uintptr_t)mb_info->framebuffer_addr_lo;
    screen_width = mb_info->framebuffer_width;
    screen_height = mb_info->framebuffer_height;
    screen_pitch = mb_info->framebuffer_pitch;
    if (screen_width < 320) screen_width = 320;
    if (screen_height < 200) screen_height = 200;

    extern char _kernel_start[],_kernel_ro_end[],_kernel_end[];
    const uint64_t limit = 137438953472ULL;
    uint64_t floor = (uintptr_t)_kernel_end + 0x2000u, lo = limit, hi = 0;
    if ((mb_info->flags & 8) && mb_info->mods_count) {
        struct mod {
            uint32_t start, end, cmd, reserved;
        } *m = (void *)(uintptr_t)mb_info->mods_addr;
        for (uint32_t i = 0; i < mb_info->mods_count; i++) {
            if (m[i].end <= m[i].start)
                continue;
            if (m[i].start < lo)
                lo = m[i].start;
            if (m[i].end > hi)
                hi = m[i].end;
            if (!install_image_ready())
                install_ref_set((const void *)(uintptr_t)m[i].start, m[i].end - m[i].start,_kernel_start,(uintptr_t)_kernel_ro_end-(uintptr_t)_kernel_start);
        }
    }
    pmm_memory_region_t regions[128];
    uint32_t nr = 0, pos = 0, bytes = (mb_info->flags & 64) ? mb_info->mmap_length : 0;
    uint64_t highest = 0;
    do {
        uint64_t begin = 0, end = ((uint64_t)mb_info->mem_upper + 1024) * 1024;
        if (bytes) {
            if (bytes - pos < sizeof(struct multiboot_mmap_entry))
                break;
            const struct multiboot_mmap_entry *r = (const void *)((uintptr_t)mb_info->mmap_addr + pos);
            if (r->size < sizeof(*r) - 4 || r->size > bytes - pos - 4)
                break;
            pos += r->size + 4;
            if (r->type != 1)
                continue;
            begin = ((uint64_t)r->addr_high << 32) | r->addr_low;
            uint64_t len = ((uint64_t)r->len_high << 32) | r->len_low;
            if (begin >= limit)
                continue;
            end = len > limit - begin ? limit : begin + len;
        }
        if (end > limit)
            end = limit;
        if (end > highest)
            highest = end;
        /* Only explicit type-1 RAM. Keep low firmware memory, the loaded ELF,
           boot modules and their bounding interval out of this extra range. */
        extern char _kernel_start[];
        if(bytes && end>0x200000 && begin<(uintptr_t)_kernel_start){
            uint64_t a=begin<0x200000?0x200000:begin,b=end<(uintptr_t)_kernel_start?end:(uintptr_t)_kernel_start;
            uint64_t t=b<lo?b:lo,u=a>hi?a:hi;
            if(t>a && nr<128)regions[nr++]=(pmm_memory_region_t){a,t};
            if(hi && b>u && nr<128)regions[nr++]=(pmm_memory_region_t){u,b};
            pmm_allow_low_desktop((uintptr_t)_kernel_start);
        }
        if (begin < floor)
            begin = floor;
        if (end <= begin)
            continue;
        uint64_t before = end < lo ? end : lo, after = hi > begin ? hi : begin;
        if (before > begin && nr < 128)
            regions[nr++] = (pmm_memory_region_t){begin, before};
        if (hi && end > after && nr < 128)
            regions[nr++] = (pmm_memory_region_t){after, end};
    } while (pos < bytes);
    if (!nr) {
        serial_puts("No usable heap. Try the lower-RAM boot entry.\n");
        return;
    }
    total_system_memory = highest;
    boot_trace("memory map parsed");
    pmm_init_regions(regions, nr);
    boot_trace("physical allocator ready");
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

                                                                      
                                            
    if (kawaii_mode) theme_set(THEME_ID_KAWAII);

                                                                    
                                                                 
    boot_trace("boot screen begin");
    boot_screen_show();
    boot_trace("boot screen ready");

    boot_screen_info("nemo (SharkOS V2) — SHKRNL, gcc, ring zero");
    {
        char cpu_model[49];
        get_cpu_model(cpu_model);
        boot_screen_info(cpu_model);
    }
    {
        char line[64];
        int p = 0;
        const char* pre = "Memory: ";
        for (int k = 0; pre[k]; k++) line[p++] = pre[k];
        char num[12];
        int_to_string((uint32_t)(total_system_memory >> 20), num);
        for (int k = 0; num[k]; k++) line[p++] = num[k];
        const char* suf = " MB available";
        for (int k = 0; suf[k]; k++) line[p++] = suf[k];
        line[p] = '\0';
        boot_screen_info(line);
    }
    {
        char line[48];
        int p = 0;
        const char* pre = "Framebuffer: ";
        for (int k = 0; pre[k]; k++) line[p++] = pre[k];
        char num[12];
        int_to_string(screen_width, num);
        for (int k = 0; num[k]; k++) line[p++] = num[k];
        line[p++] = 'x';
        int_to_string(screen_height, num);
        for (int k = 0; num[k]; k++) line[p++] = num[k];
        const char* suf = "x32";
        for (int k = 0; suf[k]; k++) line[p++] = suf[k];
        line[p] = '\0';
        boot_screen_info(line);
    }
    boot_screen_info("SHKRNL boot complete.");

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
    boot_trace("PIT and interrupts enabled");
    boot_screen_update("Setting up filesystem...", 25);
    terminal_clear();
    strcpy(current_user, "sharkuser");
    tiling_enabled = 1;
    mouse_enabled = 1;
    selected_theme = 0;
    apply_theme(selected_theme);
    boot_trace("filesystem initialization begin");
    ata_init();
    if (!store_restore()) {
        fs_initialize();
        static const char sample[] = "#include <stdint.h>\nuint32_t square(uint32_t n) { return n*n; }\n";
        struct fs_node *example = create_node("square.c", FS_FILE, current_dir);
        if (example)
            fs_node_write(example, 0, sample, sizeof sample - 1);
        static const char hello[] =
            "#include <sharkapi.h>\nint main(void) { sharkapi_println(\"Hello, world!\"); return 0; }\n";
        struct fs_node *demo = create_node("hello.c", FS_FILE, current_dir);
        if (demo)
            fs_node_write(demo, 0, hello, sizeof hello - 1);
        if (current_dir && store_installed())
            strcpy(current_dir->name, current_user);
    }
    ata_report_files();
    serial_puts(store_status());
    serial_puts("\n");
    boot_trace("filesystem initialization complete");
    boot_screen_update("Initializing input devices...", 40);

    outb(0x21, inb(0x21) & ~0x04);
    outb(0xA1, inb(0xA1) & ~0x10);
    outb(0x21, inb(0x21) & ~0x02);
    boot_trace("input initialization begin");
    keyboard_init();
    mouse_init();
    boot_trace("input initialization complete");
    boot_screen_update("Loading plugins...", 55);

    boot_trace("plugin initialization begin");
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
    extern int sharkcube_plugin_init(void);
    extern void sharkcube_plugin_cleanup(void);
    extern int sharkcube_plugin_command(int argc, char** argv);
    plugin_register_builtin("sharkcube", sharkcube_plugin_init, sharkcube_plugin_cleanup, sharkcube_plugin_command);
    extern int sharkcraft_plugin_init(void);
    extern void sharkcraft_plugin_cleanup(void);
    extern int sharkcraft_plugin_command(int argc, char** argv);
    plugin_register_builtin("sharkcraft", sharkcraft_plugin_init, sharkcraft_plugin_cleanup, sharkcraft_plugin_command);
    extern int fnaf_plugin_init(void);
    extern void fnaf_plugin_cleanup(void);
    extern int fnaf_plugin_command(int argc, char** argv);
    int fnaf_id = plugin_register_builtin("fnaf", fnaf_plugin_init, fnaf_plugin_cleanup, fnaf_plugin_command);
    if(fnaf_id>=0) plugin_unload((uint32_t)fnaf_id);
    plugin_register_builtin("ide", NULL, NULL, NULL);
    boot_trace("plugin initialization complete");
    boot_screen_update("Detecting hardware...", 70);

    boot_screen_update("Probing disks...", 72);
    boot_trace("ATA probe complete");

    boot_screen_update("Initializing audio...", 73);
    boot_trace("audio initialization begin");
    audio_init();
    boot_trace("audio initialization complete");

    boot_trace("network initialization begin");
    net_init();
    boot_trace("network initialization complete");
    boot_screen_update(net_has_nic() ? "Configuring network (DHCP)..."
                                     : "No network adapter found", 75);
    boot_trace("network stack initialization begin");
    net_stack_init();
    boot_trace("network stack initialization complete");
    firewall_init();
    router_init();
    struct fs_node* user_dir = find_node(root, "User");
    if (!store_installed() && user_dir)
        strcpy(user_dir->name, current_user);
    while (keyboard_getchar() != 0);
    terminal_clear();
    boot_screen_update("Starting desktop environment...", 85);

    if (!store_installed())
        boot_setup_run();
    else
        serial_puts("[installed] desktop boot\n");

    current_kernel_mode = KERNEL_MODE_DESKTOP;
    boot_trace("desktop initialization begin");
    rtc_init();
    desktop_init();
    if (boot_theme_id != theme_get_id()) theme_set(boot_theme_id);
    if (boot_theme_id == THEME_ID_KAWAII) kawaii_mode = true;
    apply_theme(selected_theme);
    if (kawaii_mode) desktop_set_wallpaper(WP_ID_KAWAII);
    desktop_prefs_restore();
    wm_restore_pref();
    boot_trace("desktop initialization complete");
    boot_screen_update("Ready!", 100);
    boot_trace("boot screen hide begin");
    boot_screen_hide();
    boot_trace("boot screen hide complete");

    asm volatile("sti");

    hw_lfbptr = lfbptr;
    {

        framebuffer_enable_write_combining((uintptr_t)hw_lfbptr,
                                           (uint32_t)(screen_pitch * screen_height));
        size_t fb_bytes = (size_t)(screen_pitch * screen_height);
        uint32_t* shadow = (uint32_t*)pmm_shadow(fb_bytes);
        if (shadow) {
            memcpy(shadow, lfbptr, fb_bytes);
            lfbptr = shadow;
        }
    }

    video_init();
    if (boot_to_cli) {
        current_kernel_mode = KERNEL_MODE_CLI;
        redraw_all_panes();
        print_prompt();
    } else {
        desktop_render();
    }

    serial_puts("[desktop] free KiB=");
    serial_putdec((uint32_t)(pmm_free_memory_bytes() >> 10));
    serial_puts("\n");
    uint32_t last_clock_tick = uptime_ticks;
    uint32_t last_blink_tick = uptime_ticks;
    uint32_t last_game_tick = uptime_ticks;
    uint32_t last_net_tick = uptime_ticks;
    uint32_t last_audio_tick = uptime_ticks;
    int last_mouse_buttons = 0;
    int last_net_state = -1;
    uint32_t busy_ms = 0;
    uint32_t frames_this_sec = 0;
    int last_rtc_minute = (int)rtc_minutes;

    uint32_t idle_since=uptime_ticks;bool idle_sampled=false;
    while (1) {
        if(!idle_sampled && uptime_ticks-idle_since>=10*TICKS_PER_SEC){serial_puts("[idle] free KiB=");serial_putdec(pmm_free_memory_bytes()>>10);serial_puts("\n");idle_sampled=true;}

        yield();
        uint32_t work_start = uptime_ticks;

        bool pointer_moved = false;
        int old_mx = desktop_mouse_x;
        int old_my = desktop_mouse_y;

        if (mouse_enabled) {
            int mx = mouse_cursor_x;
            int my = mouse_cursor_y;
            int buttons = mouse_state.buttons;
            int wheel = mouse_state.wheel;
            mouse_state.wheel = 0;

            if (buttons != last_mouse_buttons ||
                (buttons != 0 && (mx != desktop_mouse_x || my != desktop_mouse_y)) ||
                wheel != 0) {
                desktop_handle_mouse(mx, my, buttons, wheel);
            }
            last_mouse_buttons = buttons;
            if (mx != desktop_mouse_x || my != desktop_mouse_y) {
                desktop_mouse_x = mx;
                desktop_mouse_y = my;
                pointer_moved = true;

                bool repaint = false;
                if(wm_menu_open()){wm_hover(mx,my);}
                else if (desktop.start_menu.visible) {
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
                            hw->type != WINDOW_TYPE_FILEMANAGER &&
                            hw->type != WINDOW_TYPE_DISKMGMT)
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

                if (theme_current->flat_bevels) {
                    if (my >= (int)screen_height - TASKBAR_HEIGHT) {
                        desktop.dirty = true;
                    }
                    for (int i = 0; i < desktop.window_count; i++) {
                        window_t* hw = &desktop.windows[i];
                        if (!hw->visible || hw->state == WINDOW_STATE_MINIMIZED)
                            continue;
                        int tb_top = hw->rect.y;
                        int tb_bot = hw->rect.y + W98_BORDER_W + W98_TITLEBAR_H;
                        if (mx >= hw->rect.x && mx < hw->rect.x + hw->rect.width &&
                            my >= tb_top && my < tb_bot) {
                            desktop.dirty = true;
                        }
                    }
                }
            }
        }

        for (int k = 0; k < 16; k++) {
            char c = keyboard_getchar();
            if (c == 0) break;
            desktop_handle_keyboard(c);
        }

        desktop_anim_tick();

        if (uptime_ticks - last_audio_tick >= 10) {
            last_audio_tick = uptime_ticks;
            audio_tick();
        }
        if (uptime_ticks - last_net_tick >= 10) {
            last_net_tick = uptime_ticks;
            net_poll();
            (void)net_link_recovered();

            int ns = net_dhcp_state() * 2 + net_has_link;
            if (ns != last_net_state) {
                last_net_state = ns;
                for (int i = 0; i < desktop.window_count; i++) {
                    if (desktop.windows[i].type == WINDOW_TYPE_NETWORK) {
                        desktop.windows[i].needs_redraw = true;
                    }
                }
                desktop.dirty = true;
            }
        }

        if (uptime_ticks - last_clock_tick >= 1000) {
            last_clock_tick = uptime_ticks;
            rtc_read_time();
            if ((int)rtc_minutes != last_rtc_minute) {
                last_rtc_minute = (int)rtc_minutes;
                desktop.dirty = true;
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
                    desktop.dirty = true;
                }
            }
        }

        window_t* focused = window_get_focused();
        if (focused && !focused->game_tick && focused->type == WINDOW_TYPE_TERMINAL &&
            uptime_ticks - last_blink_tick >= 500) {
            last_blink_tick = uptime_ticks;
            focused->needs_redraw = true;
            desktop.dirty = true;
        } else if (focused && !focused->game_tick && focused->type == WINDOW_TYPE_NOTEPAD &&
                   uptime_ticks - last_blink_tick >= 500) {
            last_blink_tick = uptime_ticks;
            notepad_cursor_tick(focused);
        }

        if (focused && focused->game_tick && uptime_ticks - last_game_tick >= 16) {
            last_game_tick = uptime_ticks;
            focused->game_tick();
            desktop.dirty = true;
        }

        if (boot_url[0] && (net_configured || uptime_ticks > 20 * TICKS_PER_SEC)) {
            char u[256];
            int i = 0; for (; boot_url[i]; i++) u[i] = boot_url[i]; u[i] = 0;
            boot_url[0] = 0;
            serial_puts("boot: opening browser at "); serial_puts(u); serial_puts("\n");
            browser_open_url(u);
        }

        browser_tick();

                                                                   
                                                                       
        video_tick();
        diskmgmt_tick();
        static uint32_t prefs_tick;
        if(uptime_ticks-prefs_tick>=250){prefs_tick=uptime_ticks;desktop_prefs_capture();}
        store_tick();

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
