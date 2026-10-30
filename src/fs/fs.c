#include "kernel.h"

#define FS_CACHE_SIZE 16
typedef struct {
    struct fs_node* parent;
    char name[32];
    struct fs_node* result;
} fs_cache_entry_t;

static fs_cache_entry_t fs_cache[FS_CACHE_SIZE];
static int fs_cache_idx = 0;

void fs_cache_clear(void) {
    for (int i = 0; i < FS_CACHE_SIZE; i++) {
        fs_cache[i].parent = NULL;
        fs_cache[i].name[0] = 0;
        fs_cache[i].result = NULL;
    }
    fs_cache_idx = 0;
}

struct fs_node* find_node(struct fs_node* parent, const char* name) {
    if (!parent) return NULL;

    for (int i = 0; i < FS_CACHE_SIZE; i++) {
        if (fs_cache[i].parent == parent && strcasecmp(fs_cache[i].name, name) == 0) {
            return fs_cache[i].result;
        }
    }

    for (int i = 0; i < parent->num_children; i++) {
        if (strcasecmp(parent->children[i]->name, name) == 0) {
            if (strlen(name) < sizeof(fs_cache[0].name)) {
                fs_cache[fs_cache_idx].parent = parent;
                strcpy(fs_cache[fs_cache_idx].name, name);
                fs_cache[fs_cache_idx].result = parent->children[i];
                fs_cache_idx = (fs_cache_idx + 1) % FS_CACHE_SIZE;
            }
            return parent->children[i];
        }
    }
    return NULL;
}

struct fs_node* fs_resolve_path(const char* path) {
    if (!path || !path[0]) return NULL;
    struct fs_node* node = path[0] == '/' ? root : current_dir;
    char part[32];
    int i = 0;
    const char* p = path;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        i = 0;
        while (*p && *p != '/') {
            if (i < 31) part[i++] = *p;
            p++;
        }
        part[i] = 0;
        if (strcmp(part, ".") == 0) continue;
        if (strcmp(part, "..") == 0) {
            if (node && node->parent) node = node->parent;
            continue;
        }
        struct fs_node* next = NULL;
        if (node && node->type == FS_DIRECTORY) {
            for (int child = 0; child < node->num_children; child++) {
                if (strcasecmp(node->children[child]->name, part) == 0) {
                    next = node->children[child];
                    break;
                }
            }
        }
        node = next;
        if (!node) return NULL;
    }
    return node;
}

struct fs_node* search_path(const char* name) {
    struct fs_node* n = fs_resolve_path(name);
    if (n && n->type == FS_FILE) return n;
    n = find_node(current_dir, name);
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
    if (parent && parent->num_children >= MAX_CHILDREN) return NULL;
    uint32_t index = 0;
    while (index < (uint32_t)pool_index && node_pool[index].allocated) index++;
    if (index >= MAX_NODES) return NULL;
    struct fs_node* node = &node_pool[index];
    char* content = node->content;
    uint32_t capacity = node->content_capacity;
    if (type == FS_FILE && (!content || capacity < MAX_FILE_CONTENT_SIZE)) {
        content = (char*)kmalloc(MAX_FILE_CONTENT_SIZE);
        if (!content) return NULL;
        capacity = MAX_FILE_CONTENT_SIZE;
    }
    memset(node, 0, sizeof(*node));
    strcpy(node->name, name);
    node->type = type;
    node->parent = parent;
    node->content = content;
    node->content_capacity = capacity;
    node->content_len = 0;
    if (node->content) node->content[0] = 0;
    node->disk_backed = 0;
    node->disk_drive = 0;
    node->disk_entry_offset = 0;
    node->disk_cluster = 0;
    node->disk_size = 0;
    node->disk_capacity = 0;
    node->disk_entry_lba = 0;
    node->allocated = 1;
    if (index == (uint32_t)pool_index) pool_index++;
    if (parent) parent->children[parent->num_children++] = node;
    fs_cache_clear();
    return node;
}

