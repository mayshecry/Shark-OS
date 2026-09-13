
#include "rndr_clip.h"

void rndr_clip_reset(rndr_clip_t *c, int view_h) {
    for (int x = 0; x < RNDR_W; x++) {
        c->ceiling[x] = -1;
        c->floor[x] = view_h;
    }
    for (int y = 0; y < RNDR_H; y++)
        for (int x = 0; x < RNDR_W; x++)
            c->wallmask[y][x] = 0;
}
