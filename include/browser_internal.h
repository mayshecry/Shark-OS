#ifndef BROWSER_INTERNAL_H
#define BROWSER_INTERNAL_H

#include "kernel.h"
#include "desktop.h"
#include "win98_theme.h"
#include "net.h"
#include "browser.h"
#include "font.h"

/* ------------------------------------------------------------- limits */

#define BR_MAX_NODES      3000      /* DOM nodes per document                 */
#define BR_TEXT_POOL      (512 * 1024)
#define BR_MAX_ATTRS      16
#define BR_MAX_DEPTH      48
#define BR_MAX_TEXT_RUN   4096
#define BR_MAX_RULES      8000      /* CSS rules (Bootstrap ~3000 + icon fonts) */
#define BR_MAX_SELPARTS   5         /* descendant selector depth              */
#define BR_MAX_DECLS      24        /* declarations per rule                  */
#define BR_MAX_DECL_POOL  40000     /* declarations across all rules          */
#define BR_MAX_PART_POOL  16000     /* selector compounds across all rules    */
#define BR_MAX_FONTFACES  6         /* @font-face rules per page              */
#define BR_CSS_BUF        (256 * 1024)  /* external stylesheets (all, per page) */
#define BR_MAX_BOXES      6000      /* layout boxes                           */
#define BR_PAGE_MAX       (512 * 1024) /* fetched document size (google.com is ~85 KB) */
#define BR_URL_MAX        512       /* bing result links are ~330 bytes */
#define BR_HISTORY_MAX    24
#define BR_MAX_IMAGES     32
#define BR_MAX_VARS       1024      /* CSS custom properties in scope        */
#define BR_MAX_SHEETS     12        /* external stylesheets fetched per page */
#define BR_IMG_MAX_BYTES  (160 * 1024)         /* largest fetched image file */
#define BR_IMG_ARENA_PIXELS (640 * 1024)       /* decoded pixels per page (2.5 MB) */
#define BR_IMG_SCRATCH_BYTES (640 * 1024)      /* inflate / JPEG plane work area */

/* --------------------------------------------------------------- DOM */

enum { BR_NODE_DOCUMENT = 0, BR_NODE_ELEMENT, BR_NODE_TEXT };

typedef struct { char* name; char* value; } br_attr_t;

typedef struct br_style {
    /* Computed style. -1/0xFFFFFFFF = not set (inherit or UA default). */
    uint32_t color;
    uint32_t background;        /* 0 alpha = transparent */
    int      font_scale;        /* legacy 1..3 bucket, derived from font_px  */
    int      font_px;           /* computed font size in pixels             */
    int      font_family;       /* FONT_FAMILY_* or FONT_FAMILY_WEB + n      */
    int      line_height;       /* px, 0 = normal                            */
    int      bold;
    int      italic;            /* synthesised: slight colour shift + slant not drawn */
    int      underline;
    int      strike;
    int      display;           /* BR_DISPLAY_* */
    int      text_align;        /* 0 left, 1 center, 2 right */
    int      margin_t, margin_b, margin_l, margin_r;
    int      padding_t, padding_b, padding_l, padding_r;
    int      border;            /* width px (max of the four sides)      */
    int      border_t, border_r, border_b, border_l;
    uint32_t border_color;
    int      width;             /* -1 auto */
    int      height;            /* -1 auto */
    int      list_style;        /* 0 disc, 1 decimal, 2 none */
    int      monospace;
    int      visible;
    int      max_width;         /* -1 none */
    int      min_width;         /* 0 none */
    int      border_box;        /* box-sizing: border-box */
    int      margin_auto;       /* bit0 left auto, bit1 right auto */
    int      flex_col;          /* flex-direction: column */
    int      gap;               /* flex gap px */
    int      float_dir;         /* 0 none, 1 left, 2 right (rendered as inline-block) */
    int      letter_spacing;
    int      text_transform;    /* 0 none, 1 uppercase, 2 lowercase, 3 capitalize */
    int      nowrap;            /* white-space: nowrap / pre                 */
    int      radius;            /* border-radius px                          */
    int      justify;           /* flex: 0 start 1 center 2 end 3 between 4 around 5 evenly */
    int      align_items;       /* flex: 0 start/stretch 1 center 2 end      */
    int      flex_grow;
    int      grid_cols;         /* grid: >0 column count, <0 -(minmax px) for auto-fill */
    int      max_height;        /* -1 none */
    int      overflow_hidden;
    int      pos_t, pos_r, pos_b, pos_l;   /* top/right/bottom/left: px, -(100000+pct) for %, BR_POS_AUTO */
    int      relative;          /* position: relative/sticky (offsets shift the box) */
} br_style_t;
#define BR_POS_AUTO 0x7FFFFFFF

enum { BR_DISPLAY_INLINE = 0, BR_DISPLAY_BLOCK, BR_DISPLAY_NONE, BR_DISPLAY_LIST_ITEM,
       BR_DISPLAY_TABLE, BR_DISPLAY_TABLE_ROW, BR_DISPLAY_TABLE_CELL, BR_DISPLAY_INLINE_BLOCK,
       BR_DISPLAY_FLEX };

