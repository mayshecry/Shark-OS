#include "ata.h"
#include "install.h"
#include <stddef.h>
static const uint8_t *image;
static uint32_t length, expected;
static uint8_t block[128 * 512] __attribute__((aligned(16)));
static uint32_t le32(const uint8_t *p) {
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static const uint8_t *refs,*literal,*rom;
static uint8_t source_block[128*512] __attribute__((aligned(16)));
static const uint8_t *source_at(uint32_t lba,unsigned n){
    if(!refs)return image+(size_t)lba*512;
    for(unsigned i=0;i<n;i++){
        uint32_t v=le32(refs+4*(lba+i));uint8_t *dst=source_block+512*i;
        const uint8_t *s=(v>>31)?rom+(size_t)(v&0x7fffffffu)*512:v?literal+(size_t)(v-1)*512:NULL;
        if(s)for(unsigned j=0;j<512;j++)dst[j]=s[j];else for(unsigned j=0;j<512;j++)dst[j]=0;
    }return source_block;
}
static int mbr_valid(const uint8_t *m,uint32_t bytes){
    uint32_t sectors=bytes/512,esp=le32(m+458),data_lba=le32(m+470);
    if(m[510]!=0x55||m[511]!=0xaa||m[450]!=0xef||le32(m+454)!=2048||sectors<=2048||!esp||esp>sectors-2048)return 0;
    if(m[466])return m[466]==0xda && data_lba==2048+esp && le32(m+474)==16385 && sectors-data_lba==16385;
    return esp==sectors-2048;
}
void install_image_set(const void *data,uint32_t size){
    const uint8_t *p=data;image=refs=NULL;
    if(!p||size<528||(size-16)%512||le32(p+8)!=size-16)return;
    for(int i=0;i<8;i++)if(p[i]!="SHARKIMG"[i])return;
    if(!mbr_valid(p+16,size-16))return;
    image=p+16;length=size-16;expected=le32(p+12);
}
void install_ref_set(const void *data,uint32_t size,const void *readonly,uint32_t ro_bytes){
    const uint8_t *p=data;image=refs=NULL;if(!p||size<16)return;
    for(int i=0;i<8;i++)if(p[i]!="SHARKRF1"[i]){install_image_set(data,size);return;}
    uint32_t bytes=le32(p+8),sectors=bytes/512;
    if(!bytes||bytes%512||sectors>(size-16)/4)return;
    uint32_t remain=size-16-4*sectors;if(remain%512)return;
    for(uint32_t i=0;i<sectors;i++){uint32_t v=le32(p+16+4*i);
        if((v>>31)?(!readonly || (v&0x7fffffffu)>=ro_bytes/512):v>remain/512)return;}
    refs=p+16;literal=refs+4*sectors;rom=readonly;expected=le32(p+12);length=bytes;
    if(mbr_valid(source_at(0,1),bytes))image=p;else refs=NULL;
}
uint32_t install_size(void) { return image ? length : 0; }
int install_image_ready(void) { return image != NULL; }
static int blank(size_t n) {
    for (size_t i = 0; i < n; i++)
        if (block[i])
            return 0;
    return 1;
}
static int equal(const uint8_t *src, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (block[i] != src[i])
            return 0;
    return 1;
}
const char *install_disk(int idx, void (*progress)(const char *, unsigned)) {
    const ata_drive_t *d = ata_drive_get(idx);
    if (!image)
        return "Installer image unavailable.";
    uint32_t sectors = length / 512;
    if (!d || !d->install_supported || d->sectors < sectors)
        return "Unsupported disk or insufficient capacity.";
    progress("Checking install media", 0);
    uint32_t table[256], crc = ~0u;
    for (unsigned i = 0; i < 256; i++) {
        uint32_t v = i;
        for (int j = 0; j < 8; j++)
            v = (v >> 1) ^ ((0u - (v & 1u)) & 0xedb88320u);
        table[i] = v;
    }
    for(uint32_t lba=0;lba<sectors;){unsigned n=sectors-lba>128?128:sectors-lba;
        const uint8_t *src=source_at(lba,n);for(unsigned i=0;i<n*512;i++)crc=(crc>>8)^table[(crc^src[i])&255];lba+=n;}
    if (~crc != expected)
        return "Image checksum failed. No data written.";
    if (!ata_read_sectors(idx, d->sectors - 1, 1, block) || !blank(512))
        return "Disk tail unreadable or in use. No data written.";
    for (int pass = 0; pass < 2; pass++) {
        unsigned last = 101;
        for (uint32_t lba = pass ? 1 : 0; lba < sectors;) {
            uint8_t n = sectors - lba > 128 ? 128 : sectors - lba;
            unsigned percent = (uint64_t)lba * 100 / sectors;
            if (percent != last) {
                progress(pass ? "Writing and verifying" : "Checking unused space", percent);
                last = percent;
            }
            const uint8_t *src = pass?source_at(lba,n):NULL;
            if (pass && !ata_write_sectors(idx, lba, n, src))
                return "Write failed. Target is incomplete.";
            if (!ata_read_sectors(idx, lba, n, block))
                return pass ? "Read-back failed. Target is incomplete."
                            : "Read failed. No data written.";
            if (!pass && !blank(n * 512u))
                return "Target contains data. No data written.";
            if (pass && !equal(src, n * 512u))
                return "Verification failed. Target is incomplete.";
            lba += n;
        }
    }
    const uint8_t *mbr=source_at(0,1);
    if (!ata_write_sectors(idx, 0, 1, mbr) || !ata_read_sectors(idx, 0, 1, block) ||
        !equal(mbr, 512))
        return "Boot record verification failed.";
    return "Installed. Remove the live media and boot the disk.";
}
