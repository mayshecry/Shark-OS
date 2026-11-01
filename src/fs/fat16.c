#include "kernel.h"
#include "ata.h"

#define FAT16_MOUNTS 4
#define FAT16_MAX_DEPTH 5
#define FAT16_ENTRY_SIZE 32
#define FAT16_ATTR_DIRECTORY 0x10
#define FAT16_ATTR_VOLUME 0x08

static uint8_t fat_sector[512];
static uint8_t io_sector[512];

typedef struct {
    int active;
    int drive;
    uint32_t partition_lba;
    uint32_t fat_lba;
    uint32_t root_lba;
    uint32_t root_sectors;
    uint32_t data_lba;
    uint16_t root_entries;
    uint16_t fat_sectors;
    uint8_t sectors_per_cluster;
    uint8_t fat_count;
    struct fs_node* mountpoint;
} fat16_mount_t;

static fat16_mount_t mounts[FAT16_MOUNTS];

static uint16_t rd16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static int read_sector(int drive, uint32_t lba, uint8_t* out) {
    return ata_read_sectors(drive, lba, 1, out) ? 1 : 0;
}

static int write_sector(int drive, uint32_t lba, const uint8_t* in) {
    return ata_write_sectors(drive, lba, 1, in) ? 1 : 0;
}

static uint16_t fat_entry(fat16_mount_t* m, uint16_t cluster) {
    uint32_t byte_offset = (uint32_t)cluster * 2;
    uint32_t lba = m->fat_lba + byte_offset / 512;
    if (lba >= m->fat_lba + m->fat_sectors || !read_sector(m->drive, lba, fat_sector)) return 0xFFFF;
    return rd16(fat_sector + byte_offset % 512);
}

static uint32_t cluster_lba(const fat16_mount_t* m, uint16_t cluster) {
    return m->data_lba + (uint32_t)(cluster - 2) * m->sectors_per_cluster;
}

