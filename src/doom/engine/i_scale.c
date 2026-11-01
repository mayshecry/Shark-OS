  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                             
                                     
                                                    
  

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomtype.h"

#include "i_video.h"
#include "m_argv.h"
#include "z_zone.h"

#if defined(_MSC_VER) && !defined(__cplusplus)
#define inline __inline
#endif

                          

static byte *src_buffer;

                                          

static byte *dest_buffer;

                                                  

static int dest_pitch;

                                                                  
                                
                                
                                                               

static byte *stretch_tables[2] = { NULL, NULL };

                                                 

static byte *half_stretch_table = NULL;

                                                                    
         

void I_InitScale(byte *_src_buffer, byte *_dest_buffer, int _dest_pitch)
{
    src_buffer = _src_buffer;
    dest_buffer = _dest_buffer;
    dest_pitch = _dest_pitch;
}

  
                                     
  

                                                                    
                                                                   

static boolean I_Scale1x(int x1, int y1, int x2, int y2)
{
    byte *bufp, *screenp;
    int y;
    int w = x2 - x1;
    
                                                           

    bufp = src_buffer + y1 * SCREENWIDTH + x1;
    screenp = (byte *) dest_buffer + y1 * dest_pitch + x1;

    for (y=y1; y<y2; ++y)
    {
        memcpy(screenp, bufp, w);
        screenp += dest_pitch;
        bufp += SCREENWIDTH;
    }

    return true;
}

screen_mode_t mode_scale_1x = {
    SCREENWIDTH, SCREENHEIGHT,
    NULL,
    I_Scale1x,
    false,
};

                     

static boolean I_Scale2x(int x1, int y1, int x2, int y2)
{
    byte *bufp, *screenp, *screenp2;
    int x, y;
    int multi_pitch;

    multi_pitch = dest_pitch * 2;
    bufp = src_buffer + y1 * SCREENWIDTH + x1;
    screenp = (byte *) dest_buffer + (y1 * dest_pitch + x1) * 2;
    screenp2 = screenp + dest_pitch;

    for (y=y1; y<y2; ++y)
    {
        byte *sp, *sp2, *bp;
        sp = screenp;
        sp2 = screenp2;
        bp = bufp;

        for (x=x1; x<x2; ++x)
        {
            *sp++ = *bp;  *sp++ = *bp;
            *sp2++ = *bp; *sp2++ = *bp;
            ++bp;
        }
        screenp += multi_pitch;
        screenp2 += multi_pitch;
        bufp += SCREENWIDTH;
    }

    return true;
}

screen_mode_t mode_scale_2x = {
    SCREENWIDTH * 2, SCREENHEIGHT * 2,
    NULL,
    I_Scale2x,
    false,
};

                     

static boolean I_Scale3x(int x1, int y1, int x2, int y2)
{
    byte *bufp, *screenp, *screenp2, *screenp3;
    int x, y;
    int multi_pitch;

    multi_pitch = dest_pitch * 3;
    bufp = src_buffer + y1 * SCREENWIDTH + x1;
    screenp = (byte *) dest_buffer + (y1 * dest_pitch + x1) * 3;
    screenp2 = screenp + dest_pitch;
    screenp3 = screenp + dest_pitch * 2;

    for (y=y1; y<y2; ++y)
    {
        byte *sp, *sp2, *sp3, *bp;
        sp = screenp;
        sp2 = screenp2;
        sp3 = screenp3;
        bp = bufp;

        for (x=x1; x<x2; ++x)
        {
            *sp++ = *bp;  *sp++ = *bp;  *sp++ = *bp;
            *sp2++ = *bp; *sp2++ = *bp; *sp2++ = *bp;
            *sp3++ = *bp; *sp3++ = *bp; *sp3++ = *bp;
            ++bp;
        }
        screenp += multi_pitch;
        screenp2 += multi_pitch;
        screenp3 += multi_pitch;
        bufp += SCREENWIDTH;
    }

    return true;
}

screen_mode_t mode_scale_3x = {
    SCREENWIDTH * 3, SCREENHEIGHT * 3,
    NULL,
    I_Scale3x,
    false,
};

                      

