#ifndef SHARKOS_AUDIO_H
#define SHARKOS_AUDIO_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define AUDIO_SAMPLE_RATE 48000
#define AUDIO_CHANNELS 2
#define AUDIO_BITS 16
#define AUDIO_FRAME_BYTES (AUDIO_CHANNELS * AUDIO_BITS/8) // 4
#define AUDIO_BUFFER_FRAMES 480  // 10ms @48k => 1920 bytes
#define AUDIO_BDL_ENTRIES 32
#define AUDIO_MAX_CHANNELS 8

typedef enum {
    AUDIO_FMT_PCM_S16_LE = 0,
    AUDIO_FMT_PCM_U8 = 1,
} audio_fmt_t;

typedef struct {
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits;
    audio_fmt_t fmt;
    const uint8_t* data;
    size_t data_bytes;
    size_t pos_bytes; // playback cursor
    uint32_t volume; // 0-255
    bool loop;
    bool active;
    uint32_t id;
    // resampling state
    uint32_t src_rate;
    uint32_t frac_pos; // 16.16 fixed
    uint32_t frac_step;
} audio_channel_t;

typedef struct {
    bool initialized;
    bool has_ac97;
    bool has_hda;
    bool has_pcspkr;
    uint32_t master_volume; // 0-255
    audio_channel_t channels[AUDIO_MAX_CHANNELS];
    uint32_t next_id;
    // mixer output buffer for next tick (stereo s16)
    int16_t mix_buffer[AUDIO_BUFFER_FRAMES * AUDIO_CHANNELS];
    // stats
    uint32_t frames_mixed;
    uint32_t underruns;
    uint32_t tick_count;
} audio_state_t;

extern audio_state_t g_audio;

// core
void audio_init(void);
void audio_delay_ms(uint32_t ms);
void audio_tick(void); // call at 1kHz or vsync
void audio_shutdown(void);
bool audio_available(void);
const char* audio_backend_name(void);

// mixer api
int audio_play(const void* data, size_t bytes, uint32_t sample_rate, uint16_t channels, uint16_t bits, bool loop, uint32_t volume);
int audio_play_s16(const int16_t* samples, size_t frames, uint32_t rate);
void audio_stop(int handle);
void audio_stop_all(void);
void audio_set_master_volume(uint32_t vol); // 0-255
void audio_set_volume(int handle, uint32_t vol);
bool audio_is_playing(int handle);
void audio_mix_frames(int16_t* out, int frames); // mix N frames into out

// wav loader
typedef struct {
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits;
    const uint8_t* pcm_data;
    size_t pcm_bytes;
} wav_info_t;
bool wav_parse(const uint8_t* data, size_t len, wav_info_t* out);
int audio_play_wav(const uint8_t* wav_data, size_t wav_len, uint32_t volume, bool loop);
int audio_play_wav_mem(const char* name, uint32_t volume); // lookup in fs

// backend ops
typedef struct {
    const char* name;
    bool (*init)(void);
    void (*shutdown)(void);
    bool (*submit)(const int16_t* frames, size_t frame_count); // blocking DMA push
    void (*set_volume)(uint32_t vol);
    bool (*is_ready)(void);
    void (*tick)(void); // poll
} audio_backend_t;

extern audio_backend_t* g_audio_backend;

// PCI helpers
bool pci_find_audio_device(uint8_t* bus, uint8_t* slot, uint8_t* func, uint16_t* vendor, uint16_t* device, uint32_t* classcode);
uint32_t pci_read_bar(uint8_t bus, uint8_t slot, uint8_t func, int bar);
void pic_unmask_irq(uint8_t irq);
void pic_mask_irq(uint8_t irq);

// AC97
bool ac97_init(void);
void ac97_shutdown(void);
bool ac97_submit(const int16_t* frames, size_t frames_count);
void ac97_tick(void);
bool ac97_probe(uint8_t bus, uint8_t slot, uint8_t func);

// HDA
bool hda_init(void);
void hda_shutdown(void);
bool hda_submit(const int16_t* frames, size_t frames_count);
void hda_tick(void);
bool hda_probe(uint8_t bus, uint8_t slot, uint8_t func);

// PC speaker
bool pcspkr_init(void);
void pcspkr_beep(uint32_t freq_hz, uint32_t ms);
void pcspkr_tick(void);

#endif
