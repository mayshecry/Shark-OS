

#include "kernel.h"
#include "desktop.h"
#include "plugin_manager.h"
#include "win98_theme.h"
#include "doom.h"
#include "flappybird.h"
#include "smb.h"
#include "pong.h"
#include "geometrydash.h"
#include "net.h"
#include "icon_data.h"
#include "browser.h"
#include "store.h"

static void kmemmove(void* dst, const void* src, int n) {
    char* d = (char*)dst;
    const char* s = (const char*)src;
    if(d>s && d<s+n){for(int i=n;i-->0;)d[i]=s[i];}
    else for (int i = 0; i < n; i++) d[i] = s[i];
}
static void kstrcat(char* dst, const char* src) {
    while (*dst) dst++;
    while ((*dst++ = *src++));
}

static void label_value(int lx, int vx, int y, const char* label,
                        const char* value, uint32_t vcolor) {
    w98_text_bold(label, lx, y, W98_BTNTEXT, W98_BTNFACE, 1, NULL);
    w98_text(value, vx, y, vcolor, W98_BTNFACE, 1, NULL);
}

#define TERM_SCROLLBACK   8192
#define TERM_CAPTURE_MAX  4096
#define TERM_CELL_W       8
#define TERM_CELL_H       16
#define TERM_MAX_COLS     160

typedef struct {
    char input[256];
    int input_len;
    char output[TERM_SCROLLBACK];
    uint8_t output_colors[TERM_SCROLLBACK];
    int output_len;
    char capture[TERM_CAPTURE_MAX];
    uint8_t capture_colors[TERM_CAPTURE_MAX];
    int blink;
    int cols;
    int rows;
    int img_active;
    int img_pending;
    int img_lines;
    int img_tail;
    int img_cells;
    int img_size;
    int img_xoff;
    int history_pos;
    int hot_active;
    int anchor,end,dragging;
} term_state_t;

static term_state_t* term_current = NULL;
static window_t* term_current_win = NULL;

static void term_cur_rows(int rows) { if (term_current) term_current->rows = rows; }
static void term_cur_cols(int cols) { if (term_current) term_current->cols = cols; }

int desktop_terminal_cols = 46;   
int desktop_terminal_rows = 10;

static term_state_t* term_state(window_t* w) {
    if (!w) return NULL;
    if (!w->user_data) {
        
        term_state_t* st = pmm_cache_alloc(sizeof(*st));
        if (!st) return NULL;
        char* raw = (char*)st;
        for (size_t i = 0; i < sizeof(*st); i++) raw[i] = 0;
        st->cols = 46;
        st->rows = 10;
        w->user_data = st;

    }
    return (term_state_t*)w->user_data;
}

static window_t* term_draw_window = NULL; 

void terminal_release_window(window_t *w){
    if(term_current_win==w){term_current=NULL;term_current_win=NULL;}
    if(term_draw_window==w)term_draw_window=NULL;
    pmm_cache_free(w->user_data);w->user_data=NULL;
}
int desktop_terminal_output_len(void) {
    return term_current ? term_current->output_len : 0;
}

int desktop_terminal_rows_available(void) {
    return desktop_terminal_rows;
}


int desktop_terminal_grow(int want_rows) {
    window_t* win = term_current ? term_current_win : term_draw_window;
    if (!win) return desktop_terminal_rows;
    if (want_rows > desktop_terminal_rows) {
        int extra = (want_rows - desktop_terminal_rows) * TERM_CELL_H;
        int limit = (int)screen_height - (int)TASKBAR_HEIGHT - 8;
        int new_h = win->rect.height + extra;
        if (new_h > limit - win->rect.y) new_h = limit - win->rect.y;
        if (new_h > win->rect.height) {
            win->rect.height = new_h;
            window_update_client_rect(win);
            desktop.dirty = true;
        }
        int lines = (win->rect.client_h - 12 - 8) / TERM_CELL_H - 1;
        desktop_terminal_rows = lines > 0 ? lines : 1;
        term_cur_rows(desktop_terminal_rows);
    }
    return desktop_terminal_rows;
}


int desktop_terminal_widen(int want_cols) {
    window_t* win = term_current ? term_current_win : term_draw_window;
    if (!win) return desktop_terminal_cols;
    if (want_cols > desktop_terminal_cols) {
        int need_w = want_cols * TERM_CELL_W + 8 + 12 + 12; 
        int limit = (int)screen_width - win->rect.x - 8;
        int new_w = need_w;
        if (new_w > limit) new_w = limit;
        if (new_w > win->rect.width) {
            win->rect.width = new_w;
            window_update_client_rect(win);
            desktop.dirty = true;
        }
        int cols = (win->rect.client_w - 12 - 8) / TERM_CELL_W;
        if (cols > TERM_MAX_COLS) cols = TERM_MAX_COLS;
        if (cols < 4) cols = 4;
        desktop_terminal_cols = cols;
        term_cur_cols(cols);
    }
    return desktop_terminal_cols;
}

void desktop_terminal_image_attach(int lines, int tail_lines, int cells, int size, int xoff) {
    term_state_t* st = term_current;
    if (!st) return;
    st->img_active = 1;
    
    st->img_pending = 1;
    st->img_lines = lines;
    st->img_tail = tail_lines;
    st->img_cells = cells;
    st->img_size = size;
    st->img_xoff = xoff;
}

void desktop_terminal_hot_set(int on) {
    if (!term_current) return;
    term_current->hot_active = on ? 1 : 0;
}

void desktop_terminal_image_detach(void) {
    if (!term_current) return;
    term_current->img_active = 0;
    term_current->img_pending = 0;
}

static void terminal_output_append(term_state_t* st, const char* text,
                                   const uint8_t* colors, int len) {
    if (!st || len <= 0) return;
    if (len >= TERM_SCROLLBACK - 1) {



        int skip = len - (TERM_SCROLLBACK - 1);
        text += skip;
        if (colors) colors += skip;
        len = TERM_SCROLLBACK - 1;
    }
    if (st->output_len + len >= TERM_SCROLLBACK - 1) {

        int drop = st->output_len + len - (TERM_SCROLLBACK - 1);
        while (drop < st->output_len && st->output[drop] != '\n') drop++;
        if (drop < st->output_len) drop++;
        kmemmove(st->output, st->output + drop, st->output_len - drop);
        kmemmove(st->output_colors, st->output_colors + drop, st->output_len - drop);
        st->output_len -= drop;
    }
    for (int i = 0; i < len; i++) {
        st->output[st->output_len] = text[i];
        st->output_colors[st->output_len] = colors ? colors[i] : VGA_COLOR_LIGHT_GREY;
        st->output_len++;
    }
    st->output[st->output_len] = '\0';
    st->history_pos++;
    if (st->img_active) {
        if (st->img_pending) {
            st->img_pending = 0; 
        } else {
            st->img_active = 0; 
        }
    }
}

static void term_append_fixed(term_state_t* st, const char* text, int len, uint8_t color) {
    uint8_t cols[260];
    if (len > (int)sizeof(cols)) len = (int)sizeof(cols);
    for (int i = 0; i < len; i++) cols[i] = color;
    terminal_output_append(st, text, cols, len);
}

static void terminal_output_clear(term_state_t* st) {
    if (!st) return;
    st->img_active = 0;
    st->img_pending = 0;
    st->output_len = 0;
    st->output[0] = '\0';
}

static int terminal_layout_lines(term_state_t* st, int cols, int want,
                                 int* out_start, int* out_len) {
    int count = 0;
    int i = 0;
    if (!st) {
        if (out_start) *out_start = 0;
        if (out_len) *out_len = 0;
        return 0;
    }
    if (cols < 1) cols = 1;
    while (i < st->output_len) {
        int start = i;
        int len = 0;
        while (i < st->output_len && st->output[i] != '\n' && len < cols) {
            i++; len++;
        }
        if (want == count) {
            if (out_start) *out_start = start;
            if (out_len) *out_len = len;
            return count;
        }
        count++;
        if (i < st->output_len && st->output[i] == '\n') i++;
    }
    if (want >= 0) {
        if (out_start) *out_start = st->output_len;
        if (out_len) *out_len = 0;
    }
    return count;
}

static int bounded(int v,int lo,int hi){return v<lo?lo:v>hi?hi:v;}
static int within(int pos,int a,int b){return a<b?(pos>=a&&pos<b):(pos>=b&&pos<a);}
void app_window_mouse_terminal(window_t *w,int mx,int my,int buttons){
    term_state_t *st=term_state(w);if(!st)return;
    if(!(buttons&1)){st->dragging=0;return;} if(!st->cols || st->hot_active)return;
    int total=terminal_layout_lines(st,st->cols,-1,NULL,NULL),first=total>st->rows?total-st->rows:0;
    int row=bounded((my-w->rect.client_y-10)/TERM_CELL_H,0,1000),col=bounded((mx-w->rect.client_x-10+4)/TERM_CELL_W,0,st->cols);
    int pos;
    if(row<total-first){int begin,len;terminal_layout_lines(st,st->cols,first+row,&begin,&len);pos=begin+bounded(col,0,len);}
    else pos=st->output_len+bounded((row-total+first)*st->cols+col-2,0,st->input_len);
    if(!st->dragging)st->anchor=pos;st->end=pos;st->dragging=1;w->needs_redraw=desktop.dirty=true;
}
void app_window_draw_terminal(window_t* w) {
    term_draw_window = w;
    term_state_t* st = term_state(w);
    if (!st) return;
    int cx = w->rect.client_x;
    int cy = w->rect.client_y;
    int cw = w->rect.client_w;
    int ch = w->rect.client_h;

    st->blink++;
    if (st->blink >= 60) st->blink = 0;

    w98_fill(cx, cy, cw, ch, W98_BTNFACE);
    w98_surface(cx + 2, cy + 2, cw - 4, ch - 4, W98_BEVEL_SUNKEN,
                W98_BTNDKSHADOW);

    int tx = cx + 6, ty = cy + 6;
    int tw = cw - 12, th = ch - 12;
    if (tw <= TERM_CELL_W + 2 || th <= TERM_CELL_H + 2) return;

    w98_fill(tx, ty, tw, th, 0xFF000000);

    int cols = (tw - 8) / TERM_CELL_W;
    if (cols > TERM_MAX_COLS) cols = TERM_MAX_COLS;
    if (cols < 4) cols = 4;
    st->cols = cols;
    desktop_terminal_cols = cols;
    int max_lines = (th - 8) / TERM_CELL_H;
    if (max_lines < 1) max_lines = 1;

    int clipx0 = tx + 2, clipy0 = ty + 2;
    int clipx1 = tx + tw - 2, clipy1 = ty + th - 2;

    int prompt_chars = 2 + st->input_len;
    int prompt_rows = (prompt_chars + cols - 1) / cols;
    if (prompt_rows < 1) prompt_rows = 1;

    int total = terminal_layout_lines(st, cols, -1, NULL, NULL);
    int avail = max_lines - prompt_rows;
    if (avail < 0) avail = 0;
    st->rows = avail;
    desktop_terminal_rows = avail;
    int first = total - avail;
    if (first < 0) first = 0;

    int row = 0;
    if (st->hot_active) {
        int img_bottom = ty + 4 + (max_lines - 1) * TERM_CELL_H - 2;
        int img_h = img_bottom > ty + 2 ? img_bottom - (ty + 2) : 0;
        terminal_hot_draw_rect(tx + 2, ty + 2, tw - 4, img_h);
        row = max_lines - 1;
    } else {
        for (int li = first; li < total && row < avail; li++, row++) {
            int start = 0, len = 0;
            terminal_layout_lines(st, cols, li, &start, &len);
            if (len > TERM_MAX_COLS) len = TERM_MAX_COLS;
            for (int j = 0; j < len; j++) {
                char c = st->output[start + j];
                uint8_t cell_color = st->output_colors[start + j];
                uint32_t fg = vga_to_rgb[cell_color & 0x0F];
                uint32_t bg = vga_to_rgb[(cell_color >> 4) & 0x0F];
                if(within(start+j,st->anchor,st->end)){fg=W98_HIGHLIGHTTEXT;bg=W98_HIGHLIGHT;}
                term_put_char_win(c, tx + 4 + j * TERM_CELL_W,
                                  ty + 4 + row * TERM_CELL_H, fg, bg,
                                  clipx0, clipy0, clipx1, clipy1);
            }
        }
    }

    if (!terminal_hot_active() && st->img_active && st->img_size > 0) {
        int start_line = total - st->img_tail - st->img_lines;
        int r0 = start_line - first;
        int ix = tx + 4 + st->img_xoff * TERM_CELL_W;
        int iy = ty + 4 + r0 * TERM_CELL_H;
        int bottom = iy + st->img_lines * TERM_CELL_H;
        int clip_bottom = clipy1 < bottom ? clipy1 : bottom;
        imgfetch_blit(ix, iy, st->img_size, clipx0, clipy0, clipx1, clip_bottom);
    }

    int py = ty + 4 + row * TERM_CELL_H;
    term_put_char_win('$', tx + 4, py, 0xFF55FF55, 0xFF000000,
                      clipx0, clipy0, clipx1, clipy1);
    int col = 2;
    int prow = 0;
    for (int i = 0; i < st->input_len; i++) {
        if (col >= cols) { col = 0; prow++; }
        term_put_char_win(st->input[i], tx + 4 + col * TERM_CELL_W,
                          py + prow * TERM_CELL_H, 0xFFFFFF55, within(st->output_len+i,st->anchor,st->end)?W98_HIGHLIGHT:0xFF000000,
                          clipx0, clipy0, clipx1, clipy1);
        col++;
    }
    if (st->blink < 30) {
        if (col >= cols) { col = 0; prow++; }
        int cur_x = tx + 4 + col * TERM_CELL_W;
        int cur_y = py + prow * TERM_CELL_H;
        if (cur_x + TERM_CELL_W <= clipx1 && cur_y + TERM_CELL_H <= clipy1)
            w98_fill(cur_x, cur_y, TERM_CELL_W, TERM_CELL_H, 0xFF55FF55);
    }
}

