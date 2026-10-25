#ifndef DOOM_H
#define DOOM_H

#include <stdint.h>
#include <stdbool.h>

typedef int32_t fixed_t;
#define FRACBITS        16
#define FRACUNIT        (1 << FRACBITS)
#define FixedMul(a,b)   ((fixed_t)(((int64_t)(a)*(b))>>FRACBITS))
#define FixedDiv(a,b)   ((fixed_t)(((int64_t)(a)<<FRACBITS)/(b)))

#define ANG45           0x20000000
#define ANG90           0x40000000
#define ANG180          0x80000000
#define ANG270          0xC0000000
#define ANG1            0x00400000
#define FINEANGLES      8192
#define FINEMASK        (FINEANGLES-1)

#define DOOM_SCREEN_W   320
#define DOOM_SCREEN_H   200
#define DOOM_PALETTE_SIZE 256

extern uint8_t doom_screen[DOOM_SCREEN_H][DOOM_SCREEN_W];
extern uint32_t doom_palette[DOOM_PALETTE_SIZE];
extern int doom_scale_factor;

typedef struct { int16_t x, y; } vertex_t;

typedef struct {
    int16_t v1, v2;
    int16_t angle;
    int16_t linedef;
    int16_t side;
    int16_t offset;
} seg_t;

typedef struct {
    int16_t x, y;
    int16_t dx, dy;
    int16_t front_sector, back_sector;
} subsector_t;

typedef struct {
    int32_t x, y, dx, dy;
    int16_t front_child, back_child;
} node_t;

typedef struct {
    int16_t x, y;
    int16_t dx, dy;
    int16_t angle;
    int16_t type;
    uint16_t flags;
    int16_t sector_tag;
} linedef_t;

typedef struct {
    int16_t textureoffset;
    int16_t topheight;
    int16_t bottomheight;
    int16_t sidedef;
} sidedef_t;

typedef struct {
    int16_t floorheight;
    int16_t ceilingheight;
    char floorpic[8];
    char ceilingpic[8];
    int16_t lightlevel;
    int16_t special;
    int16_t tag;
} sector_t;

typedef struct {
    fixed_t x, y, z;
    uint32_t angle;
    int32_t pitch;
    fixed_t velocity_x, velocity_y;
    int sector;
    int health;
    int armor;
    int ammo;
    int weapon;
} player_t;

typedef enum { DOOM_MENU, DOOM_PLAYING, DOOM_QUIT } doom_state_t;

void doom_init(void);
void doom_cleanup(void);
void doom_run(void);
void doom_tick(void);
void doom_draw_frame(void);
void doom_handle_key(int key);
void doom_set_kernel_mode(void);
void doom_restore_kernel_mode(void);
bool doom_is_active(void);
void doom_set_window_rect(int x, int y, int w, int h);

#endif