static boolean I_Scale4x(int x1, int y1, int x2, int y2)
{
    byte *bufp, *screenp, *screenp2, *screenp3, *screenp4;
    int x, y;
    int multi_pitch;

    multi_pitch = dest_pitch * 4;
    bufp = src_buffer + y1 * SCREENWIDTH + x1;
    screenp = (byte *) dest_buffer + (y1 * dest_pitch + x1) * 4;
    screenp2 = screenp + dest_pitch;
    screenp3 = screenp + dest_pitch * 2;
    screenp4 = screenp + dest_pitch * 3;

    for (y=y1; y<y2; ++y)
    {
        byte *sp, *sp2, *sp3, *sp4, *bp;
        sp = screenp;
        sp2 = screenp2;
        sp3 = screenp3;
        sp4 = screenp4;
        bp = bufp;

        for (x=x1; x<x2; ++x)
        {
            *sp++ = *bp;  *sp++ = *bp;  *sp++ = *bp;  *sp++ = *bp;
            *sp2++ = *bp; *sp2++ = *bp; *sp2++ = *bp; *sp2++ = *bp;
            *sp3++ = *bp; *sp3++ = *bp; *sp3++ = *bp; *sp3++ = *bp;
            *sp4++ = *bp; *sp4++ = *bp; *sp4++ = *bp; *sp4++ = *bp;
            ++bp;
        }
        screenp += multi_pitch;
        screenp2 += multi_pitch;
        screenp3 += multi_pitch;
        screenp4 += multi_pitch;
        bufp += SCREENWIDTH;
    }

    return true;
}

screen_mode_t mode_scale_4x = {
    SCREENWIDTH * 4, SCREENHEIGHT * 4,
    NULL,
    I_Scale4x,
    false,
};

                       

static boolean I_Scale5x(int x1, int y1, int x2, int y2)
{
    byte *bufp, *screenp, *screenp2, *screenp3, *screenp4, *screenp5;
    int x, y;
    int multi_pitch;

    multi_pitch = dest_pitch * 5;
    bufp = src_buffer + y1 * SCREENWIDTH + x1;
    screenp = (byte *) dest_buffer + (y1 * dest_pitch + x1) * 5;
    screenp2 = screenp + dest_pitch;
    screenp3 = screenp + dest_pitch * 2;
    screenp4 = screenp + dest_pitch * 3;
    screenp5 = screenp + dest_pitch * 4;

    for (y=y1; y<y2; ++y)
    {
        byte *sp, *sp2, *sp3, *sp4, *sp5, *bp;
        sp = screenp;
        sp2 = screenp2;
        sp3 = screenp3;
        sp4 = screenp4;
        sp5 = screenp5;
        bp = bufp;

        for (x=x1; x<x2; ++x)
        {
            *sp++ = *bp;  *sp++ = *bp;  *sp++ = *bp;  *sp++ = *bp;  *sp++ = *bp;
            *sp2++ = *bp; *sp2++ = *bp; *sp2++ = *bp; *sp2++ = *bp; *sp2++ = *bp;
            *sp3++ = *bp; *sp3++ = *bp; *sp3++ = *bp; *sp3++ = *bp; *sp3++ = *bp;
            *sp4++ = *bp; *sp4++ = *bp; *sp4++ = *bp; *sp4++ = *bp; *sp4++ = *bp;
            *sp5++ = *bp; *sp5++ = *bp; *sp5++ = *bp; *sp5++ = *bp; *sp5++ = *bp;
            ++bp;
        }
        screenp += multi_pitch;
        screenp2 += multi_pitch;
        screenp3 += multi_pitch;
        screenp4 += multi_pitch;
        screenp5 += multi_pitch;
        bufp += SCREENWIDTH;
    }

    return true;
}

screen_mode_t mode_scale_5x = {
    SCREENWIDTH * 5, SCREENHEIGHT * 5,
    NULL,
    I_Scale5x,
    false,
};


                                                                           
                   

static int FindNearestColor(byte *palette, int r, int g, int b)
{
    byte *col;
    int best;
    int best_diff;
    int diff;
    int i;

    best = 0;
    best_diff = INT_MAX;

    for (i=0; i<256; ++i)
    {
        col = palette + i * 3;
        diff = (r - col[0]) * (r - col[0])
             + (g - col[1]) * (g - col[1])
             + (b - col[2]) * (b - col[2]);

        if (diff == 0)
        {
            return i;
        }
        else if (diff < best_diff)
        {
            best = i;
            best_diff = diff;
        }
    }

    return best;
}

                                                                       
                                                                         
                                                                     
                

