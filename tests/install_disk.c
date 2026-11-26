#include "ata.h"
#include "install.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define SECTORS 2181
static uint8_t pack[16 + SECTORS * 512], drive[(SECTORS + 3) * 512];
static ata_drive_t disk = {.present = true, .sectors = SECTORS + 3, .install_supported = true};
static unsigned writes, last_lba;
static bool fail_write, corrupt, fail_read;
const ata_drive_t *ata_drive_get(int i) { return i == 2 ? &disk : NULL; }
bool ata_read_sectors(int i, uint64_t l, uint8_t n, uint8_t *p) {
    assert(i == 2 && n && l + n <= disk.sectors);
    if (fail_read)
        return false;
    memcpy(p, drive + 512 * l, n * 512);
    if (writes && corrupt)
        p[0] ^= 1;
    return true;
}
bool ata_write_sectors(int i, uint64_t l, uint8_t n, const uint8_t *p) {
    assert(i == 2 && n && l + n <= disk.sectors);
    writes++;
    last_lba = l;
    if (fail_write)
        return false;
    memcpy(drive + 512 * l, p, n * 512);
    return true;
}
static void progress(const char *s, unsigned percent) { assert(s && percent <= 100); }
static void put32(uint8_t *p, uint32_t x) {
    for (int i = 0; i < 4; i++)
        p[i] = x >> (i * 8);
}
static void checksum(void) {
    uint32_t crc = ~0u;
    for (size_t i = 16; i < sizeof pack; i++) {
        crc ^= pack[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ ((0u - (crc & 1)) & 0xedb88320);
    }
    put32(pack + 12, ~crc);
}
int main(void) {
    uint8_t *m = pack + 16;
    memcpy(pack, "SHARKIMG", 8);
    put32(pack + 8, sizeof pack - 16);
    m[510] = 0x55;
    m[511] = 0xaa;
    m[450] = 0xef;
    put32(m + 454, 2048);
    put32(m + 458, SECTORS - 2048);
    m[(SECTORS - 1) * 512] = 17;
    checksum();
    install_image_set(pack, sizeof pack - 1);
    assert(!install_image_ready());
    install_image_set(pack, sizeof pack);
    assert(install_image_ready());
    assert(strstr(install_disk(0, progress), "Unsupported") && !writes);
    disk.sectors = SECTORS - 1;
    assert(strstr(install_disk(2, progress), "capacity") && !writes);
    disk.sectors = SECTORS + 3;
    pack[1024] ^= 1;
    assert(strstr(install_disk(2, progress), "checksum") && !writes);
    pack[1024] ^= 1;
    drive[sizeof drive - 1] = 42;
    assert(strstr(install_disk(2, progress), "tail") && !writes);
    drive[sizeof drive - 1] = 0;
    drive[40000] = 42;
    assert(strstr(install_disk(2, progress), "contains data") && !writes);
    drive[40000] = 0;
    fail_read = true;
    assert(strstr(install_disk(2, progress), "unreadable") && !writes);
    fail_read = false;
    fail_write = true;
    assert(strstr(install_disk(2, progress), "Write failed") && last_lba != 0);
    fail_write = false;
    writes = 0;
    corrupt = true;
    assert(strstr(install_disk(2, progress), "Verification failed") && last_lba != 0);
    corrupt = false;
    memset(drive, 0, sizeof drive);
    writes = 0;
    drive[SECTORS * 512] = 37;
    assert(strstr(install_disk(2, progress), "Installed.") && last_lba == 0);
    assert(!memcmp(drive, m, SECTORS * 512) && drive[SECTORS * 512] == 37);
    uint8_t ref[16+4*SECTORS+512]={0};memcpy(ref,pack,16);memcpy(ref,"SHARKRF1",8);
    put32(ref+16,1);memcpy(ref+16+4*SECTORS,m,512);uint8_t *tail=ref+16+4*(SECTORS-1);
    uint8_t *ro=m+512*(SECTORS-1);put32(tail,0x80000000u);
    install_ref_set(ref,sizeof ref,ro,511);assert(!install_image_ready());
    put32(tail,2);install_ref_set(ref,sizeof ref,ro,512);assert(!install_image_ready());
    put32(tail,0x80000000u);install_ref_set(ref,sizeof ref,ro,512);assert(install_image_ready());
    memset(drive,0,sizeof drive);writes=0;ro[0]^=1;assert(strstr(install_disk(2,progress),"checksum")&&!writes);ro[0]^=1;
    assert(strstr(install_disk(2,progress),"Installed.")&&last_lba==0&&!memcmp(drive,m,SECTORS*512));
    puts("Reference bounds, literal/zero/kernel sectors and checksum-before-write passed");
    puts("Installer preflight, I/O failures, readback and boot-record commit passed");
}
