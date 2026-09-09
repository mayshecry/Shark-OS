/* appwindows.c - Win98-styled client areas for the SharkOS desktop apps.
 *
 * Behaviour is unchanged from the previous version (buffers, key handlers,
 * file manager navigation); only the painting now follows the Windows 98
 * system palette. Every colour comes from include/win98_theme.h.
 */

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

static void kmemmove(void* dst, const void* src, int n) {
    char* d = (char*)dst;
    const char* s = (const char*)src;
    for (int i = 0; i < n; i++) d[i] = s[i];
}
static void kstrcat(char* dst, const char* src) {
    while (*dst) dst++;
    while ((*dst++ = *src++));
}

/* Label/value rows used by the info-style windows. */
static void label_value(int lx, int vx, int y, const char* label,
                        const char* value, uint32_t vcolor) {
    w98_text_bold(label, lx, y, W98_BTNTEXT, W98_BTNFACE, 1, NULL);
    w98_text(value, vx, y, vcolor, W98_BTNFACE, 1, NULL);
}

/* ----------------------------------------------------------------- terminal */

/* The terminal window keeps a scrollback of the last TERM_SCROLLBACK bytes of
 * "$ command" lines and captured command output. Older text falls off the
 * top, and the painter shows the last lines that fit in the client area, so
 * long output (help, neofetch, tree) is no longer cut to one command's worth
 * and never overruns the buffer.
 *
 * Command output is collected with terminal_capture_begin(): every terminal
 * primitive appends to the capture buffer instead of drawing while it is
 * active (see src/ui/terminal.c). */
#define TERM_SCROLLBACK   8192
#define TERM_CAPTURE_MAX  4096
#define TERM_LINE_H       10
#define TERM_MAX_COLS     160

static char terminal_buffer[256] = "";
static int terminal_buf_len = 0;
static char terminal_output[TERM_SCROLLBACK] = "";
static int terminal_output_len = 0;
static char terminal_capture_store[TERM_CAPTURE_MAX];
static int terminal_blink = 0;
static int terminal_history_pos = 0;   /* generation counter for repaints */

static void terminal_output_append(const char* text, int len) {
    if (len <= 0) return;
    if (len >= TERM_SCROLLBACK - 1) {
        text += len - (TERM_SCROLLBACK - 1);
        len = TERM_SCROLLBACK - 1;
    }
    if (terminal_output_len + len >= TERM_SCROLLBACK - 1) {
        /* Drop whole lines from the front until it fits. */
        int drop = terminal_output_len + len - (TERM_SCROLLBACK - 1);
        while (drop < terminal_output_len && terminal_output[drop] != '\n') drop++;
        if (drop < terminal_output_len) drop++;
        kmemmove(terminal_output, terminal_output + drop, terminal_output_len - drop);
        terminal_output_len -= drop;
    }
    for (int i = 0; i < len; i++) {
        terminal_output[terminal_output_len++] = text[i];
    }
    terminal_output[terminal_output_len] = '\0';
    terminal_history_pos++;
}

static void terminal_output_clear(void) {
    terminal_output_len = 0;
    terminal_output[0] = '\0';
    terminal_history_pos++;
}

/* Split the scrollback into display lines (wrapping at `cols`) and return
 * how many there are; when `want` >= 0 the start offset and length of that
 * display line are returned instead. */
static int terminal_layout_lines(int cols, int want, int* out_start, int* out_len) {
    int count = 0;
    int i = 0;
    if (cols < 1) cols = 1;
    while (i < terminal_output_len) {
        int start = i;
        int len = 0;
        while (i < terminal_output_len && terminal_output[i] != '\n' && len < cols) {
            i++; len++;
        }
        if (want == count) {
            if (out_start) *out_start = start;
            if (out_len) *out_len = len;
            return count;
        }
        count++;
        if (i < terminal_output_len && terminal_output[i] == '\n') i++;
    }
    if (want >= 0) {
        if (out_start) *out_start = terminal_output_len;
        if (out_len) *out_len = 0;
    }
    return count;
}