static byte *GenerateStretchTable(byte *palette, int pct)
{
    byte *result;
    int x, y;
    int r, g, b;
    byte *col1;
    byte *col2;

    result = Z_Malloc(256 * 256, PU_STATIC, NULL);

    for (x=0; x<256; ++x)
    {
        for (y=0; y<256; ++y)
        {
            col1 = palette + x * 3;
            col2 = palette + y * 3;
            r = (((int) col1[0]) * pct + ((int) col2[0]) * (100 - pct)) / 100;
            g = (((int) col1[1]) * pct + ((int) col2[1]) * (100 - pct)) / 100;
            b = (((int) col1[2]) * pct + ((int) col2[2]) * (100 - pct)) / 100;
            result[x * 256 + y] = FindNearestColor(palette, r, g, b);
        }
    }

    return result;
}

                                                                   
                       

static void I_InitStretchTables(byte *palette)
{
    if (stretch_tables[0] != NULL)
    {
        return;
    }

                                               
      
                                    
                                    
                                    
                                                   
                                                   
                                    

    printf("I_InitStretchTables: Generating lookup tables..");
    fflush(stdout);
    stretch_tables[0] = GenerateStretchTable(palette, 20);
    printf(".."); fflush(stdout);
    stretch_tables[1] = GenerateStretchTable(palette, 40);
    puts("");
}

                                               

static void I_InitSquashTable(byte *palette)
{
    if (half_stretch_table != NULL)
    {
        return;
    }

    printf("I_InitSquashTable: Generating lookup table..");
    fflush(stdout);
    half_stretch_table = GenerateStretchTable(palette, 50);
    puts("");
}

                                                                     
                                                                     
                                                         

void I_ResetScaleTables(byte *palette)
{
    if (stretch_tables[0] != NULL)
    {
        Z_Free(stretch_tables[0]);
        Z_Free(stretch_tables[1]);

        printf("I_ResetScaleTables: Regenerating lookup tables..\n");
        stretch_tables[0] = GenerateStretchTable(palette, 20);
        stretch_tables[1] = GenerateStretchTable(palette, 40);
    }

    if (half_stretch_table != NULL)
    {
        Z_Free(half_stretch_table);

        printf("I_ResetScaleTables: Regenerating lookup table..\n");

        half_stretch_table = GenerateStretchTable(palette, 50);
    }
}


   
                                              
  
                                                                
               
  

static inline void WriteBlendedLine1x(byte *dest, byte *src1, byte *src2, 
                               byte *stretch_table)
{
    int x;

    for (x=0; x<SCREENWIDTH; ++x)
    {
        *dest = stretch_table[*src1 * 256 + *src2];
        ++dest;
        ++src1;
        ++src2;
    }
} 

                       

static boolean I_Stretch1x(int x1, int y1, int x2, int y2)
{
    byte *bufp, *screenp;
    int y;

                                         

    if (x1 != 0 || y1 != 0 || x2 != SCREENWIDTH || y2 != SCREENHEIGHT)
    {
        return false;
    }    

                                                           

    bufp = src_buffer + y1 * SCREENWIDTH + x1;
    screenp = (byte *) dest_buffer + y1 * dest_pitch + x1;

                                                                          
                   

    for (y=0; y<SCREENHEIGHT; y += 5)
    {
                      
        memcpy(screenp, bufp, SCREENWIDTH);
        screenp += dest_pitch;

                                 
        WriteBlendedLine1x(screenp, bufp, bufp + SCREENWIDTH, stretch_tables[0]);
        screenp += dest_pitch; bufp += SCREENWIDTH;

                                 
        WriteBlendedLine1x(screenp, bufp, bufp + SCREENWIDTH, stretch_tables[1]);
        screenp += dest_pitch; bufp += SCREENWIDTH;

                                 
        WriteBlendedLine1x(screenp, bufp + SCREENWIDTH, bufp, stretch_tables[1]);
        screenp += dest_pitch; bufp += SCREENWIDTH;

                                 
        WriteBlendedLine1x(screenp, bufp + SCREENWIDTH, bufp, stretch_tables[0]);
        screenp += dest_pitch; bufp += SCREENWIDTH;

                      
        memcpy(screenp, bufp, SCREENWIDTH);
        screenp += dest_pitch; bufp += SCREENWIDTH;
    }

    return true;
}

