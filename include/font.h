#ifndef BR_FONT_H
#define BR_FONT_H

#include <stdint.h>
#if defined(FONT_HOST_TEST) && !defined(BROWSER_INTERNAL_H)
typedef struct { int x, y, w, h; } w98_rect_t;
#else
#include "win98_theme.h"
#endif

/* TrueType text for the browser (src/browser/font.c).
 * Families: 0 sans, 1 serif, 2 mono; FONT_FAMILY_WEB + n = @font-face font n. */
#define FONT_FAMILY_SANS   0
#define FONT_FAMILY_SERIF  1
#define FONT_FAMILY_MONO   2
#define FONT_FAMILY_WEB    16

#define FONT_MIN_PX        6
#define FONT_MAX_PX        96
#define FONT_WEB_ARENA     (768 * 1024)     /* fetched @font-face files per page */
#define FONT_CACHE_BYTES   (384 * 1024)     /* rendered glyph bitmaps */

void br_font_init(void);
void br_font_reset_page(void);              /* forget web fonts (new document) */
void br_font_cache_flush(void);

int  br_font_face(int family, int bold);    /* -> face index used by the calls below */
int  br_font_ascent(int face, int size);
int  br_font_descent(int face, int size);
int  br_font_line_height(int face, int size);
int  br_font_text_width(int face, int size, const char* s, int len);
int  br_font_char_width(int face, int size, uint32_t cp);
int  br_font_draw(int face, int size, const char* s, int len, int x, int baseline_y, uint32_t fg, const w98_rect_t* clip);
#define FONT_DRAW_ITALIC 1
#define FONT_DRAW_BOLD   2
int  br_font_draw_ex(int face, int size, const char* s, int len, int x, int baseline_y, uint32_t fg, const w98_rect_t* clip, int flags);
int  br_font_face_is_bold(int face);
int  br_font_web_has(const char* family, int bold);
int  br_utf8_decode(const char* s, int len, uint32_t* cp);

/* Web fonts: register raw TrueType/OpenType(glyf)/WOFF data under a family. */
int  br_font_add_web(const char* family, int bold, int italic, const uint8_t* data, uint32_t len);
int  br_font_find_web(const char* family, int bold);     /* family id or -1 */
uint8_t* br_font_web_alloc(uint32_t max, uint32_t* avail);  /* scratch space inside the arena for fetching */
int  br_font_web_count(void);

#endif