void app_window_draw_terminal(window_t* w) {
    int cx = w->rect.client_x;
    int cy = w->rect.client_y;
    int cw = w->rect.client_w;
    int ch = w->rect.client_h;

    terminal_blink++;
    if (terminal_blink >= 60) terminal_blink = 0;

    w98_fill(cx, cy, cw, ch, W98_BTNFACE);
    w98_surface(cx + 2, cy + 2, cw - 4, ch - 4, W98_BEVEL_SUNKEN,
                W98_BTNDKSHADOW);

    int tx = cx + 6, ty = cy + 6;
    int tw = cw - 12, th = ch - 12;
    if (tw <= 8 || th <= 8) return;

    w98_fill(tx, ty, tw, th, 0xFF000000);

    int cols = (tw - 8) / 6;
    if (cols > TERM_MAX_COLS) cols = TERM_MAX_COLS;
    if (cols < 4) cols = 4;
    int max_lines = (th - 8) / TERM_LINE_H;
    if (max_lines < 1) max_lines = 1;

    w98_rect_t clip;
    clip.x = tx + 2; clip.y = ty + 2; clip.w = tw - 4; clip.h = th - 4;

    /* Prompt + edit line may wrap too. */
    int prompt_chars = 2 + terminal_buf_len;
    int prompt_rows = (prompt_chars + cols - 1) / cols;
    if (prompt_rows < 1) prompt_rows = 1;

    int total = terminal_layout_lines(cols, -1, NULL, NULL);
    int avail = max_lines - prompt_rows;
    if (avail < 0) avail = 0;
    int first = total - avail;
    if (first < 0) first = 0;

    char line_buf[TERM_MAX_COLS + 1];
    int row = 0;
    for (int li = first; li < total && row < avail; li++, row++) {
        int start = 0, len = 0;
        terminal_layout_lines(cols, li, &start, &len);
        if (len > TERM_MAX_COLS) len = TERM_MAX_COLS;
        for (int j = 0; j < len; j++) line_buf[j] = terminal_output[start + j];
        line_buf[len] = '\0';
        /* Echoed command lines are drawn in the prompt colours. */
        uint32_t fg = 0xFFAAAAAA;
        if (len >= 2 && line_buf[0] == '$' && line_buf[1] == ' ') fg = 0xFFFFFF55;
        w98_text(line_buf, tx + 4, ty + 4 + row * TERM_LINE_H, fg, 0xFF000000, 1, &clip);
    }

    /* Prompt and the command being typed. */
    int py = ty + 4 + row * TERM_LINE_H;
    w98_text("$ ", tx + 4, py, 0xFF55FF55, 0xFF000000, 1, &clip);
    int col = 2;
    int prow = 0;
    for (int i = 0; i < terminal_buf_len; i++) {
        if (col >= cols) { col = 0; prow++; }
        char str[2] = { terminal_buffer[i], 0 };
        w98_text(str, tx + 4 + col * 6, py + prow * TERM_LINE_H,
                 0xFFFFFF55, 0xFF000000, 1, &clip);
        col++;
    }
    if (terminal_blink < 30) {
        if (col >= cols) { col = 0; prow++; }
        int cx0 = tx + 4 + col * 6;
        int cy0 = py + prow * TERM_LINE_H;
        if (cy0 + 8 <= clip.y + clip.h) w98_fill(cx0, cy0, 6, 8, 0xFF55FF55);
    }
}

/* Commands that take over the whole screen or block waiting for keys can't
 * run from inside the desktop Terminal window (there is no separate task, so
 * they would freeze the desktop). They still work from the Lite console. */
static bool terminal_cmd_blocked(const char* cmd, char* why, int why_len) {
    static const char* blocked[] = {
        "edit", "guess", "tictactoe", "python", "doom", "flappybird", "smb",
        "pong", "gdash", "microsoft", "htop", "exec", NULL
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
                     strcmp(name, "gdash") == 0)
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
    terminal_buffer[terminal_buf_len] = '\0';

    /* Echo the command line into the scrollback. */
    terminal_output_append("$ ", 2);
    terminal_output_append(terminal_buffer, terminal_buf_len);
    terminal_output_append("\n", 1);

    char why[96];
    if (terminal_cmd_blocked(terminal_buffer, why, sizeof(why))) {
        terminal_output_append(why, (int)strlen(why));
    } else if (strcmp(terminal_buffer, "clear") == 0 || strcmp(terminal_buffer, "cls") == 0) {
        terminal_output_clear();
    } else {
        /* Run the command with all terminal output redirected into
         * terminal_capture_store. The shell's execute_command() has a
         * large stack frame; the kernel stack was enlarged (64 KB, now 256 KB) in
         * boot.s so this nested call is safe. */
        uint8_t saved_color = terminal_color;
        terminal_in_desktop_window = true;
        terminal_capture_begin(terminal_capture_store, TERM_CAPTURE_MAX);
        execute_command(terminal_buffer);
        bool cleared = terminal_capture_cleared;
        int len = terminal_capture_len;
        terminal_capture_end();
        terminal_in_desktop_window = false;
        terminal_color = saved_color;

        if (cleared) terminal_output_clear();

        /* execute_command() starts every command with a newline; drop it so
         * output starts right under the echoed command. */
        const char* out = terminal_capture_store;
        while (len > 0 && *out == '\n') { out++; len--; }
        /* Collapse trailing blank lines to a single newline. */
        while (len > 1 && out[len - 1] == '\n' && out[len - 2] == '\n') len--;
        if (len > 0) {
            terminal_output_append(out, len);
            if (out[len - 1] != '\n') terminal_output_append("\n", 1);
        }
    }

    terminal_buf_len = 0;
    terminal_buffer[0] = '\0';
    w->needs_redraw = true;
    desktop.dirty = true;
}