static bool terminal_cmd_blocked(const char* cmd, char* why, int why_len) {
    static const char* blocked[] = {
        "guess", "tictactoe", "python", "doom", "flappybird", "smb",
        "pong", "gdash", "sharkcube", "sharkcraft", "microsoft", "htop", "exec", NULL
    };
    char name[32];
    int n = 0;
    while (cmd[n] && cmd[n] != ' ' && n < 31) { name[n] = cmd[n]; n++; }
    name[n] = '\0';
    for (int i = 0; blocked[i]; i++) {
        if (strcmp(name, blocked[i]) == 0) {
            const char* msg;
            if (strcmp(name, "edit") == 0) msg = "edit: use Notepad on the desktop.\n";
            else if (strcmp(name, "doom") == 0 || strcmp(name, "flappybird") == 0 ||
                     strcmp(name, "smb") == 0 || strcmp(name, "pong") == 0 ||
                     strcmp(name, "gdash") == 0 || strcmp(name, "sharkcube") == 0 ||
                     strcmp(name, "sharkcraft") == 0)
                msg = "Games open from the Start menu or a desktop icon.\n";
            else if (strcmp(name, "htop") == 0) msg = "htop: use Task Manager on the desktop (or 'ps').\n";
            else msg = "This command needs the full-screen console (boot 'SharkOS Lite').\n";
            int k = 0;
            while (msg[k] && k < why_len - 1) { why[k] = msg[k]; k++; }
            why[k] = '\0';
            return true;
        }
    }
    return false;
}

static void terminal_run_command(window_t* w) {
    term_state_t* st = term_state(w);
    if (!st) return;
    st->input[st->input_len] = '\0';

    term_append_fixed(st, "$ ", 2, VGA_COLOR_LIGHT_GREEN);
    term_append_fixed(st, st->input, st->input_len, VGA_COLOR_LIGHT_BROWN);
    terminal_output_append(st, "\n", NULL, 1);

    char why[96];
    if (terminal_cmd_blocked(st->input, why, sizeof(why))) {
        terminal_output_append(st, why, NULL, (int)strlen(why));
    } else if (strcmp(st->input, "clear") == 0 || strcmp(st->input, "cls") == 0) {
        terminal_output_clear(st);
    } else {

        uint8_t saved_color = terminal_color;
        term_current = st;
        term_current_win = w;
        desktop_terminal_cols = st->cols;
        desktop_terminal_rows = st->rows;
        terminal_in_desktop_window = true;
        terminal_capture_begin(st->capture, st->capture_colors, TERM_CAPTURE_MAX);
        execute_command(st->input);
        bool cleared = terminal_capture_cleared;
        int len = terminal_capture_len;
        terminal_capture_end();
        terminal_in_desktop_window = false;
        terminal_color = saved_color;

        if (cleared) terminal_output_clear(st);

        const char* out = st->capture;
        const uint8_t* out_colors = st->capture_colors;
        while (len > 0 && *out == '\n') { out++; out_colors++; len--; }

        while (len > 1 && out[len - 1] == '\n' && out[len - 2] == '\n') len--;
        if (len > 0) {
            terminal_output_append(st, out, out_colors, len);
            if (out[len - 1] != '\n') terminal_output_append(st, "\n", NULL, 1);
        }
    }

    term_current = NULL;
    term_current_win = NULL;
    st->input_len = 0;
    st->input[0] = '\0';
    w->needs_redraw = true;
    desktop.dirty = true;
}

void app_window_draw_doom(window_t* w) {
    doom_set_window_rect(w->rect.client_x, w->rect.client_y,
                         w->rect.client_w, w->rect.client_h);
    doom_draw_frame();
}
void app_window_draw_flappybird(window_t* w) {
    flappybird_set_window_rect(w->rect.client_x, w->rect.client_y,
                               w->rect.client_w, w->rect.client_h);
    flappybird_draw_frame();
}
void app_window_draw_smb(window_t* w) {
    smb_set_window_rect(w->rect.client_x, w->rect.client_y,
                        w->rect.client_w, w->rect.client_h);
    smb_draw_frame();
}
void app_window_draw_pong(window_t* w) {
    pong_set_window_rect(w->rect.client_x, w->rect.client_y,
                         w->rect.client_w, w->rect.client_h);
    pong_draw_frame();
}
void app_window_draw_gdash(window_t* w) {
    gd_set_window_rect(w->rect.client_x, w->rect.client_y,
                       w->rect.client_w, w->rect.client_h);
    gd_draw_frame();
}

void app_window_draw_sharkcube(window_t* w) {
    sharkcube_draw_window(w->rect.client_x, w->rect.client_y,
                          w->rect.client_w, w->rect.client_h);
}

void app_window_draw_sharkcraft(window_t* w) {
    sharkcraft_draw_window(w->rect.client_x, w->rect.client_y,
                           w->rect.client_w, w->rect.client_h);
}


typedef struct {
    uint32_t color;
    const char* label;
} settings_color_opt_t;

static const settings_color_opt_t settings_colors[] = {
    { 0xFF008080u,        "Teal (Default)" },
    { 0xFF1A0A0Au,        "Dark Red" },
    { 0xFF0A1A0Au,        "Dark Green" },
    { 0xFF1A1A0Au,        "Amber" },
    { 0xFF0A0A0Au,        "Pure Black" },
    { 0xFF2A1A0Au,        "Brown" },
    { 0xFF1A0A2Au,        "Purple" },
    { 0xFF0A1A2Au,        "Teal Blue" },
};
#define SETTINGS_COLOR_COUNT 8

#include "settings.inc"

static int faq_wrap_text(const char* prefix, const char* text, int x, int y,
                         int max_w, int indent, int y_limit, uint32_t fg,
                         uint32_t bg, bool bold, const w98_rect_t* clip) {
    char line[160];
    int n = (int)strlen(text);
    int p = 0;
    bool first = true;
    int plen = (int)strlen(prefix);

    while (p < n && y + 8 <= y_limit) {
        while (p < n && text[p] == ' ') p++;
        if (p >= n) break;

        int lx = first ? x : x + indent;
        int max_chars = (max_w - (lx - x)) / 6 - (first ? plen : 0);
        if (max_chars < 6) max_chars = 6;

        int take = n - p;
        if (take > max_chars) {
            take = max_chars;
            int k = take;
            while (k > 0 && text[p + k] != ' ') k--;
            if (k > 0) take = k;
        }
        if (take > (int)sizeof(line) - plen - 1) take = (int)sizeof(line) - plen - 1;

        int ln = 0;
        if (first) { for (int j = 0; j < plen; j++) line[ln++] = prefix[j]; }
        for (int j = 0; j < take; j++) line[ln++] = text[p + j];
        line[ln] = '\0';

        while (ln > 1 && w98_text_width(line, 1) > max_w) {
            ln--;
            line[ln] = '\0';
        }

        if (bold) w98_text_bold(line, lx, y, fg, bg, 1, clip);
        else      w98_text(line, lx, y, fg, bg, 1, clip);

        y += 11;
        p += take;
        first = false;
    }
    return y;
}

typedef struct { const char* q; const char* a; } faq_entry_t;

static const faq_entry_t faq_entries[] = {
    { "Who made SharkOS?",
      "Mayshecry. Source, issues and releases live on GitHub: github.com/mayshecry/Shark-OS" },
    { "What is SharkOS?",
      "A hobby 32-bit x86 operating system written from scratch in C and assembly: "
      "its own Multiboot bootstrap, kernel, drivers, in-memory filesystem, network "
      "stack, shell (nemo-shell) and this Windows 98 style desktop." },
    { "How does it work?",
      "Everything runs at Ring 0 in one flat address space. The desktop is composed "
      "into a RAM back buffer and copied to the framebuffer once per frame. Read the "
      "source on GitHub - it is small enough to follow in an afternoon." },
    { "Does it run on real hardware?",
      "Yes. Write sharkos.iso to a USB stick (dd, or Rufus in DD mode) and boot it on "
      "a BIOS PC with a PS/2 keyboard and mouse (most laptops emulate them). Pick "
      "'SharkOS Lite' at the boot menu for a plain text console." },
    { "Something is slow or the screen tears?",
      "Use a lower resolution in grub.cfg (1024x768 is the default), switch the "
      "wallpaper to the teal dither in Settings, and close games you are not playing." },
    { "Where are the games?",
      "Doom, Flappy Bird, Super Mario Bros, Pong and Geometry Dash are plugins. Open "
      "them from the Start menu or a desktop icon; ESC quits." },
    { "How do I write my own program?",
      "Plugins are C files compiled into the kernel (see PLUGIN_SYSTEM.md and "
      "plugins/TEMPLATE.c). Simple ELF binaries in System/Bin also run from the shell." },
    { "Is TempleOS better?",
      "We are sorry, Terry, but... no. Your OS was really badly optimized." },
};
#define FAQ_COUNT ((int)(sizeof(faq_entries) / sizeof(faq_entries[0])))

void app_window_draw_faq(window_t* w) {
    int cx = w->rect.client_x;
    int cy = w->rect.client_y;
    int cw = w->rect.client_w;
    int ch = w->rect.client_h;

    w98_fill(cx, cy, cw, ch, W98_BTNFACE);

    w98_rect_t clip;
    clip.x = cx; clip.y = cy; clip.w = cw; clip.h = ch;

    w98_text_bold("SharkOS FAQ", cx + cw / 2 - 33, cy + 8, W98_BTNTEXT,
                  W98_BTNFACE, 1, &clip);
    w98_bevel(cx + 4, cy + 22, cw - 8, 2, W98_BEVEL_ETCHED);

    int px = cx + 6, py = cy + 30;
    int pw = cw - 12, ph = ch - 36;
    if (pw < 60 || ph < 24) return;
    w98_surface(px, py, pw, ph, W98_BEVEL_SUNKEN, W98_WINDOW);

    w98_rect_t pane;
    pane.x = px + 2; pane.y = py + 2; pane.w = pw - 4; pane.h = ph - 4;

    int x = px + 8;
    int y = py + 6;
    int text_w = pw - 16;
    int y_limit = py + ph - 6;
    int shown = 0;

    for (int i = 0; i < FAQ_COUNT; i++) {
        if (y + 22 > y_limit) break;
        y = faq_wrap_text("Q: ", faq_entries[i].q, x, y, text_w, 18, y_limit,
                          W98_HIGHLIGHT, W98_WINDOW, true, &pane);
        y = faq_wrap_text("A: ", faq_entries[i].a, x, y, text_w, 18, y_limit,
                          W98_WINDOWTEXT, W98_WINDOW, false, &pane);
        y += 7;
        shown++;
    }

    if (shown < FAQ_COUNT) {
        w98_fill(px + 3, py + ph - 14, pw - 6, 11, W98_WINDOW);
        w98_text("(maximize the window to read all questions)",
                 x, py + ph - 13, W98_GRAYTEXT, W98_WINDOW, 1, &pane);
    }
}

#define TM_ROW_H      13

static void tm_cat(char* dst, const char* src) {
    while (*dst) dst++;
    while (*src) *dst++ = *src++;
    *dst = '\0';
}
#define TM_TAB_APPS   0
#define TM_TAB_PERF   1
#define TM_TAB_PROCS  2

static int tm_tab = TM_TAB_APPS;
static int tm_selected = -1;
static int tm_selected_z = -1;

typedef struct {
    int tabs_y;
    int list_y, list_h;
    int btn_y;
} tm_layout_t;

static void tm_layout(window_t* w, tm_layout_t* L) {
    L->tabs_y = w->rect.client_y + 6;
    L->list_y = L->tabs_y + 22;
    L->btn_y  = w->rect.client_y + w->rect.client_h - 30;
    L->list_h = L->btn_y - 8 - L->list_y;
    if (L->list_h < TM_ROW_H) L->list_h = TM_ROW_H;
}

static const char* tm_tab_names[3] = { "Applications", "Performance", "Processes" };

