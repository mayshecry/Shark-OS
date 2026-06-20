#include "kernel.h"

task_t* create_task(const char* name) {
    spin_lock(&task_list_lock);
    task_t* new_task = (task_t*)kmalloc(sizeof(task_t));
    new_task->id = next_pid++;
    new_task->state = TASK_READY;
    new_task->cpu_id = 0;
    strcpy(new_task->name, name);

    new_task->next = task_list;
    task_list = new_task;
    spin_unlock(&task_list_lock);
    return new_task;
}

void yield() { }

struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t num, size, addr, shndx;
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
    uint32_t framebuffer_addr_lo;
    uint32_t framebuffer_addr_hi;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
} __attribute__((packed));

struct multiboot_mmap_entry {
    uint32_t size;
    uint32_t addr_low;
    uint32_t addr_high;
    uint32_t len_low;
    uint32_t len_high;
    uint32_t type;
} __attribute__((packed));

void kmain(uint32_t magic, struct multiboot_info* mb_info) {
    if (magic != 0x2BADB002) return;

    asm volatile("cli");

    if ((mb_info->flags & (1 << 12)) && mb_info->framebuffer_bpp == 32) {
        lfbptr = (uint32_t*)(uintptr_t)mb_info->framebuffer_addr_lo;
        screen_width = mb_info->framebuffer_width;
        screen_height = mb_info->framebuffer_height;
        screen_pitch = mb_info->framebuffer_pitch;

        terminal_initialize();
        init_descriptor_tables();

        if (mb_info->flags & (1 << 6)) {
            uintptr_t mmap_addr = (uintptr_t)mb_info->mmap_addr;
            uint32_t mmap_length = mb_info->mmap_length;
            total_system_memory = 0;
            for (uint32_t i = 0; i < mmap_length; ) {
                struct multiboot_mmap_entry* entry = (struct multiboot_mmap_entry*)(uintptr_t)(mmap_addr + i);
                if (entry->type == 1) {
                    uint64_t length = ((uint64_t)entry->len_high << 32) | entry->len_low;
                    total_system_memory += length;
                }
                i += entry->size + 4;
            }
        } else {
            total_system_memory = (uint64_t)mb_info->mem_upper * 1024;
        }

        pmm_init(total_system_memory);

        for(int i = 0; i < MAX_CPUS; i++) {
            cpus[i].id = i;
            cpus[i].started = (i == 0);
        }

        cpus[0].current_task = create_task("kernel_init");
        cpus[0].current_task->state = TASK_RUNNING;

        fs_initialize();
        rtl8139_init();

        tiling_enabled = true;
        mouse_enabled = false;

        show_welcome_tour();

        settings_open();
        while (current_kernel_mode == KERNEL_MODE_SETTINGS) {
            char c = keyboard_getchar();
            if (c == 27) {
                settings_close();
            } else if (c == '\n') {
                if (settings_selected == 0) {
                    tiling_enabled = !tiling_enabled;
                } else if (settings_selected == 1) {
                    mouse_enabled = !mouse_enabled;
                    if (mouse_enabled) {
                        mouse_init();
                    }
                } else if (settings_selected == 2) {
                    apply_theme((selected_theme + 1) % MAX_THEMES);
                }
                settings_draw();
            } else if (c == 0x10) {
                if (settings_selected > 0) settings_selected--;
                settings_draw();
            } else if (c == 0x11) {
                if (settings_selected < 2) settings_selected++;
                settings_draw();
            }
        }

        while (1) {
            char c = keyboard_getchar();
            if (c != 0) {
                if (current_kernel_mode == KERNEL_MODE_SETTINGS) {
                    if (c == 27) {
                        settings_close();
                    } else if (c == '\n') {
                        if (settings_selected == 0) {
                            tiling_enabled = !tiling_enabled;
                        } else if (settings_selected == 1) {
                            mouse_enabled = !mouse_enabled;
                            if (mouse_enabled) {
                                mouse_init();
                            }
                        } else if (settings_selected == 2) {
                            apply_theme((selected_theme + 1) % MAX_THEMES);
                        }
                        settings_draw();
                    } else if (c == 0x10) {
                        if (settings_selected > 0) settings_selected--;
                        settings_draw();
                    } else if (c == 0x11) {
                        if (settings_selected < 2) settings_selected++;
                        settings_draw();
                    }
                } else if (current_kernel_mode == KERNEL_MODE_CLI) {
                    if (c == '\t') {
                        active_pane = (active_pane + 1) % pane_count;
                        draw_pane_tabs();
                    } else if (c == '+') {
                        if (tiling_enabled) split_active_pane();
                    } else if (c == '-') {
                        if (tiling_enabled) close_active_pane();
                    } else if (c == '?') {
                        if (current_kernel_mode == KERNEL_MODE_FAQ) {
                            faq_close();
                        } else {
                            faq_open();
                        }
                    } else if (c == 's' && ctrl_pressed) {
                        settings_open();
                    } else if (c == '\n') {
                        command_buffer[command_index] = '\0';
                        execute_command(command_buffer);
                        command_index = 0;
                    } else {
                        terminal_putchar_cli(c);
                    }
                } else if (current_kernel_mode == KERNEL_MODE_FAQ) {
                    if (c == 27) {
                        faq_close();
                    }
                } else if (current_kernel_mode == KERNEL_MODE_EDITOR) {
                    if (c == 27) {
                        editor_buffer[editor_buffer_idx] = '\0';
                        strcpy(editor_target_file->content, editor_buffer);
                        editor_target_file->content_len = editor_buffer_idx;
                        current_kernel_mode = KERNEL_MODE_CLI;
                        terminal_clear();
                        terminal_writestring("File saved. Exiting editor.\n\n");
                        print_prompt();
                    } else {
                        terminal_putchar_editor(c);
                    }
                }
            }
            if (mouse_enabled) {
                mouse_update_cursor();
            }
            __asm__ volatile("hlt");
        }
    } else {
        const char* msg_no_lfb = "No LFB or 32-bit mode. Halting.";
        uint16_t* vga_buffer = (uint16_t*)0xB8000;
        for (int i = 0; msg_no_lfb[i] != '\0'; i++) {
            vga_buffer[80 + i] = (uint16_t)msg_no_lfb[i] | 0x0C00;
        }
        while(1) { asm volatile("hlt"); }
    }
    while(1) { asm volatile("hlt"); }
}