/* ------------------------------------------------------------------- games */

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

/* ----------------------------------------------------------------- settings */

typedef struct {
    uint32_t color;
    const char* label;
} settings_color_opt_t;

static const settings_color_opt_t settings_colors[] = {
    { W98_DESKTOP,        "Teal (Default)" },
    { 0xFF1A0A0Au,        "Dark Red" },
    { 0xFF0A1A0Au,        "Dark Green" },
    { 0xFF1A1A0Au,        "Amber" },
    { 0xFF0A0A0Au,        "Pure Black" },
    { 0xFF2A1A0Au,        "Brown" },
    { 0xFF1A0A2Au,        "Purple" },
    { 0xFF0A1A2Au,        "Teal Blue" },
};
#define SETTINGS_COLOR_COUNT 8

#define SETTINGS_ROW_H 16

static int settings_hover = -1;   /* 0..2 radio, 10..17 swatch, 99 shutdown */

/* Shared layout so the painter and the mouse handler agree.
 * Returns the y position of the shutdown button. */
static int settings_layout(window_t* w, int* radio_y, int* swatch_y,
                           int* btn_y) {
    int cy = w->rect.client_y;
    int ch = w->rect.client_h;

    *radio_y = cy + 26;
    *swatch_y = *radio_y + 3 * SETTINGS_ROW_H + 22;
    *btn_y = cy + ch - 30;
    return *btn_y;
}

static void settings_draw_radio(int x, int y, bool on, const char* label,
                                bool hovered) {
    /* 12x12 sunken white circle-ish square, black dot when selected. */
    w98_surface(x, y, 12, 12, W98_BEVEL_SUNKEN, W98_WINDOW);
    if (on) {
        w98_fill(x + 4, y + 4, 4, 4, W98_BTNTEXT);
    }
    uint32_t bg = hovered ? W98_BTNFACE : W98_BTNFACE;
    w98_text(label, x + 18, y + 2, W98_BTNTEXT, bg, 1, NULL);
}

