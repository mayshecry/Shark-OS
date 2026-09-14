/* systemd-style verbose boot log.
 *
 * The old Win98-shaped splash (logo + progress bar) is gone: boot now
 * prints a scrolling service log — "[  OK  ] Started ..." style — on a
 * black screen for about five seconds, then hands over to the desktop.
 * Colours follow the active theme (the Kawaii GRUB entry boots pink).
 */

#include "kernel.h"
#include "desktop.h"
#include "win98_theme.h"
#include "theme.h"

#define BOOT_LINE_MAX  64
#define BOOT_MSG_MAX   56

#define BOOT_BG        0xFF000000u
#define BOOT_GRAY      0xFF9E9E9Eu
#define BOOT_TEXT      0xFFCCCCCCu
#define BOOT_OK_GREEN  0xFF33B944u
#define BOOT_FAIL_RED  0xFFCC3333u

typedef struct {
    char msg[BOOT_MSG_MAX];
    bool done;
    bool banner;      /* drawn without the [ OK ] tag column */
    uint32_t color;   /* banner colour */
} boot_line_t;

static boot_line_t boot_lines[BOOT_LINE_MAX];
static int boot_line_count = 0;
static int boot_spin = 0;

/* ------------------------------------------------------------------ */
/* The SharkOS logo painter lives here (also used by the About box).  */
/* ------------------------------------------------------------------ */

static void draw_pixel_safe(int x, int y, uint32_t color) {
    if (x < 0 || y < 0) return;
    if ((uint32_t)x >= screen_width || (uint32_t)y >= screen_height) return;
    uint32_t stride = screen_pitch / 4;
    lfbptr[(uint32_t)y * stride + (uint32_t)x] = color;
}

