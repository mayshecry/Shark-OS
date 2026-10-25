#ifndef SHARKOS_ATA_H
#define SHARKOS_ATA_H

#include <stdint.h>
#include <stdbool.h>

#define ATA_MAX_DRIVES 4

typedef struct {
    bool present;
    bool master;
    uint16_t io_base;
    uint16_t ctrl_base;
    char model[41];
    uint32_t sectors;
    uint64_t bytes;
} ata_drive_t;

void ata_init(void);
int ata_drive_count(void);
const ata_drive_t* ata_drive_get(int idx);
bool ata_read_sectors(int drive, uint32_t lba, uint8_t count, uint8_t* dst);
bool ata_write_sectors(int drive, uint32_t lba, uint8_t count,
                       const uint8_t* src);

#endif