void app_window_draw_settings(window_t* w) {
    int cx = w->rect.client_x;
    int cy = w->rect.client_y;
    int cw = w->rect.client_w;

    int radio_y, swatch_y, btn_y;
    settings_layout(w, &radio_y, &swatch_y, &btn_y);

    /* Hover is derived from the pointer every paint, so it cannot drift from
     * what the click handler tests. */
    settings_hover = -1;
    int mxp = desktop_mouse_x, myp = desktop_mouse_y;
    for (int i = 0; i < 3; i++) {
        int ry = radio_y + i * SETTINGS_ROW_H;
        if (mxp >= cx && mxp < cx + cw && myp >= ry && myp < ry + SETTINGS_ROW_H) {
            settings_hover = i;
        }
    }
    for (int i = 0; i < SETTINGS_COLOR_COUNT; i++) {
        int ry = swatch_y + i * SETTINGS_ROW_H;
        if (mxp >= cx && mxp < cx + cw && myp >= ry && myp < ry + SETTINGS_ROW_H) {
            settings_hover = 10 + i;
        }
    }
    if (mxp >= cx + cw / 2 - 37 && mxp < cx + cw / 2 - 37 + 75 &&
        myp >= btn_y && myp < btn_y + 23) {
        settings_hover = 99;
    }

    w98_fill(cx, cy, cw, w->rect.client_h, W98_BTNFACE);

    w98_text_bold("Settings - Customize", cx + cw / 2 - 60, cy + 6,
                  W98_BTNTEXT, W98_BTNFACE, 1, NULL);
    w98_bevel(cx + 4, cy + 18, cw - 8, 2, W98_BEVEL_ETCHED);

    /* Wallpaper mode radios. */
    w98_text_bold("Wallpaper:", cx + 8, radio_y - 14, W98_BTNTEXT,
                  W98_BTNFACE, 1, NULL);
    const char* modes[3] = { "Teal (dithered)", "Solid colour",
                             "SharkOS image" };
    for (int i = 0; i < 3; i++) {
        bool on = (desktop.wallpaper_mode == i);
        settings_draw_radio(cx + 10, radio_y + i * SETTINGS_ROW_H, on,
                            modes[i], settings_hover == i);
    }

    w98_text_bold("Solid colour:", cx + 8, swatch_y - 14, W98_BTNTEXT,
                  W98_BTNFACE, 1, NULL);
    for (int i = 0; i < SETTINGS_COLOR_COUNT; i++) {
        int ry = swatch_y + i * SETTINGS_ROW_H;
        bool current = (settings_colors[i].color == wallpaper_top);

        if (settings_hover == 10 + i) {
            w98_fill(cx + 6, ry, cw - 12, SETTINGS_ROW_H - 1, W98_HIGHLIGHT);
        }
        /* Swatch: flat colour with a sunken ring. */
        w98_surface(cx + 10, ry, 16, 14, W98_BEVEL_SUNKEN,
                    settings_colors[i].color);

        uint32_t fg = (settings_hover == 10 + i) ? W98_HIGHLIGHTTEXT
                                                 : W98_BTNTEXT;
        uint32_t bg = (settings_hover == 10 + i) ? W98_HIGHLIGHT
                                                 : W98_BTNFACE;
        w98_text(settings_colors[i].label, cx + 32, ry + 3, fg, bg, 1, NULL);
        if (current) {
            w98_text("<- Active", cx + 32 +
                     w98_text_width(settings_colors[i].label, 1) + 6,
                     ry + 3, W98_GRAYTEXT, bg, 1, NULL);
        }
    }

    w98_button(cx + cw / 2 - 37, btn_y, 75, 23, "Shut Down...",
               settings_hover == 99, true, false);
}

void app_window_mouse_settings(window_t* w, int mx, int my, int buttons) {
    (void)w;
    if (!(buttons & 1)) return;

    int radio_y, swatch_y, btn_y;
    settings_layout(w, &radio_y, &swatch_y, &btn_y);
    int cx = w->rect.client_x;
    int cw = w->rect.client_w;

    for (int i = 0; i < 3; i++) {
        int ry = radio_y + i * SETTINGS_ROW_H;
        if (mx >= cx && mx < cx + cw && my >= ry && my < ry + SETTINGS_ROW_H) {
            desktop_set_wallpaper_mode(i);
            return;
        }
    }

    for (int i = 0; i < SETTINGS_COLOR_COUNT; i++) {
        int ry = swatch_y + i * SETTINGS_ROW_H;
        if (mx >= cx && mx < cx + cw && my >= ry && my < ry + SETTINGS_ROW_H) {
            uint32_t color = settings_colors[i].color;
            uint8_t r = (color >> 16) & 0xFF;
            uint8_t g = (color >> 8) & 0xFF;
            uint8_t b = color & 0xFF;
            desktop_set_wallpaper_color(color,
                0xFF000000u | ((r / 2) << 16) | ((g / 2) << 8) | (b / 2));
            desktop_set_wallpaper_mode(W98_WALL_SOLID);
            return;
        }
    }

    if (mx >= cx + cw / 2 - 37 && mx < cx + cw / 2 - 37 + 75 &&
        my >= btn_y && my < btn_y + 23) {
        shutdown();
    }
}

/* --------------------------------------------------------------------- faq */

/* Word-wrap `text` and paint it inside `clip`. `prefix` ("Q: "/"A: ") goes on
 * the first line; continuation lines are indented by `indent` px. Returns the
 * y just below the last line. Nothing is painted past `y_limit`, so a small
 * window shows as much as fits instead of spilling over the frame. */
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
            while (k > 0 && text[p + k] != ' ') k--;   /* keep words whole */
            if (k > 0) take = k;
        }
        if (take > (int)sizeof(line) - plen - 1) take = (int)sizeof(line) - plen - 1;

        int ln = 0;
        if (first) { for (int j = 0; j < plen; j++) line[ln++] = prefix[j]; }
        for (int j = 0; j < take; j++) line[ln++] = text[p + j];
        line[ln] = '\0';

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

    /* Sunken white panel for the text, like a Win98 help pane. */
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

