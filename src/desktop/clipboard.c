#include "kernel.h"

static char clipboard_buf[CLIPBOARD_MAX];
static int clipboard_len = 0;

void clipboard_clear(void) {
    clipboard_len = 0;
    clipboard_buf[0] = '\0';
}

int clipboard_length(void) {
    return clipboard_len;
}

const char* clipboard_get(void) {
    return clipboard_buf;
}

int clipboard_set_len(const char* text, int len) {
    if (!text || len <= 0) {
        clipboard_clear();
        return 0;
    }
    if (len > CLIPBOARD_MAX - 1) len = CLIPBOARD_MAX - 1;
    int out = 0;
    for (int i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)text[i];
        if (ch == '\0') break;
        if (ch == '\r') continue;
        if (ch < 32 && ch != '\n' && ch != '\t') continue;
        clipboard_buf[out++] = (char)ch;
    }
    clipboard_buf[out] = '\0';
    clipboard_len = out;
    return out;
}

int clipboard_set(const char* text) {
    if (!text) {
        clipboard_clear();
        return 0;
    }
    return clipboard_set_len(text, (int)strlen(text));
}

int clipboard_append(const char* text, int len) {
    if (!text || len <= 0) return clipboard_len;
    int room = CLIPBOARD_MAX - 1 - clipboard_len;
    if (room <= 0) return clipboard_len;
    if (len > room) len = room;
    int out = clipboard_len;
    for (int i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)text[i];
        if (ch == '\0') break;
        if (ch == '\r') continue;
        if (ch < 32 && ch != '\n' && ch != '\t') continue;
        if (out >= CLIPBOARD_MAX - 1) break;
        clipboard_buf[out++] = (char)ch;
    }
    clipboard_buf[out] = '\0';
    clipboard_len = out;
    return out;
}