screen_mode_t mode_stretch_1x = {
    SCREENWIDTH, SCREENHEIGHT_4_3,
    I_InitStretchTables,
    I_Stretch1x,
    true,
};

static inline void WriteLine2x(byte *dest, byte *src)
{
    int x;

    for (x=0; x<SCREENWIDTH; ++x)
    {
        dest[0] = *src;
        dest[1] = *src;
        dest += 2;
        ++src;
    }
}

static inline void WriteBlendedLine2x(byte *dest, byte *src1, byte *src2, 
                               byte *stretch_table)
{
    int x;
    int val;

    for (x=0; x<SCREENWIDTH; ++x)
    {
        val = stretch_table[*src1 * 256 + *src2];
        dest[0] = val;
        dest[1] = val;
        dest += 2;
        ++src1;
        ++src2;
    }
} 

                       

static boolean I_Stretch2x(int x1, int y1, int x2, int y2)
{
    byte *bufp, *screenp;
    int y;

                                         

    if (x1 != 0 || y1 != 0 || x2 != SCREENWIDTH || y2 != SCREENHEIGHT)
    {
        return false;
    }    

                                                           

    bufp = src_buffer + y1 * SCREENWIDTH + x1;
    screenp = (byte *) dest_buffer + y1 * dest_pitch + x1;

                                                                            
                   

    for (y=0; y<SCREENHEIGHT; y += 5)
    {
                      
        WriteLine2x(screenp, bufp);
        screenp += dest_pitch;

                      
        WriteLine2x(screenp, bufp);
        screenp += dest_pitch;

                                 
        WriteBlendedLine2x(screenp, bufp, bufp + SCREENWIDTH, stretch_tables[1]);
        screenp += dest_pitch; bufp += SCREENWIDTH;

                      
        WriteLine2x(screenp, bufp);
        screenp += dest_pitch;

                                 
        WriteBlendedLine2x(screenp, bufp + SCREENWIDTH, bufp, stretch_tables[0]);
        screenp += dest_pitch; bufp += SCREENWIDTH;

                      
        WriteLine2x(screenp, bufp);
        screenp += dest_pitch;

                      
        WriteLine2x(screenp, bufp);
        screenp += dest_pitch;

                                 
        WriteBlendedLine2x(screenp, bufp, bufp + SCREENWIDTH, stretch_tables[0]);
        screenp += dest_pitch; bufp += SCREENWIDTH;

                      
        WriteLine2x(screenp, bufp);
        screenp += dest_pitch;

                                 
        WriteBlendedLine2x(screenp, bufp + SCREENWIDTH, bufp, stretch_tables[1]);
        screenp += dest_pitch; bufp += SCREENWIDTH;

                      
        WriteLine2x(screenp, bufp);
        screenp += dest_pitch;

                      
        WriteLine2x(screenp, bufp);
        screenp += dest_pitch; bufp += SCREENWIDTH;
    }

    return true;
}

screen_mode_t mode_stretch_2x = {
    SCREENWIDTH * 2, SCREENHEIGHT_4_3 * 2,
    I_InitStretchTables,
    I_Stretch2x,
    false,
};

static inline void WriteLine3x(byte *dest, byte *src)
{
    int x;

    for (x=0; x<SCREENWIDTH; ++x)
    {
        dest[0] = *src;
        dest[1] = *src;
        dest[2] = *src;
        dest += 3;
        ++src;
    }
}

static inline void WriteBlendedLine3x(byte *dest, byte *src1, byte *src2, 
                               byte *stretch_table)
{
    int x;
    int val;

    for (x=0; x<SCREENWIDTH; ++x)
    {
        val = stretch_table[*src1 * 256 + *src2];
        dest[0] = val;
        dest[1] = val;
        dest[2] = val;
        dest += 3;
        ++src1;
        ++src2;
    }
} 

                       