static void tm_draw_tabs(window_t* w, tm_layout_t* L, const w98_rect_t* clip) {
    int x = w->rect.client_x + 6;
    int cw = w->rect.client_w - 12;
    int tab_w = 84;
    for (int i = 0; i < 3; i++) {
        int tx = x + i * tab_w;
        bool sel = (i == tm_tab);
        int ty = sel ? L->tabs_y - 2 : L->tabs_y;
        int th = sel ? 20 : 18;

        w98_fill(tx, ty, tab_w, th, W98_BTNFACE);
        w98_fill(tx, ty, 1, th, W98_BTNHILITE);
        w98_fill(tx + 1, ty, tab_w - 2, 1, W98_BTNHILITE);
        w98_fill(tx + tab_w - 1, ty + 1, 1, th - 1, W98_BTNDKSHADOW);
        w98_fill(tx + tab_w - 2, ty + 1, 1, th - 1, W98_BTNSHADOW);
        w98_text(tm_tab_names[i], tx + 8, ty + 6, W98_BTNTEXT, W98_BTNFACE, 1, clip);
    }

    int page_y = L->tabs_y + 18;
    w98_fill(x, page_y, cw, 1, W98_BTNHILITE);
    int stx = x + tm_tab * tab_w;
    w98_fill(stx + 1, page_y, tab_w - 3, 1, W98_BTNFACE);
}

static int tm_count_task_rows(void) {
    int n = 0;
    for (int i = 0; i < desktop.window_count; i++) {
        if (desktop.windows[i].visible) n++;
    }
    return n;
}

static const char* tm_state_name(window_t* w) {
    if (w->state == WINDOW_STATE_MINIMIZED) return "Minimized";
    if (w->game_tick) return "Running";
    return "Running";
}

static void tm_draw_applications(window_t* w, tm_layout_t* L, const w98_rect_t* clip) {
    (void)clip;
    int x = w->rect.client_x + 6;
    int cw = w->rect.client_w - 12;

    w98_surface(x, L->list_y, cw, L->list_h, W98_BEVEL_SUNKEN, W98_WINDOW);
    w98_rect_t lc;
    lc.x = x + 2; lc.y = L->list_y + 2; lc.w = cw - 4; lc.h = L->list_h - 4;

    int hy = L->list_y + 2;
    w98_fill(x + 2, hy, cw - 4, TM_ROW_H, W98_BTNFACE);
    w98_fill(x + 2, hy + TM_ROW_H - 1, cw - 4, 1, W98_BTNSHADOW);
    w98_text("Task", x + 8, hy + 3, W98_BTNTEXT, W98_BTNFACE, 1, &lc);
    w98_text("Status", x + cw - 96, hy + 3, W98_BTNTEXT, W98_BTNFACE, 1, &lc);

    int y = hy + TM_ROW_H + 1;
    int rows = (L->list_h - TM_ROW_H - 6) / TM_ROW_H;
    int shown = 0;

    if (tm_selected >= desktop.window_count || tm_selected < 0 ||
        !desktop.windows[tm_selected].visible) {
        tm_selected = -1;
        for (int i = 0; i < desktop.window_count && tm_selected_z >= 0; i++) {
            if (desktop.windows[i].z_order == tm_selected_z && desktop.windows[i].visible) {
                tm_selected = i;
                break;
            }
        }
    }

    for (int i = 0; i < desktop.window_count && shown < rows; i++) {
        window_t* t = &desktop.windows[i];
        if (!t->visible) continue;
        bool sel = (i == tm_selected);
        uint32_t bg = sel ? W98_HIGHLIGHT : W98_WINDOW;
        uint32_t fg = sel ? W98_HIGHLIGHTTEXT : W98_WINDOWTEXT;
        if (sel) w98_fill(x + 2, y, cw - 4, TM_ROW_H, bg);

        const uint32_t* icon = desktop_icon_pixels_for_type(t->type);
        if (icon) {
            for (int iy = 0; iy < 12; iy++) {
                for (int ix = 0; ix < 12; ix++) {
                    uint32_t px = icon[(iy * 32 / 12) * 32 + (ix * 32 / 12)];
                    if (px >> 24) draw_pixel(x + 6 + ix, y + iy, px);
                }
            }
        }
        char title[40];
        int n = 0;
        while (t->title[n] && n < 38) { title[n] = t->title[n]; n++; }
        title[n] = '\0';
        w98_text(title, x + 22, y + 3, fg, bg, 1, &lc);
        w98_text(tm_state_name(t), x + cw - 96, y + 3, fg, bg, 1, &lc);
        y += TM_ROW_H;
        shown++;
    }

    if (shown == 0) {
        w98_text("(no applications running)", x + 8, y + 3, W98_GRAYTEXT, W98_WINDOW, 1, &lc);
    }

    int bw = 78, bh = 22;
    int bx = x + cw - bw;
    bool has_sel = tm_selected >= 0 && tm_selected < desktop.window_count &&
                   &desktop.windows[tm_selected] != w;
    w98_button(bx, L->btn_y, bw, bh, "End Task", false, has_sel, false);
    w98_button(bx - bw - 6, L->btn_y, bw, bh, "Switch To", false,
               tm_selected >= 0 && tm_selected < desktop.window_count, false);
    w98_button(bx - 2 * (bw + 6), L->btn_y, bw, bh, "Refresh", false, true, false);
}

static void tm_draw_bar(int x, int y, int w, int h, int percent, const char* label,
                        const w98_rect_t* clip) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    w98_text(label, x, y, W98_BTNTEXT, W98_BTNFACE, 1, clip);
    int by = y + 11;
    w98_surface(x, by, w, h, W98_BEVEL_SUNKEN, W98_WINDOW);
    int fill_w = (w - 4) * percent / 100;

    for (int bx = 0; bx < fill_w; bx += 8) {
        int seg = (fill_w - bx) < 6 ? (fill_w - bx) : 6;
        w98_fill(x + 2 + bx, by + 2, seg, h - 4, W98_HIGHLIGHT);
    }
    char buf[16];
    int_to_string((uint32_t)percent, buf);
    int l = (int)strlen(buf);
    buf[l] = '%'; buf[l + 1] = '\0';
    w98_text(buf, x + w + 6, by + (h - 8) / 2, W98_BTNTEXT, W98_BTNFACE, 1, clip);
}

static void tm_fmt_kb(uint32_t bytes, char* out) {

    if (bytes >= 1024u * 1024u) {
        uint32_t mb10 = bytes / (1024u * 1024u / 10u);
        int_to_string(mb10 / 10, out);
        int l = (int)strlen(out);
        out[l] = '.'; out[l + 1] = (char)('0' + mb10 % 10); out[l + 2] = '\0';
        tm_cat(out, " MB");
    } else {
        int_to_string(bytes / 1024u, out);
        tm_cat(out, " KB");
    }
}

static void tm_draw_performance(window_t* w, tm_layout_t* L, const w98_rect_t* clip) {
    int x = w->rect.client_x + 6;
    int cw = w->rect.client_w - 12;
    int y = L->list_y;
    int bottom = L->btn_y + 22;

    w98_text_bold("CPU Usage History", x, y, W98_BTNTEXT, W98_BTNFACE, 1, clip);
    y += 12;
    int gh = (bottom - y) / 2 - 40;
    if (gh < 40) gh = 40;
    if (gh > 110) gh = 110;
    int gw = cw;
    w98_surface(x, y, gw, gh, W98_BEVEL_SUNKEN, 0xFF000000u);

    for (int gx = x + 2 + 12; gx < x + gw - 2; gx += 12)
        for (int gy = y + 2; gy < y + gh - 2; gy++) draw_pixel(gx, gy, 0xFF004000u);
    for (int gy = y + 2 + 12; gy < y + gh - 2; gy += 12)
        for (int gx = x + 2; gx < x + gw - 2; gx++) draw_pixel(gx, gy, 0xFF004000u);

    int inner_w = gw - 4;
    int inner_h = gh - 4;
    int step = inner_w / SYS_CPU_HISTORY;
    if (step < 1) step = 1;
    int samples = inner_w / step;
    if (samples > SYS_CPU_HISTORY) samples = SYS_CPU_HISTORY;
    int prev_y = -1;
    for (int i = 0; i < samples; i++) {
        int idx = (sys_cpu_history_pos - samples + i + SYS_CPU_HISTORY * 2) % SYS_CPU_HISTORY;
        int v = (int)sys_cpu_history[idx];
        if (v > 100) v = 100;
        int py = y + 2 + inner_h - 1 - (v * (inner_h - 1)) / 100;
        int px = x + gw - 2 - (samples - i) * step;
        if (prev_y >= 0) {
            int y0 = prev_y < py ? prev_y : py;
            int y1 = prev_y < py ? py : prev_y;
            for (int yy = y0; yy <= y1; yy++) draw_pixel(px, yy, 0xFF00FF00u);
        }
        for (int sx = 0; sx < step; sx++) draw_pixel(px + sx, py, 0xFF00FF00u);
        prev_y = py;
    }
    y += gh + 8;

    char buf[48], num[16];
    int col2 = x + cw / 2;

    tm_draw_bar(x, y, cw / 2 - 40, 14, (int)sys_cpu_percent, "CPU Usage:", clip);

    uint64_t total_bytes = total_system_memory, free_bytes = pmm_free_memory_bytes();
    uint64_t used_bytes = total_bytes > free_bytes ? total_bytes - free_bytes : 0;
    uint32_t total = total_bytes > 0xffffffffu ? 0xffffffffu : (uint32_t)total_bytes;
    uint32_t used = used_bytes > 0xffffffffu ? 0xffffffffu : (uint32_t)used_bytes;
    int mem_pct = total_bytes ? (int)(used_bytes * 100 / total_bytes) : 0;
    tm_draw_bar(col2, y, cw / 2 - 40, 14, mem_pct, "Memory Usage:", clip);
    y += 34;

    strcpy(buf, "Frames/sec: ");
    int_to_string(sys_frames_per_sec, num); tm_cat(buf, num);
    w98_text(buf, x, y, W98_BTNTEXT, W98_BTNFACE, 1, clip);

    strcpy(buf, "Physical memory: ");
    tm_fmt_kb(total, num); tm_cat(buf, num);
    w98_text(buf, col2, y, W98_BTNTEXT, W98_BTNFACE, 1, clip);
    y += 11;

    strcpy(buf, "Frames total: ");
    int_to_string(sys_frames_rendered, num); tm_cat(buf, num);
    w98_text(buf, x, y, W98_BTNTEXT, W98_BTNFACE, 1, clip);

    strcpy(buf, "Used/reserved: ");
    tm_fmt_kb(used, num); tm_cat(buf, num);
    w98_text(buf, col2, y, W98_BTNTEXT, W98_BTNFACE, 1, clip);
    y += 11;

    uint32_t secs = uptime_ticks / TICKS_PER_SEC;
    strcpy(buf, "Uptime: ");
    int_to_string(secs / 3600, num); tm_cat(buf, num); tm_cat(buf, "h ");
    int_to_string((secs / 60) % 60, num); tm_cat(buf, num); tm_cat(buf, "m ");
    int_to_string(secs % 60, num); tm_cat(buf, num); tm_cat(buf, "s");
    w98_text(buf, x, y, W98_BTNTEXT, W98_BTNFACE, 1, clip);

    strcpy(buf, "Free: ");
    tm_fmt_kb(total > used ? total - used : 0, num); tm_cat(buf, num);
    w98_text(buf, col2, y, W98_BTNTEXT, W98_BTNFACE, 1, clip);
    y += 11;

    strcpy(buf, "Screen: ");
    int_to_string(screen_width, num); tm_cat(buf, num); tm_cat(buf, "x");
    int_to_string(screen_height, num); tm_cat(buf, num); tm_cat(buf, " ");
    int_to_string(desktop.window_count, num); tm_cat(buf, num); tm_cat(buf, " window(s)");
    w98_text(buf, x, y, W98_BTNTEXT, W98_BTNFACE, 1, clip);

    strcpy(buf, "Timer: ");
    int_to_string(TICKS_PER_SEC, num); tm_cat(buf, num); tm_cat(buf, " Hz");
    w98_text(buf, col2, y, W98_BTNTEXT, W98_BTNFACE, 1, clip);
}