/* ------------------------------------------------------------ task manager */

/* Win98-style Task Manager: an Applications list (every desktop window) with
 * End Task / Switch To, a Performance section (CPU load history from the main
 * loop, memory bars, frame rate) and the kernel task table. */

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
static int tm_selected = -1;          /* index into desktop.windows */
static int tm_selected_z = -1;        /* remembered by z_order so it survives re-sorts */

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
        /* Tab: raised on top/left/right, open at the bottom. */
        w98_fill(tx, ty, tab_w, th, W98_BTNFACE);
        w98_fill(tx, ty, 1, th, W98_BTNHILITE);
        w98_fill(tx + 1, ty, tab_w - 2, 1, W98_BTNHILITE);
        w98_fill(tx + tab_w - 1, ty + 1, 1, th - 1, W98_BTNDKSHADOW);
        w98_fill(tx + tab_w - 2, ty + 1, 1, th - 1, W98_BTNSHADOW);
        w98_text(tm_tab_names[i], tx + 8, ty + 6, W98_BTNTEXT, W98_BTNFACE, 1, clip);
    }
    /* Page border: the selected tab merges into it. */
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

    /* Header row */
    int hy = L->list_y + 2;
    w98_fill(x + 2, hy, cw - 4, TM_ROW_H, W98_BTNFACE);
    w98_fill(x + 2, hy + TM_ROW_H - 1, cw - 4, 1, W98_BTNSHADOW);
    w98_text("Task", x + 8, hy + 3, W98_BTNTEXT, W98_BTNFACE, 1, &lc);
    w98_text("Status", x + cw - 96, hy + 3, W98_BTNTEXT, W98_BTNFACE, 1, &lc);

    int y = hy + TM_ROW_H + 1;
    int rows = (L->list_h - TM_ROW_H - 6) / TM_ROW_H;
    int shown = 0;

    /* Re-resolve the selection: window indices shift when windows close. */
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

        /* 16x16 icon scaled from the 32x32 app icon, then the title. */
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

    /* Buttons */
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
    /* Win98 progress: chunky blue blocks. */
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
    /* "12.3 MB" / "512 KB" without floats */
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

    /* --- CPU usage history graph ------------------------------------- */
    w98_text_bold("CPU Usage History", x, y, W98_BTNTEXT, W98_BTNFACE, 1, clip);
    y += 12;
    int gh = (bottom - y) / 2 - 40;
    if (gh < 40) gh = 40;
    if (gh > 110) gh = 110;
    int gw = cw;
    w98_surface(x, y, gw, gh, W98_BEVEL_SUNKEN, 0xFF000000u);
    /* grid */
    for (int gx = x + 2 + 12; gx < x + gw - 2; gx += 12)
        for (int gy = y + 2; gy < y + gh - 2; gy++) draw_pixel(gx, gy, 0xFF004000u);
    for (int gy = y + 2 + 12; gy < y + gh - 2; gy += 12)
        for (int gx = x + 2; gx < x + gw - 2; gx++) draw_pixel(gx, gy, 0xFF004000u);
    /* history line, newest on the right, one sample per second */
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

    /* --- Live numbers --------------------------------------------------- */
    char buf[48], num[16];
    int col2 = x + cw / 2;

    tm_draw_bar(x, y, cw / 2 - 40, 14, (int)sys_cpu_percent, "CPU Usage:", clip);

    extern char _kernel_end[];
    uint32_t total = total_system_memory > 0xFFFFFFFFull ? 0xFFFFFFFFu
                                                         : (uint32_t)total_system_memory;
    /* Kernel image (from 1 MB) plus everything the bump allocator handed out. */
    uint32_t used = (uint32_t)(free_memory_start - 0x100000u);
    (void)_kernel_end;
    if (total && used > total) used = total;
    int mem_pct = total ? (int)((used / 1024u) * 100u / (total / 1024u ? total / 1024u : 1u)) : 0;
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

    strcpy(buf, "Kernel + heap: ");
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

    /* Row 0: the kernel itself (main loop) */
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

    /* Loaded plugins count as processes too (they are compiled in). */
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

    /* Status line like Win98's: "Processes: N  CPU Usage: X%  Mem: Y" */
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

    /* Tabs */
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

    /* List rows */
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

    /* Buttons */
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
            /* nothing to do: the redraw below re-reads everything */
        }
        for (int i = 0; i < desktop.window_count; i++) desktop.windows[i].needs_redraw = true;
        desktop.dirty = true;
    }
}

