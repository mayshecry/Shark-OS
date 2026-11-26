#ifndef SHARK_AHCI_H
#define SHARK_AHCI_H
#include "ata.h"
int ahci_probe(uint8_t bus,uint8_t slot,uint8_t fn,ata_drive_t *out,int room);
bool ahci_io(const ata_drive_t *d,uint64_t lba,uint8_t count,uint8_t *buf,bool write);
#endif
