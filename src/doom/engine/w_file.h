  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                     
  


#ifndef __W_FILE__
#define __W_FILE__

#include <stdio.h>
#include "doomtype.h"

typedef struct _wad_file_s wad_file_t;

typedef struct
{
                               

    wad_file_t *(*OpenFile)(char *path);

                                

    void (*CloseFile)(wad_file_t *file);

                                                                  
                                                          

    size_t (*Read)(wad_file_t *file, unsigned int offset,
                   void *buffer, size_t buffer_len);

} wad_file_class_t;

struct _wad_file_s
{
                          

    wad_file_class_t *file_class;

                                                                       
                                                       

    byte *mapped;

                                    

    unsigned int length;
};

                                                                  
                                                              

wad_file_t *W_OpenFile(char *path);

                                

void W_CloseFile(wad_file_t *wad);

                                                                   
                                                                     
                                    

size_t W_Read(wad_file_t *wad, unsigned int offset,
              void *buffer, size_t buffer_len);

#endif                         