void app_window_keyboard_taskmanager(window_t* w, char c) {
    if (c == '\t') {
        tm_tab = (tm_tab + 1) % 3;
    } else if (c == 127 || c == 8) {      /* Delete / Backspace: End Task */
        if (tm_tab == TM_TAB_APPS && tm_selected >= 0 &&
            tm_selected < desktop.window_count && &desktop.windows[tm_selected] != w) {
            window_close(tm_selected);
            tm_selected = -1; tm_selected_z = -1;
        }
    } else if (c == '\n') {               /* Enter: Switch To */
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

/* --------------------------------------------------------------- fastfetch */

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
    uint32_t free_kb = (uint32_t)(total_system_memory * 3 / 4);
    uint32_t avail_kb = (uint32_t)(total_system_memory * 7 / 8);
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
    label_value(label_x, value_x, y, "Arch:", "x86 (32-bit)", W98_BTNTEXT);
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
    label_value(label_x, value_x, y, "Memory Avail:", "", W98_BTNTEXT);
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
    label_value(label_x, value_x, y, "Theme:", "Windows 98 (SharkOS 98)",
                W98_BTNTEXT);
}

/* ----------------------------------------------------------------- notepad */

static char notepad_buffer[4096] = "";
static int notepad_len = 0;
static char notepad_path[128] = "";
static int notepad_cursor = 0;
static bool notepad_dirty = false;

#define NOTEPAD_MENU_H 16

static const char* notepad_menus[] = { "File", "Edit", "Search", "Help" };

void app_window_draw_notepad(window_t* w) {
    int cx = w->rect.client_x;
    int cy = w->rect.client_y;
    int cw = w->rect.client_w;
    int ch = w->rect.client_h;

    w98_fill(cx, cy, cw, ch, W98_BTNFACE);

    /* Menu strip. */
    int mx = cx + 2;
    for (int i = 0; i < 4; i++) {
        int tw = w98_text_width(notepad_menus[i], 1) + 10;
        w98_text(notepad_menus[i], mx + 5, cy + 4, W98_BTNTEXT, W98_BTNFACE,
                 1, NULL);
        mx += tw;
    }

    char title[160];
    int p = 0;
    if (notepad_path[0]) { strcpy(title + p, notepad_path); p = (int)strlen(title); }
    else { strcpy(title + p, "Untitled"); p = (int)strlen(title); }
    if (notepad_dirty) { strcpy(title + p, " *"); }
    w98_text(title, cx + cw - w98_text_width(title, 1) - 6, cy + 4,
             W98_GRAYTEXT, W98_BTNFACE, 1, NULL);

    /* White sunken text area. */
    int ta_x = cx + 2, ta_y = cy + NOTEPAD_MENU_H + 2;
    int ta_w = cw - 4, ta_h = ch - NOTEPAD_MENU_H - 6;
    w98_surface(ta_x, ta_y, ta_w, ta_h, W98_BEVEL_SUNKEN, W98_WINDOW);

    w98_rect_t clip;
    clip.x = ta_x + 3; clip.y = ta_y + 3; clip.w = ta_w - 6; clip.h = ta_h - 6;

    int max_chars_per_line = (ta_w - 8) / 6;
    if (max_chars_per_line > 90) max_chars_per_line = 90;
    int visible_lines = (ta_h - 8) / 11;

    int line = 0;
    int col = 0;
    for (int i = 0; i < notepad_len && line < visible_lines; i++) {
        char c = notepad_buffer[i];
        if (c == '\n') { line++; col = 0; }
        else {
            if (col < max_chars_per_line) {
                char str[2] = { c, 0 };
                w98_text(str, ta_x + 4 + col * 6, ta_y + 4 + line * 11,
                         W98_WINDOWTEXT, W98_WINDOW, 1, &clip);
                col++;
            }
        }
    }

    static int blink = 0;
    if (blink < 30 && notepad_cursor <= notepad_len) {
        int cur_line = 0;
        int cur_col = 0;
        for (int i = 0; i < notepad_cursor && i < notepad_len; i++) {
            if (notepad_buffer[i] == '\n') { cur_line++; cur_col = 0; }
            else { cur_col++; }
        }
        if (cur_line < visible_lines && cur_col < max_chars_per_line) {
            int cur_x = ta_x + 4 + cur_col * 6;
            int cur_y = ta_y + 4 + cur_line * 11;
            if (cur_x >= clip.x && cur_y >= clip.y &&
                cur_x < clip.x + clip.w && cur_y < clip.y + clip.h) {
                w98_fill(cur_x, cur_y, 6, 11, W98_HIGHLIGHT);
            }
        }
    }
    if (blink++ >= 60) blink = 0;
}

void app_window_keyboard_notepad(window_t* w, char c) {
    if (c == '\n') {
        if (notepad_len < (int)sizeof(notepad_buffer) - 1) {
            if (notepad_cursor < notepad_len) {
                kmemmove(notepad_buffer + notepad_cursor + 1,
                         notepad_buffer + notepad_cursor,
                         notepad_len - notepad_cursor);
            }
            notepad_buffer[notepad_cursor] = '\n';
            notepad_len++; notepad_cursor++; notepad_dirty = true;
        }
    } else if (c == '\b') {
        if (notepad_cursor > 0) {
            notepad_cursor--;
            kmemmove(notepad_buffer + notepad_cursor,
                     notepad_buffer + notepad_cursor + 1,
                     notepad_len - notepad_cursor - 1);
            notepad_len--; notepad_dirty = true;
        }
    } else if (c == 27) { return; }
    else if (c >= 32 && c < 127) {
        if (notepad_len < (int)sizeof(notepad_buffer) - 1) {
            if (notepad_cursor < notepad_len) {
                kmemmove(notepad_buffer + notepad_cursor + 1,
                         notepad_buffer + notepad_cursor,
                         notepad_len - notepad_cursor);
            }
            notepad_buffer[notepad_cursor] = c;
            notepad_len++; notepad_cursor++; notepad_dirty = true;
        }
    }
    w->needs_redraw = true;
}

/* ------------------------------------------------------------ file manager */

static struct fs_node* fm_current_dir = NULL;
static int fm_hovered_item = -1;
static int fm_items_y[21];

#define FM_STATUS_H 18

void app_window_draw_filemanager(window_t* w) {
    int cx = w->rect.client_x;
    int cy = w->rect.client_y;
    int cw = w->rect.client_w;
    int ch = w->rect.client_h;

    w98_fill(cx, cy, cw, ch, W98_BTNFACE);

    if (!fm_current_dir) fm_current_dir = root;

    /* Address bar. */
    char path[128] = "";
    struct fs_node* tmp = fm_current_dir;
    char parts[8][32];
    int pcount = 0;
    while (tmp && tmp != root && pcount < 8) {
        strcpy(parts[pcount], tmp->name); pcount++; tmp = tmp->parent;
    }
    for (int i = pcount - 1; i >= 0; i--) {
        if (path[0]) kstrcat(path, "\\");
        kstrcat(path, parts[i]);
    }
    if (path[0] == '\0') strcpy(path, "C:\\");

    w98_surface(cx + 2, cy + 2, cw - 4, 16, W98_BEVEL_SUNKEN, W98_WINDOW);
    w98_text(path, cx + 6, cy + 6, W98_WINDOWTEXT, W98_WINDOW, 1, NULL);

    /* Listview. */
    int lv_y = cy + 22;
    int lv_h = ch - 22 - FM_STATUS_H - 4;
    w98_surface(cx + 2, lv_y, cw - 4, lv_h, W98_BEVEL_SUNKEN, W98_WINDOW);

    int y = lv_y + 4;
    int max_items = (lv_h - 8) / 16;
    if (max_items > 20) max_items = 20;
    fm_hovered_item = -1;
    fm_items_y[0] = y;

    /* ".." entry */
    bool hov0 = (desktop_mouse_x >= cx + 6 && desktop_mouse_x < cx + cw - 6 &&
                 desktop_mouse_y >= y && desktop_mouse_y < y + 16);
    if (hov0) {
        fm_hovered_item = 0;
        w98_fill(cx + 4, y, cw - 8, 16, W98_HIGHLIGHT);
    }
    w98_icon_blit_small(desktop_icon_get_folder(), ICON_SIZE, 16, cx + 6,
                        y, NULL);
    w98_text("..", cx + 26, y + 4,
             hov0 ? W98_HIGHLIGHTTEXT : W98_WINDOWTEXT,
             hov0 ? W98_HIGHLIGHT : W98_WINDOW, 1, NULL);
    y += 16;

    for (int i = 0; i < fm_current_dir->num_children && i < max_items - 1;
         i++) {
        struct fs_node* child = fm_current_dir->children[i];
        fm_items_y[i + 1] = y;

        bool hovered = (desktop_mouse_x >= cx + 6 &&
                        desktop_mouse_x < cx + cw - 6 &&
                        desktop_mouse_y >= y && desktop_mouse_y < y + 16);
        if (hovered) {
            fm_hovered_item = i + 1;
            w98_fill(cx + 4, y, cw - 8, 16, W98_HIGHLIGHT);
        }

        const uint32_t* glyph =
            (child->type == FS_DIRECTORY) ? desktop_icon_get_folder()
                                          : desktop_icon_get_file();
        w98_icon_blit_small(glyph, ICON_SIZE, 16, cx + 6, y, NULL);
        w98_text(child->name, cx + 26, y + 4,
                 hovered ? W98_HIGHLIGHTTEXT : W98_WINDOWTEXT,
                 hovered ? W98_HIGHLIGHT : W98_WINDOW, 1, NULL);
        y += 16;
    }

    /* Status bar. */
    int sb_y = cy + ch - FM_STATUS_H - 2;
    w98_bevel(cx + 2, sb_y, cw - 4, FM_STATUS_H, W98_BEVEL_SUNKEN);
    char status[40];
    int_to_string((uint32_t)(fm_current_dir->num_children + 1), status);
    kstrcat(status, " object(s)");
    w98_text(status, cx + 8, sb_y + 5, W98_BTNTEXT, W98_BTNFACE, 1, NULL);
}

void app_window_mouse_filemanager(window_t* w, int mx, int my, int buttons) {
    (void)mx; (void)my;
    if (!(buttons & 1)) return;
    if (!fm_current_dir) fm_current_dir = root;
    if (fm_hovered_item == 0) {
        if (fm_current_dir && fm_current_dir->parent) {
            fm_current_dir = fm_current_dir->parent;
        }
    } else if (fm_hovered_item > 0 &&
               fm_hovered_item <= fm_current_dir->num_children) {
        struct fs_node* child = fm_current_dir->children[fm_hovered_item - 1];
        if (child && child->type == FS_DIRECTORY) {
            fm_current_dir = child;
        }
    }
    w->needs_redraw = true;
}

void app_window_keyboard_filemanager(window_t* w, char c) {
    if (!fm_current_dir) fm_current_dir = root;
    if (c == 27) return;
    else if (c == '\b' || c == 8) {
        if (fm_current_dir && fm_current_dir->parent) {
            fm_current_dir = fm_current_dir->parent;
        }
    }
    w->needs_redraw = true;
}

/* ------------------------------------------------------------------- about */

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
}

