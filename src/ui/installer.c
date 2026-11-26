#include "kernel.h"
#include "ata.h"
#include "install.h"
#include "store.h"
#define BG 0xff091e32u
static char key(void) {
    char c;
    while (!(c = keyboard_getchar()))
        yield();
    return c;
}
static void line(int row, const char *text) { draw_string_px(text, 16, 12 + row * 20, 0xffe2f0ff, BG); }
static void clear(void) {
    draw_rect(0, 0, screen_width, screen_height, BG);
    line(0, "SharkOS disk installer");
}
static void message(const char *msg) {
    clear();
    line(2, msg);
    line(4, "Enter or Esc: return to setup");
    serial_puts(msg);
    serial_puts("\n");
    for (;;) {
        char c = key();
        if (c == 27 || c == '\n' || c == '\r')
            return;
    }
}
static void progress(const char *msg, unsigned percent) {
    char n[12];
    int_to_string(percent, n);
    draw_rect(0, 48, screen_width, 52, BG);
    line(2, msg);
    line(3, n);
}
static void describe(int row, int i) {
    const ata_drive_t *d = ata_drive_get(i);
    char text[128], n[16];
    int pos = 0;
    const char *parts[] = {d->ahci?"AHCI ":"ATA ",
                           NULL,
                           "  ",
                           d->model,
                           "  ",
                           NULL,
                           " MiB  ",
                           d->ahci ? "SATA" : d->io_base == 0x1f0 ? "primary " : "secondary ",
                           d->ahci ? "" : d->master ? "master" : "slave",
                           d->install_supported ? "" : " (inventory only)"};
    for (unsigned k = 0; k < sizeof parts / sizeof parts[0]; k++) {
        const char *s = parts[k];
        if (!s) {
            ata_number(k == 1 ? (unsigned)i : d->bytes >> 20, n);
            s = n;
        }
        while (*s && pos < 127)
            text[pos++] = *s++;
    }
    text[pos] = 0;
    line(row, text);
}
static int read_phrase(const char *expected) {
    char typed[20]={0};
    unsigned n = 0;
    for (;;) {
        char c = key();
        if (c == 27)
            return -1;
        if (c == '\n' || c == '\r') {
            return !strcmp(expected, typed);
        }
        if (c == '\b' && n)
            typed[--n] = 0;
        else if (c >= 32 && n < sizeof typed - 1) {
            typed[n++] = c;
            typed[n] = 0;
        } else if (c >= 32) {
            return 0;
        }
        draw_rect(16, 212, 160, 20, BG);
        line(10, typed);
    }
}
static int confirm_erase(int idx,const char *reason) {
    char phrase[]="erase 0";phrase[6]+=(char)idx;
    clear();line(1,"Erase and reinstall?");describe(2,idx);line(3,reason);
    line(4,"This replaces the partition table and old OS.");
    line(5,"Files on this disk may be lost. Back up first.");
    line(6,"Not a secure wipe. Read/write failures still stop installation.");
    line(7,"Esc or Enter alone: cancel. To erase, type exactly:");line(8,phrase);
    serial_puts("[install] Erase and reinstall? ");serial_puts(reason);serial_puts("\n");
    return read_phrase(phrase)==1;
}
void install_menu(void) {
    if (!install_image_ready()) {
        message("No installer payload. Boot the desktop live ISO.");
        return;
    }
    int count = ata_drive_count();
    if (!count) {
        message(ata_scan_status());
        return;
    }
    clear();
    line(2, "Select a target disk by number. Esc cancels.");
    for (int i = 0; i < count; i++)
        describe(4 + i, i);
    int idx;
    for (;;) {
        char c = key();
        if (c == 27)
            return;
        idx = c - '0';
        if (idx >= 0 && idx < count)
            break;
    }
    const ata_drive_t *chosen = ata_drive_get(idx);
    if (!chosen || !chosen->install_supported) {
        message("Requires a supported 512-byte ATA or AHCI disk.");
        return;
    }
    if (chosen->bytes < install_size()) {
        message("Disk too small for this install image.");
        return;
    }
    if(store_installed() || fat16_drive_mounted(idx)){message("Boot the live ISO and unmount this disk first.");return;}
    clear();
    describe(2, idx);
    line(4, "This writes a boot partition to the selected disk.");
    line(5, "Blank disks install normally. Used disks need a second confirmation.");
    char summary[48] = "Image size (MiB): ";
    int_to_string((install_size() + 1048575u) >> 20, summary + 18);
    line(6, summary);
    char expected[] = "install 0";
    expected[8] += (char)idx;
    line(7, "Type the phrase below, then Enter. Esc cancels.");
    line(8, expected);
    int confirmed=read_phrase(expected);
    if (confirmed<0)return;
    if (!confirmed){message("Confirmation did not match. No data written.");return;}
    clear();
    message(install_disk_confirmed(idx,progress,confirm_erase));
}
