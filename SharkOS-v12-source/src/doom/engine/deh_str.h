  
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
  
                               
  

#ifndef DEH_STR_H
#define DEH_STR_H

#include <stdio.h>

#include "doomfeatures.h"

                                                                

#ifdef FEATURE_DEHACKED

char *DEH_String(char *s);
void DEH_printf(char *fmt, ...);
void DEH_fprintf(FILE *fstream, char *fmt, ...);
void DEH_snprintf(char *buffer, size_t len, char *fmt, ...);
void DEH_AddStringReplacement(char *from_text, char *to_text);


#else

#define DEH_String(x) (x)
#define DEH_printf printf
#define DEH_fprintf fprintf
#define DEH_snprintf snprintf
#define DEH_AddStringReplacement(x, y)

#endif

#endif                        