/* ----------------------------------------------------------------- network */

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

    /* DHCP state straight from the client: this is what the user actually
     * wants to know when "the internet does not work". */
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

    /* Renew button: re-runs discovery in the background. */
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
    /* Same layout arithmetic as the painter: 26 + 14*3 + 18 + 14*4 (+14 lease) + 6 */
    int y = cy + 26 + 14 * 3 + 18 + 14 * 4;
    if (net_dhcp_lease_left()) y += 14;
    y += 6;
    if (mx >= cx + 8 && mx < cx + 100 && my >= y && my < y + 22) {
        net_dhcp_start();
        w->needs_redraw = true;
        desktop.dirty = true;
    }
}

/* ---------------------------------------------------------- key forwarding */

void app_window_keyboard_terminal(window_t* w, char c) {
    if (c == 27) return;
    if (c == '\n') {
        if (terminal_buf_len > 0) {
            terminal_run_command(w);
        } else {
            terminal_output_append("$ \n", 3);
        }
        w->needs_redraw = true;
        desktop.dirty = true;
    } else if (c == '\b') {
        if (terminal_buf_len > 0) {
            terminal_buf_len--;
            terminal_buffer[terminal_buf_len] = '\0';
            w->needs_redraw = true;
            desktop.dirty = true;
        }
    } else if (c >= 32 && c < 127) {
        if (terminal_buf_len < 255) {
            terminal_buffer[terminal_buf_len++] = c;
            terminal_buffer[terminal_buf_len] = '\0';
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
