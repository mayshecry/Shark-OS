#include "kernel.h"
#include "desktop.h"
#include "ata.h"

extern char _kernel_start[];
extern char _kernel_end[];

#define DM_LBA_HDR   2048
#define DM_LBA_DATA  2049
#define DM_CHUNK     200
#define DM_ROW_H     21

#define DM_IDLE      0
#define DM_ARMED     1
#define DM_WRITING   2
#define DM_VERIFYING 3
#define DM_DONE      4
#define DM_ERR       5

static int dm_selected = 0;
static int dm_hover = -1;
static int dm_state = DM_IDLE;
static uint32_t dm_total_sectors = 0;
static uint32_t dm_done = 0;
static uint32_t dm_wsum = 0;
static uint32_t dm_vsum = 0;
static bool dm_has_install = false;
static uint32_t dm_install_ver = 0;
static bool dm_first_refresh = false;
static char dm_msg[72] = "Select a disk.";

static uint8_t dm_buf[DM_CHUNK * 512];

static void dm_set_msg(const char* s) {
    int i = 0;
    for (i = 0; s[i] && i < (int)sizeof(dm_msg) - 1; i++) dm_msg[i] = s[i];
    dm_msg[i] = '\0';
}

static bool dm_magic_ok(const uint8_t* sec) {
    const char* m = "SHKRNL01";
    for (int i = 0; i < 8; i++) {
        if (sec[i] != (uint8_t)m[i]) return false;
    }
    return true;
}

static uint32_t dm_rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void dm_wr32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void dm_refresh_install(void) {
    dm_has_install = false;
    dm_install_ver = 0;
    const ata_drive_t* d = ata_drive_get(dm_selected);
    if (!d) return;
    if (!ata_read_sectors(dm_selected, DM_LBA_HDR, 1, dm_buf)) return;
    if (!dm_magic_ok(dm_buf)) return;
    dm_has_install = true;
    dm_install_ver = dm_rd32(dm_buf + 8);
}

static void dm_format_size(uint64_t bytes, char* out) {
    uint32_t mb = (uint32_t)(bytes >> 20);
    if (mb >= 1024) {
        uint32_t gb = mb / 1024;
        uint32_t frac = (mb % 1024) * 10 / 1024;
        int p = 0;
        int_to_string(gb, out + p);
        p = (int)strlen(out);
        out[p++] = '.';
        out[p++] = (char)('0' + frac);
        out[p++] = ' ';
        out[p++] = 'G';
        out[p++] = 'B';
        out[p] = '\0';
    } else {
        uint32_t frac = (uint32_t)(((bytes >> 10) % 1024) * 10 / 1024);
        int p = 0;
        int_to_string(mb, out + p);
        p = (int)strlen(out);
        out[p++] = '.';
        out[p++] = (char)('0' + frac);
        out[p++] = ' ';
        out[p++] = 'M';
        out[p++] = 'B';
        out[p] = '\0';
    }
}

static void dm_layout(window_t* w, int* list_y, int* det_y, int* bar_y,
                      int* btn_y) {
    int cy = w->rect.client_y;
    int ch = w->rect.client_h;
    *list_y = cy + 38;
    *det_y = *list_y + 84 + 12;
    *bar_y = *det_y + 78;
    *btn_y = cy + ch - 30;
}

static int dm_list_rows(void) {
    int n = ata_drive_count();
    return n < 4 ? 4 : n;
}

