#include "kernel.h"
#include "desktop.h"
#include "theme.h"
#include "audio.h"
#include "ata.h"
#include "plugin_manager.h"
#include "browser.h"


desktop_state_t desktop;
audio_state_t g_audio;
audio_backend_t* g_audio_backend;

int fat16_mount(int drive, struct fs_node* mountpoint) {
    (void)drive;
    (void)mountpoint;
    return -1;
}

int fat16_unmount(struct fs_node* mountpoint) {
    (void)mountpoint;
    return -1;
}

int fat16_is_mounted(struct fs_node* node) {
    (void)node;
    return 0;
}

int fs_node_sync(struct fs_node* node) {
    (void)node;
    return 1;
}

int ata_drive_count(void) {
    return 0;
}

const ata_drive_t* ata_drive_get(int idx) {
    (void)idx;
    return NULL;
}

bool audio_available(void) {
    return 0;
}

const char* audio_backend_name(void) {
    return "unavailable in server image";
}

void audio_stop_all(void) {
}

void audio_set_master_volume(uint32_t volume) {
    g_audio.master_volume = volume;
}

int audio_play_wav(const uint8_t* data, size_t length, uint32_t volume, bool loop) {
    (void)data;
    (void)length;
    (void)volume;
    (void)loop;
    return -1;
}

int audio_play(const void* data, size_t bytes, uint32_t sample_rate, uint16_t channels, uint16_t bits, bool loop, uint32_t volume) {
    (void)data;
    (void)bytes;
    (void)sample_rate;
    (void)channels;
    (void)bits;
    (void)loop;
    (void)volume;
    return -1;
}

bool wav_parse(const uint8_t* data, size_t length, wav_info_t* info) {
    (void)data;
    (void)length;
    (void)info;
    return false;
}

void pcspkr_beep(uint32_t frequency, uint32_t duration) {
    (void)frequency;
    (void)duration;
}

int cmd_spkg(int argc, char** argv) {
    (void)argc;
    (void)argv;
    terminal_writestring("spkg: plugin packages are not included in the server image\n");
    return -1;
}

plugin_t* plugin_get_list(int* count) {
    if (count) *count = 0;
    return NULL;
}

plugin_t* plugin_get_by_name(const char* name) {
    (void)name;
    return NULL;
}

int plugin_is_plugin_command(const char* name, uint32_t* id) {
    (void)name;
    (void)id;
    return 0;
}

int plugin_execute(uint32_t id, int argc, char** argv) {
    (void)id;
    (void)argc;
    (void)argv;
    return -1;
}

void browser_open_url(const char* url) {
    (void)url;
}

void notepad_open_file(const char* path) {
    (void)path;
}

const char* desktop_wallpaper_name(int id) {
    (void)id;
    return "none";
}

void desktop_icon_launch_offset(window_type_t type, const char* title) {
    (void)type;
    (void)title;
}

void window_restore(int index) {
    (void)index;
}

void window_focus(int index) {
    (void)index;
}

int theme_get_id(void) {
    return 0;
}

const char* theme_get_name(int id) {
    (void)id;
    return "server";
}

char keyboard_getchar(void) {
    return 0;
}

void execute_elf(uint8_t* data, const char* args) {
    (void)data;
    (void)args;
    terminal_writestring("exec: executable loading is unavailable in the server image\n");
}
