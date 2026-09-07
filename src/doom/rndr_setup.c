/* rndr_setup.c - level construction: grid extraction and BSP build.
 * The original engine gets segs/subsectors/nodes from WAD lumps built
 * by an offline node builder; here the same structures are built once
 * at init from the extracted geometry, then rendering walks the tree. */
#include "rndr_setup.h"

static rndr_seg_t tmp_segs[RNDR_MAX_SEGS];
static int tmp_num_segs;
static rndr_subsector_t tmp_subs[RNDR_MAX_SUBS];
static int tmp_num_subs;
static rndr_node_t tmp_nodes[RNDR_MAX_NODES];
static int tmp_num_nodes;

/* front sector must be on the left of (v1 -> v2); renderer treats
 * cross(dir, p - v1) < 0 as the front side. */
static void add_line(int32_t ax, int32_t ay, int32_t bx, int32_t by,
                     int front, int back, int mid, int top, int bot) {
    if (tmp_num_segs >= RNDR_MAX_SEGS) return;
    rndr_seg_t *sg = &tmp_segs[tmp_num_segs++];
    sg->x1 = ax; sg->y1 = ay; sg->x2 = bx; sg->y2 = by;
    int32_t dx = bx - ax, dy = by - ay;
    sg->length = fp_abs(dx) + fp_abs(dy); /* axis aligned */
    sg->front = (int16_t)front;
    sg->back = (int16_t)back;
    sg->midtex = (int16_t)mid;
    sg->toptex = (int16_t)top;
    sg->bottex = (int16_t)bot;
}

static int cell_open(const rndr_map_source_t *src, int x, int y) {
    return x >= 0 && x < src->map_w && y >= 0 && y < src->map_h &&
           src->grid[y * src->map_w + x] == 0 &&
           src->cell_sector[y * src->map_w + x] >= 0;
}

/* canonical front side for two-sided lines: higher ceiling, then higher
 * floor, then lower index (stable, generated exactly once) */
static int is_front_side(const rndr_map_source_t *src, int a, int b) {
    if (src->sector_ceilh[a] != src->sector_ceilh[b])
        return src->sector_ceilh[a] > src->sector_ceilh[b];
    if (src->sector_floorh[a] != src->sector_floorh[b])
        return src->sector_floorh[a] > src->sector_floorh[b];
    return a < b;
}

/* Merge a unit-length edge into the seg list: extend an existing
 * collinear seg when sectors/textures match (grid -> DOOM linedefs). */
static void add_unit_edge(int32_t ax, int32_t ay, int32_t bx, int32_t by,
                          int front, int back, int mid, int top, int bot) {
    for (int i = 0; i < tmp_num_segs; i++) {
        rndr_seg_t *sg = &tmp_segs[i];
        if (sg->front != front || sg->back != back || sg->midtex != mid ||
            sg->toptex != top || sg->bottex != bot)
            continue;
        /* only merge collinear, same-orientation edges */
        int e_vert = (ax == bx), s_vert = (sg->x1 == sg->x2);
        if (e_vert != s_vert) continue;
        if (e_vert) {
            if (sg->x1 != ax) continue;
            int ed = (by > ay) ? 1 : -1;
            int sd = (sg->y2 > sg->y1) ? 1 : -1;
            if (ed != sd) continue;
        } else {
            if (sg->y1 != ay) continue;
            int ed = (bx > ax) ? 1 : -1;
            int sd = (sg->x2 > sg->x1) ? 1 : -1;
            if (ed != sd) continue;
        }
        if (sg->x2 == ax && sg->y2 == ay) { sg->x2 = bx; sg->y2 = by; return; }
        if (sg->x1 == bx && sg->y1 == by) { sg->x1 = ax; sg->y1 = ay; return; }
    }
    add_line(ax, ay, bx, by, front, back, mid, top, bot);
}