static int fs_node_ensure_capacity(struct fs_node* node, uint32_t needed) {
    if (!node || node->type != FS_FILE || needed > FS_MAX_FILE_SIZE + 1u) return -1;
    if (needed <= node->content_capacity) return 0;
    uint32_t capacity = node->content_capacity ? node->content_capacity : MAX_FILE_CONTENT_SIZE;
    while (capacity < needed) {
        if (capacity > (FS_MAX_FILE_SIZE + 1u) / 2u) {
            capacity = FS_MAX_FILE_SIZE + 1u;
            break;
        }
        capacity *= 2u;
    }
    char* expanded = (char*)kmalloc(capacity);
    if (!expanded) return -1;
    if (node->content_len > 0) memcpy(expanded, node->content, (uint32_t)node->content_len);
    if ((uint32_t)node->content_len < capacity) expanded[node->content_len] = 0;
    node->content = expanded;
    node->content_capacity = capacity;
    return 0;
}

const char* fs_node_data(const struct fs_node* node) {
    return node && node->type == FS_FILE ? node->content : NULL;
}

int fs_node_write(struct fs_node* node, uint32_t offset, const void* data, uint32_t length) {
    if (!node || node->type != FS_FILE || offset > FS_MAX_FILE_SIZE || length > FS_MAX_FILE_SIZE - offset) return -1;
    uint32_t old_length = (uint32_t)node->content_len;
    uint32_t end = offset + length;
    uint32_t new_length = end > old_length ? end : old_length;
    if (fs_node_ensure_capacity(node, new_length + 1u) != 0) return -1;
    if (offset > old_length) memset(node->content + old_length, 0, offset - old_length);
    if (length) memcpy(node->content + offset, data, length);
    node->content[new_length] = 0;
    node->content_len = (int)new_length;
    node->disk_backed = 0;
    return 0;
}

int fs_node_truncate(struct fs_node* node, uint32_t length) {
    if (!node || node->type != FS_FILE || length > FS_MAX_FILE_SIZE || fs_node_ensure_capacity(node, length + 1u) != 0) return -1;
    if (length > (uint32_t)node->content_len) memset(node->content + node->content_len, 0, length - (uint32_t)node->content_len);
    node->content[length] = 0;
    node->content_len = (int)length;
    node->disk_backed = 0;
    return 0;
}

int fs_node_remove(struct fs_node* node) {
    if (!node || !node->allocated || !node->parent || (node->type == FS_DIRECTORY && node->num_children)) return -1;
    struct fs_node* parent = node->parent;
    uint32_t i = 0;
    while (i < (uint32_t)parent->num_children && parent->children[i] != node) i++;
    if (i >= (uint32_t)parent->num_children) return -1;
    while (i + 1 < (uint32_t)parent->num_children) {
        parent->children[i] = parent->children[i + 1];
        i++;
    }
    parent->num_children--;
    if (node->content) node->content[0] = 0;
    node->content_len = 0;
    node->parent = NULL;
    node->num_children = 0;
    node->name[0] = 0;
    node->allocated = 0;
    node->disk_backed = 0;
    fs_cache_clear();
    return 0;
}