void app_window_draw_diskmgmt(window_t* w) {
    int cx = w->rect.client_x;
    int cy = w->rect.client_y;
    int cw = w->rect.client_w;

    int list_y, det_y, bar_y, btn_y;
    dm_layout(w, &list_y, &det_y, &bar_y, &btn_y);

    dm_hover = -1;
    int mxp = desktop_mouse_x, myp = desktop_mouse_y;
    for (int i = 0; i < dm_list_rows(); i++) {
        int ry = list_y + 2 + i * DM_ROW_H;
        if (mxp >= cx + 8 && mxp < cx + cw - 8 && myp >= ry &&
            myp < ry + DM_ROW_H) {
            dm_hover = i;
        }
    }
    if (myp >= btn_y && myp < btn_y + 23) {
        if (mxp >= cx + 8 && mxp < cx + 98) dm_hover = 100;
        if (mxp >= cx + cw - 178 && mxp < cx + cw - 8) dm_hover = 101;
    }

    w98_fill(cx, cy, cw, w->rect.client_h, W98_BTNFACE);

    w98_text_bold("Disk Management", cx + cw / 2 - 55, cy + 6, W98_BTNTEXT,
                  W98_BTNFACE, 1, NULL);
    w98_bevel(cx + 4, cy + 18, cw - 8, 2, W98_BEVEL_ETCHED);

    w98_text_bold("Detected disks:", cx + 8, list_y - 12, W98_BTNTEXT,
                  W98_BTNFACE, 1, NULL);
    w98_surface(cx + 8, list_y, cw - 16, dm_list_rows() * DM_ROW_H + 4,
                W98_BEVEL_SUNKEN, W98_WINDOW);

    int n = ata_drive_count();
    if (dm_selected >= n) {
        dm_selected = n > 0 ? n - 1 : 0;
        dm_refresh_install();
    }

    /* One-shot: show a pre-existing install the first time the window
     * opens, without making the user hit Refresh. */
    if (!dm_first_refresh && dm_state == DM_IDLE && n > 0) {
        dm_first_refresh = true;
        dm_refresh_install();
    }

    for (int i = 0; i < n; i++) {
        const ata_drive_t* d = ata_drive_get(i);
        int ry = list_y + 2 + i * DM_ROW_H;
        bool sel = (i == dm_selected);
        if (sel) w98_fill(cx + 10, ry, cw - 20, DM_ROW_H - 1, W98_HIGHLIGHT);
        else if (dm_hover == i)
            w98_fill(cx + 10, ry, cw - 20, DM_ROW_H - 1, W98_BTNLIGHT);

        char line[64];
        char sz[16];
        dm_format_size(d->bytes, sz);
        strcpy(line, "Disk ");
        char num[8];
        int_to_string((uint32_t)i, num);
        int p = (int)strlen(line);
        for (int k = 0; num[k]; k++) line[p++] = num[k];
        line[p++] = ':';
        line[p++] = ' ';
        for (int k = 0; d->model[k] && p < 44; k++) line[p++] = d->model[k];
        line[p++] = ' ';
        line[p++] = '(';
        for (int k = 0; sz[k]; k++) line[p++] = sz[k];
        line[p++] = ')';
        line[p] = '\0';

        uint32_t fg = sel ? W98_HIGHLIGHTTEXT : W98_WINDOWTEXT;
        uint32_t bg = sel ? W98_HIGHLIGHT : W98_WINDOW;
        w98_text(line, cx + 14, ry + 5, fg, bg, 1, NULL);
    }
    if (n == 0) {
        w98_text("No disks detected. Add an IDE drive (qemu -hda disk.img).",
                 cx + 14, list_y + 7, W98_GRAYTEXT, W98_WINDOW, 1, NULL);
    }

    const ata_drive_t* d = ata_drive_get(dm_selected);
    char tmp[80];

    if (d) {
        strcpy(tmp, "Model: ");
        for (int k = 0; d->model[k] && k < 40; k++)
            tmp[7 + k] = d->model[k];
        tmp[7 + strlen(d->model)] = '\0';
        w98_text(tmp, cx + 8, det_y, W98_BTNTEXT, W98_BTNFACE, 1, NULL);

        char sz[16];
        dm_format_size(d->bytes, sz);
        strcpy(tmp, "Capacity: ");
        int p = (int)strlen(tmp);
        for (int k = 0; sz[k]; k++) tmp[p++] = sz[k];
        tmp[p] = '\0';
        w98_text(tmp, cx + 8, det_y + 15, W98_BTNTEXT, W98_BTNFACE, 1, NULL);

        strcpy(tmp, "Sectors: ");
        p = (int)strlen(tmp);
        char num[12];
        int_to_string(d->sectors, num);
        for (int k = 0; num[k]; k++) tmp[p++] = num[k];
        tmp[p] = '\0';
        w98_text(tmp, cx + 8, det_y + 30, W98_BTNTEXT, W98_BTNFACE, 1, NULL);

        if (dm_has_install) {
            strcpy(tmp, "SharkOS install: present (kernel V");
            p = (int)strlen(tmp);
            char num[8];
            int_to_string(dm_install_ver, num);
            for (int k = 0; num[k]; k++) tmp[p++] = num[k];
            tmp[p++] = ')';
            tmp[p] = '\0';
            w98_text(tmp, cx + 8, det_y + 45, 0xFF008040u, W98_BTNFACE, 1,
                     NULL);
        } else {
            w98_text("SharkOS install: none", cx + 8, det_y + 45,
                     W98_GRAYTEXT, W98_BTNFACE, 1, NULL);
        }
    } else {
        w98_text("No disk selected.", cx + 8, det_y, W98_GRAYTEXT,
                 W98_BTNFACE, 1, NULL);
    }

    int pct = 0;
    if (dm_state == DM_WRITING && dm_total_sectors)
        pct = (int)((uint64_t)dm_done * 100 / dm_total_sectors);
    if (dm_state == DM_VERIFYING)
        pct = (int)(90 + (uint64_t)dm_done * 10 / dm_total_sectors);
    if (dm_state == DM_DONE) pct = 100;

    w98_text("Install progress:", cx + 8, bar_y - 12, W98_BTNTEXT,
             W98_BTNFACE, 1, NULL);
    w98_surface(cx + 8, bar_y, cw - 96, 16, W98_BEVEL_SUNKEN, W98_WINDOW);
    if (pct > 0) {
        int fw = (cw - 100) * pct / 100;
        if (fw > 2) w98_fill(cx + 10, bar_y + 2, fw, 12, W98_HIGHLIGHT);
    }
    char pbuf[8];
    int_to_string((uint32_t)pct, pbuf);
    int pl = (int)strlen(pbuf);
    pbuf[pl++] = '%';
    pbuf[pl] = '\0';
    w98_text(pbuf, cx + cw - 80, bar_y + 4, W98_BTNTEXT, W98_BTNFACE, 1, NULL);

    w98_text(dm_msg, cx + 8, bar_y + 22, W98_BTNTEXT, W98_BTNFACE, 1, NULL);

    bool busy = (dm_state == DM_WRITING || dm_state == DM_VERIFYING);
    w98_button(cx + 8, btn_y, 90, 23, "Refresh", dm_hover == 100, !busy,
               false);

    const char* ilabel = "Install SharkOS...";
    if (dm_state == DM_ARMED) ilabel = "Confirm: erase disk!";
    if (busy) ilabel = "Working...";
    w98_button(cx + cw - 178, btn_y, 170, 23, ilabel, dm_hover == 101,
               d != NULL && !busy, false);
}