static void tm_draw_processes(window_t* w, tm_layout_t* L, const w98_rect_t* clip) {
    (void)clip;
    int x = w->rect.client_x + 6;
    int cw = w->rect.client_w - 12;
    int lh = L->btn_y + 22 - L->list_y;

    w98_surface(x, L->list_y, cw, lh, W98_BEVEL_SUNKEN, W98_WINDOW);
    w98_rect_t lc;
    lc.x = x + 2; lc.y = L->list_y + 2; lc.w = cw - 4; lc.h = lh - 4;

    int hy = L->list_y + 2;
    w98_fill(x + 2, hy, cw - 4, TM_ROW_H, W98_BTNFACE);
    w98_fill(x + 2, hy + TM_ROW_H - 1, cw - 4, 1, W98_BTNSHADOW);
    w98_text("PID", x + 8, hy + 3, W98_BTNTEXT, W98_BTNFACE, 1, &lc);
    w98_text("Name", x + 44, hy + 3, W98_BTNTEXT, W98_BTNFACE, 1, &lc);
    w98_text("State", x + 160, hy + 3, W98_BTNTEXT, W98_BTNFACE, 1, &lc);
    w98_text("CPU", x + 226, hy + 3, W98_BTNTEXT, W98_BTNFACE, 1, &lc);
    w98_text("Syscalls", x + 270, hy + 3, W98_BTNTEXT, W98_BTNFACE, 1, &lc);

    int y = hy + TM_ROW_H + 1;
    int rows = (lh - TM_ROW_H - 6) / TM_ROW_H;
    int shown = 0;
    char num[16];

    w98_text("0", x + 8, y + 3, W98_WINDOWTEXT, W98_WINDOW, 1, &lc);
    w98_text("kernel (sharkos)", x + 44, y + 3, W98_WINDOWTEXT, W98_WINDOW, 1, &lc);
    w98_text("Running", x + 160, y + 3, W98_WINDOWTEXT, W98_WINDOW, 1, &lc);
    int_to_string(sys_cpu_percent, num);
    w98_text(num, x + 226, y + 3, W98_WINDOWTEXT, W98_WINDOW, 1, &lc);
    w98_text("-", x + 270, y + 3, W98_WINDOWTEXT, W98_WINDOW, 1, &lc);
    y += TM_ROW_H; shown++;

    spin_lock(&task_list_lock);
    for (task_t* t = task_list; t && shown < rows; t = t->next) {
        const char* st = t->state == TASK_RUNNING ? "Running" :
                         t->state == TASK_READY ? "Ready" :
                         t->state == TASK_SLEEPING ? "Sleeping" : "Zombie";
        int_to_string((uint32_t)t->id, num);
        w98_text(num, x + 8, y + 3, W98_WINDOWTEXT, W98_WINDOW, 1, &lc);
        w98_text(t->name, x + 44, y + 3, W98_WINDOWTEXT, W98_WINDOW, 1, &lc);
        w98_text(st, x + 160, y + 3, W98_WINDOWTEXT, W98_WINDOW, 1, &lc);
        int_to_string(t->cpu_usage, num);
        w98_text(num, x + 226, y + 3, W98_WINDOWTEXT, W98_WINDOW, 1, &lc);
        int_to_string(t->syscall_count, num);
        w98_text(num, x + 270, y + 3, W98_WINDOWTEXT, W98_WINDOW, 1, &lc);
        y += TM_ROW_H; shown++;
    }
    spin_unlock(&task_list_lock);

    int pc = 0;
    plugin_t* pl = plugin_get_list(&pc);
    for (int i = 0; i < pc && shown < rows; i++) {
        int_to_string((uint32_t)(100 + i), num);
        w98_text(num, x + 8, y + 3, W98_GRAYTEXT, W98_WINDOW, 1, &lc);
        w98_text(pl[i].name[0] ? pl[i].name : (pl[i].info ? pl[i].info->name : "plugin"),
                 x + 44, y + 3, W98_GRAYTEXT, W98_WINDOW, 1, &lc);
        w98_text("Loaded", x + 160, y + 3, W98_GRAYTEXT, W98_WINDOW, 1, &lc);
        w98_text("0", x + 226, y + 3, W98_GRAYTEXT, W98_WINDOW, 1, &lc);
        w98_text("-", x + 270, y + 3, W98_GRAYTEXT, W98_WINDOW, 1, &lc);
        y += TM_ROW_H; shown++;
    }
}

void app_window_draw_taskmanager(window_t* w) {
    int cx = w->rect.client_x, cy = w->rect.client_y;
    int cw = w->rect.client_w, ch = w->rect.client_h;
    w98_fill(cx, cy, cw, ch, W98_BTNFACE);
    if (cw < 200 || ch < 120) {
        w98_rect_t small; small.x = cx; small.y = cy; small.w = cw; small.h = ch;
        w98_text("Window too small", cx + 6, cy + 6, W98_BTNTEXT, W98_BTNFACE, 1, &small);
        return;
    }

    w98_rect_t clip; clip.x = cx; clip.y = cy; clip.w = cw; clip.h = ch;
    tm_layout_t L; tm_layout(w, &L);
    tm_draw_tabs(w, &L, &clip);

    switch (tm_tab) {
        case TM_TAB_PERF:  tm_draw_performance(w, &L, &clip); break;
        case TM_TAB_PROCS: tm_draw_processes(w, &L, &clip);  break;
        default:           tm_draw_applications(w, &L, &clip); break;
    }

    char st[80], num[16];
    strcpy(st, "Windows: ");
    int_to_string((uint32_t)tm_count_task_rows(), num); tm_cat(st, num);
    tm_cat(st, "   CPU Usage: ");
    int_to_string(sys_cpu_percent, num); tm_cat(st, num); tm_cat(st, "%");
    tm_cat(st, "   FPS: ");
    int_to_string(sys_frames_per_sec, num); tm_cat(st, num);
    if (tm_tab != TM_TAB_APPS) {
        w98_bevel(cx + 4, L.btn_y + 26, cw - 8, 1, W98_BEVEL_ETCHED);
    }
    w98_text(st, cx + 8, cy + ch - 10, W98_BTNTEXT, W98_BTNFACE, 1, &clip);
}

void app_window_mouse_taskmanager(window_t* w, int mx, int my, int buttons) {
    if (!(buttons & 1)) return;
    tm_layout_t L; tm_layout(w, &L);
    int x = w->rect.client_x + 6;
    int cw = w->rect.client_w - 12;

    if (my >= L.tabs_y - 2 && my < L.tabs_y + 18) {
        int i = (mx - x) / 84;
        if (mx >= x && i >= 0 && i < 3) {
            tm_tab = i;
            w->needs_redraw = true;
            desktop.dirty = true;
        }
        return;
    }

    if (tm_tab != TM_TAB_APPS) return;

    int first_row_y = L.list_y + 2 + TM_ROW_H + 1;
    if (mx >= x && mx < x + cw && my >= first_row_y && my < L.list_y + L.list_h - 2) {
        int row = (my - first_row_y) / TM_ROW_H;
        int shown = 0;
        tm_selected = -1;
        tm_selected_z = -1;
        for (int i = 0; i < desktop.window_count; i++) {
            if (!desktop.windows[i].visible) continue;
            if (shown == row) {
                tm_selected = i;
                tm_selected_z = desktop.windows[i].z_order;
                break;
            }
            shown++;
        }
        w->needs_redraw = true;
        desktop.dirty = true;
        return;
    }

    int bw = 78, bh = 22;
    int bx_end = x + cw - bw;
    int bx_switch = bx_end - bw - 6;
    int bx_refresh = bx_end - 2 * (bw + 6);
    if (my >= L.btn_y && my < L.btn_y + bh) {
        if (mx >= bx_end && mx < bx_end + bw) {
            if (tm_selected >= 0 && tm_selected < desktop.window_count &&
                &desktop.windows[tm_selected] != w) {
                window_close(tm_selected);
                tm_selected = -1;
                tm_selected_z = -1;
            }
        } else if (mx >= bx_switch && mx < bx_switch + bw) {
            if (tm_selected >= 0 && tm_selected < desktop.window_count) {
                if (desktop.windows[tm_selected].state == WINDOW_STATE_MINIMIZED) {
                    window_restore(tm_selected);
                } else {
                    window_focus(tm_selected);
                }
            }
        } else if (mx >= bx_refresh && mx < bx_refresh + bw) {

        }
        for (int i = 0; i < desktop.window_count; i++) desktop.windows[i].needs_redraw = true;
        desktop.dirty = true;
    }
}

void app_window_keyboard_taskmanager(window_t* w, char c) {
    if (c == '\t') {
        tm_tab = (tm_tab + 1) % 3;
    } else if (c == 127 || c == 8) {
        if (tm_tab == TM_TAB_APPS && tm_selected >= 0 &&
            tm_selected < desktop.window_count && &desktop.windows[tm_selected] != w) {
            window_close(tm_selected);
            tm_selected = -1; tm_selected_z = -1;
        }
    } else if (c == '\n') {
        if (tm_tab == TM_TAB_APPS && tm_selected >= 0 && tm_selected < desktop.window_count) {
            if (desktop.windows[tm_selected].state == WINDOW_STATE_MINIMIZED) window_restore(tm_selected);
            else window_focus(tm_selected);
        }
    } else if (c == 27) {
        window_close_by_ptr(w);
        return;
    }
    w->needs_redraw = true;
    desktop.dirty = true;
}

void app_window_draw_fastfetch(window_t* w) {
    int cx = w->rect.client_x;
    int cy = w->rect.client_y;
    int cw = w->rect.client_w;

    w98_fill(cx, cy, cw, w->rect.client_h, W98_BTNFACE);

    w98_text_bold("SharkOS System Information", cx + 8, cy + 6, W98_BTNTEXT,
                  W98_BTNFACE, 1, NULL);
    w98_bevel(cx + 4, cy + 18, cw - 8, 2, W98_BEVEL_ETCHED);

    char buf[64];
    uint32_t total_mb = (uint32_t)(total_system_memory >> 20);
    uint32_t free_kb = (uint32_t)(pmm_free_memory_bytes() >> 10);
    uint32_t avail_kb = (uint32_t)(total_system_memory >> 10) - free_kb;
    uint32_t up_secs = uptime_ticks / TICKS_PER_SEC;
    uint32_t hours = up_secs / 3600;
    uint32_t mins = (up_secs % 3600) / 60;
    uint32_t secs = up_secs % 60;
    int task_count = 0;
    task_t* t;
    for (t = task_list; t; t = t->next) task_count++;
    char cpu_model[49];
    get_cpu_model(cpu_model);

    int y = cy + 26;
    int label_x = cx + 8;
    int value_x = cx + 110;
    int line_h = 13;

    label_value(label_x, value_x, y, "OS:", "SharkOS 98 (Sharkslayer)",
                W98_BTNTEXT); y += line_h;
    label_value(label_x, value_x, y, "Host:", "SharkOS PC", W98_BTNTEXT);
    y += line_h;
    label_value(label_x, value_x, y, "Kernel:", "SharkOS V2.2", W98_BTNTEXT);
    y += line_h;
    label_value(label_x, value_x, y, "Shell:", "nemo-shell", W98_BTNTEXT);
    y += line_h;
    label_value(label_x, value_x, y, "Arch:", "x86-64", W98_BTNTEXT);
    y += line_h + 4;
    label_value(label_x, value_x, y, "CPU:", cpu_model, W98_BTNTEXT);
    y += line_h;
    label_value(label_x, value_x, y, "Memory:", "", W98_BTNTEXT);
    int_to_string(total_mb, buf);
    kstrcat(buf, " MB");
    w98_text(buf, value_x, y, W98_BTNTEXT, W98_BTNFACE, 1, NULL);
    y += line_h;
    label_value(label_x, value_x, y, "Memory Free:", "", W98_BTNTEXT);
    int_to_string((uint32_t)(free_kb >> 10), buf);
    kstrcat(buf, " MB");
    w98_text(buf, value_x, y, W98_BTNTEXT, W98_BTNFACE, 1, NULL);
    y += line_h;
    label_value(label_x, value_x, y, "Memory Used:", "", W98_BTNTEXT);
    int_to_string((uint32_t)(avail_kb >> 10), buf);
    kstrcat(buf, " MB");
    w98_text(buf, value_x, y, W98_BTNTEXT, W98_BTNFACE, 1, NULL);
    y += line_h + 4;
    label_value(label_x, value_x, y, "Resolution:", "", W98_BTNTEXT);
    int_to_string((uint32_t)screen_width, buf);
    kstrcat(buf, "x");
    char b2[16];
    int_to_string((uint32_t)screen_height, b2);
    kstrcat(buf, b2);
    w98_text(buf, value_x, y, W98_BTNTEXT, W98_BTNFACE, 1, NULL);
    y += line_h + 4;
    label_value(label_x, value_x, y, "Uptime:", "", W98_BTNTEXT);
    int_to_string(hours, buf);
    kstrcat(buf, "h ");
    int_to_string(mins, b2);
    kstrcat(buf, b2);
    kstrcat(buf, "m ");
    int_to_string(secs, b2);
    kstrcat(buf, b2);
    kstrcat(buf, "s");
    w98_text(buf, value_x, y, W98_BTNTEXT, W98_BTNFACE, 1, NULL);
    y += line_h;
    label_value(label_x, value_x, y, "Tasks:", "", W98_BTNTEXT);
    int_to_string((uint32_t)task_count, buf);
    w98_text(buf, value_x, y, W98_BTNTEXT, W98_BTNFACE, 1, NULL);
    y += line_h;
    label_value(label_x, value_x, y, "Theme:",
                theme_get_name(theme_get_id()), W98_BTNTEXT);
}

static char notepad_buffer[MAX_FILE_CONTENT_SIZE] = "";
static int notepad_len = 0;
static char notepad_path[128] = "";
static int notepad_cursor = 0;
static int np_anchor=-1,np_drag;
static int notepad_view_line = 0;
static bool notepad_dirty = false;
static bool notepad_highlight_dirty = true;
static bool notepad_cursor_visible = true;
static struct fs_node* notepad_file = NULL;
static char notepad_status[48] = "Ready";

