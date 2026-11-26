#ifndef SHARK_ATA_IDENTIFY_H
#define SHARK_ATA_IDENTIFY_H
#include "ata.h"
static int ata_decode(const uint16_t id[256], ata_drive_t *d) {
    if (!id[0] || (id[0] & 0x8004) || !(id[49] & 0x200) || id[49] == 0xffff) return 0;
    if ((id[255] & 255) == 0xa5) {
        unsigned sum=0;
        for (unsigned i=0;i<256;i++) sum+=(id[i]&255)+(id[i]>>8);
        if (sum&255) return 0;
    }
    uint32_t small=id[60]|((uint32_t)id[61]<<16);
    if (!small || small>0x0fffffff) return 0;
    uint64_t n=small;
    if ((id[83]&0xc400)==0x4400) {
        n=id[100]|((uint64_t)id[101]<<16)|((uint64_t)id[102]<<32);
        if (id[103] || !n || small!=(n>0x0fffffff?0x0fffffff:n)) return 0;
    }
    unsigned sector=512;
    if ((id[106]&0xd000)==0x5000) {
        unsigned words=id[117]|((uint32_t)id[118]<<16);
        if (words<=256 || words>32768 || (words&(words-1))) return 0;
        sector=words*2;
    }
    int last=-1;
    for (unsigned i=0;i<40;i++) {
        unsigned c=(id[27+i/2] >> ((i&1)?0:8))&255;
        if (c && (c<32 || c>126)) return 0;
        if(c && c!=' ')last=i;
        d->model[i]=c?c:' ';
    }
    if(last<0)return 0;
    d->model[last+1]=0;
    d->sectors=n; d->bytes=n*sector; d->sector_bytes=sector;
    d->install_supported=sector==512 && n<=0x0fffffff;
    return 1;
}
#endif
