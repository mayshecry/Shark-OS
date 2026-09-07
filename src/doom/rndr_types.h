/* rndr_types.h - core types for the level renderer, in the spirit of
 * cpp-doom's core/ (math, vec, radians, wad_types): fixed point helpers,
 * binary angular measure, vectors and the r_defs level structures. */
#ifndef RNDR_TYPES_H
#define RNDR_TYPES_H

#include <stdint.h>
#include <stddef.h>

/* renderer target geometry */
#define RNDR_W 320
#define RNDR_H 200

/* texture metrics */
#define RNDR_TEX_W 64
#define RNDR_TEX_H 64

#define RNDR_MAX_SECTORS 16
#define RNDR_MAX_SEGS    1024
#define RNDR_MAX_SUBS    512
#define RNDR_MAX_NODES   512

/* 16:16 fixed point, like the original engine */
#define FP_ONE 65536

static inline int32_t int_to_fp(int x) { return (int32_t)((uint32_t)x << 16); }
static inline int fp_to_int(int32_t x) { return (int)(x >> 16); }

static inline int32_t fp_mul(int32_t a, int32_t b) {
    return (int32_t)(((int64_t)a * (int64_t)b) >> 16);
}

static inline int32_t fp_div(int32_t a, int32_t b) {
    if (b == 0) return (a >= 0) ? 0x7FFFFFFF : (int32_t)0x80000001;
    if (b < 0) { a = (int32_t)(0u - (uint32_t)a); b = (int32_t)(0u - (uint32_t)b); }
    if (a < 0) {
        uint32_t ua = 0u - (uint32_t)a;
        uint32_t ub = (uint32_t)b;
        uint32_t result = (ua / ub) << 16;
        result |= (((ua % ub) << 16) / ub);
        return -(int32_t)result;
    } else {
        uint32_t ua = (uint32_t)a;
        uint32_t ub = (uint32_t)b;
        uint32_t result = (ua / ub) << 16;
        result |= (((ua % ub) << 16) / ub);
        return (int32_t)result;
    }
}

static inline int32_t fp_abs(int32_t x) {
    return (x < 0) ? (int32_t)(0u - (uint32_t)x) : x;
}

/* angles: 360deg / 2^32, wrapping by overflow, like the original */
typedef uint32_t rndr_angle_t;
#define RNDR_ANG45  0x20000000u
#define RNDR_ANG90  0x40000000u
#define RNDR_ANG180 0x80000000u

typedef struct { int32_t x, y; } rndr_vec2_t;

/* ---------------- level structures (r_defs style) ---------------- */

typedef struct {
    int16_t floorh, ceilh;
    int16_t floorpic, ceilpic;
    int16_t light;
} rndr_sector_t;

typedef struct {
    int32_t x1, y1, x2, y2;      /* fixed world coords */
    int32_t length;
    int16_t front, back;         /* sector idx; back = -1 one-sided */
    int16_t midtex, toptex, bottex;
} rndr_seg_t;

typedef struct { int16_t firstseg, numsegs; } rndr_subsector_t;

typedef struct {
    int32_t x, y, dx, dy;        /* split plane */
    int32_t children[2];         /* >=0 node idx, <0 = ~(subsector idx) */
} rndr_node_t;

typedef struct {
    rndr_sector_t sectors[RNDR_MAX_SECTORS];
    int num_sectors;
    rndr_seg_t segs[RNDR_MAX_SEGS];
    int num_segs;
    rndr_subsector_t subs[RNDR_MAX_SUBS];
    int num_subs;
    rndr_node_t nodes[RNDR_MAX_NODES];
    int num_nodes;
    int root;
    int built;
} rndr_level_t;

static inline int64_t rndr_cross64(int32_t ax, int32_t ay, int32_t bx, int32_t by) {
    return (int64_t)ax * by - (int64_t)ay * bx;
}

#endif /* RNDR_TYPES_H */