static boolean I_Stretch3x(int x1, int y1, int x2, int y2)
{
    byte *bufp, *screenp;
    int y;

                                         

    if (x1 != 0 || y1 != 0 || x2 != SCREENWIDTH || y2 != SCREENHEIGHT)
    {
        return false;
    }    

                                                           

    bufp = src_buffer + y1 * SCREENWIDTH + x1;
    screenp = (byte *) dest_buffer + y1 * dest_pitch + x1;

                                                                            
                   

    for (y=0; y<SCREENHEIGHT; y += 5)
    {
                      
        WriteLine3x(screenp, bufp);
        screenp += dest_pitch;

                      
        WriteLine3x(screenp, bufp);
        screenp += dest_pitch;

                      
        WriteLine3x(screenp, bufp);
        screenp += dest_pitch;

                                 
        WriteBlendedLine3x(screenp, bufp + SCREENWIDTH, bufp, stretch_tables[1]);
        screenp += dest_pitch; bufp += SCREENWIDTH;

                      
        WriteLine3x(screenp, bufp);
        screenp += dest_pitch;

                      
        WriteLine3x(screenp, bufp);
        screenp += dest_pitch;

                      
        WriteLine3x(screenp, bufp);
        screenp += dest_pitch;

                                 
        WriteBlendedLine3x(screenp, bufp, bufp + SCREENWIDTH, stretch_tables[0]);
        screenp += dest_pitch; bufp += SCREENWIDTH;

                      
        WriteLine3x(screenp, bufp);
        screenp += dest_pitch;

                      
        WriteLine3x(screenp, bufp);
        screenp += dest_pitch;

                                 
        WriteBlendedLine3x(screenp, bufp + SCREENWIDTH, bufp, stretch_tables[0]);
        screenp += dest_pitch; bufp += SCREENWIDTH;

                      
        WriteLine3x(screenp, bufp);
        screenp += dest_pitch;

                      
        WriteLine3x(screenp, bufp);
        screenp += dest_pitch;

                      
        WriteLine3x(screenp, bufp);
        screenp += dest_pitch;

                                 
        WriteBlendedLine3x(screenp, bufp, bufp + SCREENWIDTH, stretch_tables[1]);
        screenp += dest_pitch; bufp += SCREENWIDTH;

                      
        WriteLine3x(screenp, bufp);
        screenp += dest_pitch;

                      
        WriteLine3x(screenp, bufp);
        screenp += dest_pitch;

                      
        WriteLine3x(screenp, bufp);
        screenp += dest_pitch; bufp += SCREENWIDTH;
    }

    return true;
}

screen_mode_t mode_stretch_3x = {
    SCREENWIDTH * 3, SCREENHEIGHT_4_3 * 3,
    I_InitStretchTables,
    I_Stretch3x,
    false,
};

static inline void WriteLine4x(byte *dest, byte *src)
{
    int x;

    for (x=0; x<SCREENWIDTH; ++x)
    {
        dest[0] = *src;
        dest[1] = *src;
        dest[2] = *src;
        dest[3] = *src;
        dest += 4;
        ++src;
    }
}

static inline void WriteBlendedLine4x(byte *dest, byte *src1, byte *src2, 
                               byte *stretch_table)
{
    int x;
    int val;

    for (x=0; x<SCREENWIDTH; ++x)
    {
        val = stretch_table[*src1 * 256 + *src2];
        dest[0] = val;
        dest[1] = val;
        dest[2] = val;
        dest[3] = val;
        dest += 4;
        ++src1;
        ++src2;
    }
} 

                        

