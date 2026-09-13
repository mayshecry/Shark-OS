

#ifndef WIN98_THEME_H
#define WIN98_THEME_H

#include <stdbool.h>
#include <stdint.h>

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

#define W98_TITLE_GRADIENT   0

#define W98_RAMP_MAX         2048

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

#define W98_GLYPH_W 10
#define W98_GLYPH_H 10

#define W98_GLYPH_MIN { \
    0x000, 0x000, 0x000, 0x000, 0x000, 0x000, \
    0x000, 0x3FC, 0x3FC, 0x000 }

#define W98_GLYPH_MAX { \
    0x3FF, 0x3FF, 0x201, 0x201, 0x201, 0x201, \
    0x201, 0x201, 0x3FF, 0x3FF }

#define W98_GLYPH_RESTORE { \
    0x0FF, 0x0FF, 0x081, 0x381, 0x281, 0x281, \
    0x281, 0x2FF, 0x2FF, 0x000 }

#define W98_GLYPH_CLOSE { \
    0x303, 0x387, 0x1CE, 0x0FC, 0x078, 0x0FC, \
    0x1CE, 0x387, 0x303, 0x000 }

typedef struct {
    int x;
    int y;
    int w;
    int h;
} w98_rect_t;

typedef enum {
    W98_BEVEL_RAISED = 0,
    W98_BEVEL_RAISED_PRESSED,
    W98_BEVEL_SUNKEN,
    W98_BEVEL_ETCHED
} w98_bevel_t;

typedef enum {
    W98_GLYPHKIND_MIN = 0,
    W98_GLYPHKIND_MAX,
    W98_GLYPHKIND_RESTORE,
    W98_GLYPHKIND_CLOSE
} w98_glyphkind_t;

typedef enum {
    W98_WALL_DITHER = 0,
    W98_WALL_SOLID,
    W98_WALL_IMAGE
} w98_wall_mode_t;

void w98_fill_dither(int x, int y, int w, int h);

void w98_fill(int x, int y, int w, int h, uint32_t color);

void w98_bevel(int x, int y, int w, int h, w98_bevel_t style);

void w98_surface(int x, int y, int w, int h, w98_bevel_t style, uint32_t fill);

void w98_button(int x, int y, int w, int h, const char* label,
                bool pressed, bool enabled, bool focused);

void w98_hgradient(int x, int y, int w, int h, uint32_t left, uint32_t right);

void w98_vgradient(int x, int y, int w, int h, uint32_t top, uint32_t bottom);

void w98_text(const char* s, int x, int y, uint32_t fg, uint32_t bg,
              int scale, const w98_rect_t* clip);

void w98_text_bold(const char* s, int x, int y, uint32_t fg, uint32_t bg,
                   int scale, const w98_rect_t* clip);

void w98_text_outline(const char* s, int x, int y, uint32_t fg, int scale,
                      const w98_rect_t* clip);

void w98_text_vertical(const char* s, int x, int y_bottom, uint32_t fg,
                       int scale, const w98_rect_t* clip);

int w98_text_width(const char* s, int scale);
int w98_text_height(int scale);

void w98_text_fit(const char* src, char* dst, int dstlen, int maxw, int scale);

void w98_glyph(int kind, int x, int y, uint32_t fg);

void w98_icon_blit(const uint32_t* src, int src_size, int dst_size,
                   int x, int y, const w98_rect_t* clip);

void w98_icon_blit_small(const uint32_t* src, int src_size, int dst_size,
                         int x, int y, const w98_rect_t* clip);

#endif /* WIN98_THEME_H */
