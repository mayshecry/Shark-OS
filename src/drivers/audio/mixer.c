#include "kernel.h"
#include "audio.h"

audio_state_t g_audio;

static inline int16_t clamp16(int32_t v){
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

// linear resampler: src_rate -> 48k
static void mix_channel(audio_channel_t* ch, int16_t* out, int frames, uint32_t master_vol){
    if (!ch->active || !ch->data) return;
    uint32_t step = ch->frac_step;
    if (step == 0){
        // compute step = src_rate / 48000 * 65536
        // but we stored frac_step already at play time
        step = ((uint64_t)ch->src_rate * 65536ULL) / AUDIO_SAMPLE_RATE;
        if (step==0) step=65536;
        ch->frac_step = step;
    }
    uint32_t pos = ch->frac_pos;
    // total source frames available
    size_t src_frames = ch->data_bytes / (ch->channels * ch->bits/8);
    if (ch->channels==0 || ch->bits==0) return;
    uint32_t vol = (ch->volume * master_vol) / 255; // 0-255
    if (vol==0) return;

    for (int i=0;i<frames;i++){
        // src index
        uint32_t idx = pos >> 16;
        uint32_t frac = pos & 0xFFFF;
        if (idx >= src_frames){
            if (ch->loop){
                pos = 0;
                idx = 0;
                frac = 0;
            } else {
                ch->active = false;
                break;
            }
        }
        int32_t s0_l=0, s0_r=0, s1_l=0, s1_r=0;
        // fetch two adjacent frames for interpolation
        if (ch->bits==16){
            const int16_t* p = (const int16_t*)ch->data;
            if (ch->channels==1){
                // mono -> duplicate to stereo
                int16_t v0 = (idx < src_frames) ? p[idx] : 0;
                int16_t v1 = (idx+1 < src_frames) ? p[idx+1] : v0;
                s0_l = s0_r = v0;
                s1_l = s1_r = v1;
            } else if (ch->channels==2){
                int16_t v0l = (idx < src_frames) ? p[idx*2+0] : 0;
                int16_t v0r = (idx < src_frames) ? p[idx*2+1] : 0;
                int16_t v1l = (idx+1 < src_frames) ? p[(idx+1)*2+0] : v0l;
                int16_t v1r = (idx+1 < src_frames) ? p[(idx+1)*2+1] : v0r;
                s0_l=v0l; s0_r=v0r; s1_l=v1l; s1_r=v1r;
            }
        } else if (ch->bits==8){
            const uint8_t* p = (const uint8_t*)ch->data;
            // 8-bit unsigned 0-255 -> signed -128..127 -> scaled to 16-bit
            if (ch->channels==1){
                int16_t v0 = ((int16_t)p[idx] - 128) << 8;
                int16_t v1 = (idx+1 < src_frames) ? ((int16_t)p[idx+1]-128)<<8 : v0;
                s0_l=s0_r=v0; s1_l=s1_r=v1;
            } else if (ch->channels==2){
                int16_t v0l = ((int16_t)p[idx*2+0]-128)<<8;
                int16_t v0r = ((int16_t)p[idx*2+1]-128)<<8;
                int16_t v1l = (idx+1 < src_frames) ? ((int16_t)p[(idx+1)*2+0]-128)<<8 : v0l;
                int16_t v1r = (idx+1 < src_frames) ? ((int16_t)p[(idx+1)*2+1]-128)<<8 : v0r;
                s0_l=v0l; s0_r=v0r; s1_l=v1l; s1_r=v1r;
            }
        } else {
            // unsupported bits ignore
            break;
        }
        // linear interp
        int32_t cur_l = s0_l + (((s1_l - s0_l) * (int32_t)frac) >> 16);
        int32_t cur_r = s0_r + (((s1_r - s0_r) * (int32_t)frac) >> 16);
        // volume scaling and mix
        cur_l = (cur_l * (int32_t)vol) >> 8;
        cur_r = (cur_r * (int32_t)vol) >> 8;
        // mix into output (accumulate with clipping later per out element? but we sum directly)
        // out already may contain previous channel sums; we add
        int idx2 = i*2;
        int32_t mixed_l = (int32_t)out[idx2+0] + cur_l;
        int32_t mixed_r = (int32_t)out[idx2+1] + cur_r;
        out[idx2+0] = clamp16(mixed_l);
        out[idx2+1] = clamp16(mixed_r);

        pos += step;
    }
    ch->frac_pos = pos;
    // update bytes pos for state (for simplicity keep frac sync)
    // If finished and not loop, already deactivated above; if at end handle wrap
    if (!ch->loop && (pos>>16) >= src_frames){
        ch->active = false;
    }
}

void audio_mix_frames(int16_t* out, int frames){
    // clear
    for (int i=0;i<frames*AUDIO_CHANNELS;i++) out[i]=0;
    for (int c=0;c<AUDIO_MAX_CHANNELS;c++){
        mix_channel(&g_audio.channels[c], out, frames, g_audio.master_volume);
    }
    g_audio.frames_mixed += frames;
    g_audio.tick_count++;
}

int audio_play(const void* data, size_t bytes, uint32_t sample_rate, uint16_t channels, uint16_t bits, bool loop, uint32_t volume){
    if (!data || !bytes || !sample_rate) return -1;
    // find free channel
    int idx = -1;
    for (int i=0;i<AUDIO_MAX_CHANNELS;i++){
        if (!g_audio.channels[i].active){
            idx = i; break;
        }
    }
    if (idx==-1) return -1; // no free
    audio_channel_t* ch = &g_audio.channels[idx];
    ch->data = (const uint8_t*)data;
    ch->data_bytes = bytes;
    ch->pos_bytes = 0;
    ch->sample_rate = AUDIO_SAMPLE_RATE; // output rate always 48k (informational)
    ch->src_rate = sample_rate;
    ch->channels = channels;
    ch->bits = bits;
    ch->fmt = (bits==8) ? AUDIO_FMT_PCM_U8 : AUDIO_FMT_PCM_S16_LE;
    ch->volume = volume > 255 ? 255 : volume;
    ch->loop = loop;
    ch->active = true;
    ch->frac_pos = 0;
    ch->frac_step = ((uint64_t)sample_rate * 65536ULL) / AUDIO_SAMPLE_RATE;
    if (ch->frac_step==0) ch->frac_step=65536;
    ch->id = g_audio.next_id++;
    if (g_audio.next_id==0) g_audio.next_id=1;
    return idx; // handle = channel index
}

int audio_play_s16(const int16_t* samples, size_t frames, uint32_t rate){
    return audio_play(samples, frames* AUDIO_FRAME_BYTES, rate, 2, 16, false, 255);
}

void audio_stop(int handle){
    if (handle<0 || handle>=AUDIO_MAX_CHANNELS) return;
    g_audio.channels[handle].active = false;
}
void audio_stop_all(void){
    for (int i=0;i<AUDIO_MAX_CHANNELS;i++) g_audio.channels[i].active=false;
}
void audio_set_master_volume(uint32_t vol){
    if (vol>255) vol=255;
    g_audio.master_volume=vol;
    if (g_audio_backend && g_audio_backend->set_volume) g_audio_backend->set_volume(vol);
}
void audio_set_volume(int handle, uint32_t vol){
    if (handle<0 || handle>=AUDIO_MAX_CHANNELS) return;
    if (vol>255) vol=255;
    g_audio.channels[handle].volume=vol;
}
bool audio_is_playing(int handle){
    if (handle<0 || handle>=AUDIO_MAX_CHANNELS) return false;
    return g_audio.channels[handle].active;
}
