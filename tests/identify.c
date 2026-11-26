#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/drivers/ata_identify.h"
int main(void){
    uint16_t w[256]={0};ata_drive_t d={0};
    assert(!ata_decode(w,&d));memset(w,255,sizeof w);assert(!ata_decode(w,&d));
    memset(w,0,sizeof w);w[0]=0x40;w[49]=0x200;w[27]=0x4142;w[61]=4;
    assert(ata_decode(w,&d)&&d.bytes==(128ull<<20)&&d.install_supported&&!strcmp(d.model,"AB"));
    w[61]=0x8000;assert(!ata_decode(w,&d));
    w[61]=4;w[83]=0x4400;w[101]=0x8000;assert(!ata_decode(w,&d));
    w[60]=0xffff;w[61]=0xfff;assert(ata_decode(w,&d)&&d.bytes==(1ull<<40)&&!d.install_supported);
    w[0]=0x8540;assert(!ata_decode(w,&d));w[0]=0x40;w[27]=0x2020;assert(!ata_decode(w,&d));w[27]=0x4142;
    w[106]=0x5000;w[117]=2048;assert(ata_decode(w,&d)&&d.bytes==(8ull<<40)&&!d.install_supported);
    w[117]=0;assert(!ata_decode(w,&d));w[106]=0;w[255]=0xa5;assert(!ata_decode(w,&d));
    puts("IDENTIFY: valid small/large/4Kn disks, absent, packet, bad model/checksum and phantom capacities passed");
}