static void extract_level(rndr_level_t *l, const rndr_map_source_t *src) {
    tmp_num_segs = 0; tmp_num_subs = 0; tmp_num_nodes = 0;

    for (int i = 0; i < src->num_sectors && i < RNDR_MAX_SECTORS; i++) {
        rndr_sector_t *s = &l->sectors[i];
        s->floorh = src->sector_floorh[i];
        s->ceilh = src->sector_ceilh[i];
        s->floorpic = src->sector_floorpic[i];
        s->ceilpic = src->sector_ceilpic[i];
        s->light = src->sector_light[i];
    }
    l->num_sectors = (src->num_sectors < RNDR_MAX_SECTORS)
                         ? src->num_sectors : RNDR_MAX_SECTORS;

    for (int y = 0; y < src->map_h; y++) {
        for (int x = 0; x < src->map_w; x++) {
            if (!cell_open(src, x, y)) continue;
            int s = src->cell_sector[y * src->map_w + x];
            int32_t cell = src->cell_size;
            int32_t gx = int_to_fp(x * fp_to_int(cell));
            int32_t gy = int_to_fp(y * fp_to_int(cell));
            int32_t ex = gx + cell;
            int32_t ey = gy + cell;

            /* one-sided walls against solid cells */
            if (x + 1 >= src->map_w || src->grid[y * src->map_w + x + 1] > 0 ||
                src->cell_sector[y * src->map_w + x + 1] < 0) {
                int wt = 0;
                if (x + 1 < src->map_w && src->grid[y * src->map_w + x + 1] > 0)
                    wt = src->grid[y * src->map_w + x + 1] - 1;
                add_unit_edge(ex, ey, ex, gy, s, -1, wt, -1, -1);
            }
            if (x - 1 < 0 || src->grid[y * src->map_w + x - 1] > 0 ||
                src->cell_sector[y * src->map_w + x - 1] < 0) {
                int wt = 0;
                if (x - 1 >= 0 && src->grid[y * src->map_w + x - 1] > 0)
                    wt = src->grid[y * src->map_w + x - 1] - 1;
                add_unit_edge(gx, gy, gx, ey, s, -1, wt, -1, -1);
            }
            if (y + 1 >= src->map_h || src->grid[(y + 1) * src->map_w + x] > 0 ||
                src->cell_sector[(y + 1) * src->map_w + x] < 0) {
                int wt = 0;
                if (y + 1 < src->map_h && src->grid[(y + 1) * src->map_w + x] > 0)
                    wt = src->grid[(y + 1) * src->map_w + x] - 1;
                add_unit_edge(gx, ey, ex, ey, s, -1, wt, -1, -1);
            }
            if (y - 1 < 0 || src->grid[(y - 1) * src->map_w + x] > 0 ||
                src->cell_sector[(y - 1) * src->map_w + x] < 0) {
                int wt = 0;
                if (y - 1 >= 0 && src->grid[(y - 1) * src->map_w + x] > 0)
                    wt = src->grid[(y - 1) * src->map_w + x] - 1;
                add_unit_edge(ex, gy, gx, gy, s, -1, wt, -1, -1);
            }

            /* two-sided lines between different sectors (steps/doorways) */
            if (cell_open(src, x + 1, y)) {
                int s2 = src->cell_sector[y * src->map_w + x + 1];
                if (s2 != s && is_front_side(src, s, s2))
                    add_unit_edge(ex, ey, ex, gy, s, s2, -1, 4, 4);
            }
            if (cell_open(src, x, y + 1)) {
                int s2 = src->cell_sector[(y + 1) * src->map_w + x];
                if (s2 != s && is_front_side(src, s, s2))
                    add_unit_edge(gx, ey, ex, ey, s, s2, -1, 4, 4);
            }
        }
    }

    for (int i = 0; i < tmp_num_segs; i++) {
        rndr_seg_t *sg = &tmp_segs[i];
        sg->length = fp_abs(sg->x2 - sg->x1) + fp_abs(sg->y2 - sg->y1);
    }
}

/* ------------------------- BSP builder ------------------------- */

static int bsp_arena[RNDR_MAX_SEGS * 4];
static int bsp_arena_used;