void draw_shark_logo(int cx, int cy, int size, uint32_t color) {
    int half = size / 2;

    for (int dy = -half; dy <= half; dy++) {
        int row_width = half - (dy * dy) / (half + 1);
        if (row_width < 0) row_width = 0;
        for (int dx = -row_width; dx <= row_width; dx++) {
            draw_pixel_safe(cx + dx, cy + dy, color);
        }
    }

    int eye_y = cy - half / 3;
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            draw_pixel_safe(cx + half / 3 + dx, eye_y + dy, W98_DESKTOP);
        }
    }

    for (int dy = 0; dy < half / 2; dy++) {
        int fw = half / 2 - dy;
        for (int dx = 0; dx < fw; dx++) {
            draw_pixel_safe(cx - half / 4 + dx, cy - half - dy, color);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Boot log renderer                                                  */
/* ------------------------------------------------------------------ */

static int boot_scale(void) {
    return screen_height >= 900 ? 2 : 1;
}

static uint32_t boot_ok_color(void) {
    return (theme_get_id() == THEME_ID_KAWAII) ? theme_current->highlight
                                               : BOOT_OK_GREEN;
}

static void boot_delay(volatile int iters) {
    for (volatile int i = 0; i < iters; i++);
}

/* Pace a line: once the PIT ticks we wait in real time; before the IDT
 * is up (uptime frozen) we fall back to a rough busy loop. */
static void boot_wait_ms(uint32_t ms) {
    uint32_t a = uptime_ticks;
    boot_delay(200000);
    if (uptime_ticks != a) {
        uint32_t t0 = uptime_ticks;
        while (uptime_ticks - t0 < ms) asm volatile("hlt");
    } else {
        boot_delay((int)(ms * 400000u));
    }
}

static void boot_render(void) {
    int s = boot_scale();
    int lh = w98_text_height(s) + 6 * s;
    int maxlines = ((int)screen_height - 16 * s) / lh;
    if (maxlines < 1) maxlines = 1;

    draw_rect(0, 0, (int)screen_width, (int)screen_height, BOOT_BG);

    int start = boot_line_count - maxlines;
    if (start < 0) start = 0;

    int x0 = 6 * s;
    char tagpre[4] = "[ ";
    char tagpost[4] = " ]";
    int wpre = w98_text_width(tagpre, s);
    int wok = w98_text_width("  OK  ", s);
    int wpost = w98_text_width(tagpost, s);
    int x_msg = x0 + wpre + wok + wpost;

    const char* spin = "|/-\\";

    for (int i = start; i < boot_line_count; i++) {
        boot_line_t* ln = &boot_lines[i];
        int y = 8 * s + (i - start) * lh;

        if (ln->banner) {
            w98_text_bold(ln->msg, x0, y, ln->color, BOOT_BG, s, NULL);
            continue;
        }

        if (ln->done) {
            w98_text(tagpre, x0, y, BOOT_GRAY, BOOT_BG, s, NULL);
            w98_text_bold("  OK  ", x0 + wpre, y, boot_ok_color(), BOOT_BG,
                          s, NULL);
            w98_text(tagpost, x0 + wpre + wok, y, BOOT_GRAY, BOOT_BG, s, NULL);
        } else {
            char sp[2] = { spin[boot_spin & 3], '\0' };
            w98_text(sp, x0 + wpre / 2, y, BOOT_GRAY, BOOT_BG, s, NULL);
        }

        w98_text(ln->msg, x_msg, y, BOOT_TEXT, BOOT_BG, s, NULL);
    }
}

static void boot_add_line(const char* msg, bool banner, uint32_t color) {
    /* an arriving update completes the previous pending line */
    for (int i = 0; i < boot_line_count; i++) boot_lines[i].done = true;

    if (boot_line_count >= BOOT_LINE_MAX) {
        for (int i = 1; i < BOOT_LINE_MAX; i++) boot_lines[i - 1] = boot_lines[i];
        boot_line_count = BOOT_LINE_MAX - 1;
    }
    boot_line_t* ln = &boot_lines[boot_line_count++];
    int k = 0;
    for (k = 0; msg[k] && k < BOOT_MSG_MAX - 1; k++) ln->msg[k] = msg[k];
    ln->msg[k] = '\0';
    ln->done = false;
    ln->banner = banner;
    ln->color = color;
    boot_spin++;
}

/* ------------------------------------------------------------------ */
/* Public API (same signatures as the old splash)                     */
/* ------------------------------------------------------------------ */

void boot_screen_show(void) {
    boot_line_count = 0;

    char banner[BOOT_MSG_MAX];
    int p = 0;
    const char* b = "SharkOS 2.2 (Sharkslayer)";
    for (int k = 0; b[k] && p < BOOT_MSG_MAX - 1; k++) banner[p++] = b[k];
    banner[p] = '\0';
    boot_add_line(banner, true, theme_current->logo);
    boot_lines[0].done = true;

    boot_add_line("nemo kernel 0.0.7 — ring zero, single address space",
                  true, BOOT_GRAY);
    boot_lines[1].done = true;

    boot_add_line("", true, BOOT_GRAY);
    boot_lines[2].done = true;

    boot_render();
}

void boot_screen_update(const char* message, int progress) {
    (void)progress;
    if (!message) return;
    boot_add_line(message, false, 0);
    boot_render();
    boot_wait_ms(220);   /* let it read like a real init */
}

void boot_screen_info(const char* message) {
    if (!message) return;
    boot_add_line(message, true, BOOT_GRAY);
    boot_lines[boot_line_count - 1].done = true;
    boot_render();
    boot_wait_ms(120);
}

void boot_screen_hide(void) {
    boot_add_line("Reached target SharkOS 98 desktop.", false, 0);
    for (int i = 0; i < boot_line_count; i++) boot_lines[i].done = true;
    boot_render();

    /* Pace the whole sequence to ~5 s once the PIT is ticking; on slow
     * machines that already took longer we don't add extra waiting. */
    boot_wait_ms(400);
    while (uptime_ticks < 4600) asm volatile("hlt");
    boot_wait_ms(300);

    draw_rect(0, 0, (int)screen_width, (int)screen_height, BOOT_BG);
}