static void kstrcpy(char* dst, const char* src) {
    while (*src) *dst++ = *src++;
    *dst = '\0';
}

void app_window_mouse_diskmgmt(window_t* w, int mx, int my, int buttons) {
    if (!(buttons & 1)) return;

    int cx = w->rect.client_x;
    int cw = w->rect.client_w;
    int list_y, det_y, bar_y, btn_y;
    dm_layout(w, &list_y, &det_y, &bar_y, &btn_y);
    (void)det_y;
    (void)bar_y;

    /* Never let clicks re-target or cancel an in-flight write/verify. */
    bool busy = (dm_state == DM_WRITING || dm_state == DM_VERIFYING);
    if (busy) return;

    int n = ata_drive_count();
    for (int i = 0; i < n; i++) {
        int ry = list_y + 2 + i * DM_ROW_H;
        if (mx >= cx + 8 && mx < cx + cw - 8 && my >= ry && my < ry + DM_ROW_H) {
            if (dm_selected != i) {
                dm_selected = i;
                dm_state = DM_IDLE;
                dm_refresh_install();
                dm_set_msg("Disk selected. Installing is optional.");
            }
            w->needs_redraw = true;
            return;
        }
    }

    if (my >= btn_y && my < btn_y + 23 && mx >= cx + 8 && mx < cx + 98) {
        ata_init();
        n = ata_drive_count();
        if (dm_selected >= n) dm_selected = 0;
        dm_state = DM_IDLE;
        dm_refresh_install();
        dm_set_msg(n ? "Disk list refreshed." : "No disks detected.");
        w->needs_redraw = true;
        desktop.dirty = true;
        return;
    }

    if (my >= btn_y && my < btn_y + 23 && mx >= cx + cw - 178 &&
        mx < cx + cw - 8) {
        const ata_drive_t* d = ata_drive_get(dm_selected);
        if (!d) return;

        if (dm_state == DM_IDLE) {
            dm_state = DM_ARMED;
            dm_set_msg("This will overwrite the disk. Click again to confirm.");
        } else if (dm_state == DM_ARMED || dm_state == DM_DONE ||
                   dm_state == DM_ERR) {
            uint32_t kbytes = (uint32_t)(_kernel_end - _kernel_start);
            uint32_t secs = (kbytes + 511) / 512;
            if (DM_LBA_DATA + secs + 16 > d->sectors) {
                dm_state = DM_ERR;
                dm_set_msg("Disk too small for a SharkOS install.");
            } else {
                dm_state = DM_WRITING;
                dm_total_sectors = secs;
                dm_done = 0;
                dm_wsum = 0;
                dm_set_msg("Writing SharkOS kernel to disk...");
            }
        }
        w->needs_redraw = true;
        desktop.dirty = true;
    }
}