static int bsp_alloc(int n) {
    if (bsp_arena_used + n > RNDR_MAX_SEGS * 4) return -1;
    int p = bsp_arena_used;
    bsp_arena_used += n;
    return p;
}

static int bsp_make_leaf(const int *list, int count) {
    if (tmp_num_subs >= RNDR_MAX_SUBS) return 0;
    int first = tmp_num_segs;
    for (int i = 0; i < count && first + i < RNDR_MAX_SEGS; i++) {
        /* segs already live in tmp_segs; leaf records their indices
         * compactly by copying to the tail region */
        tmp_segs[first + i] = tmp_segs[list[i]];
    }
    tmp_num_segs = first + count;
    tmp_subs[tmp_num_subs].firstseg = (int16_t)first;
    tmp_subs[tmp_num_subs].numsegs = (int16_t)count;
    return ~(tmp_num_subs++);
}

/* Classify a seg against a split line. Returns 1 when the seg truly
 * straddles the line and must be split. Endpoints exactly on the line
 * never trigger a split - they are classified to the other endpoint's
 * side (collinear segs go front). Scoring and partitioning must use
 * this identical logic or the builder loops on slivers. */
static int bsp_classify(const rndr_seg_t *l, const rndr_seg_t *sg,
                        int *s1, int *s2) {
    int64_t c1 = rndr_cross64(l->x2 - l->x1, l->y2 - l->y1,
                              sg->x1 - l->x1, sg->y1 - l->y1);
    int64_t c2 = rndr_cross64(l->x2 - l->x1, l->y2 - l->y1,
                              sg->x2 - l->x1, sg->y2 - l->y1);
    *s1 = (c1 > 0) ? 1 : 0;
    *s2 = (c2 > 0) ? 1 : 0;
    if (c1 == 0 && c2 == 0) { *s1 = 0; *s2 = 0; return 0; }
    if (c1 == 0) { *s1 = *s2; return 0; }
    if (c2 == 0) { *s2 = *s1; return 0; }
    return (*s1 != *s2) ? 1 : 0;
}