static void notepad_save(void) {
    if (!notepad_file) {
        strcpy(notepad_status, "No file selected");
        return;
    }
    if (notepad_len >= MAX_FILE_CONTENT_SIZE) {
        strcpy(notepad_status, "File exceeds filesystem limit");
        return;
    }
    if(notepad_file->disk_backed){
        if ((unsigned)notepad_len >= notepad_file->content_capacity) {strcpy(notepad_status,"Mounted file capacity exceeded");return;}
        memcpy(notepad_file->content,notepad_buffer,notepad_len); notepad_file->content_len=notepad_len;notepad_file->content[notepad_len]=0;
    } else if(fs_node_write(notepad_file,0,notepad_buffer,notepad_len) || fs_node_truncate(notepad_file,notepad_len)) {
        strcpy(notepad_status,"Not enough file space");return;
    }
    if (!fs_node_sync(notepad_file)) {
        strcpy(notepad_status, "Disk full or file too large");
        return;
    }
    if (store_installed() && !store_sync()) {
        strcpy(notepad_status, "Not saved to installed disk");
        return;
    }
    notepad_dirty = false;
    strcpy(notepad_status, "Saved");
}

void notepad_open_node(struct fs_node* file) {
    if (!file || file->type != FS_FILE) return;
    np_anchor=-1;np_drag=0;
    notepad_file = file;
    notepad_len = file->content_len;
    if (notepad_len >= (int)sizeof(notepad_buffer)) notepad_len = (int)sizeof(notepad_buffer) - 1;
    memcpy(notepad_buffer, file->content, (size_t)notepad_len);
    notepad_buffer[notepad_len] = 0;
    notepad_cursor = notepad_len;
    notepad_view_line = 0;
    notepad_cursor_visible = true;
    notepad_dirty = false;
    notepad_highlight_dirty = true;
    notepad_status[0] = 0;
    char parts[8][32];
    int count = 0;
    struct fs_node* n = file;
    while (n && n != root && count < 8) {
        strcpy(parts[count++], n->name);
        n = n->parent;
    }
    int pos = 0;
    notepad_path[pos++] = '/';
    for (int i = count - 1; i >= 0 && pos < (int)sizeof(notepad_path) - 2; i--) {
        for (int j = 0; parts[i][j] && pos < (int)sizeof(notepad_path) - 2; j++) notepad_path[pos++] = parts[i][j];
        if (i) notepad_path[pos++] = '/';
    }
    notepad_path[pos] = 0;
    desktop_icon_launch_offset(WINDOW_TYPE_NOTEPAD, "Shark IDE");
}

void notepad_open_file(const char* path) {
    struct fs_node* file = fs_resolve_path(path);
    if (!file && path && !strchr(path, '/') && !strchr(path, '\\')) file = create_node(path, FS_FILE, current_dir);
    notepad_open_node(file);
}

static uint32_t notepad_colors[MAX_FILE_CONTENT_SIZE];

static int notepad_word_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int notepad_keyword(const char* text, int len) {
    static const char* words[] = {"int", "char", "void", "return", "if", "else", "for", "while", "struct", "const", "static", "uint8_t", "uint32_t", "size_t", "include", "define", "bool", "unsigned", "break", "switch", "case", "NULL"};
    for (int k = 0; k < (int)(sizeof(words) / sizeof(words[0])); k++) {
        int word_len = (int)strlen(words[k]);
        if (word_len != len) continue;
        int match = 1;
        for (int j = 0; j < len; j++) if (text[j] != words[k][j]) match = 0;
        if (match) return 1;
    }
    return 0;
}

static void notepad_highlight(void) {
    if (!notepad_highlight_dirty) return;
    int i = 0;
    while (i < notepad_len) {
        uint32_t color = W98_WINDOWTEXT;
        if (i + 1 < notepad_len && notepad_buffer[i] == '/' && notepad_buffer[i + 1] == '/') {
            int end = i;
            while (end < notepad_len && notepad_buffer[end] != '\n') notepad_colors[end++] = 0xFF6A9955u;
            i = end;
            continue;
        }
        if (i + 1 < notepad_len && notepad_buffer[i] == '/' && notepad_buffer[i + 1] == '*') {
            int end = i;
            while (end < notepad_len) {
                notepad_colors[end] = 0xFF6A9955u;
                if (notepad_buffer[end] == '*' && end + 1 < notepad_len && notepad_buffer[end + 1] == '/') {
                    notepad_colors[end + 1] = 0xFF6A9955u;
                    end += 2;
                    break;
                }
                end++;
            }
            i = end;
            continue;
        }
        if (notepad_buffer[i] == '"' || notepad_buffer[i] == 39) {
            char quote = notepad_buffer[i];
            int end = i;
            int escaped = 0;
            while (end < notepad_len) {
                char c = notepad_buffer[end];
                notepad_colors[end++] = 0xFFCE9178u;
                if (c == quote && !escaped && end - 1 != i) break;
                if (c == '\\' && !escaped) escaped = 1;
                else escaped = 0;
            }
            i = end;
            continue;
        }
        if (notepad_buffer[i] >= '0' && notepad_buffer[i] <= '9') {
            int end = i;
            while (end < notepad_len && ((notepad_buffer[end] >= '0' && notepad_buffer[end] <= '9') || (notepad_buffer[end] >= 'a' && notepad_buffer[end] <= 'z') || (notepad_buffer[end] >= 'A' && notepad_buffer[end] <= 'Z') || notepad_buffer[end] == '.' || notepad_buffer[end] == '_')) notepad_colors[end++] = 0xFFB5CEA8u;
            i = end;
            continue;
        }
        if (notepad_word_char(notepad_buffer[i])) {
            int end = i + 1;
            while (end < notepad_len && (notepad_word_char(notepad_buffer[end]) || (notepad_buffer[end] >= '0' && notepad_buffer[end] <= '9'))) end++;
            if (notepad_keyword(notepad_buffer + i, end - i)) color = 0xFF569CD6u;
            for (int j = i; j < end; j++) notepad_colors[j] = color;
            i = end;
            continue;
        }
        notepad_colors[i++] = color;
    }
    notepad_highlight_dirty = false;
}

#define NOTEPAD_MENU_H 16

static const char* notepad_menus[] = { "File", "Edit", "Search", "Help" };


static void notepad_draw_header(window_t* w) {
    int cx = w->rect.client_x;
    int cy = w->rect.client_y;
    int cw = w->rect.client_w;
    w98_fill(cx, cy, cw, NOTEPAD_MENU_H, W98_BTNFACE);
    int mx = cx + 2;
    for (int i = 0; i < 4; i++) {
        int tw = w98_text_width(notepad_menus[i], 1) + 10;
        w98_text(notepad_menus[i], mx + 5, cy + 4, W98_BTNTEXT, W98_BTNFACE, 1, NULL);
        mx += tw;
    }
    char title[160];
    if (notepad_path[0]) strcpy(title, notepad_path);
    else strcpy(title, "Untitled");
    if (notepad_dirty) {
        int n = (int)strlen(title);
        title[n++] = ' ';
        title[n++] = '*';
        title[n] = 0;
    }
    w98_text(title, cx + cw - w98_text_width(title, 1) - 6, cy + 4,
             W98_GRAYTEXT, W98_BTNFACE, 1, NULL);
}

static void notepad_draw_footer(window_t* w) {
    int cx = w->rect.client_x;
    int cy = w->rect.client_y;
    int cw = w->rect.client_w;
    int ch = w->rect.client_h;
    w98_fill(cx + 2, cy + ch - 16, cw - 4, 14, W98_BTNFACE);
    w98_text("C IDE  |  Ctrl+S Save  |  Tab Indent  |  Arrows Move",
             cx + 6, cy + ch - 12, W98_BTNTEXT, W98_BTNFACE, 1, NULL);
    if (notepad_status[0])
        w98_text(notepad_status, cx + cw - w98_text_width(notepad_status, 1) - 8,
                 cy + ch - 12, W98_HIGHLIGHT, W98_BTNFACE, 1, NULL);
}

extern void mouse_restore_under_cursor(void);
extern void mouse_draw_cursor(void);

static int notepad_line_start(int target_line) {
    int index = 0;
    int line = 0;
    while (index < notepad_len && line < target_line) {
        if (notepad_buffer[index++] == '\n') line++;
    }
    return index;
}

static void notepad_metrics(window_t* w, int* ta_x, int* ta_y, int* ta_w,
                            int* ta_h, int* text_x, int* max_chars,
                            int* visible_lines) {
    *ta_x = w->rect.client_x + 2;
    *ta_y = w->rect.client_y + NOTEPAD_MENU_H + 2;
    *ta_w = w->rect.client_w - 4;
    *ta_h = w->rect.client_h - NOTEPAD_MENU_H - 22;
    *text_x = *ta_x + 32;
    *max_chars = (*ta_w - 40) / 6;
    if (*max_chars > 90) *max_chars = 90;
    *visible_lines = (*ta_h - 8) / 11;
}

void app_window_mouse_notepad(window_t *w,int mx,int my,int buttons){
    if(!(buttons&1)){np_drag=0;return;}
    int ax,ay,aw,ah,tx,cols,rows;notepad_metrics(w,&ax,&ay,&aw,&ah,&tx,&cols,&rows);
    if(rows<1 || cols<1 || (!np_drag && (my<ay || my>=ay+ah)))return;
    int pos=notepad_line_start(notepad_view_line+bounded((my-ay-4)/11,0,rows-1));
    int col=bounded((mx-tx+3)/6,0,cols);while(col-->0 && pos<notepad_len && notepad_buffer[pos]!='\n')pos++;
    if(!np_drag && (!shift_pressed || np_anchor<0))np_anchor=shift_pressed?notepad_cursor:pos;
    np_drag=1;notepad_cursor=pos;w->needs_redraw=desktop.dirty=true;
}
static void notepad_draw_line(window_t* w, int logical_line) {
    int ta_x, ta_y, ta_w, ta_h, text_x, max_chars, visible_lines;
    notepad_metrics(w, &ta_x, &ta_y, &ta_w, &ta_h, &text_x, &max_chars, &visible_lines);
    int row = logical_line - notepad_view_line;
    if (row < 0 || row >= visible_lines || max_chars < 1) return;
    int line_start = notepad_line_start(logical_line);
    int line_end = line_start;
    while (line_end < notepad_len && notepad_buffer[line_end] != '\n') line_end++;
    int line_len = line_end - line_start;
    if (line_len > max_chars) line_len = max_chars;
    int y = ta_y + 4 + row * 11;
    w98_fill(ta_x + 3, y - 1, 26, 12, 0xFFF1F1F1u);
    w98_fill(text_x, y - 1, max_chars * 6, 12, W98_WINDOW);
    char number[12];
    int_to_string((uint32_t)(logical_line + 1), number);
    w98_text(number, ta_x + 4, y, 0xFF777777u, 0xFFF1F1F1u, 1, NULL);
    int run_start = 0;
    while (run_start < line_len) {
        uint32_t color = notepad_colors[line_start + run_start];
        int sel=np_anchor>=0 && within(line_start+run_start,np_anchor,notepad_cursor);
        int run_end=run_start+1;
        while(run_end<line_len && notepad_colors[line_start+run_end]==color && sel==(np_anchor>=0 && within(line_start+run_end,np_anchor,notepad_cursor)))run_end++;
        char run[96];
        int run_len = run_end - run_start;
        for (int i = 0; i < run_len; i++) run[i] = notepad_buffer[line_start + run_start + i];
        run[run_len] = 0;
        w98_text(run, text_x + run_start * 6, y, sel?W98_HIGHLIGHTTEXT:color, sel?W98_HIGHLIGHT:W98_WINDOW, 1, NULL);
        run_start = run_end;
    }
}

static void notepad_cursor_position(int* line, int* col) {
    *line = 0;
    *col = 0;
    for (int i = 0; i < notepad_cursor && i < notepad_len; i++) {
        if (notepad_buffer[i] == '\n') { (*line)++; *col = 0; }
        else (*col)++;
    }
}

static void notepad_draw_cursor(window_t* w) {
    if (!notepad_cursor_visible) return;
    int line, col;
    notepad_cursor_position(&line, &col);
    int ta_x, ta_y, ta_w, ta_h, text_x, max_chars, visible_lines;
    notepad_metrics(w, &ta_x, &ta_y, &ta_w, &ta_h, &text_x, &max_chars, &visible_lines);
    int row = line - notepad_view_line;
    if (row < 0 || row >= visible_lines || col < 0 || col >= max_chars) return;
    w98_fill(text_x + col * 6, ta_y + 4 + row * 11, 6, 11, W98_HIGHLIGHT);
}