typedef struct br_node {
    int type;
    int id;
    const char* tag;
    char* text;
    br_attr_t attrs[BR_MAX_ATTRS];
    int attr_count;
    struct br_node* parent;
    struct br_node* first_child;
    struct br_node* last_child;
    struct br_node* next;
    /* document only */
    struct br_node* body;
    struct br_node* head;
    /* per-node state */
    br_style_t style;
    int js_obj;                 /* handle of the JS wrapper object, -1 none */
    int inline_style_parsed;
    /* layout: bounding box in page coordinates (union of its boxes) */
    int lx, ly, lw, lh;
    int used_w;                 /* natural (max-content) width of the content box */
    /* form controls */
    char* value;                /* <input> current value */
    int checked;
} br_node_t;

/* ---------------------------------------------------------------- CSS */

typedef struct {
    const char* tag;            /* NULL = any */
    const char* id;             /* NULL = any */
    const char* cls;            /* NULL = any; "a b" = all of these classes */
    const char* attr_name;      /* [attr], [attr=value], [attr^=..], [attr*=..] */
    const char* attr_value;
    int attr_op;                /* 0 exists, '=' exact, '^' prefix, '$' suffix, '*' contains, '~' word */
    int pseudo_hover;
    int pseudo;                 /* 0 none, 1 first-child, 2 last-child, 3 nth even, 4 nth odd, 5 empty */
    int neg;                    /* :not(<simple>) : parts stored in not_tag/not_cls/not_id */
    const char* not_tag; const char* not_cls; const char* not_id;
    int combinator;             /* relation to the NEXT (ancestor-side) part: 0 descendant, '>' child, '+' adjacent */
} br_selpart_t;

typedef struct {
    const char* name;
    const char* value;
    int important;
} br_decl_t;

/* @font-face */
typedef struct {
    char family[40];
    char url[BR_URL_MAX];
    int bold, italic;
    int loaded;                 /* 0 pending, 1 ok, -1 failed */
    int family_id;
} br_fontface_t;

typedef struct {
    br_selpart_t* parts;        /* part_count compounds in the shared pool; parts[0] = rightmost (subject) */
    int part_count;
    br_decl_t* decls;           /* decl_count declarations in the shared pool */
    int decl_count;
    int var_first, var_count;   /* custom properties (--x) of this rule in the shared var_decls[] */
    int specificity;
    int order;
} br_rule_t;
#define BR_MAX_VAR_DECLS 4096   /* --x declarations across all rules of a page */

/* ------------------------------------------------------------- layout */

enum { BR_BOX_TEXT = 0, BR_BOX_RECT, BR_BOX_IMAGE, BR_BOX_HR, BR_BOX_BULLET, BR_BOX_INPUT, BR_BOX_BUTTON, BR_BOX_CHECKBOX };

typedef struct {
    int kind;
    int x, y, w, h;             /* page coordinates */
    br_node_t* node;            /* owning element (for clicks/hover)   */
    br_node_t* link;            /* nearest <a href> ancestor, or NULL   */
    const char* text;           /* BR_BOX_TEXT: pointer into text pool  */
    int text_len;
    uint32_t color;
    uint32_t bg;                /* BR_BOX_RECT: fill; text: background   */
    int scale;                  /* legacy bucket (widgets)               */
    int font_px;                /* text: pixel size                      */
    int face;                   /* text: font face index (br_font_face)  */
    int baseline;               /* text: baseline y offset within box    */
    int lh;                     /* text: line-height of the run          */
    int group;                  /* boxes of one inline-block: leader index + 1 */
    int group_h, group_base;    /* leader only: height and baseline offset */
    int bold, italic, underline, strike, mono;
    int border;
    int bt, br_, bb, bl;        /* BR_BOX_RECT: per-side border widths   */
    uint32_t border_color;
    int radius;                 /* BR_BOX_RECT: corner radius            */
    int img_index;              /* BR_BOX_IMAGE */
} br_box_t;

typedef struct {
    const char* src;
    uint32_t* pixels;
    int w, h;
    int failed;
} br_image_t;

/* Bytes of kernel stack left below the current frame. Every recursive walker
 * in the browser bails out (gracefully) when this drops under BR_STACK_MIN,
 * so hostile pages (deep DOM, recursive JS, pathological regexes) cannot
 * overflow the stack no matter what the individual frame sizes are. */
extern char stack_bottom;
static inline int br_stack_headroom(void) {
    char* sp = (char*)__builtin_frame_address(0);
    return (int)(sp - &stack_bottom);
}
#define BR_STACK_MIN (24 * 1024)

/* ----------------------------------------------------- shared state */

