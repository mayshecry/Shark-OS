#include "w_file.h"
#include "doomgeneric.h"
#include <string.h>

extern unsigned char sharkos_freedoom_wad_start[];
extern unsigned char sharkos_freedoom_wad_end[];

static wad_file_t sharkos_wad_file;
static int sharkos_wad_open;

static wad_file_t* sharkos_open(char* path) {
    if (!path) return NULL;
    size_t length = strlen(path);
    const char* name = path + length;
    while (name > path && name[-1] != '/' && name[-1] != '\\') name--;
    if (strcasecmp(name, "freedoom1.wad") != 0 && strcasecmp(name, "doom1.wad") != 0 && strcasecmp(name, "doom.wad") != 0) return NULL;
    sharkos_wad_file.mapped = sharkos_freedoom_wad_start;
    sharkos_wad_file.length = (unsigned int)(sharkos_freedoom_wad_end - sharkos_freedoom_wad_start);
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
    memcpy(buffer, file->mapped + offset, length);
    return length;
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