static void notepad_direct_update(window_t* w, int old_line, bool redraw_all,
                                  bool redraw_header, bool redraw_footer) {
    notepad_highlight();
    int line, col;
    notepad_cursor_position(&line, &col);
    int ta_x, ta_y, ta_w, ta_h, text_x, max_chars, visible_lines;
    notepad_metrics(w, &ta_x, &ta_y, &ta_w, &ta_h, &text_x, &max_chars, &visible_lines);
    if (visible_lines < 1) redraw_all = true;
    int old_view = notepad_view_line;
    if (line < notepad_view_line) notepad_view_line = line;
    if (visible_lines > 0 && line >= notepad_view_line + visible_lines)
        notepad_view_line = line - visible_lines + 1;
    if (notepad_view_line != old_view) redraw_all = true;
    w98_classic_font_force = true;
    mouse_restore_under_cursor();
    if (redraw_all) {
        app_window_draw_notepad(w);
        mouse_draw_cursor();
        flush_rows_to_hw(w->rect.client_y, w->rect.client_y + w->rect.client_h);
        return;
    }
    notepad_draw_line(w, old_line);
    if (line != old_line) notepad_draw_line(w, line);
    if (redraw_header) notepad_draw_header(w);
    if (redraw_footer) notepad_draw_footer(w);
    notepad_draw_cursor(w);
    w98_classic_font_force = false;
    mouse_draw_cursor();
    int old_row = old_line - notepad_view_line;
    if (old_row >= 0 && old_row < visible_lines) {
        int y = ta_y + 3 + old_row * 11;
        flush_rows_to_hw(y, y + 13);
    }
    int row = line - notepad_view_line;
    if (row >= 0 && row < visible_lines && line != old_line) {
        int y = ta_y + 3 + row * 11;
        flush_rows_to_hw(y, y + 13);
    }
    if (redraw_header) flush_rows_to_hw(w->rect.client_y, w->rect.client_y + NOTEPAD_MENU_H);
    if (redraw_footer) flush_rows_to_hw(w->rect.client_y + w->rect.client_h - 16,
                                        w->rect.client_y + w->rect.client_h);
    if (mouse_cursor_y >= 0 && mouse_cursor_y < (int)screen_height)
        flush_rows_to_hw(mouse_cursor_y, mouse_cursor_y + 16);
}

void app_window_draw_notepad(window_t* w) {
    int cx = w->rect.client_x;
    int cy = w->rect.client_y;
    int cw = w->rect.client_w;
    int ch = w->rect.client_h;
    int ta_x, ta_y, ta_w, ta_h, text_x, max_chars, visible_lines;
    notepad_metrics(w, &ta_x, &ta_y, &ta_w, &ta_h, &text_x, &max_chars, &visible_lines);
    w98_classic_font_force = true;
    w98_fill(cx, cy, cw, ch, W98_BTNFACE);
    notepad_draw_header(w);
    w98_surface(ta_x, ta_y, ta_w, ta_h, W98_BEVEL_SUNKEN, W98_WINDOW);
    w98_fill(ta_x + 3, ta_y + 3, 26, ta_h - 6, 0xFFF1F1F1u);
    notepad_highlight();
    int cursor_line, cursor_col;
    notepad_cursor_position(&cursor_line, &cursor_col);
    if (cursor_line < notepad_view_line) notepad_view_line = cursor_line;
    if (visible_lines > 0 && cursor_line >= notepad_view_line + visible_lines)
        notepad_view_line = cursor_line - visible_lines + 1;
    for (int row = 0; row < visible_lines; row++)
        notepad_draw_line(w, notepad_view_line + row);
    notepad_draw_footer(w);
    notepad_draw_cursor(w);
    w98_classic_font_force = false;
}

void notepad_cursor_tick(window_t* w) {
    if (!w || !w->visible) return;
    int line, col;
    notepad_cursor_position(&line, &col);
    notepad_cursor_visible = !notepad_cursor_visible;
    w98_classic_font_force = true;
    mouse_restore_under_cursor();
    notepad_draw_line(w, line);
    notepad_draw_cursor(w);
    w98_classic_font_force = false;
    mouse_draw_cursor();
    int ta_x, ta_y, ta_w, ta_h, text_x, max_chars, visible_lines;
    notepad_metrics(w, &ta_x, &ta_y, &ta_w, &ta_h, &text_x, &max_chars, &visible_lines);
    int row = line - notepad_view_line;
    if (row >= 0 && row < visible_lines) {
        int y = ta_y + 3 + row * 11;
        flush_rows_to_hw(y, y + 13);
    }
    if (mouse_cursor_y >= 0 && mouse_cursor_y < (int)screen_height)
        flush_rows_to_hw(mouse_cursor_y, mouse_cursor_y + 16);
}

void app_window_keyboard_notepad(window_t* w, char c) {
    if (!w) return;
    int old_cursor = notepad_cursor;
    int old_line, old_col;
    notepad_cursor_position(&old_line, &old_col);
    bool was_dirty = notepad_dirty;
    char old_status[48];
    strcpy(old_status, notepad_status);
    bool changed_text = false;
    bool redraw_all = false;
    bool handled = true;

    int lo=np_anchor<notepad_cursor?np_anchor:notepad_cursor, hi=np_anchor>notepad_cursor?np_anchor:notepad_cursor;
    bool selected=np_anchor>=0 && lo<hi, arrow=(c>=0x10 && c<=0x13);
    if(c==1){np_anchor=0;notepad_cursor=notepad_len;desktop.dirty=w->needs_redraw=true;return;}
    if(selected && c==3){desktop_copy_text(notepad_buffer+lo,hi-lo);strcpy(notepad_status,"Selection copied");desktop.dirty=true;return;}
    if(arrow){if(shift_pressed && np_anchor<0)np_anchor=notepad_cursor;if(!shift_pressed)np_anchor=-1;redraw_all=selected || shift_pressed;}
    if(selected && !(ctrl_pressed && (c=='s'||c=='S')) && (c==22 || c=='\b' || c=='\t' || c=='\n' || c>=32)){
        if(c==22 && (!*desktop_paste_text() || strlen(desktop_paste_text())>=sizeof notepad_buffer-(notepad_len-hi+lo)))return;
        kmemmove(notepad_buffer+lo,notepad_buffer+hi,notepad_len-hi);notepad_len-=hi-lo;notepad_buffer[notepad_len]=0;notepad_cursor=lo;
        np_anchor=-1;redraw_all=changed_text=true;if(c=='\b')c=0;
    } else if(!arrow && c!=3 && !(ctrl_pressed && (c=='s'||c=='S')))np_anchor=-1;
    if (c==3) {
        int a=notepad_cursor,b=a; while(a && notepad_buffer[a-1]!='\n')a--;
        while(b<notepad_len && notepad_buffer[b]!='\n')b++;
        desktop_copy_text(notepad_buffer+a,b-a); strcpy(notepad_status,"Copied current line");
    } else if(c==22) {
        const char *t=desktop_paste_text(); unsigned n=strlen(t);
        if (n && n<sizeof notepad_buffer-notepad_len) {
            kmemmove(notepad_buffer+notepad_cursor+n,notepad_buffer+notepad_cursor,notepad_len-notepad_cursor);
            memcpy(notepad_buffer+notepad_cursor,t,n);notepad_len+=n;notepad_cursor+=n;notepad_buffer[notepad_len]=0;
            changed_text=redraw_all=true;
        }
    } else if (ctrl_pressed && (c == 's' || c == 'S')) {
        notepad_save();
    } else if (c == 0x12) {
        if (notepad_cursor > 0) notepad_cursor--;
    } else if (c == 0x13) {
        if (notepad_cursor < notepad_len) notepad_cursor++;
    } else if (c == 0x10 || c == 0x11) {
        int current_start = notepad_cursor;
        while (current_start > 0 && notepad_buffer[current_start - 1] != '\n') current_start--;
        int col = notepad_cursor - current_start;
        if (c == 0x10 && current_start > 0) {
            int prev_end = current_start - 1;
            int prev_start = prev_end;
            while (prev_start > 0 && notepad_buffer[prev_start - 1] != '\n') prev_start--;
            int prev_len = prev_end - prev_start;
            notepad_cursor = prev_start + (col < prev_len ? col : prev_len);
        } else if (c == 0x11) {
            int current_end = notepad_cursor;
            while (current_end < notepad_len && notepad_buffer[current_end] != '\n') current_end++;
            if (current_end < notepad_len) {
                int next_start = current_end + 1;
                int next_end = next_start;
                while (next_end < notepad_len && notepad_buffer[next_end] != '\n') next_end++;
                int next_len = next_end - next_start;
                notepad_cursor = next_start + (col < next_len ? col : next_len);
            }
        }
    } else if (c == '\t') {
        for (int n = 0; n < 4 && notepad_len < (int)sizeof(notepad_buffer) - 1; n++) {
            kmemmove(notepad_buffer + notepad_cursor + 1, notepad_buffer + notepad_cursor,
                     notepad_len - notepad_cursor);
            notepad_buffer[notepad_cursor++] = ' ';
            notepad_len++;
            changed_text = true;
        }
    } else if (c == '\n') {
        if (notepad_len < (int)sizeof(notepad_buffer) - 1) {
            if (notepad_cursor < notepad_len)
                kmemmove(notepad_buffer + notepad_cursor + 1, notepad_buffer + notepad_cursor,
                         notepad_len - notepad_cursor);
            notepad_buffer[notepad_cursor++] = '\n';
            notepad_len++;
            changed_text = true;
            redraw_all = true;
        }
    } else if (c == '\b') {
        if (notepad_cursor > 0) {
            if (notepad_buffer[notepad_cursor - 1] == '\n') redraw_all = true;
            notepad_cursor--;
            kmemmove(notepad_buffer + notepad_cursor, notepad_buffer + notepad_cursor + 1,
                     notepad_len - notepad_cursor - 1);
            notepad_len--;
            changed_text = true;
        }
    } else if (c >= 32 && c < 127) {
        if (notepad_len < (int)sizeof(notepad_buffer) - 1) {
            if (notepad_cursor < notepad_len)
                kmemmove(notepad_buffer + notepad_cursor + 1, notepad_buffer + notepad_cursor,
                         notepad_len - notepad_cursor);
            notepad_buffer[notepad_cursor++] = c;
            notepad_len++;
            changed_text = true;
        }
    } else {
        handled = false;
    }

    if (!handled && !changed_text) return;
    if (changed_text) {
        notepad_dirty = true;
        notepad_highlight_dirty = true;
        strcpy(notepad_status, "Modified");
    }
    notepad_cursor_visible = true;
    notepad_direct_update(w, old_line, redraw_all, was_dirty != notepad_dirty,
                          strcmp(old_status, notepad_status) != 0);
    (void)old_cursor;
    (void)old_col;
}

static struct fs_node *fm_dir, *fm_target;
static int fm_sel, fm_top, fm_mode, fm_anchor;
static uint64_t fm_mask;
static uint64_t filebit(int row){return row>0 && row<=64?1ULL<<(row-1):0;}
static void fm_select(int row,bool range,bool toggle){
    uint64_t mask=filebit(row);
    if(range && fm_anchor>0 && row>0){mask=0;for(int i=1;i<=64;i++)if(within(i,row,fm_anchor)||i==row||i==fm_anchor)mask|=filebit(i);}
    else fm_anchor=row;
    fm_mask=toggle?(fm_mask^mask):mask;fm_sel=row;
}