static boolean I_Stretch4x(int x1, int y1, int x2, int y2)
{
    byte *bufp, *screenp;
    int y;

                                         

    if (x1 != 0 || y1 != 0 || x2 != SCREENWIDTH || y2 != SCREENHEIGHT)
    {
        return false;
    }    

                                                           

    bufp = src_buffer + y1 * SCREENWIDTH + x1;
    screenp = (byte *) dest_buffer + y1 * dest_pitch + x1;

                                                                            
                   

    for (y=0; y<SCREENHEIGHT; y += 5)
    {
                      
        WriteLine4x(screenp, bufp);
        screenp += dest_pitch;

                      
        WriteLine4x(screenp, bufp);
        screenp += dest_pitch;

                      
        WriteLine4x(screenp, bufp);
        screenp += dest_pitch;

                      
        WriteLine4x(screenp, bufp);
        screenp += dest_pitch;

                                 
        WriteBlendedLine4x(screenp, bufp + SCREENWIDTH, bufp, stretch_tables[0]);
        screenp += dest_pitch; bufp += SCREENWIDTH;

                      
        WriteLine4x(screenp, bufp);
        screenp += dest_pitch;

                      
        WriteLine4x(screenp, bufp);
        screenp += dest_pitch;

                      
        WriteLine4x(screenp, bufp);
        screenp += dest_pitch;

                      
        WriteLine4x(screenp, bufp);
        screenp += dest_pitch;

                                 
        WriteBlendedLine4x(screenp, bufp + SCREENWIDTH, bufp, stretch_tables[1]);
        screenp += dest_pitch; bufp += SCREENWIDTH;

                      
        WriteLine4x(screenp, bufp);
        screenp += dest_pitch;

                      
        WriteLine4x(screenp, bufp);
        screenp += dest_pitch;

                      
        WriteLine4x(screenp, bufp);
        screenp += dest_pitch;

                      
        WriteLine4x(screenp, bufp);
        screenp += dest_pitch;

                                 
        WriteBlendedLine4x(screenp, bufp, bufp + SCREENWIDTH, stretch_tables[1]);
        screenp += dest_pitch; bufp += SCREENWIDTH;

                      
        WriteLine4x(screenp, bufp);
        screenp += dest_pitch;

                      
        WriteLine4x(screenp, bufp);
        screenp += dest_pitch;

                      
        WriteLine4x(screenp, bufp);
        screenp += dest_pitch;

                      
        WriteLine4x(screenp, bufp);
        screenp += dest_pitch;

                                 
        WriteBlendedLine4x(screenp, bufp, bufp + SCREENWIDTH, stretch_tables[0]);
        screenp += dest_pitch; bufp += SCREENWIDTH;

                      
        WriteLine4x(screenp, bufp);
        screenp += dest_pitch;

                      
        WriteLine4x(screenp, bufp);
        screenp += dest_pitch;

                      
        WriteLine4x(screenp, bufp);
        screenp += dest_pitch;

                      
        WriteLine4x(screenp, bufp);
        screenp += dest_pitch; bufp += SCREENWIDTH;
    }

    return true;
}

screen_mode_t mode_stretch_4x = {
    SCREENWIDTH * 4, SCREENHEIGHT_4_3 * 4,
    I_InitStretchTables,
    I_Stretch4x,
    false,
};

static inline void WriteLine5x(byte *dest, byte *src)
{
    int x;

    for (x=0; x<SCREENWIDTH; ++x)
    {
        dest[0] = *src;
        dest[1] = *src;
        dest[2] = *src;
        dest[3] = *src;
        dest[4] = *src;
        dest += 5;
        ++src;
    }
}

                         

static boolean I_Stretch5x(int x1, int y1, int x2, int y2)
{
    byte *bufp, *screenp;
    int y;

                                         

    if (x1 != 0 || y1 != 0 || x2 != SCREENWIDTH || y2 != SCREENHEIGHT)
    {
        return false;
    }    

                                                           

    bufp = src_buffer + y1 * SCREENWIDTH + x1;
    screenp = (byte *) dest_buffer + y1 * dest_pitch + x1;

                                                                          
                    

    for (y=0; y<SCREENHEIGHT; y += 1)
    {
                      
        WriteLine5x(screenp, bufp);
        screenp += dest_pitch;

                      
        WriteLine5x(screenp, bufp);
        screenp += dest_pitch;

                      
        WriteLine5x(screenp, bufp);
        screenp += dest_pitch;

                      
        WriteLine5x(screenp, bufp);
        screenp += dest_pitch;

                      
        WriteLine5x(screenp, bufp);
        screenp += dest_pitch;

                      
        WriteLine5x(screenp, bufp);
        screenp += dest_pitch; bufp += SCREENWIDTH;
    }

                                                           
                                                        

    if (M_CheckParm("-scanline") > 0)
    {
        screenp = (byte *) dest_buffer + 2 * dest_pitch;

        for (y=0; y<1198; y += 3)
        {
            memset(screenp, 0, 1600);

            screenp += dest_pitch * 3;
        }
    }

    return true;
}

screen_mode_t mode_stretch_5x = {
    SCREENWIDTH * 5, SCREENHEIGHT_4_3 * 5,
    I_InitStretchTables,
    I_Stretch5x,
    false,
};

  
                                               
  
                                                                    
                                                                 
                                                                    
  
                                                                   
                                                                    
                                                        
  

   
                              
  