static int fat_name(const uint8_t* entry, char* out) {
    int p = 0;
    int base_end = 8;
    while (base_end > 0 && entry[base_end - 1] == ' ') base_end--;
    for (int i = 0; i < base_end && p < 30; i++) {
        char c = (char)entry[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        out[p++] = c;
    }
    int ext_end = 3;
    while (ext_end > 0 && entry[8 + ext_end - 1] == ' ') ext_end--;
    if (ext_end) {
        out[p++] = '.';
        for (int i = 0; i < ext_end && p < 30; i++) {
            char c = (char)entry[8 + i];
            if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
            out[p++] = c;
        }
    }
    out[p] = 0;
    return p;
}

static void import_directory(fat16_mount_t* m, struct fs_node* parent, uint16_t first_cluster, uint32_t fixed_lba, uint16_t fixed_entries, int depth) {
    if (depth > FAT16_MAX_DEPTH || !parent || parent->type != FS_DIRECTORY) return;
    uint32_t max_sectors = fixed_entries ? (uint32_t)m->root_sectors : 4096;
    uint8_t dir_sector[512];
    uint16_t cluster = first_cluster;
    uint32_t sector_count = 0;
    int done = 0;
    while (!done && sector_count < max_sectors) {
        uint32_t base_lba;
        uint32_t sectors_here;
        if (fixed_entries) {
            base_lba = fixed_lba;
            sectors_here = m->root_sectors;
        } else {
            if (cluster < 2 || cluster >= 0xFFF0) break;
            base_lba = cluster_lba(m, cluster);
            sectors_here = m->sectors_per_cluster;
        }
        for (uint32_t s = 0; s < sectors_here && !done; s++) {
            if (!read_sector(m->drive, base_lba + s, dir_sector)) { done = 1; break; }
            for (int off = 0; off < 512; off += FAT16_ENTRY_SIZE) {
                uint8_t* e = dir_sector + off;
                if (e[0] == 0) { done = 1; break; }
                if (e[0] == 0xE5 || e[11] == 0x0F || (e[11] & FAT16_ATTR_VOLUME)) continue;
                char name[32];
                if (!fat_name(e, name)) continue;
                if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
                node_type_t type = (e[11] & FAT16_ATTR_DIRECTORY) ? FS_DIRECTORY : FS_FILE;
                if (parent->num_children >= MAX_CHILDREN) continue;
                struct fs_node* node = create_node(name, type, parent);
                if (!node) return;
                uint16_t start = rd16(e + 26);
                uint32_t size = rd32(e + 28);
                node->disk_backed = 1;
                node->disk_drive = (uint8_t)m->drive;
                node->disk_cluster = start;
                node->disk_size = size;
                node->disk_entry_lba = base_lba + s;
                node->disk_entry_offset = (uint16_t)off;
                if (type == FS_FILE && start >= 2) {
                    uint32_t remain = size;
                    uint32_t pos = 0;
                    uint32_t capacity = 0;
                    uint16_t cur = start;
                    uint32_t guard = 0;
                    while (cur >= 2 && cur < 0xFFF0 && guard++ < 65536) {
                        capacity += (uint32_t)m->sectors_per_cluster * 512;
                        for (uint32_t cs = 0; cs < m->sectors_per_cluster && remain; cs++) {
                            if (!read_sector(m->drive, cluster_lba(m, cur) + cs, io_sector)) { remain = 0; break; }
                            uint32_t take = remain < 512 ? remain : 512;
                            if (take > MAX_FILE_CONTENT_SIZE - 1 - pos) take = MAX_FILE_CONTENT_SIZE - 1 - pos;
                            memcpy(node->content + pos, io_sector, take);
                            pos += take;
                            remain = remain > take ? remain - take : 0;
                            if (pos >= MAX_FILE_CONTENT_SIZE - 1) { remain = 0; break; }
                        }
                        uint16_t next = fat_entry(m, cur);
                        if (next >= 0xFFF8 || next == 0 || next == 0xFFF7) break;
                        cur = next;
                    }
                    node->content_len = (int)pos;
                    node->content[pos] = 0;
                    node->disk_capacity = capacity;
                } else if (type == FS_DIRECTORY && start >= 2) {
                    import_directory(m, node, start, 0, 0, depth + 1);
                }
            }
        }
        sector_count += sectors_here;
        if (fixed_entries) break;
        uint16_t next = fat_entry(m, cluster);
        if (next >= 0xFFF8 || next == 0 || next == 0xFFF7) break;
        cluster = next;
    }
}

static int parse_volume(fat16_mount_t* m, uint32_t base) {
    uint8_t b[512];
    if (!read_sector(m->drive, base, b)) return 0;
    if (b[510] != 0x55 || b[511] != 0xAA) return 0;
    uint16_t bytes_per_sector = rd16(b + 11);
    uint8_t spc = b[13];
    uint16_t reserved = rd16(b + 14);
    uint8_t fats = b[16];
    uint16_t root_entries = rd16(b + 17);
    uint16_t total16 = rd16(b + 19);
    uint16_t fat_sectors = rd16(b + 22);
    if (bytes_per_sector != 512 || !spc || spc > 64 || !reserved || !fats || !root_entries || !fat_sectors) return 0;
    uint32_t root_sectors = ((uint32_t)root_entries * 32 + 511) / 512;
    uint32_t total_sectors = total16 ? total16 : rd32(b + 32);
    const ata_drive_t* drive = ata_drive_get(m->drive);
    if (!drive || base >= drive->sectors || total_sectors > drive->sectors - base) return 0;
    uint32_t metadata_sectors = reserved + (uint32_t)fats * fat_sectors + root_sectors;
    if (total_sectors <= metadata_sectors) return 0;
    uint32_t data_sectors = total_sectors - metadata_sectors;
    uint32_t clusters = data_sectors / spc;
    if (clusters < 4085 || clusters >= 65525) return 0;
    m->partition_lba = base;
    m->fat_lba = base + reserved;
    m->fat_sectors = fat_sectors;
    m->fat_count = fats;
    m->root_lba = m->fat_lba + (uint32_t)fats * fat_sectors;
    m->root_sectors = root_sectors;
    m->data_lba = m->root_lba + root_sectors;
    m->root_entries = root_entries;
    m->sectors_per_cluster = spc;
    return 1;
}

int fat16_mount(int drive, struct fs_node* mountpoint) {
    if (drive < 0 || drive >= ata_drive_count() || !mountpoint || mountpoint->type != FS_DIRECTORY) return -1;
    for (int i = 0; i < FAT16_MOUNTS; i++) if (mounts[i].active && mounts[i].mountpoint == mountpoint) return -2;
    int slot = -1;
    for (int i = 0; i < FAT16_MOUNTS; i++) if (!mounts[i].active) { slot = i; break; }
    if (slot < 0) return -3;
    fat16_mount_t* m = &mounts[slot];
    memset(m, 0, sizeof(*m));
    m->drive = drive;
    uint8_t mbr[512];
    if (!read_sector(drive, 0, mbr)) return -4;
    uint32_t base = 0;
    int valid = 0;
    if (parse_volume(m, 0)) valid = 1;
    if (!valid && mbr[510] == 0x55 && mbr[511] == 0xAA) {
        for (int i = 0; i < 4 && !valid; i++) {
            const uint8_t* p = mbr + 446 + i * 16;
            uint8_t type = p[4];
            if (type == 0x04 || type == 0x06 || type == 0x0E || type == 0x01) {
                base = rd32(p + 8);
                if (base && parse_volume(m, base)) valid = 1;
            }
        }
    }
    if (!valid) return -5;
    m->active = 1;
    m->mountpoint = mountpoint;
    import_directory(m, mountpoint, 0, m->root_lba, m->root_entries, 0);
    return slot;
}

int fat16_unmount(struct fs_node* mountpoint) {
    if (!mountpoint || mountpoint->type != FS_DIRECTORY) return 0;
    for (int i = 0; i < FAT16_MOUNTS; i++) {
        fat16_mount_t* m = &mounts[i];
        if (!m->active || m->mountpoint != mountpoint) continue;
        mountpoint->num_children = 0;
        m->active = 0;
        m->mountpoint = NULL;
        return 1;
    }
    return 0;
}

int fs_node_sync(struct fs_node* node) {
    if (!node || node->type != FS_FILE) return 0;
    if (!node->disk_backed) {
        if (node->content_len < 0 || node->content_len >= MAX_FILE_CONTENT_SIZE) return 0;
        node->content[node->content_len] = 0;
        return 1;
    }
    if (node->content_len < 0 || node->content_len >= MAX_FILE_CONTENT_SIZE) return 0;
    if (node->disk_size >= MAX_FILE_CONTENT_SIZE || (uint32_t)node->content_len > node->disk_capacity) return 0;
    fat16_mount_t* m = NULL;
    for (int i = 0; i < FAT16_MOUNTS; i++) if (mounts[i].active && mounts[i].drive == node->disk_drive) { m = &mounts[i]; break; }
    if (!m) return 0;
    uint16_t cluster = (uint16_t)node->disk_cluster;
    uint32_t offset = 0;
    uint32_t guard = 0;
    while (cluster >= 2 && cluster < 0xFFF0 && guard++ < 65536) {
        for (uint32_t s = 0; s < m->sectors_per_cluster; s++) {
            memset(io_sector, 0, sizeof(io_sector));
            uint32_t take = (uint32_t)node->content_len - offset;
            if (take > 512) take = 512;
            if (take) memcpy(io_sector, node->content + offset, take);
            if (!write_sector(m->drive, cluster_lba(m, cluster) + s, io_sector)) return 0;
            offset += take;
        }
        uint16_t next = fat_entry(m, cluster);
        if (next >= 0xFFF8 || next == 0 || next == 0xFFF7) break;
        cluster = next;
    }
    if (!read_sector(m->drive, node->disk_entry_lba, io_sector)) return 0;
    wr32(io_sector + node->disk_entry_offset + 28, (uint32_t)node->content_len);
    if (!write_sector(m->drive, node->disk_entry_lba, io_sector)) return 0;
    node->disk_size = (uint32_t)node->content_len;
    return 1;
}

int fat16_is_mounted(struct fs_node* node) {
    for (int i = 0; i < FAT16_MOUNTS; i++) if (mounts[i].active && mounts[i].mountpoint == node) return 1;
    return 0;
}
