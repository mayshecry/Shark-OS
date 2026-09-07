/* rndr_level.c - level data queries */
#include "rndr_level.h"

const rndr_subsector_t *rndr_point_in_subsector(const rndr_level_t *l,
                                                int32_t x, int32_t y) {
    if (!l || !l->built || l->num_subs == 0)
        return l ? &l->subs[0] : NULL;
    int ni = l->root;
    while (ni >= 0) {
        const rndr_node_t *n = &l->nodes[ni];
        int64_t c = rndr_cross64(n->dx, n->dy, x - n->x, y - n->y);
        ni = n->children[c > 0 ? 1 : 0];
    }
    return &l->subs[~ni];
}