static inline void WriteSquashedLine1x(byte *dest, byte *src)
{
    int x;

    for (x=0; x<SCREENWIDTH; )
    {
                              

                                     

        *dest++ = stretch_tables[0][src[1] * 256 + src[0]];

                                     

        *dest++ = stretch_tables[1][src[2] * 256 + src[1]];

                                     

        *dest++ = stretch_tables[1][src[2] * 256 + src[3]];

                                     

        *dest++ = stretch_tables[0][src[3] * 256 + src[4]];

        x += 5;
        src += 5;
    }
}

                        

static boolean I_Squash1x(int x1, int y1, int x2, int y2)
{
    byte *bufp, *screenp;
    int y;

                                         

    if (x1 != 0 || y1 != 0 || x2 != SCREENWIDTH || y2 != SCREENHEIGHT)
    {
        return false;
    }    

    bufp = src_buffer;
    screenp = (byte *) dest_buffer;

    for (y=0; y<SCREENHEIGHT; ++y) 
    {
        WriteSquashedLine1x(screenp, bufp);

        screenp += dest_pitch;
        bufp += SCREENWIDTH;
    }

    return true;
}

screen_mode_t mode_squash_1x = {
    SCREENWIDTH_4_3, SCREENHEIGHT,
    I_InitStretchTables,
    I_Squash1x,
    true,
};


  
                              
  

#define DRAW_PIXEL2 \
      *dest++ = *dest2++ = c;

static inline void WriteSquashedLine2x(byte *dest, byte *src)
{
    byte *dest2;
    int x, c;

    dest2 = dest + dest_pitch;

    for (x=0; x<SCREENWIDTH; )
    {
                              

                       

        c = src[0];
        DRAW_PIXEL2;

                                   

        c = stretch_tables[1][src[1] * 256 + src[0]];
        DRAW_PIXEL2;

                       

        c = src[1];
        DRAW_PIXEL2;

                                   

        c = stretch_tables[0][src[1] * 256 + src[2]];
        DRAW_PIXEL2;

                                   

        c = stretch_tables[0][src[3] * 256 + src[2]];
        DRAW_PIXEL2;

                       

        c = src[3];
        DRAW_PIXEL2;

                                   

        c = stretch_tables[1][src[3] * 256 + src[4]];
        DRAW_PIXEL2;

                       

        c = src[4];
        DRAW_PIXEL2;

        x += 5;
        src += 5;
    }
}

                      

static boolean I_Squash2x(int x1, int y1, int x2, int y2)
{
    byte *bufp, *screenp;
    int y;

                                         

    if (x1 != 0 || y1 != 0 || x2 != SCREENWIDTH || y2 != SCREENHEIGHT)
    {
        return false;
    }    

    bufp = src_buffer;
    screenp = (byte *) dest_buffer;

    for (y=0; y<SCREENHEIGHT; ++y) 
    {
        WriteSquashedLine2x(screenp, bufp);

        screenp += dest_pitch * 2;
        bufp += SCREENWIDTH;
    }

    return true;
}

screen_mode_t mode_squash_2x = {
    SCREENWIDTH_4_3 * 2, SCREENHEIGHT * 2,
    I_InitStretchTables,
    I_Squash2x,
    false,
};


#define DRAW_PIXEL3 \
        *dest++ = *dest2++ = *dest3++ = c

static inline void WriteSquashedLine3x(byte *dest, byte *src)
{
    byte *dest2, *dest3;
    int x, c;

    dest2 = dest + dest_pitch;
    dest3 = dest + dest_pitch * 2;

    for (x=0; x<SCREENWIDTH; )
    {
                                                 

                          

        c = src[0];

        DRAW_PIXEL3;
        DRAW_PIXEL3;

                                   

        c = half_stretch_table[src[0] * 256 + src[1]];

        DRAW_PIXEL3;

                       

        c = src[1];

        DRAW_PIXEL3;
        DRAW_PIXEL3;

        x += 2;
        src += 2;
    }
}


  
                              
  
                                                                       
                                                                  
           
  

static boolean I_Squash3x(int x1, int y1, int x2, int y2)
{
    byte *bufp, *screenp;
    int y;

                                         

    if (x1 != 0 || y1 != 0 || x2 != SCREENWIDTH || y2 != SCREENHEIGHT)
    {
        return false;
    }    

    bufp = src_buffer;
    screenp = (byte *) dest_buffer;

    for (y=0; y<SCREENHEIGHT; ++y) 
    {
        WriteSquashedLine3x(screenp, bufp);

        screenp += dest_pitch * 3;
        bufp += SCREENWIDTH;
    }

    return true;
}

