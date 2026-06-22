#include "kernel.h"

struct fs_node* find_node(struct fs_node* parent, const char* name) {
    if (!parent) return NULL;
    for (int i = 0; i < parent->num_children; i++) {
        if (strcasecmp(parent->children[i]->name, name) == 0) return parent->children[i];
    }
    return NULL;
}

struct fs_node* search_path(const char* name) {
    struct fs_node* n = find_node(current_dir, name);
    if (n && n->type == FS_FILE) return n;

    struct fs_node* sys = find_node(root, "System");
    if (sys) {
        struct fs_node* bin = find_node(sys, "Bin");
        if (bin) {
            n = find_node(bin, name);
            if (n && n->type == FS_FILE) return n;
        }
    }

    return NULL;
}

struct fs_node* create_node(const char* name, node_type_t type, struct fs_node* parent) {
    if (pool_index >= MAX_NODES) return NULL;
    struct fs_node* node = &node_pool[pool_index++];
    strcpy(node->name, name);
    node->type = type;
    node->parent = parent;
    node->num_children = 0;
    node->content[0] = '\0';
    node->content_len = 0;
    if (parent && parent->num_children < MAX_CHILDREN) {
        parent->children[parent->num_children++] = node;
    }
    return node;
}

static void set_content(struct fs_node* node, const char* data) {
    strcpy(node->content, data);
    node->content_len = strlen(data);
}

void fs_initialize() {
    root = create_node("/", FS_DIRECTORY, NULL);

    struct fs_node* user = create_node("User", FS_DIRECTORY, root);
    current_dir = user;

    create_node("Documents", FS_DIRECTORY, user);
    create_node("Photos", FS_DIRECTORY, user);
    struct fs_node* readme = create_node("readme.txt", FS_FILE, user);
    set_content(readme,
        "Welcome to SharkOS!\n"
        "=================\n\n"
        "SharkOS is a hobby operating system built from scratch.\n"
        "It features a custom boot process, elf executable support,\n"
        "a graphical terminal with themes, mouse support, and more.\n\n"
        "Commands: help, ls, cd, cat, edit, touch, mkdir, calc, echo\n"
        "Shortcuts: ? (FAQ), + (split pane), - (close pane), Tab (switch pane)\n");

    struct fs_node* system = create_node("System", FS_DIRECTORY, root);
    struct fs_node* bin_dir = create_node("Bin", FS_DIRECTORY, system);
    create_node("Drivers", FS_DIRECTORY, system);
    
    struct fs_node* kernel_sys = create_node("Kernel.sys", FS_FILE, system);
    set_content(kernel_sys,
        "SHKRNL version 0.5 (root@sharkos) (gcc) #1 SMP PREEMPT_DYNAMIC\n"
        "Architecture: i686\n"
        "Kernel type: Monolithic\n"
        "Preemption: Voluntary\n");

    struct fs_node* boot_log = create_node("boot.log", FS_FILE, system);
    set_content(boot_log,
        "[    0.000000] SHKRNL version 0.5 (root@sharkos) (gcc)\n"
        "[    0.000001] BIOS-e820 memory map detected\n"
        "[    0.000002] 128 MB usable RAM\n"
        "[    0.000003] Framebuffer: 1024x768@32bpp\n"
        "[    0.000004] CPU features: FPU, PAE, PSE detected\n"
        "[    0.000005] Local APIC timer: 100 Hz\n"
        "[    0.000006] HZ: 1000\n"
        "[    0.000007] PID max: 32768\n"
        "[    0.000008] Initializing cgroup subsys cpuset\n"
        "[    0.000009] Booting paravirtualized kernel on KVM\n"
        "[    0.000010] KVM setup done\n"
        "[    0.000011] kvm-clock: cpu 0, primary 0\n"
        "[    0.000012] TSC: PIT calibration matches HPET\n"
        "[    0.000013] Booting processor 1/1 APIC 0x0\n"
        "[    0.000014] x86/mm: Memory block size: 128MB\n"
        "[    0.000015] ACPI: Core revision 20240322\n"
        "[    0.000016] PM:  4.0.5\n"
        "[    0.000017] SLUB: HWalign=64, Order=0-3\n"
        "[    0.000018] rcu: Hierarchical RCU implementation\n"
        "[    0.000019] Memory: 128M available\n"
        "[    0.000020] Built 1 zonelists, Total pages: 0x8000\n"
        "[    0.000021] Kernel command line: root=/dev/ram0 rw\n"
        "[    0.000022] PID hash table entries: 4096\n"
        "[    0.000023] Dentry cache hash table entries: 131072\n"
        "[    0.000024] Inode-cache hash table entries: 65536\n"
        "[    0.000025] Freeing SMP alternatives: 0frees\n"
        "[    0.000026] smpboot: CPU0: Intel(R) Core(TM) i7\n"
        "[    0.000027] ACPI: 2 ACPI AML tables successfully acquired\n"
        "[    0.000028] ACPI: Setting up all available GPEs\n"
        "[    0.000029] Last level iTLB entries: 4KB 0, 2MB 0, 4MB 0\n"
        "[    0.000030] Last level dTLB entries: 4KB 0, 2MB 0, 4MB 0, 1GB 0\n"
        "[    0.000031] NMI watchdog: Enabled\n"
        "[    0.000032] SHKRNL boot complete.\n");

    struct fs_node* version = create_node("version", FS_FILE, system);
    set_content(version,
        "SharkOS v0.5\n"
        "Codename: Shark\n"
        "Kernel: SHKRNL 0.5\n"
        "Arch: i686\n"
        "Compiler: gcc\n"
        "Shell: shark shell v1.0\n");

    struct fs_node* modules = create_node("modules", FS_FILE, system);
    set_content(modules,
        "Module                  Size     Used by\n"
        "sharkfs                24576    1\n"
        "shkern                 65536    0\n"
        "shproc                 16384    0\n"
        "shnet                  28672    0\n"
        "shinput                12288    0\n"
        "shsound                 8192    0\n");

    struct fs_node* cpu_info = create_node("cpuinfo", FS_FILE, system);
    set_content(cpu_info,
        "processor       : 0\n"
        "vendor_id       : GenuineIntel\n"
        "cpu family      : 6\n"
        "model           : 42\n"
        "model name      : Intel(R) Core(TM) i7\n"
        "stepping        : 7\n"
        "cpu MHz         : 2394.466\n"
        "cache size      : 6144 KB\n"
        "fpu             : yes\n"
        "fpu_exception   : yes\n"
        "cpuid level     : 5\n"
        "wp              : yes\n"
        "flags           : fpu vme de pse tsc msr pae mce cx8 apic\n");

    struct fs_node* mem_info = create_node("meminfo", FS_FILE, system);
    set_content(mem_info,
        "MemTotal:       131072 kB\n"
        "MemFree:         98304 kB\n"
        "MemAvailable:   114688 kB\n"
        "Buffers:          8192 kB\n"
        "Cached:          16384 kB\n"
        "SwapTotal:           0 kB\n"
        "SwapFree:            0 kB\n");


    create_node("shs", FS_FILE, bin_dir);
}