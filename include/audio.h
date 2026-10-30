#ifndef SHARKOS_AUDIO_H
#define SHARKOS_AUDIO_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define AUDIO_SAMPLE_RATE 48000
#define AUDIO_CHANNELS 2
#define AUDIO_BITS 16
#define AUDIO_FRAME_BYTES (AUDIO_CHANNELS * AUDIO_BITS/8)  
#define AUDIO_BUFFER_FRAMES 480   
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
    size_t pos_bytes;  
    uint32_t volume;  
    bool loop;
    bool active;
    uint32_t id;
     
    uint32_t src_rate;
    uint32_t frac_pos;  
    uint32_t frac_step;
} audio_channel_t;

typedef struct {
    bool initialized;
    bool has_ac97;
    bool has_hda;
    bool has_pcspkr;
    uint32_t master_volume;  
    audio_channel_t channels[AUDIO_MAX_CHANNELS];
    uint32_t next_id;
     
    int16_t mix_buffer[AUDIO_BUFFER_FRAMES * AUDIO_CHANNELS];
     
    uint32_t frames_mixed;
    uint32_t underruns;
    uint32_t tick_count;
} audio_state_t;

extern audio_state_t g_audio;

 
void audio_init(void);
void audio_delay_ms(uint32_t ms);
void audio_tick(void);  
void audio_shutdown(void);
bool audio_available(void);
const char* audio_backend_name(void);

 
int audio_play(const void* data, size_t bytes, uint32_t sample_rate, uint16_t channels, uint16_t bits, bool loop, uint32_t volume);
int audio_play_s16(const int16_t* samples, size_t frames, uint32_t rate);
void audio_stop(int handle);
void audio_stop_all(void);
void audio_set_master_volume(uint32_t vol);  
void audio_set_volume(int handle, uint32_t vol);
bool audio_is_playing(int handle);
void audio_mix_frames(int16_t* out, int frames);  

 
typedef struct {
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits;
    const uint8_t* pcm_data;
    size_t pcm_bytes;
} wav_info_t;
bool wav_parse(const uint8_t* data, size_t len, wav_info_t* out);
int audio_play_wav(const uint8_t* wav_data, size_t wav_len, uint32_t volume, bool loop);
int audio_play_wav_mem(const char* name, uint32_t volume);  

 
typedef struct {
    const char* name;
    bool (*init)(void);
    void (*shutdown)(void);
    bool (*submit)(const int16_t* frames, size_t frame_count);  
    void (*set_volume)(uint32_t vol);
    bool (*is_ready)(void);
    void (*tick)(void);  
} audio_backend_t;

extern audio_backend_t* g_audio_backend;

 
bool pci_find_audio_device(uint8_t* bus, uint8_t* slot, uint8_t* func, uint16_t* vendor, uint16_t* device, uint32_t* classcode);
uint32_t pci_read_bar(uint8_t bus, uint8_t slot, uint8_t func, int bar);
void pic_unmask_irq(uint8_t irq);
void pic_mask_irq(uint8_t irq);

 
bool ac97_init(void);
void ac97_shutdown(void);
bool ac97_submit(const int16_t* frames, size_t frames_count);
void ac97_tick(void);
bool ac97_probe(uint8_t bus, uint8_t slot, uint8_t func);

 
bool hda_init(void);
void hda_shutdown(void);
bool hda_submit(const int16_t* frames, size_t frames_count);
void hda_tick(void);
bool hda_probe(uint8_t bus, uint8_t slot, uint8_t func);

 
bool pcspkr_init(void);
void pcspkr_beep(uint32_t freq_hz, uint32_t ms);
void pcspkr_tick(void);

#endif
