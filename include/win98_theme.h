/* win98_theme.h - Windows 98 system metrics, palette and glyph bitmaps.
 *
 * Every colour used by the desktop shell must come from this header.
 * Colours are 0xAARRGGBB; alpha is always 0xFF except where noted.
 */

#ifndef WIN98_THEME_H
#define WIN98_THEME_H

#include <stdbool.h>
#include <stdint.h>

/* ---------------------------------------------------------------- palette */

#define W98_DESKTOP          0xFF008080u   /* desktop teal                     */
#define W98_DESKTOP_DARK     0xFF007070u   /* teal dither partner              */
#define W98_BTNFACE          0xFFC0C0C0u   /* window bodies, taskbar, menus    */
#define W98_BTNHILITE        0xFFFFFFFFu   /* outer top/left highlight         */
#define W98_BTNLIGHT         0xFFDFDFDFu   /* inner top/left highlight         */
#define W98_BTNSHADOW        0xFF808080u   /* inner bottom/right shadow        */
#define W98_BTNDKSHADOW      0xFF000000u   /* outer bottom/right shadow        */
#define W98_BTNTEXT          0xFF000000u
#define W98_GRAYTEXT         0xFF808080u   /* disabled                         */
#define W98_ACTIVE_TITLE     0xFF000080u   /* navy, focused titlebar           */
#define W98_ACTIVE_TITLE2    0xFF1084D0u   /* gradient right stop              */
#define W98_INACTIVE_TITLE   0xFF808080u
#define W98_INACTIVE_TITLE2  0xFFB5B5B5u
#define W98_TITLETEXT        0xFFFFFFFFu
#define W98_TITLETEXT_INACT  0xFFD4D0C8u
#define W98_HIGHLIGHT        0xFF000080u   /* selection background             */
#define W98_HIGHLIGHTTEXT    0xFFFFFFFFu
#define W98_WINDOW           0xFFFFFFFFu   /* edit/list client areas           */
#define W98_WINDOWTEXT       0xFF000000u
#define W98_CAPTION_START    0xFF000040u   /* start-menu sidebar top           */
#define W98_CAPTION_END      0xFF0000A0u   /* start-menu sidebar bottom        */
#define W98_MENU_BG          0xFFC0C0C0u

/* Win98 draws a gradient caption only when the "Active title bar gradient"
 * option is on. Off is the factory default, so that is what we ship. */
#define W98_TITLE_GRADIENT   0

/* Scratch buffer size for the horizontal gradient ramp. Wider than any
 * titlebar or start-menu sidebar this shell can produce. */
#define W98_RAMP_MAX         2048

/* --------------------------------------------------------------- metrics */

#define W98_TEXT_SCALE       2      /* desktop UI text: 6x12 advance          */
#define W98_CELL_W(s)        (6 * (s))
#define W98_CELL_H(s)        (12 * (s) / 2)   /* scale 1 -> 8, scale 2 -> 12   */

#define W98_TITLEBAR_H       18
#define W98_BORDER_W         3      /* outer bevel ring                       */
#define W98_FRAME_PAD        1      /* BTNFACE gap between ring and titlebar  */
#define W98_CLIENT_INSET     6      /* total non-client inset, left/right     */
#define W98_CLIENT_TOP       25     /* y offset of client area inside window  */
#define W98_CLIENT_BOTTOM    6

#define W98_CAPBTN_W         16
#define W98_CAPBTN_H         14
#define W98_CAPBTN_GAP       2
#define W98_CAPBTN_MARGIN    2

#define W98_TASKBAR_H        28
#define W98_STARTBTN_W       58
#define W98_STARTBTN_H       24
#define W98_TASKBTN_W        160
#define W98_TASKBTN_H        24
#define W98_TRAY_H           22

#define W98_MENU_SIDEBAR_W   24
#define W98_MENU_ITEM_H      26
#define W98_MENU_ICON        16

#define W98_DESKTOP_ICON     32
#define W98_ICON_CELL_W      80
#define W98_ICON_CELL_H      78
#define W98_ICON_ORIGIN_X    8
#define W98_ICON_ORIGIN_Y    8

/* ------------------------------------------------- caption-button glyphs */
/* 10x10 bitmaps, bit 9 (0x200) is the leftmost pixel of each row. */

#define W98_GLYPH_W 10
#define W98_GLYPH_H 10

/* _ minimise: a bar two rows up from the baseline */
#define W98_GLYPH_MIN { \
    0x000, 0x000, 0x000, 0x000, 0x000, 0x000, \
    0x000, 0x3FC, 0x3FC, 0x000 }

