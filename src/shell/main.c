#include "kernel.h"
task_t* create_task(const char* name) {
    spin_lock(&task_list_lock);
    task_t* t = (task_t*)kmalloc(sizeof(task_t));
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
static void boot_print(const char* s, int ms) { terminal_writestring(s); delay_ms(ms); }
void kmain(uint32_t magic, struct multiboot_info* mb_info) {
    if (magic != 0x2BADB002) return;
    asm volatile("cli");
    if ((mb_info->flags & (1 << 12)) && mb_info->framebuffer_bpp == 32) {
        lfbptr = (uint32_t*)(uintptr_t)mb_info->framebuffer_addr_lo;
        screen_width = mb_info->framebuffer_width;
        screen_height = mb_info->framebuffer_height;
        screen_pitch = mb_info->framebuffer_pitch;
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
        terminal_writestring("SharkOS v0.1 [");
        hex_to_string((uint32_t)total_system_memory >> 20, buf);
        terminal_writestring(buf);
        terminal_writestring(" MB RAM]\nBooting SHKRNL...\n");
        boot_print("[    0.000000] SHKRNL version 0.1 (root@sharkos) (gcc) #1 SMP\n", 100);
        boot_print("[    0.000001] Command line: root=/dev/ram0 rw\n", 50);
        boot_print("[    0.000002] KERNEL supported cpus:\n", 50);
        char cpu_model[49];
        get_cpu_model(cpu_model);
        boot_print(cpu_model, 100);
        boot_print("[    0.000003] x86/fpu: Supporting XSAVE features\n", 50);
        boot_print("[    0.000004] BIOS-provided physical RAM map:\n", 50);
        boot_print("[    0.000005] BIOS-e820: usable regions detected\n", 50);
        boot_print("[    0.000006] NX (Execute Disable) protection: active\n", 50);
        boot_print("[    0.000007] SMBIOS present.\n", 50);
        boot_print("[    0.000008] DMI: SharkOS Virtual Machine\n", 50);
        boot_print("[    0.000009] Hypervisor detected: KVM\n", 50);
        boot_print("[    0.000010] tsc: Fast TSC calibration using PIT\n", 50);
        boot_print("[    0.000011] x86/PAT: MTRR support enabled\n", 50);
        boot_print("[    0.000012] BSP: CPU 0\n", 50);
        boot_print("[    0.000013] ACPI: RSDP found (v02 SHARK)\n", 50);
        boot_print("[    0.000014] PCI: MMCONFIG for domain 0000 [bus 00-ff]\n", 50);
        boot_print("[    0.000015] PCI: using ACPI for IRQ routing\n", 50);
        boot_print("[    0.000016] clocksource: refined-jiffies\n", 50);
        boot_print("[    0.000017] Calibrating delay loop... done.\n", 100);
        boot_print("[    0.000018] Initializing cgroup subsys cpuset\n", 50);
        boot_print("[    0.000019] Booting paravirtualized kernel on KVM\n", 50);
        boot_print("[    0.000020] KVM setup done\n", 50);
        boot_print("[    0.000021] kvm-clock: cpu 0, primary 0\n", 50);
        boot_print("[    0.000022] TSC: PIT calibration matches HPET. Entering 64-bit mode.\n", 100);
        boot_print("[    0.000023] Booting processor 1/1 APIC 0x0\n", 50);
        boot_print("[    0.000024] x86/mm: Memory block size: 128MB\n", 50);
        boot_print("[    0.000025] ACPI: Core revision 20240322\n", 50);
        boot_print("[    0.000026] PM:  4.0.5\n", 50);
        boot_print("[    0.000027] SLUB: HWalign=64, Order=0-3\n", 50);
        boot_print("[    0.000028] rcu: Hierarchical RCU implementation.\n", 50);
        boot_print("[    0.000029] Memory: ", 50);
        hex_to_string((uint32_t)total_system_memory >> 20, buf);
        terminal_writestring(buf);
        boot_print("M available\n", 50);
        boot_print("[    0.000030] Built 1 zonelists, Total pages: ", 50);
        hex_to_string((uint32_t)total_system_memory >> 12, buf);
        terminal_writestring(buf);
        boot_print("\n[    0.000031] Kernel command line: root=/dev/ram0 rw\n", 50);
        boot_print("[    0.000032] PID hash table entries: 4096\n", 50);
        boot_print("[    0.000033] Dentry cache hash table entries: 131072\n", 50);
        boot_print("[    0.000034] Inode-cache hash table entries: 65536\n", 50);
        boot_print("[    0.000035] Freeing SMP alternatives: 0frees\n", 50);
        boot_print("[    0.000036] smpboot: CPU0: ", 50);
        terminal_writestring(cpu_model);
        boot_print("\n[    0.000037] ACPI: 2 ACPI AML tables successfully acquired\n", 50);
        boot_print("[    0.000038] ACPI: Setting up all available GPEs\n", 50);
        boot_print("[    0.000039] Last level iTLB entries: 4KB 0, 2MB 0, 4MB 0\n", 50);
        boot_print("[    0.000040] Last level dTLB entries: 4KB 0, 2MB 0, 4MB 0, 1GB 0\n", 50);
        boot_print("[    0.000041] NMI watchdog: Enabled\n", 50);
        boot_print("[    0.000042] SHKRNL boot complete.\n", 100);
        delay_ms(3000);
        terminal_initialize();
        init_descriptor_tables();
        fs_initialize();
        outb(0x21, inb(0x21) & ~0x02);
        asm volatile("sti");
        print_prompt();
        
        while (1) {
            char c = keyboard_getchar();
            if (c == 0) continue;
            
            // Handle settings mode
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
            
            // Handle editor mode
            if (current_kernel_mode == KERNEL_MODE_EDITOR) {
                if (c == 27) {  // ESC - save and exit
                    if (editor_target_file) {
                        // Copy editor buffer to file
                        int i;
                        for (i = 0; i < editor_buffer_idx && i < MAX_FILE_CONTENT_SIZE - 1; i++) {
                            editor_target_file->content[i] = editor_buffer[i];
                        }
                        editor_target_file->content[i] = '\0';
                        editor_target_file->content_len = i;
                    }
                    current_kernel_mode = KERNEL_MODE_CLI;
                    terminal_clear();
                    print_prompt();
                    continue;
                }
                terminal_putchar_editor(c);
                continue;
            }
            
            // Handle FAQ mode
            if (current_kernel_mode == KERNEL_MODE_FAQ) {
                if (c == 27 || c == '?') {
                    faq_close();
                }
                continue;
            }
            
            // CLI mode handling
            if (ctrl_pressed && c == 's') {
                settings_open();
                continue;
            }
            if (c == '\t') {
                int new_pane = (active_pane + 1) % pane_count;
                active_pane = new_pane;
                draw_pane_tabs();
                
                // Position cursor at end of prompt + any typed text
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
            terminal_putchar_cli(c);
            if (c == '\n') {
                if (command_index > 0) {
                    command_buffer[command_index - 1] = '\0';
                } else {
                    command_buffer[0] = '\0';
                }
                execute_command(command_buffer);
                command_index = 0;
                command_buffer[0] = '\0';
                terminal_writestring("\n");
                print_prompt();
            }
        }
    }
}
