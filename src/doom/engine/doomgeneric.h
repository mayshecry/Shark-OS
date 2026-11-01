#ifndef DOOM_GENERIC
#define DOOM_GENERIC

#include <stdlib.h>
#include <stdint.h>

#ifndef DOOMGENERIC_RESX
#define DOOMGENERIC_RESX 640
#endif                     

#ifndef DOOMGENERIC_RESY
#define DOOMGENERIC_RESY 400
#endif                     


#ifdef CMAP256

typedef uint8_t pixel_t;

#else            

typedef uint32_t pixel_t;

#endif            


extern pixel_t* DG_ScreenBuffer;

#ifdef __cplusplus
extern "C" {
#endif

void doomgeneric_Create(int argc, char **argv);
void doomgeneric_Tick();


                                             
void DG_Init();
void DG_DrawFrame();
void DG_SleepMs(uint32_t ms);
uint32_t DG_GetTicksMs();
int DG_GetKey(int* pressed, unsigned char* key);
void DG_SetWindowTitle(const char * title);

#ifdef __cplusplus
}
#endif

#endif               