screen_mode_t mode_squash_3x = {
    800, 600,
    I_InitSquashTable,
    I_Squash3x,
    false,
};

#define DRAW_PIXEL4 \
        *dest++ = *dest2++ = *dest3++ = *dest4++ = c;
      
static inline void WriteSquashedLine4x(byte *dest, byte *src)
{
    int x;
    int c;
    byte *dest2, *dest3, *dest4;

    dest2 = dest + dest_pitch;
    dest3 = dest + dest_pitch * 2;
    dest4 = dest + dest_pitch * 3;

    for (x=0; x<SCREENWIDTH; )
    {
                              

                           

        c = src[0];
        DRAW_PIXEL4;
        DRAW_PIXEL4;
        DRAW_PIXEL4;

                                    

        c = stretch_tables[0][src[0] * 256 + src[1]];
        DRAW_PIXEL4;

                          

        c = src[1];
        DRAW_PIXEL4;
        DRAW_PIXEL4;

                                   

        c = stretch_tables[1][src[1] * 256 + src[2]];
        DRAW_PIXEL4;

                          

        c = src[2];
        DRAW_PIXEL4;
        DRAW_PIXEL4;

                                   

        c = stretch_tables[1][src[3] * 256 + src[2]];
        DRAW_PIXEL4;

                          

        c = src[3];
        DRAW_PIXEL4;
        DRAW_PIXEL4;

                                   

        c = stretch_tables[0][src[4] * 256 + src[3]];
        DRAW_PIXEL4;

                       

        c = src[4];
        DRAW_PIXEL4;
        DRAW_PIXEL4;
        DRAW_PIXEL4;

        x += 5;
        src += 5;
    }
}

  
                         
  

static boolean I_Squash4x(int x1, int y1, int x2, int y2)
{
    byte *bufp, *screenp;
    int y;

                                         

    if (x1 != 0 || y1 != 0 || x2 != SCREENWIDTH || y2 != SCREENHEIGHT)
    {
        return false;
    }    

    bufp = src_buffer;
    screenp = (byte *) dest_buffer;

    for (y=0; y<SCREENHEIGHT; ++y) 
    {
        WriteSquashedLine4x(screenp, bufp);

        screenp += dest_pitch * 4;
        bufp += SCREENWIDTH;
    }

    return true;
}

screen_mode_t mode_squash_4x = {
    SCREENWIDTH_4_3 * 4, SCREENHEIGHT * 4,
    I_InitStretchTables,
    I_Squash4x,
    false,
};

#define DRAW_PIXEL5 \
        *dest++ = *dest2++ = *dest3++ = *dest4++ = *dest5++ = c

static inline void WriteSquashedLine5x(byte *dest, byte *src)
{
    int x;
    int c;
    byte *dest2, *dest3, *dest4, *dest5;

    dest2 = dest + dest_pitch;
    dest3 = dest + dest_pitch * 2;
    dest4 = dest + dest_pitch * 3;
    dest5 = dest + dest_pitch * 4;

    for (x=0; x<SCREENWIDTH; ++x)
    {
                              

                           

        c = *src++;
        DRAW_PIXEL5;
        DRAW_PIXEL5;
        DRAW_PIXEL5;
        DRAW_PIXEL5;
    }
}

  
                          
  

static boolean I_Squash5x(int x1, int y1, int x2, int y2)
{
    byte *bufp, *screenp;
    int y;

                                         

    if (x1 != 0 || y1 != 0 || x2 != SCREENWIDTH || y2 != SCREENHEIGHT)
    {
        return false;
    }    

    bufp = src_buffer;
    screenp = (byte *) dest_buffer;

    for (y=0; y<SCREENHEIGHT; ++y) 
    {
        WriteSquashedLine5x(screenp, bufp);

        screenp += dest_pitch * 5;
        bufp += SCREENWIDTH;
    }

    return true;
}

screen_mode_t mode_squash_5x = {
    SCREENWIDTH_4_3 * 5, SCREENHEIGHT * 5,
    I_InitStretchTables,
    I_Squash5x,
    false,
};