static char fm_name[32], fm_msg[100];
static struct fs_node *fm_clicked;
static uint32_t fm_click_tick;
static void fm_enter(struct fs_node *dir) {
    fm_dir = dir;fm_anchor=0;fm_mask=0;
    fm_sel = fm_top = fm_mode = 0;
    fm_clicked = fm_target = NULL;
    fm_msg[0] = 0;
}
static void fm_check(void) {
    if (!fm_dir || !fm_dir->allocated || fm_dir->type != FS_DIRECTORY)
        fm_enter(root);
    if (fm_sel > fm_dir->num_children)
        fm_sel = fm_dir->num_children;
}
static int fm_path(struct fs_node *n, char *out, int cap) {
    struct fs_node *chain[MAX_NODES];
    int d = 0, p = 0;
    while (n && n != root && d < MAX_NODES) {
        chain[d++] = n;
        n = n->parent;
    }
    if (n != root)
        return 0;
    if (!d)
        out[p++] = '/';
    while (d) {
        const char *s = chain[--d]->name;
        if (p + strlen(s) + 2 > (unsigned)cap)
            return 0;
        out[p++] = '/';
        while (*s)
            out[p++] = *s++;
    }
    out[p] = 0;
    return 1;
}
static int fm_rows(window_t *w) {
    int n = (w->rect.client_h - 104) / 16;
    return n < 1 ? 1 : n;
}
static const char *fm_type(struct fs_node *n) {
    if (n->type == FS_DIRECTORY)
        return "Folder";
    const char *dot = NULL;
    for (char *p = n->name; *p; p++)
        if (*p == '.')
            dot = p;
    if (dot && (!strcasecmp(dot, ".png") || !strcasecmp(dot, ".jpg") || !strcasecmp(dot, ".jpeg") ||
                !strcasecmp(dot, ".gif") || !strcasecmp(dot, ".webp") || !strcasecmp(dot, ".svg")))
        return "Image";
    if (dot && (!strcasecmp(dot, ".htm") || !strcasecmp(dot, ".html")))
        return "Web page";
    for (int i = 0; i < n->content_len; i++)
        if (!n->content[i])
            return "Binary";
    return "Text";
}
static void fm_open(void) {
    if (!fm_sel) {
        if (fm_dir->parent)
            fm_enter(fm_dir->parent);
        return;
    }
    struct fs_node *n = fm_dir->children[fm_sel - 1];
    if (n->type == FS_DIRECTORY) {
        fm_enter(n);
        return;
    }
    const char *type = fm_type(n);
    if (!strcmp(type, "Image") || !strcmp(type, "Web page")) {
        char url[512] = "file:";
        if (!fm_path(n, url + 5, sizeof url - 5)) {
            strcpy(fm_msg, "Path too long to open");
            return;
        }
        browser_open_url(url);
        return;
    }
    if (!strcmp(type, "Binary")) {
        strcpy(fm_msg, "Binary file: not opened as text");
        return;
    }
    if (n->content_len >= MAX_FILE_CONTENT_SIZE) {
        strcpy(fm_msg, "Too large for the text editor");
        return;
    }
    if(!desktop_app_available(WINDOW_TYPE_NOTEPAD)){strcpy(fm_msg,"To edit text: spkg install ide");return;}
    notepad_open_node(n);
}
static void fm_edit(int mode) {
    for (struct fs_node *p = fm_dir; p; p = p->parent)
        if (fat16_is_mounted(p)) {
            strcpy(fm_msg, "Use the shell for mounted FAT volumes");
            return;
        }
    if(mode>=3 && (fm_mask & (fm_mask-1))){strcpy(fm_msg,"Select only one item for this operation");return;}
    if (mode >= 3 && (!fm_sel || !(fm_mask&filebit(fm_sel)))) {
        strcpy(fm_msg, "Select an item first");
        return;
    }
    fm_mode = mode;
    fm_target = fm_sel ? fm_dir->children[fm_sel - 1] : NULL;
    fm_name[0] = 0;
    fm_msg[0] = 0;
    if (mode == 3)
        strcpy(fm_name, fm_target->name);
}
static void fm_apply(void) {
    int mode = fm_mode;
    fm_mode = 0;
    if (mode >= 3 &&
        (!fm_target || !fm_target->allocated || fm_target->parent != fm_dir || fat16_is_mounted(fm_target))) {
        strcpy(fm_msg, "Item unavailable");
        return;
    }
    if (mode == 4) {
        if (fm_target == current_dir || fm_target == notepad_file || fs_node_remove(fm_target)) {
            strcpy(fm_msg, "Cannot delete: open or not empty");
            return;
        }
        fm_sel = 0;
    } else {
        if (!fm_name[0] || !strcmp(fm_name, ".") || !strcmp(fm_name, "..") || strchr(fm_name, '/') ||
            strchr(fm_name, '\\')) {
            strcpy(fm_msg, "Invalid name");
            return;
        }
        struct fs_node *dup = find_node(fm_dir, fm_name);
        if (dup && !(mode == 3 && dup == fm_target)) {
            strcpy(fm_msg, "That name is in use");
            return;
        }
        if (mode == 3) {
            strcpy(fm_target->name, fm_name);
            fs_cache_clear();
        } else if (!create_node(fm_name, mode == 1 ? FS_DIRECTORY : FS_FILE, fm_dir)) {
            strcpy(fm_msg, "Filesystem full");
            return;
        } else
            fm_sel = fm_dir->num_children;
    }
    fm_select(fm_sel,false,false);fm_clicked = NULL;
    store_sync();
    strcpy(fm_msg, store_status());
}
static const char *places[]={"This PC","Home","Documents","Downloads"};
static struct fs_node *fm_place(int i) {
    struct fs_node *home=find_node(root,current_user);
    if(!home)home=find_node(root,"User");
    if(i==0)return root;
    if(i==1)return home;
    if(i==2)return home?find_node(home,"Documents"):NULL;
    return find_node(root,"Downloads");
}
static uint8_t copied_data[256*1024];
static struct {char name[32];unsigned offset,len;} copied[MAX_CHILDREN];
static int copied_count;
static void fm_copy(void){
    unsigned used=0;int count=0;
    for(int i=0;i<fm_dir->num_children;i++)if(fm_mask&filebit(i+1)){
        struct fs_node *n=fm_dir->children[i];
        if(n->type!=FS_FILE || (unsigned)n->content_len>sizeof copied_data-used){strcpy(fm_msg,"Copy files only, up to 256 KiB total");return;}
        used+=n->content_len;count++;
    }
    if(!count){strcpy(fm_msg,"Select files to copy");return;}
    copied_count=0;used=0;
    for(int i=0;i<fm_dir->num_children;i++)if(fm_mask&filebit(i+1)){
        struct fs_node *n=fm_dir->children[i];int j=copied_count++;strcpy(copied[j].name,n->name);
        copied[j].offset=used;copied[j].len=n->content_len;memcpy(copied_data+used,fs_node_data(n),n->content_len);used+=n->content_len;
    }
    strcpy(fm_msg,"Selection copied. Open destination and paste.");
}
static void fm_paste(void){
    if(!copied_count){strcpy(fm_msg,"No files copied");return;}
    for(struct fs_node *p=fm_dir;p;p=p->parent)if(fat16_is_mounted(p)){strcpy(fm_msg,"Cannot paste into mounted FAT volume");return;}
    if(copied_count>MAX_CHILDREN-fm_dir->num_children){strcpy(fm_msg,"Folder is full");return;}
    struct fs_node *made[MAX_CHILDREN];int done=0;uint64_t added=0;
    for(int j=0;j<copied_count;j++){
        char name[32];strcpy(name,copied[j].name);
        for(unsigned i=1;find_node(fm_dir,name);i++){
            char num[12];int_to_string(i,num);strcpy(name,"Copy");kstrcat(name,num);kstrcat(name,"-");
            int k=strlen(name);for(int q=0;k<31 && copied[j].name[q];)name[k++]=copied[j].name[q++];name[k]=0;
        }
        struct fs_node *n=create_node(name,FS_FILE,fm_dir);if(!n)goto fail;made[done++]=n;
        if(fs_node_write(n,0,copied_data+copied[j].offset,copied[j].len))goto fail;
        added|=filebit(fm_dir->num_children);
    }
    fm_anchor=fm_sel=fm_dir->num_children;fm_mask=added;strcpy(fm_msg,"Files pasted");
    if(store_installed() && !store_sync())strcpy(fm_msg,"Pasted in RAM; disk save failed");return;
fail:
    while(done)fs_node_remove(made[--done]);strcpy(fm_msg,"Paste failed: filesystem full");
}
static const char *fm_tools[] = {"Up", "Folder", "File", "Rename", "Delete"};
void app_window_draw_filemanager(window_t *w) {
    fm_check();
    int x = w->rect.client_x, y = w->rect.client_y, cw = w->rect.client_w, ch = w->rect.client_h;
    w98_rect_t clip = {x + 3, y + 2, cw - 6, ch - 4};
    int bw = (cw - 8) / 5;
    if (bw > 66)
        bw = 66;
    w98_fill(x, y, cw, ch, W98_BTNFACE);
    for (int i = 0; i < 5; i++)
        w98_button(x + 3 + i * bw, y + 2, bw - 1, 18, fm_tools[i], 0, !fm_mode, 0);
    char path[512];
    if (!fm_path(fm_dir, path, sizeof path))
        strcpy(path, ".../");
    w98_surface(x + 3, y + 23, cw - 6, 19, W98_BEVEL_SUNKEN, W98_WINDOW);
    w98_text(path, x + 8, y + 28, W98_WINDOWTEXT, W98_WINDOW, 1, &clip);
    w98_surface(x + 3, y + 45, cw - 6, ch - 85, W98_BEVEL_SUNKEN, W98_WINDOW);
    int side=cw>=460?116:0;
    if(side){
        w98_fill(x+5,y+47,side-3,ch-89,W98_BTNFACE);
        w98_text_bold("Quick access",x+10,y+51,W98_BTNTEXT,W98_BTNFACE,1,&clip);
        for(int i=0;i<4;i++)w98_text(places[i],x+14,y+75+i*20,W98_BTNTEXT,W98_BTNFACE,1,&clip);
    }
    w98_fill(x+5+side,y+47,cw-10-side,16,W98_BTNFACE);
    w98_text("Name",x+25+side,y+51,W98_BTNTEXT,W98_BTNFACE,1,&clip);
    if(cw>=310)w98_text("Size",x+cw-90,y+51,W98_BTNTEXT,W98_BTNFACE,1,&clip);
    int rows = fm_rows(w);
    if (fm_sel < fm_top)
        fm_top = fm_sel;
    if (fm_sel >= fm_top + rows)
        fm_top = fm_sel - rows + 1;
    for (int row = 0; row < rows && fm_top + row <= fm_dir->num_children; row++) {
        int i = fm_top + row, yy = y + 64 + row * 16;
        struct fs_node *n = i ? fm_dir->children[i - 1] : NULL;
        uint32_t bg = ((fm_mask&filebit(i)) || (!i && !fm_sel)) ? W98_HIGHLIGHT : W98_WINDOW,
                 fg = ((fm_mask&filebit(i)) || (!i && !fm_sel)) ? W98_HIGHLIGHTTEXT : W98_WINDOWTEXT;
        w98_fill(x + 5 + side, yy, cw - 10 - side, 16, bg);
        w98_icon_blit_small(!n || n->type == FS_DIRECTORY ? desktop_icon_get_folder() : desktop_icon_get_file(),
                            ICON_SIZE, 16, x + 7 + side, yy, &clip);
        w98_text(n ? n->name : "..", x + 29 + side, yy + 4, fg, bg, 1, &clip);
        if (n && cw >= 310) {
            char size[32] = "Folder";
            if (n->type == FS_FILE) {
                int_to_string(n->content_len, size);
                kstrcat(size, " bytes");
            }
            w98_text(size, x + cw - 90, yy + 4, fg, bg, 1, &clip);
        }
    }
    char line[128];
    if (fm_mode) {
        strcpy(line, fm_mode == 1   ? "New folder: "
                     : fm_mode == 2 ? "New file: "
                     : fm_mode == 3 ? "Rename: "
                                    : "Delete? Enter=yes, Esc=no: ");
        kstrcat(line, fm_mode == 4 ? fm_target->name : fm_name);
    } else if (fm_msg[0])
        strcpy(line, fm_msg);
    else {
        int selected=0;for(int i=1;i<=fm_dir->num_children;i++)if(fm_mask&filebit(i))selected++;
        int_to_string(selected,line);kstrcat(line," selected / ");int_to_string(fm_dir->num_children,line+strlen(line));
        kstrcat(line, " items  |  ");
        kstrcat(line, store_status());
    }
    w98_text(line, x + 6, y + ch - 32, W98_BTNTEXT, W98_BTNFACE, 1, &clip);
    w98_text(fm_mode ? "Enter confirms / Esc cancels" : "Enter: open   Ctrl+C: copy file   Ctrl+V: paste", x + 6,
             y + ch - 16, W98_BTNTEXT, W98_BTNFACE, 1, &clip);
}
void app_window_mouse_filemanager(window_t *w, int mx, int my, int buttons) {
    static bool held;
    if(!(buttons&1)){held=false;return;}if(held || fm_mode)return;held=true;
    fm_check();
    int x = mx - w->rect.client_x, y = my - w->rect.client_y;
    int bw = (w->rect.client_w - 8) / 5;
    if (bw > 66)
        bw = 66;
    if(w->rect.client_w>=460 && x>=3 && x<119 && y>=45){
        if(y>=70 && y<150){struct fs_node *p=fm_place((y-70)/20);if(p && p->type==FS_DIRECTORY)fm_enter(p);}
        w->needs_redraw=desktop.dirty=true;return;
    }
    if (y >= 2 && y < 20 && x >= 3 && bw > 0 && x < 3 + bw * 5) {
        int i = (x - 3) / bw;
        if (!i) {
            if (fm_dir->parent)
                fm_enter(fm_dir->parent);
        } else
            fm_edit(i);
    } else if (y >= 64 && (y - 64) / 16 < fm_rows(w)) {
        int i = fm_top + (y - 64) / 16;
        if (i > fm_dir->num_children)
            return;
        fm_select(i,shift_pressed,ctrl_pressed);
        fm_msg[0] = 0;
        struct fs_node *n = i ? fm_dir->children[i - 1] : fm_dir;
        if(ctrl_pressed || shift_pressed)fm_clicked=NULL;
        else if (fm_clicked == n && uptime_ticks - fm_click_tick < 500) {
            fm_clicked = NULL;
            fm_open();
        } else {
            fm_clicked = n;
            fm_click_tick = uptime_ticks;
        }
    }
    w->needs_redraw = true;
    desktop.dirty = true;
}
void app_window_keyboard_filemanager(window_t *w, char c) {
    fm_check();
    if (fm_mode) {
        size_t n = strlen(fm_name);
        if (c == 27)
            fm_mode = 0;
        else if (c == '\n')
            fm_apply();
        else if (fm_mode != 4) {
            if (c == '\b' && n)
                fm_name[n - 1] = 0;
            else if (c >= 32 && c < 127 && n < 31) {
                fm_name[n] = c;
                fm_name[n + 1] = 0;
            }
        }
    } else {
        fm_msg[0] = 0;
        fm_clicked = NULL;
        if(c==1){fm_mask=0;for(int i=1;i<=fm_dir->num_children;i++)fm_mask|=filebit(i);fm_anchor=fm_sel=fm_dir->num_children?1:0;}
        else if(c==3)fm_copy();
        else if(c==22)fm_paste();
        else if (c == 0x10 && fm_sel > 0)
            fm_select(fm_sel-1,shift_pressed,false);
        else if (c == 0x11 && fm_sel < fm_dir->num_children)
            fm_select(fm_sel+1,shift_pressed,false);
        else if (c == '\n')
            fm_open();
        else if (c == '\b' && fm_dir->parent)
            fm_enter(fm_dir->parent);
        else if (c == 'n')
            fm_edit(1);
        else if (c == 'f')
            fm_edit(2);
        else if (c == 'r')
            fm_edit(3);
        else if (c == 'd')
            fm_edit(4);
    }
    w->needs_redraw = true;
    desktop.dirty = true;
}

