#include "w_file.h"
#include "doomgeneric.h"
#include <string.h>

extern unsigned char sharkos_freedoom_wad_start[];
extern unsigned char sharkos_freedoom_wad_end[];

static wad_file_t sharkos_wad_file;
static int sharkos_wad_open;
#define CHUNK 65536u
static unsigned char chunk_cache[2][CHUNK];
static unsigned cache_id[2]={~0u,~0u};
extern long inflate_zlib(const unsigned char*,size_t,unsigned char*,size_t);
static unsigned word(const unsigned char *p){return p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24);}


static wad_file_t* sharkos_open(char* path) {
    if (!path) return NULL;
    size_t length = strlen(path);
    const char* name = path + length;
    while (name > path && name[-1] != '/' && name[-1] != '\\') name--;
    if (strcasecmp(name, "freedoom1.wad") != 0 && strcasecmp(name, "doom1.wad") != 0 && strcasecmp(name, "doom.wad") != 0) return NULL;
    sharkos_wad_file.mapped = NULL;
    sharkos_wad_file.length = word(sharkos_freedoom_wad_start+4);
    sharkos_wad_file.file_class = NULL;
    sharkos_wad_open = 1;
    return &sharkos_wad_file;
}

static void sharkos_close(wad_file_t* file) {
    if (file == &sharkos_wad_file) sharkos_wad_open = 0;
}

static size_t sharkos_read(wad_file_t* file, unsigned int offset, void* buffer, size_t length) {
    if (!sharkos_wad_open || file != &sharkos_wad_file || !buffer || offset >= file->length) return 0;
    if (length > file->length - offset) length = file->length - offset;
    size_t done=0;const unsigned char *p=sharkos_freedoom_wad_start;
    while(done<length){unsigned block=offset/CHUNK,in=offset%CHUNK,slot=block%2;
        unsigned size=file->length-block*CHUNK;if(size>CHUNK)size=CHUNK;
        if(cache_id[slot]!=block){unsigned a=word(p+8+4*block),b=word(p+12+4*block);
            cache_id[slot]=~0u;
            if(b<=a || b>(size_t)(sharkos_freedoom_wad_end-p) || inflate_zlib(p+a,b-a,chunk_cache[slot],size)!=(long)size)break;
            cache_id[slot]=block;
        }
        size_t n=size-in;if(n>length-done)n=length-done;memcpy((unsigned char*)buffer+done,chunk_cache[slot]+in,n);done+=n;offset+=n;
    }return done;
}

static wad_file_class_t sharkos_wad_class = {sharkos_open, sharkos_close, sharkos_read};

wad_file_t* W_OpenFile(char* path) {
    wad_file_t* file = sharkos_wad_class.OpenFile(path);
    if (file) file->file_class = &sharkos_wad_class;
    return file;
}

void W_CloseFile(wad_file_t* file) {
    if (file && file->file_class && file->file_class->CloseFile) file->file_class->CloseFile(file);
}

size_t W_Read(wad_file_t* file, unsigned int offset, void* buffer, size_t length) {
    if (!file || !file->file_class || !file->file_class->Read) return 0;
    return file->file_class->Read(file, offset, buffer, length);
}
