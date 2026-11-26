#include "kernel.h"
#include "ata.h"
#include "store.h"

#define BANK 8192u
#define CAP ((BANK - 1u) * 512u)
#define PART (1u + 2u * BANK)
#define MAGIC 0x32534653u
static char token[33];
static unsigned last_tile = 1;
static int disk = -1, active = -1;
static uint32_t start, generation, last_crc, last_size, tick;
/* Common small snapshots never need to pin the entire 4 MiB bank. */
static uint8_t sec[512], small[16384], *buf=small;
static int ensure_room(uint32_t n,uint32_t preserve){
    if(n>CAP)return 0;
    if(n<=sizeof small || buf!=small)return 1;
    uint8_t *big=kmalloc(CAP);if(!big)return 0;
    memcpy(big,small,preserve);buf=big;return 1;
}
static const char *state = "Live session (RAM only)";
static uint32_t crc_table[256];
static uint32_t crc(const void *data, size_t n) {
    uint32_t c = ~0u;
    const uint8_t *p = data;
    while (n--)
        c = (c >> 8) ^ crc_table[(c ^ *p++) & 255];
    return ~c;
}
static uint32_t get(const uint8_t *p) {
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put(uint8_t *p, uint32_t x) {
    for (int i = 0; i < 4; i++)
        p[i] = x >> (8 * i);
}
static int same(const void *a, const void *b, size_t n) {
    const uint8_t *p = a, *q = b;
    while (n--)
        if (*p++ != *q++)
            return 0;
    return 1;
}
void store_boot_option(const char *cmd) {
    while (cmd && *cmd) {
        while (*cmd == ' ')
            cmd++;
        const char *end = cmd;
        while (*end && *end != ' ')
            end++;
        if (end - cmd == 40 && same(cmd, "sharkfs=", 8)) {
            for (int i = 8; i < 40; i++)
                if (!((cmd[i] >= '0' && cmd[i] <= '9') || (cmd[i] >= 'a' && cmd[i] <= 'f')))
                    return;
            memcpy(token, cmd + 8, 32);
            return;
        }
        cmd = end;
    }
}
int store_installed(void) { return token[0] != 0; }
const char *store_status(void) { return state; }
static int transfer(uint32_t lba, uint8_t *p, uint32_t n, int writing) {
    while (n) {
        uint8_t batch = n > 128 ? 128 : n;
        if (!(writing ? ata_write_sectors(disk, lba, batch, p) : ata_read_sectors(disk, lba, batch, p)))
            return 0;
        n -= batch;
        lba += batch;
        p += batch * 512u;
    }
    return 1;
}
static uint32_t bank_lba(int slot) { return start + 1 + slot * BANK; }
struct header {
    uint32_t gen, size, crc, nodes, tile;
};
static int read_header(int slot, struct header *h) {
    if (!transfer(bank_lba(slot), sec, 1, 0))
        return -2;
    if (!get(sec)) {
        for (int i = 0; i < 512; i++)
            if (sec[i])
                return -1;
        return 0;
    }
    if (get(sec) != MAGIC || get(sec + 24) != crc(sec, 24))
        return -1;
    h->gen = get(sec + 4);
    h->size = get(sec + 8);
    h->crc = get(sec + 12);
    h->nodes = get(sec + 16);
    h->tile = get(sec + 20);
    return h->size && h->size <= CAP && h->nodes && h->nodes <= MAX_NODES && h->tile <= 1 ? 1 : -1;
}
static uint8_t *recs[MAX_NODES];
static int inspect(const struct header *h) {
    uint32_t pos = 0, children[MAX_NODES] = {0};
    for (unsigned i = 0; i < h->nodes; i++) {
        if (h->size - pos < 44)
            return 0;
        uint8_t *r = buf + pos;
        unsigned parent = r[0], type = r[1], len = get(r + 8);
        if ((type != FS_FILE && type != FS_DIRECTORY) || (type == FS_DIRECTORY && len) || len > h->size - pos - 44)
            return 0;
        unsigned j = 0;
        while (j < 32 && r[12 + j])
            j++;
        if (!j || j == 32)
            return 0;
        if (!i) {
            if (parent != 255 || type != FS_DIRECTORY || strcmp((char *)r + 12, "/"))
                return 0;
        } else {
            if (parent >= i || recs[parent][1] != FS_DIRECTORY || ++children[parent] > MAX_CHILDREN)
                return 0;
            if (strchr((char *)r + 12, '/') || !strcmp((char *)r + 12, ".") || !strcmp((char *)r + 12, ".."))
                return 0;
            for (unsigned k = 1; k < i; k++)
                if (recs[k][0] == parent && !strcmp((char *)recs[k] + 12, (char *)r + 12))
                    return 0;
        }
        recs[i] = r;
        pos += 44 + len;
    }
    return pos == h->size;
}
static int load_bank(int slot, struct header *h) {
    if (!ensure_room((h->size+511)&~511u,0) || !transfer(bank_lba(slot) + 1, buf, (h->size + 511) / 512, 0))
        return -1;
    if (crc(buf, h->size) != h->crc || !inspect(h))
        return 0;
    struct fs_node *list[MAX_NODES];
    for (unsigned i = 0; i < h->nodes; i++) {
        uint8_t *r = recs[i];
        struct fs_node *node = create_node((char *)r + 12, r[1], i ? list[r[0]] : NULL);
        if (!node || (r[1] == FS_FILE && fs_node_write(node, 0, r + 44, get(r + 8)))) {
            memset(node_pool, 0, sizeof node_pool);
            pool_index = 0;
            root = current_dir = NULL;
            fs_cache_clear();
            return -1;
        }
        list[i] = node;
        node->permissions = get(r + 4);
    }
    root = list[0];
    current_dir = find_node(root, "sharkuser");
    if (!current_dir || current_dir->type != FS_DIRECTORY)
        current_dir = root;
    active = slot;
    generation = h->gen;
    last_crc = h->crc;
    last_size = h->size;
    last_tile = h->tile;
    tiling_enabled = h->tile;
    state = "Installed: saved";
    return 1;
}
int store_restore(void) {
    if (!store_installed())
        return 0;
    state = "No matching data volume (RAM only)";
    for (int i = 0; i < ata_drive_count(); i++) {
        const ata_drive_t *d = ata_drive_get(i);
        if (!d || !d->install_supported || !ata_read_sectors(i, 0, 1, sec) || sec[510] != 0x55 ||
            sec[511] != 0xaa || sec[450] != 0xef || sec[466] != 0xda)
            continue;
        uint32_t base = get(sec + 470), first = get(sec + 454), size = get(sec + 458);
        if (first != 2048 || base < first || size != base - first || base >= d->sectors ||
            PART > d->sectors - base || get(sec + 474) != PART)
            continue;
        if (!ata_read_sectors(i, base, 1, sec) || !same(sec, "SHARKFS2", 8) || !same(sec + 8, token, 32))
            continue;
        if (disk >= 0) {
            disk = -1;
            state = "Duplicate data volumes (RAM only)";
            return 0;
        }
        disk = i;
        start = base;
    }
    if (disk < 0)
        return 0;
    for (unsigned i = 0; i < 256; i++) {
        uint32_t v = i;
        for (int j = 0; j < 8; j++)
            v = (v >> 1) ^ ((0u - (v & 1)) & 0xedb88320u);
        crc_table[i] = v;
    }
    struct header h[2];
    int valid[2];
    for (int i = 0; i < 2; i++)
        valid[i] = read_header(i, h + i);
    if (!valid[0] && !valid[1]) {
        state = "Installed: new volume";
        return 0;
    }
    int first = valid[1] == 1 && (valid[0] != 1 || (int32_t)(h[1].gen - h[0].gen) > 0);
    for (int k = 0; k < 2; k++) {
        int slot = first ^ k;
        if (valid[slot] != 1)
            continue;
        int r = load_bank(slot, h + slot);
        if (r > 0)
            return 1;
    }
    state = "Data volume unreadable (RAM only)";
    disk = -1;
    return 0;
}
static uint32_t used, count;
static int pack(struct fs_node *n, unsigned parent) {
    if (!n || !n->allocated || count >= MAX_NODES || n->content_len < 0)
        return 0;
    uint32_t len = n->type == FS_FILE ? (uint32_t)n->content_len : 0;
    if (CAP - used < 44 || len > CAP - used - 44 || len > n->content_capacity || !n->name[0])
        return 0;
    if(!ensure_room(used+44+len,used))return 0;
    uint8_t *p = buf + used;
    unsigned id = count++;
    memset(p, 0, 44);
    p[0] = parent;
    p[1] = n->type;
    put(p + 4, n->permissions);
    put(p + 8, len);
    memcpy(p + 12, n->name, 32);
    p[43] = 0;
    if (len)
        memcpy(p + 44, n->content, len);
    used += 44 + len;
    if (n->type == FS_DIRECTORY && !fat16_is_mounted(n))
        for (int i = 0; i < n->num_children; i++)
            if (!pack(n->children[i], id))
                return 0;
    return 1;
}
int store_sync(void) {
    if (disk < 0)
        return !store_installed();
    used = count = 0;
    if (!pack(root, 255)) {
        state = "Not saved: volume full or invalid tree";
        return 0;
    }
    struct header h = {0, used, 0, count, 0};
    if (!inspect(&h)) {
        state = "Not saved: invalid tree";
        return 0;
    }
    uint32_t sum = crc(buf, used);
    unsigned tile = (lite_mode || legacy_mode) ? last_tile : !!tiling_enabled;
    if (active >= 0 && sum == last_crc && used == last_size && tile == last_tile) {
        state = "Installed: saved";
        return 1;
    }
    int slot = active == 0 ? 1 : 0;
    uint32_t n = (used + 511) / 512;
    memset(buf + used, 0, n * 512 - used);
    state = "Not saved: disk write failed";
    if (!transfer(bank_lba(slot) + 1, buf, n, 1))
        return 0;
    memset(sec, 0, 512);
    put(sec, MAGIC);
    put(sec + 4, generation + 1);
    put(sec + 8, used);
    put(sec + 12, sum);
    put(sec + 16, count);
    put(sec + 20, tile);
    put(sec + 24, crc(sec, 24));
    if (!transfer(bank_lba(slot), sec, 1, 1))
        return 0;
    active = slot;
    generation++;
    last_crc = sum;
    last_size = used;
    last_tile = tile;
    state = "Installed: saved";
    serial_puts("[store] committed\n");
    return 1;
}
void store_tick(void) {
    if (disk >= 0 && uptime_ticks - tick >= 2000) {
        tick = uptime_ticks;
        store_sync();
    }
}