typedef struct {
    char url[BR_URL_MAX];
    char title[96];
    char status[128];
    char address[BR_URL_MAX];       /* address-bar edit buffer */
    int  address_cursor;
    int  address_focus;
    int  address_select_all;
    char history[BR_HISTORY_MAX][BR_URL_MAX];
    int  history_len;
    int  history_pos;               /* index of the current page */
    int  scroll_y;
    int  page_h;
    int  loading;
    int  error;                      /* last load failed */
    int  hover_link;                 /* node id of hovered link or -1 */
    int  focus_input;                /* node id of focused <input> or -1 */
    int  menu_open;                  /* bookmark menu */
    int  pressed_btn;
    int  layout_width;
    int  needs_layout;
    int  needs_js;                    /* run <script>s after layout */
    int  js_console_lines;
    char js_console[8][96];
    int  alert_open;
    char alert_text[160];
    int  view_source;
    int  nav_replace;                 /* pending script navigation replaces the history entry */
    int  refresh_pending;             /* <meta http-equiv=refresh> scheduled */
    int  refresh_replace;             /* replace the history entry instead of pushing */
    int  refresh_auto;                /* immediate refresh: counts toward the redirect-chain guard */
    uint32_t refresh_at;              /* uptime tick to fire at */
    char refresh_url[BR_URL_MAX];
    window_t* win;
} browser_state_t;

extern browser_state_t brs;
extern br_node_t* br_doc;
extern char br_page_src[BR_PAGE_MAX];
extern int br_page_len;

/* html.c */
void br_dom_reset(void);
int br_dom_node_count(void);
br_node_t* br_dom_node(int i);
char* br_strdup_n(const char* s, int n);
char* br_strdup(const char* s);
br_node_t* br_node_new(int type);
void br_node_append(br_node_t* parent, br_node_t* child);
void br_node_remove_children(br_node_t* n);
int br_strieq(const char* a, const char* b);
int br_streq(const char* a, const char* b);
int br_streq_n(const char* a, int n, const char* lit);     /* a[0..n) == lit */
int br_streq_prefix(const char* s, const char* prefix);    /* s starts with prefix */
const char* my_strstr_ci(const char* hay, const char* needle);
const char* br_attr(const br_node_t* n, const char* name);
void br_set_attr(br_node_t* n, const char* name, const char* value);
int br_is_block_tag(const char* t);
char* br_decode_text(const char* s, int n, int collapse);
br_node_t* br_html_parse(const char* html, int len);
void br_html_parse_fragment(br_node_t* parent, const char* html, int len);
int br_node_text_content(const br_node_t* n, char* out, int max);
int br_node_inner_html(const br_node_t* n, char* out, int max);
br_node_t* br_find_by_id(br_node_t* n, const char* id);
int br_has_class(const br_node_t* n, const char* cls);
br_node_t* br_find_first(br_node_t* n, const char* tag);

/* css.c */
void br_css_reset(void);
int br_css_fontface_count(void);
br_fontface_t* br_css_fontface(int i);
int br_css_import_count(void);
const char* br_css_import_url(int i);
void br_css_clear_imports(void);
void br_css_parse_sheet(const char* css, int len);
void br_css_set_noscript(int on);
int br_css_noscript_visible(void);
void br_css_compute(br_node_t* doc);
void br_css_apply_inline(br_node_t* n, const char* css);
uint32_t br_css_parse_color(const char* s, int* ok);
const char* br_css_pseudo_content(br_node_t* n, int after, br_style_t* st);
int br_subres_allowed(void);                 /* browser.c: sub-resource time budget not exhausted */
int br_css_has_pseudo_content(void);
int br_css_match_selector_string(br_node_t* n, const char* sel);

/* layout.c */
void br_layout(br_node_t* doc, int width);
int br_box_count(void);
br_box_t* br_box(int i);
void br_layout_reset_images(void);
br_image_t* br_image_get(int i);
int br_image_request(const char* src);
int br_image_request_sized(const char* src, int want_w, int want_h);   /* downsamples to the display size */

/* js.c */
void br_js_reset(void);
void br_js_run_document(br_node_t* doc);
void br_js_run_source(const char* src, int len, const char* origin);
void br_js_dispatch_click(br_node_t* n);
void br_js_dispatch_input(br_node_t* n);
void br_js_dispatch_change(br_node_t* n);
void br_js_dispatch_key(br_node_t* n, char c);
void br_js_tick(void);
int br_js_has_timers(void);
void br_js_console(const char* msg);
void br_js_dom_changed(void);

/* browser.c */
void br_navigate(const char* url, int push_history);
void br_resolve_url(const char* base, const char* rel, char* out, int max);
void br_status(const char* s);
void br_request_repaint(void);
void br_alert(const char* msg);
int br_fetch_resource(const char* url, uint8_t* out, int max);
int br_text_in_pool(const char* s);

/* fmt helpers */
void br_itoa(int v, char* out);
int br_atoi(const char* s);
void br_strlcpy(char* d, const char* s, int max);
void br_strlcat(char* d, const char* s, int max);

#endif