/* [] maximise: 1px ring */
#define W98_GLYPH_MAX { \
    0x3FF, 0x3FF, 0x201, 0x201, 0x201, 0x201, \
    0x201, 0x201, 0x3FF, 0x3FF }

/* [][] restore: two overlapping windows */
#define W98_GLYPH_RESTORE { \
    0x0FF, 0x0FF, 0x081, 0x381, 0x281, 0x281, \
    0x281, 0x2FF, 0x2FF, 0x000 }

/* X close */
#define W98_GLYPH_CLOSE { \
    0x303, 0x387, 0x1CE, 0x0FC, 0x078, 0x0FC, \
    0x1CE, 0x387, 0x303, 0x000 }

/* ------------------------------------------------------------ structures */

typedef struct {
    int x;
    int y;
    int w;
    int h;
} w98_rect_t;

typedef enum {
    W98_BEVEL_RAISED = 0,      /* windows, buttons, taskbar top edge */
    W98_BEVEL_RAISED_PRESSED,  /* a button being held down           */
    W98_BEVEL_SUNKEN,          /* text boxes, listviews, clock tray  */
    W98_BEVEL_ETCHED           /* group separators                   */
} w98_bevel_t;

typedef enum {
    W98_GLYPHKIND_MIN = 0,
    W98_GLYPHKIND_MAX,
    W98_GLYPHKIND_RESTORE,
    W98_GLYPHKIND_CLOSE
} w98_glyphkind_t;

typedef enum {
    W98_WALL_DITHER = 0,   /* factory default: 2x2 teal checkerboard */
    W98_WALL_SOLID,
    W98_WALL_IMAGE
} w98_wall_mode_t;

/* ------------------------------------------------------------ primitives */

/* Fills a rectangle with the 2x2 Win98 desktop dither. */
void w98_fill_dither(int x, int y, int w, int h);

/* Solid fill, clipped. */
void w98_fill(int x, int y, int w, int h, uint32_t color);

/* 2px (or 1px for etched) bevel ring. Does not fill the interior. */
void w98_bevel(int x, int y, int w, int h, w98_bevel_t style);

/* Bevel + BTNFACE fill: a standard Win98 surface. */
void w98_surface(int x, int y, int w, int h, w98_bevel_t style, uint32_t fill);

/* A push button. Draws pressed when `pressed` is set. */
void w98_button(int x, int y, int w, int h, const char* label,
                bool pressed, bool enabled, bool focused);

/* Horizontal lerp between two colours, painted as one row at a time. */
void w98_hgradient(int x, int y, int w, int h, uint32_t left, uint32_t right);

/* Vertical lerp between two colours. */
void w98_vgradient(int x, int y, int w, int h, uint32_t top, uint32_t bottom);

/* Text. scale 1 = 6x8 advance, scale 2 = 6x12 (Win98 default). The trailing
 * column of every cell is filled with `bg`, otherwise at scale 2 adjacent
 * glyphs touch. clip may be NULL for "whole screen". */
void w98_text(const char* s, int x, int y, uint32_t fg, uint32_t bg,
              int scale, const w98_rect_t* clip);

/* Bold: a second pass offset +1px in x. */
void w98_text_bold(const char* s, int x, int y, uint32_t fg, uint32_t bg,
                   int scale, const w98_rect_t* clip);

/* White text with a 1px black outline, for labels over wallpapers. */
void w98_text_outline(const char* s, int x, int y, uint32_t fg, int scale,
                      const w98_rect_t* clip);

/* Rotated 90 degrees counter-clockwise, anchored so the last glyph sits at
 * y_bottom. Used by the start-menu sidebar. */
void w98_text_vertical(const char* s, int x, int y_bottom, uint32_t fg,
                       int scale, const w98_rect_t* clip);

/* Width/height in pixels of a string at the given scale. */
int w98_text_width(const char* s, int scale);
int w98_text_height(int scale);

/* Truncates `src` into `dst` (dst must hold dstlen bytes) so that it fits
 * `maxw` pixels, appending '.' when truncated. */
void w98_text_fit(const char* src, char* dst, int dstlen, int maxw, int scale);

/* One 10x10 caption-button glyph. */
void w98_glyph(int kind, int x, int y, uint32_t fg);

/* Blits a 32x32 ARGB icon, nearest-neighbour scaled to dst_size. Pixels with
 * alpha <= 128 are skipped. */
void w98_icon_blit(const uint32_t* src, int src_size, int dst_size,
                   int x, int y, const w98_rect_t* clip);

/* Same, but averaged 2x2 downsampling so 16px title icons stay crisp. */
void w98_icon_blit_small(const uint32_t* src, int src_size, int dst_size,
                         int x, int y, const w98_rect_t* clip);

#endif /* WIN98_THEME_H */