void app_window_draw_about(window_t* w) {
    int cx = w->rect.client_x;
    int cy = w->rect.client_y;
    int cw = w->rect.client_w;

    w98_fill(cx, cy, cw, w->rect.client_h, W98_BTNFACE);

    const uint32_t* start_pix = desktop_icon_get_start();
    w98_icon_blit(start_pix, 16, 32, cx + cw / 2 - 16, cy + 12, NULL);

    w98_text_bold("About SharkOS 98", cx + cw / 2 - 54, cy + 52, W98_BTNTEXT,
                  W98_BTNFACE, 1, NULL);
    w98_bevel(cx + 8, cy + 66, cw - 16, 2, W98_BEVEL_ETCHED);
    w98_text("Version: 2.2 Desktop (Windows 98 skin)", cx + cw / 2 - 110,
             cy + 76, W98_BTNTEXT, W98_BTNFACE, 1, NULL);
    w98_text("A Win98-styled desktop environment", cx + cw / 2 - 96, cy + 92,
             W98_BTNTEXT, W98_BTNFACE, 1, NULL);
    w98_text("for the SharkOS kernel", cx + cw / 2 - 66, cy + 106,
             W98_BTNTEXT, W98_BTNFACE, 1, NULL);
    w98_text("Created by Mayshecry", cx + cw / 2 - 66, cy + 128,
             W98_GRAYTEXT, W98_BTNFACE, 1, NULL);

                                                                    
                                                                  
    draw_shark_logo(cx + cw / 2, cy + 200, 46, W98_BTNDKSHADOW);
    draw_shark_logo(cx + cw / 2, cy + 200, 43, W98_LOGO);
}

static void append_u8(char* dst, uint32_t v) {
    char b[16];
    int_to_string(v, b);
    kstrcat(dst, b);
}

static void append_ip(char* dst, const uint8_t* ip) {
    append_u8(dst, ip[0]); kstrcat(dst, ".");
    append_u8(dst, ip[1]); kstrcat(dst, ".");
    append_u8(dst, ip[2]); kstrcat(dst, ".");
    append_u8(dst, ip[3]);
}

void app_window_draw_network(window_t* w) {
    int cx = w->rect.client_x;
    int cy = w->rect.client_y;
    int cw = w->rect.client_w;
    int ch = w->rect.client_h;

    w98_fill(cx, cy, cw, ch, W98_BTNFACE);
    w98_rect_t clip; clip.x = cx; clip.y = cy; clip.w = cw; clip.h = ch;

    w98_text_bold("Network Status", cx + cw / 2 - 48, cy + 6, W98_BTNTEXT,
                  W98_BTNFACE, 1, &clip);
    w98_bevel(cx + 4, cy + 18, cw - 8, 2, W98_BEVEL_ETCHED);

    int y = cy + 26;
    int has_nic = net_driver_name[0] && net_driver_name[0] != 'n';

    label_value(cx + 8, cx + 80, y, "NIC:", has_nic ? net_driver_name : "none detected",
                has_nic ? W98_BTNTEXT : 0xFFA00000u);
    y += 14;

    char mac_str[20];
    net_format_mac(mac_str);
    label_value(cx + 8, cx + 80, y, "MAC:", has_nic ? mac_str : "-", W98_BTNTEXT);
    y += 14;

    label_value(cx + 8, cx + 80, y, "Link:", !has_nic ? "-" : net_has_link ? "UP" : "DOWN",
                !has_nic ? W98_BTNTEXT : net_has_link ? 0xFF008000u : 0xFFA00000u);
    y += 14;

    int st = net_dhcp_state();
    const char* st_name = !has_nic ? "no adapter" :
                          st == 3 ? "bound (DHCP)" :
                          st == 4 || st == 5 ? "renewing lease" :
                          st == 6 ? "link-local (no DHCP server)" :
                          st == 1 ? "discovering..." :
                          st == 2 ? "requesting..." : "waiting for link";
    uint32_t st_color = st == 3 ? 0xFF008000u : (st == 6 || !has_nic) ? 0xFFA00000u : 0xFF806000u;
    label_value(cx + 8, cx + 80, y, "Address:", st_name, st_color);
    y += 18;

    char ip_str[24] = "";
    append_ip(ip_str, net_ip);
    label_value(cx + 8, cx + 80, y, "IP:", ip_str, W98_BTNTEXT);
    y += 14;
    char mask_str[24] = "";
    append_ip(mask_str, net_mask);
    label_value(cx + 8, cx + 80, y, "Mask:", mask_str, W98_BTNTEXT);
    y += 14;
    char gw_str[24] = "";
    append_ip(gw_str, net_gw);
    label_value(cx + 8, cx + 80, y, "Gateway:", gw_str, W98_BTNTEXT);
    y += 14;
    char dns_str[24] = "";
    append_ip(dns_str, net_dns);
    label_value(cx + 8, cx + 80, y, "DNS:", dns_str, W98_BTNTEXT);
    y += 14;

    uint32_t left = net_dhcp_lease_left();
    if (left) {
        char lease[40] = "";
        append_u8(lease, left / 3600); kstrcat(lease, "h ");
        append_u8(lease, (left / 60) % 60); kstrcat(lease, "m ");
        append_u8(lease, left % 60); kstrcat(lease, "s left");
        label_value(cx + 8, cx + 80, y, "Lease:", lease, W98_BTNTEXT);
        y += 14;
    }
    y += 6;

    int bw = 92, bh = 22;
    w98_button(cx + 8, y, bw, bh, "Renew DHCP", false, has_nic && net_has_link, false);
    w98_text(net_dhcp_status(), cx + 8 + bw + 10, y + 7, W98_GRAYTEXT, W98_BTNFACE, 1, &clip);
    y += bh + 10;

    w98_text_bold("Commands:", cx + 8, y, W98_BTNTEXT, W98_BTNFACE, 1, &clip);
    y += 14;
    w98_text("ifconfig, dhcp, ping <ip>, dns <host>,", cx + 8, y,
             W98_GRAYTEXT, W98_BTNFACE, 1, &clip);
    y += 12;
    w98_text("wget <url>, netstat   (use in Terminal)", cx + 8, y,
             W98_GRAYTEXT, W98_BTNFACE, 1, &clip);
}

void app_window_mouse_network(window_t* w, int mx, int my, int buttons) {
    if (!(buttons & 1)) return;
    int cx = w->rect.client_x;
    int cy = w->rect.client_y;

    int y = cy + 26 + 14 * 3 + 18 + 14 * 4;
    if (net_dhcp_lease_left()) y += 14;
    y += 6;
    if (mx >= cx + 8 && mx < cx + 100 && my >= y && my < y + 22) {
        net_dhcp_start();
        w->needs_redraw = true;
        desktop.dirty = true;
    }
}

void app_window_keyboard_terminal(window_t* w, char c) {
    term_state_t* st = term_state(w);
    if (!st) return;
    if(c==1){st->anchor=st->output_len;st->end=st->output_len+st->input_len;w->needs_redraw=desktop.dirty=true;return;}
    if(c==3) {
        int a=st->anchor,b=st->end;if(a>b){int t=a;a=b;b=t;}
        if(a==b)desktop_copy_text(st->input,st->input_len);
        else {char tmp[4096];int k=0;for(int i=a;i<b && i<st->output_len+st->input_len && k<4095;i++)tmp[k++]=i<st->output_len?st->output[i]:st->input[i-st->output_len];desktop_copy_text(tmp,k);}
        return;
    }
    if(st->anchor!=st->end && st->anchor>=st->output_len && st->end>=st->output_len && (c==22 || c=='\b' || c>=32)){
        int a=st->anchor-st->output_len,b=st->end-st->output_len;if(a>b){int t=a;a=b;b=t;}
        if(b<=st->input_len){
            int old=st->input_len; kmemmove(st->input+a,st->input+b,old-b+1);st->input_len-=b-a;
            /* Insert the replacement at the beginning of the selected span. */
            char ins[256];int n=0;if(c==22){for(const char *p=desktop_paste_text();*p && n<255-st->input_len;p++)if(*p>=32 && *p<127)ins[n++]=*p;}
            else if(c!='\b')ins[n++]=c;
            kmemmove(st->input+a+n,st->input+a,st->input_len-a+1);memcpy(st->input+a,ins,n);st->input_len+=n;
            st->anchor=st->end=0;w->needs_redraw=desktop.dirty=true;return;
        }
    }
    st->anchor=st->end=0;
    if(c==22) {
        for(const char *t=desktop_paste_text();*t && st->input_len<255;t++)
            if(*t>=32 && *t<127)st->input[st->input_len++]=*t;
        st->input[st->input_len]=0;w->needs_redraw=desktop.dirty=true;return;
    }
    if (c == 27) return;
    if (c == '\n') {
        if (st->input_len > 0) {
            terminal_run_command(w);
        } else {
            static const uint8_t prompt_cols[3] = {
                VGA_COLOR_LIGHT_GREEN, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_LIGHT_GREY
            };
            terminal_output_append(st, "$ \n", prompt_cols, 3);
        }
        w->needs_redraw = true;
        desktop.dirty = true;
    } else if (c == '\b') {
        if (st->input_len > 0) {
            st->input_len--;
            st->input[st->input_len] = '\0';
            w->needs_redraw = true;
            desktop.dirty = true;
        }
    } else if (c >= 32 && c < 127) {
        if (st->input_len < 255) {
            st->input[st->input_len++] = c;
            st->input[st->input_len] = '\0';
            w->needs_redraw = true;
            desktop.dirty = true;
        }
    }
}

void app_window_keyboard_doom(window_t* w, char c) { (void)w; doom_handle_key((int)c); }
void app_window_keyboard_flappybird(window_t* w, char c) { (void)w; flappybird_handle_key((int)c); }
void app_window_keyboard_smb(window_t* w, char c) { (void)w; smb_handle_key((int)c); }

void app_window_keyboard_pong(window_t* w, char c) {
    if (c == 27) { window_close_by_ptr(w); return; }
    pong_handle_key((int)c);
}

void app_window_keyboard_gdash(window_t* w, char c) {
    if (c == 27) { window_close_by_ptr(w); return; }
    gd_handle_key((int)c);
}

void app_window_keyboard_sharkcube(window_t* w, char c) {
    if (c == 27) { window_close_by_ptr(w); return; }
    if (sharkcube_handle_key(c)) {
        w->needs_redraw = true;
        desktop.dirty = true;
    }
}

void app_window_mouse_sharkcube(window_t* w, int mx, int my, int buttons) {
    if (sharkcube_handle_mouse(mx, my, buttons)) {
        w->needs_redraw = true;
        desktop.dirty = true;
    }
}

void app_window_keyboard_sharkcraft(window_t* w, char c) {
    if (c == 27) { window_close_by_ptr(w); return; }
    if (sharkcraft_handle_key(c)) { w->needs_redraw=true; desktop.dirty=true; }
}

void app_window_mouse_sharkcraft(window_t* w, int mx, int my, int buttons) {
    if (sharkcraft_handle_mouse(mx,my,buttons)) { w->needs_redraw=true; desktop.dirty=true; }
}

#include "fnaf.h"
void app_window_draw_fnaf(window_t* w) {
    fnaf_set_window_rect(w->rect.client_x, w->rect.client_y, w->rect.client_w, w->rect.client_h);
    fnaf_draw_frame();
}
void app_window_keyboard_fnaf(window_t* w, char c) {
    (void)w;
    fnaf_handle_key((int)c);
    w->needs_redraw=true; desktop.dirty=true;
}
void app_window_mouse_fnaf(window_t* w, int mx, int my, int buttons) {
    fnaf_handle_mouse(mx, my, buttons);
    w->needs_redraw=true; desktop.dirty=true;
}