static void set_content(struct fs_node* node, const char* data) {
    if (!node || node->type != FS_FILE || !node->content || !data) return;
    uint32_t length = (uint32_t)strlen(data);
    if (length > FS_MAX_FILE_SIZE || fs_node_write(node, 0, data, length) != 0) return;
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
    create_node("Config", FS_DIRECTORY, system);
    create_node("Logs", FS_DIRECTORY, system);
    create_node("Modules", FS_DIRECTORY, system);
    create_node("Proc", FS_DIRECTORY, system);
    create_node("Security", FS_DIRECTORY, system);
    create_node("Network", FS_DIRECTORY, system);
    create_node("Plugins", FS_DIRECTORY, system);
    create_node("Boot", FS_DIRECTORY, system);

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
        "SharkOS V2\n"
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

    struct fs_node* hostname_f = create_node("hostname", FS_FILE, system);
    set_content(hostname_f, "shark\n");

    struct fs_node* osrelease = create_node("os-release", FS_FILE, system);
    set_content(osrelease,
        "NAME=\"SharkOS\"\n"
        "VERSION=\"0.5\"\n"
        "ID=sharkos\n"
        "PRETTY_NAME=\"SharkOS 0.5\"\n"
        "HOME_URL=\"https://example.com\"\n"
        );

    struct fs_node* uptime_f = create_node("uptime", FS_FILE, system);
    set_content(uptime_f, "0\n");

    struct fs_node* loadavg = create_node("loadavg", FS_FILE, system);
    set_content(loadavg, "0.00 0.01 0.00 1/1 1\n");

    struct fs_node* stat_f = create_node("stat", FS_FILE, system);
    set_content(stat_f,
        "cpu  1 0 2 100 0 0 0 0 0 0\n"
        "intr 123 45 67 89\n"
        "ctxt 4567\n"
        "btime 1234567890\n"
        "processes 12\n"
        "procs_running 1\n"
        "procs_blocked 0\n");

    struct fs_node* partitions = create_node("partitions", FS_FILE, system);
    set_content(partitions,
        "major minor  #blocks  name\n"
        "   1     0      65536 ram0\n"
        "   8     0    1048576 sda\n"
        "   8     1     524288 sda1\n"
        "   8     2     524288 sda2\n");

    struct fs_node* devices = create_node("devices", FS_FILE, system);
    set_content(devices,
        "Device list:\n"
        "/dev/ram0  - System memory\n"
        "/dev/sda   - Virtual disk\n"
        "/dev/tty0  - Terminal\n"
        "/dev/input/mouse0 - PS/2 Mouse\n"
        "/dev/input/kbd0  - PS/2 Keyboard\n"
        "/dev/net/rtl8139 - Ethernet adapter\n");

    struct fs_node* interrupts = create_node("interrupts", FS_FILE, system);
    set_content(interrupts,
        "           CPU0\n"
        "  0:        100    XT-PIC-XT  timer\n"
        "  1:         50    XT-PIC-XT  keyboard\n"
        "  2:          0    XT-PIC-XT  cascade\n"
        "  8:          1    XT-PIC-XT  rtc\n"
        " 12:         10    XT-PIC-XT  mouse\n"
        " 14:         25    XT-PIC-XT  ata\n"
        " 15:          0    XT-PIC-XT  ata\n");

    struct fs_node* cmdline = create_node("cmdline", FS_FILE, system);
    set_content(cmdline, "root=/dev/ram0 rw quiet splash\n");

    struct fs_node* driver_info = create_node("driver_info", FS_FILE, system);
    set_content(driver_info,
        "Driver            Status   Version\n"
        "shkbd             loaded   1.0\n"
        "shmouse           loaded   1.0\n"
        "shrtl8139         loaded   1.0\n"
        "shpci             loaded   1.0\n"
        "shata             absent   -\n");

    struct fs_node* config_sys = create_node("sysconfig", FS_FILE, system);
    set_content(config_sys,
        "# System configuration\n"
        "hostname=shark\n"
        "timezone=UTC\n"
        "keymap=us\n"
        "font=default\n"
        "mouse_speed=2\n"
        "theme=sharkos\n"
        "resolution=1024x768\n"
        "bpp=32\n");

    struct fs_node* security_pol = create_node("security.pol", FS_FILE, system);
    set_content(security_pol,
        "# Security Policy\n"
        "allow_exec=all\n"
        "allow_net=trusted\n"
        "audit_level=full\n"
        "force_protect=enabled\n");

    struct fs_node* net_config = create_node("netconfig", FS_FILE, system);
    set_content(net_config,
        "# Network Configuration\n"
        "interface=eth0\n"
        "dhcp=yes\n"
        "address=10.0.2.15\n"
        "netmask=255.255.255.0\n"
        "gateway=10.0.2.1\n"
        "dns=8.8.8.8\n"
        "mac=DE:AD:BE:EF:00:01\n");

    struct fs_node* fstab = create_node("fstab", FS_FILE, system);
    set_content(fstab,
        "# Filesystem table\n"
        "ram0       /          sharkfs   defaults  0 0\n"
        "proc       /System/Proc  proc      defaults  0 0\n"
        "sys        /System    sysfs     defaults  0 0\n");

    struct fs_node* syslog = create_node("syslog", FS_FILE, system);
    set_content(syslog,
        "[  OK  ] Started Kernel initialization.\n"
        "[  OK  ] Mounted root filesystem.\n"
        "[  OK  ] Started PCI enumeration.\n"
        "[  OK  ] Loaded module: sharkfs\n"
        "[  OK  ] Loaded module: shproc\n"
        "[  OK  ] Started network interface eth0.\n"
        "[  OK  ] Input devices initialized.\n"
        "[  OK  ] System ready.\n");

    struct fs_node* dmesg = create_node("dmesg", FS_FILE, system);
    set_content(dmesg,
        "[0.000000] SharkOS booting...\n"
        "[0.000100] CPU: GenuineIntel i686\n"
        "[0.000200] Memory: 128MB\n"
        "[0.000300] APIC: enabled\n"
        "[0.000400] PIC: remapped\n"
        "[0.000500] PIT: 1000Hz\n"
        "[0.000600] Keyboard: PS/2 detected\n"
        "[0.000700] Mouse: PS/2 detected\n"
        "[0.000800] PCI: 1 device(s) found\n"
        "[0.000900] RTL8139: Ethernet at I/O 0xC000\n"
        "[0.001000] Framebuffer: initialized\n"
        "[0.001100] Shell: ready\n");

    struct fs_node* boot_cfg = create_node("grub.cfg", FS_FILE, system);
    set_content(boot_cfg,
        "set default=0\n"
        "set timeout=5\n"
        "menuentry \"SharkOS\" {\n"
        "  multiboot /boot/sharkos.bin\n"
        "}\n");

    struct fs_node* initrd = create_node("initrd.img", FS_FILE, system);
    set_content(initrd,
        "\x7F" "ELF...\n"
        "SharkOS initial ramdisk\n"
        "Version: 0.5\n"
        "Contains: kernel modules\n");

    struct fs_node* sysmap = create_node("System.map", FS_FILE, system);
    set_content(sysmap,
        "c0100000 T _start\n"
        "c0100020 T kmain\n"
        "c0100100 T init_descriptor_tables\n"
        "c0100200 T pmm_init\n"
        "c0100300 T terminal_initialize\n"
        "c0100400 T keyboard_handler\n"
        "c0100500 T mouse_handler\n"
        "c0100600 T fs_initialize\n"
        "c0100700 T execute_command\n"
        "c0100800 T show_fastfetch\n"
        "c0100900 T irq_handler\n"
        "c0100a00 T syscall_handler\n");

    create_node("shs", FS_FILE, bin_dir);

    struct fs_node* mount_dir = create_node("mnt", FS_DIRECTORY, root);
    (void)mount_dir;

    struct fs_node* pc_dir = create_node("pc", FS_DIRECTORY, root);
    struct fs_node* wallpaper = create_node("9fc3fc59d52dc24748feb8836feded7e.png", FS_FILE, pc_dir);
    set_content(wallpaper,
        "\x89PNG\r\n\x1A\n"
        "\x00\x00\x00\rIHDR\x00\x00\x00\x01\x00\x00\x00\x01"
        "\x08\x02\x00\x00\x00\x90\x77\x53\xDE"
        "\x00\x00\x00\x0CIDATh\x01\x01\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
        );

    fs_cache_clear();
}
