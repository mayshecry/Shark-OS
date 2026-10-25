#include "kernel.h"
#include "audio.h"

static uint32_t le32(const uint8_t* p){ return p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24); }
static uint16_t le16(const uint8_t* p){ return p[0] | (p[1]<<8); }

bool wav_parse(const uint8_t* data, size_t len, wav_info_t* out){
    if (!data || len < 44 || !out) return false;
    if (data[0]!='R' || data[1]!='I' || data[2]!='F' || data[3]!='F') return false;
    if (data[8]!='W' || data[9]!='A' || data[10]!='V' || data[11]!='E') return false;
    size_t off = 12;
    uint16_t audio_fmt = 0, channels=0, bits=0;
    uint32_t sample_rate=0;
    const uint8_t* pcm=NULL; size_t pcm_len=0;
    while (off + 8 <= len){
        char tag[4]; tag[0]=data[off]; tag[1]=data[off+1]; tag[2]=data[off+2]; tag[3]=data[off+3];
        uint32_t sz = le32(data+off+4);
        if (off+8+sz > len) break;
        if (tag[0]=='f' && tag[1]=='m' && tag[2]=='t' && tag[3]==' '){
            if (sz < 16) return false;
            audio_fmt = le16(data+off+8);
            channels = le16(data+off+10);
            sample_rate = le32(data+off+12);
            // byte rate at 16, block align 20
            bits = le16(data+off+22);
            // audio_fmt 1 = PCM, 3 = IEEE float (unsupported)
            if (audio_fmt != 1) return false;
        } else if (tag[0]=='d' && tag[1]=='a' && tag[2]=='t' && tag[3]=='a'){
            pcm = data+off+8;
            pcm_len = sz;
        }
        off += 8 + sz;
        // pad byte
        if (sz & 1) off++;
    }
    if (!pcm || !sample_rate || !channels || !bits) return false;
    out->sample_rate = sample_rate;
    out->channels = channels;
    out->bits = bits;
    out->pcm_data = pcm;
    out->pcm_bytes = pcm_len;
    return true;
}

int audio_play_wav(const uint8_t* wav_data, size_t wav_len, uint32_t volume, bool loop){
    wav_info_t wi;
    if (!wav_parse(wav_data, wav_len, &wi)) return -1;
    audio_fmt_t fmt = (wi.bits==8) ? AUDIO_FMT_PCM_U8 : AUDIO_FMT_PCM_S16_LE;
    // For 8-bit, we can convert on fly? mixer currently expects s16; handle 8-bit as unsigned -> signed
    // We'll pass as raw and let mixer detect fmt via channels/bits? Simpler: keep fmt
    // Our audio_play takes bytes + rate + ch + bits, so pass through
    return audio_play(wi.pcm_data, wi.pcm_bytes, wi.sample_rate, wi.channels, wi.bits, loop, volume);
}

int audio_play_wav_mem(const char* name, uint32_t volume){
    // lookup in FS
    struct fs_node* n = search_path(name);
    if (!n) n = find_node(root, (char*)name);
    if (!n || n->type != FS_FILE) return -1;
    return audio_play_wav((uint8_t*)n->content, n->content_len, volume, false);
}