static int bsp_build(const int *list, int count, int depth) {
    if (count <= 1 || depth > 24 || tmp_num_nodes >= RNDR_MAX_NODES - 1)
        return bsp_make_leaf(list, count);

    /* choose split line minimizing splits; candidates that would leave
     * one side empty are useless partitions and get rejected */
    int best = -1, bestscore = 0x7FFFFFFF;
    for (int i = 0; i < count; i++) {
        const rndr_seg_t *sp = &tmp_segs[list[i]];
        int splits = 0;
        int f = 0, b = 0;
        for (int j = 0; j < count; j++) {
            if (i == j) continue;
            int s1, s2;
            if (bsp_classify(sp, &tmp_segs[list[j]], &s1, &s2)) { splits++; f++; b++; }
            else if (s1 == 0) f++;
            else b++;
        }
        if (f == 0 || b == 0) continue;
        int diff = f - b; if (diff < 0) diff = -diff;
        int score = splits * 8 + diff;
        if (score < bestscore) { bestscore = score; best = i; }
    }
    if (best < 0) return bsp_make_leaf(list, count);

    const rndr_seg_t split = tmp_segs[list[best]];

    int fl = bsp_alloc(count + 1);
    int bl = bsp_alloc(count + 1);
    if (fl < 0 || bl < 0) return bsp_make_leaf(list, count);
    int nf = 0, nb = 0;

    for (int i = 0; i < count; i++) {
        rndr_seg_t sg = tmp_segs[list[i]];
        int s1, s2;
        if (!bsp_classify(&split, &sg, &s1, &s2)) {
            if (s1 == 0) bsp_arena[fl + nf++] = list[i];
            else bsp_arena[bl + nb++] = list[i];
        } else {
            int64_t dxs = split.x2 - split.x1, dys = split.y2 - split.y1;
            int64_t den = rndr_cross64(sg.x2 - sg.x1, sg.y2 - sg.y1,
                                       (int32_t)dxs, (int32_t)dys);
            if (den == 0) { bsp_arena[fl + nf++] = list[i]; continue; }
            int64_t num = rndr_cross64(split.x1 - sg.x1, split.y1 - sg.y1,
                                       (int32_t)dxs, (int32_t)dys);
            /* t = num/den in 16.16, computed without shifting negatives */
            int neg = (num < 0) != (den < 0);
            uint64_t un = (uint64_t)(num < 0 ? 0 - (uint64_t)num : (uint64_t)num);
            uint64_t ud = (uint64_t)(den < 0 ? 0 - (uint64_t)den : (uint64_t)den);
            uint64_t r = (un << 16) / ud;
            if (r > 0x7FFFFFFFu) r = 0x7FFFFFFFu;
            int32_t t = neg ? (int32_t)(0u - (uint32_t)r) : (int32_t)r;
            if (t <= 0) {          /* v1 on the line: body lies on v2's side */
                if (s2 == 0) bsp_arena[fl + nf++] = list[i];
                else bsp_arena[bl + nb++] = list[i];
                continue;
            }
            if (t >= FP_ONE) {     /* v2 on the line: body lies on v1's side */
                if (s1 == 0) bsp_arena[fl + nf++] = list[i];
                else bsp_arena[bl + nb++] = list[i];
                continue;
            }
            int32_t ix = sg.x1 + fp_mul(t, sg.x2 - sg.x1);
            int32_t iy = sg.y1 + fp_mul(t, sg.y2 - sg.y1);
            if (tmp_num_segs + 2 > RNDR_MAX_SEGS) { bsp_arena[fl + nf++] = list[i]; continue; }
            int ia = tmp_num_segs, ib = tmp_num_segs + 1;
            tmp_num_segs += 2;
            tmp_segs[ia] = sg; tmp_segs[ia].x2 = ix; tmp_segs[ia].y2 = iy;
            tmp_segs[ib] = sg; tmp_segs[ib].x1 = ix; tmp_segs[ib].y1 = iy;
            if (s1 == 0) { bsp_arena[fl + nf++] = ia; bsp_arena[bl + nb++] = ib; }
            else { bsp_arena[bl + nb++] = ia; bsp_arena[fl + nf++] = ib; }
        }
    }

    if (nf == 0) return bsp_build(bsp_arena + bl, nb, depth + 1);
    if (nb == 0) return bsp_build(bsp_arena + fl, nf, depth + 1);

    int ni = tmp_num_nodes++;
    tmp_nodes[ni].x = split.x1;
    tmp_nodes[ni].y = split.y1;
    tmp_nodes[ni].dx = split.x2 - split.x1;
    tmp_nodes[ni].dy = split.y2 - split.y1;
    tmp_nodes[ni].children[0] = bsp_build(bsp_arena + fl, nf, depth + 1);
    tmp_nodes[ni].children[1] = bsp_build(bsp_arena + bl, nb, depth + 1);
    return ni;
}

void rndr_build_level(rndr_level_t *l, const rndr_map_source_t *src) {
    if (!l || !src || l->built) return;

    extract_level(l, src);

    bsp_arena_used = 0;
    int root_list = bsp_alloc(tmp_num_segs);
    if (root_list >= 0 && tmp_num_segs > 0) {
        for (int i = 0; i < tmp_num_segs; i++)
            bsp_arena[root_list + i] = i;
        int n0 = tmp_num_segs;
        l->root = bsp_build(bsp_arena + root_list, n0, 0);
    }

    /* publish the built level */
    l->num_segs = tmp_num_segs;
    for (int i = 0; i < tmp_num_segs; i++)
        l->segs[i] = tmp_segs[i];
    l->num_subs = tmp_num_subs;
    for (int i = 0; i < tmp_num_subs; i++)
        l->subs[i] = tmp_subs[i];
    l->num_nodes = tmp_num_nodes;
    for (int i = 0; i < tmp_num_nodes; i++)
        l->nodes[i] = tmp_nodes[i];
    l->built = 1;
}