static void dm_sum_chunk(uint32_t words) {
    const uint32_t* p = (const uint32_t*)dm_buf;
    for (uint32_t i = 0; i < words; i++) dm_wsum += p[i];
}

static void dm_sum_verify(uint32_t words) {
    const uint32_t* p = (const uint32_t*)dm_buf;
    for (uint32_t i = 0; i < words; i++) dm_vsum += p[i];
}

void diskmgmt_tick(void) {
    if (dm_state != DM_WRITING && dm_state != DM_VERIFYING) return;
    desktop.dirty = true;   /* we tick globally; drive our own repaints */
    const ata_drive_t* d = ata_drive_get(dm_selected);
    if (!d) {
        dm_state = DM_ERR;
        dm_set_msg("Disk disappeared.");
        return;
    }

    if (dm_state == DM_WRITING) {
        uint32_t remain = dm_total_sectors - dm_done;
        uint32_t chunk = remain < DM_CHUNK ? remain : DM_CHUNK;
        uint32_t bytes = chunk * 512;
        uint32_t off = dm_done * 512;
        uint32_t kbytes = (uint32_t)(_kernel_end - _kernel_start);
        uint32_t avail = kbytes > off ? kbytes - off : 0;
        uint32_t copy = avail < bytes ? avail : bytes;

        const uint8_t* src = (const uint8_t*)_kernel_start + off;
        uint32_t i = 0;
        for (; i < copy; i++) dm_buf[i] = src[i];
        for (; i < bytes; i++) dm_buf[i] = 0;

        dm_sum_chunk(bytes / 4);

        if (!ata_write_sectors(dm_selected, DM_LBA_DATA + dm_done,
                               (uint8_t)chunk, dm_buf)) {
            dm_state = DM_ERR;
            dm_set_msg("Write error at sector ");
            int p = (int)strlen(dm_msg);
            char num[12];
            int_to_string(DM_LBA_DATA + dm_done, num);
            for (int k = 0; num[k]; k++) dm_msg[p++] = num[k];
            dm_msg[p] = '\0';
            return;
        }

        dm_done += chunk;
        if (dm_done >= dm_total_sectors) {
            for (i = 0; i < 512; i++) dm_buf[i] = 0;
            kstrcpy((char*)dm_buf, "SHKRNL01");
            dm_wr32(dm_buf + 8, 2);
            dm_wr32(dm_buf + 12, kbytes);
            dm_wr32(dm_buf + 16, dm_total_sectors);
            dm_wr32(dm_buf + 20, dm_wsum);
            if (!ata_write_sectors(dm_selected, DM_LBA_HDR, 1, dm_buf)) {
                dm_state = DM_ERR;
                dm_set_msg("Failed to write install record.");
                return;
            }
            dm_state = DM_VERIFYING;
            dm_done = 0;
            dm_vsum = 0;
            dm_set_msg("Verifying install...");
        }
        return;
    }

    uint32_t remain = dm_total_sectors - dm_done;
    uint32_t chunk = remain < DM_CHUNK ? remain : DM_CHUNK;
    if (!ata_read_sectors(dm_selected, DM_LBA_DATA + dm_done, (uint8_t)chunk,
                          dm_buf)) {
        dm_state = DM_ERR;
        dm_set_msg("Read error while verifying.");
        return;
    }
    dm_sum_verify(chunk * 128);
    dm_done += chunk;
    if (dm_done >= dm_total_sectors) {
        if (dm_vsum == dm_wsum) {
            dm_state = DM_DONE;
            dm_has_install = true;
            dm_install_ver = 2;
            dm_set_msg("SharkOS installed and verified. Boot still uses the ISO/USB.");
        } else {
            dm_state = DM_ERR;
            dm_set_msg("Verification failed: checksum mismatch.");
        }
    }
}
