#include "kernel.h"
#include "ata.h"

#define ATA_REG_DATA   0
#define ATA_REG_ERR    1
#define ATA_REG_SECCNT 2
#define ATA_REG_LBA0   3
#define ATA_REG_LBA1   4
#define ATA_REG_LBA2   5
#define ATA_REG_DRV    6
#define ATA_REG_STAT   7

#define ATA_SR_BSY  0x80
#define ATA_SR_DRQ  0x08
#define ATA_SR_DF   0x20
#define ATA_SR_ERR  0x01

#define ATA_CMD_IDENTIFY  0xEC
#define ATA_CMD_READ      0x20
#define ATA_CMD_WRITE     0x30
#define ATA_CMD_FLUSH     0xE7

#define ATA_POLL_TIMEOUT 200000

typedef struct {
    uint16_t io;
    uint16_t ctrl;
} ata_chan_t;

static const ata_chan_t ata_chans[2] = {
    { 0x1F0, 0x3F6 },
    { 0x170, 0x376 }
};

static ata_drive_t ata_drives[ATA_MAX_DRIVES];
static int ata_count = 0;

static uint8_t ata_status(const ata_drive_t* d) {
    return inb(d->io_base + ATA_REG_STAT);
}

static void ata_io_delay(const ata_chan_t* c) {
    for (int i = 0; i < 4; i++) (void)inb(c->ctrl);
}

static bool ata_wait_ready(const ata_drive_t* d) {
    for (int i = 0; i < ATA_POLL_TIMEOUT; i++) {
        uint8_t st = ata_status(d);
        if (!(st & ATA_SR_BSY)) return true;
    }
    return false;
}

static bool ata_wait_drq(const ata_drive_t* d) {
    for (int i = 0; i < ATA_POLL_TIMEOUT; i++) {
        uint8_t st = ata_status(d);
        if (st & ATA_SR_ERR || st & ATA_SR_DF) return false;
        if (st & ATA_SR_DRQ) return true;
    }
    return false;
}

static void ata_probe_slot(int chan, bool master) {
    if (ata_count >= ATA_MAX_DRIVES) return;
    const ata_chan_t* c = &ata_chans[chan];

    outb(c->ctrl, 0);
    outb(c->io + ATA_REG_DRV, master ? 0xA0 : 0xB0);
    ata_io_delay(c);

    outb(c->io + ATA_REG_SECCNT, 0);
    outb(c->io + ATA_REG_LBA0, 0);
    outb(c->io + ATA_REG_LBA1, 0);
    outb(c->io + ATA_REG_LBA2, 0);
    outb(c->io + ATA_REG_STAT, ATA_CMD_IDENTIFY);

    uint8_t st = inb(c->io + ATA_REG_STAT);
    if (st == 0) return;          /* channel not present */
    if (st == 0xFF) return;       /* floating bus: no device on this slot */

    for (int i = 0; i < ATA_POLL_TIMEOUT; i++) {
        st = inb(c->io + ATA_REG_STAT);
        if (!(st & ATA_SR_BSY)) break;
    }

    if (st == 0 || st == 0xFF) return;

    if (st & ATA_SR_ERR) {
        uint8_t l1 = inb(c->io + ATA_REG_LBA1);
        uint8_t l2 = inb(c->io + ATA_REG_LBA2);
        if (l1 == 0x14 && l2 == 0xEB) return;
        if (l1 == 0x69 && l2 == 0x96) return;
        if (inb(c->io + ATA_REG_SECCNT) == 0 && l1 == 0 && l2 == 0) return;
    }

    if (!(st & ATA_SR_DRQ)) return;

    static uint16_t id[256];
    for (int i = 0; i < 256; i++) id[i] = inw(c->io + ATA_REG_DATA);

    uint32_t lba28 = (uint32_t)id[60] | ((uint32_t)id[61] << 16);
    if (lba28 == 0) return;
    if (lba28 == 0xFFFFFFFFu) return;   /* garbage identify from floating bus */

    ata_drive_t* d = &ata_drives[ata_count];
    d->present = true;
    d->master = master;
    d->io_base = c->io;
    d->ctrl_base = c->ctrl;
    d->sectors = lba28;
    d->bytes = (uint64_t)lba28 * 512u;

    for (int i = 0; i < 40; i++) {
        uint16_t w = id[27 + i / 2];
        d->model[i] = (char)((i & 1) ? (w & 0xFF) : (w >> 8));
    }
    d->model[40] = '\0';

    int last = 39;
    while (last >= 0 && (d->model[last] == ' ' || d->model[last] == '\0')) {
        d->model[last] = '\0';
        last--;
    }

    ata_count++;
}

void ata_init(void) {
    ata_count = 0;
    for (int i = 0; i < ATA_MAX_DRIVES; i++) ata_drives[i].present = false;
    for (int c = 0; c < 2; c++) {
        ata_probe_slot(c, true);
        ata_probe_slot(c, false);
    }
}

int ata_drive_count(void) {
    return ata_count;
}

const ata_drive_t* ata_drive_get(int idx) {
    if (idx < 0 || idx >= ata_count) return NULL;
    return &ata_drives[idx];
}

static bool ata_transfer(int drive, uint32_t lba, uint8_t count,
                         uint8_t* buf, bool write) {
    const ata_drive_t* d = ata_drive_get(drive);
    if (!d || count == 0) return false;
    if (lba + count > d->sectors) return false;

    if (!ata_wait_ready(d)) return false;

    outb(d->io_base + ATA_REG_DRV,
         (uint8_t)(0xE0 | (d->master ? 0x00 : 0x10) | ((lba >> 24) & 0x0F)));
    ata_io_delay(&ata_chans[d->io_base == ata_chans[0].io ? 0 : 1]);
    if (!ata_wait_ready(d)) return false;   /* BSY must clear after select */
    outb(d->io_base + ATA_REG_ERR, 0);
    outb(d->io_base + ATA_REG_SECCNT, count);
    outb(d->io_base + ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(d->io_base + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(d->io_base + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(d->io_base + ATA_REG_STAT, write ? ATA_CMD_WRITE : ATA_CMD_READ);

    uint16_t* w = (uint16_t*)buf;
    for (int s = 0; s < count; s++) {
        if (!ata_wait_drq(d)) return false;
        if (write) {
            for (int i = 0; i < 256; i++) outw(d->io_base + ATA_REG_DATA, w[i]);
        } else {
            for (int i = 0; i < 256; i++) w[i] = inw(d->io_base + ATA_REG_DATA);
        }
        w += 256;
    }

    /* Final status: catch errors the per-sector DRQ poll can miss. */
    {
        uint8_t st = ata_status(d);
        if (st & (ATA_SR_ERR | ATA_SR_DF)) return false;
    }

    if (write) {
        outb(d->io_base + ATA_REG_STAT, ATA_CMD_FLUSH);
        ata_wait_ready(d);
    }
    return true;
}

bool ata_read_sectors(int drive, uint32_t lba, uint8_t count, uint8_t* dst) {
    return ata_transfer(drive, lba, count, dst, false);
}

bool ata_write_sectors(int drive, uint32_t lba, uint8_t count,
                       const uint8_t* src) {
    return ata_transfer(drive, lba, count, (uint8_t*)src, true);
}
