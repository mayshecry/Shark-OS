  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                                     
      


#ifndef __M_CONFIG__
#define __M_CONFIG__

#include "doomtype.h"

void M_LoadDefaults(void);
void M_SaveDefaults(void);
void M_SaveDefaultsAlternate(char *main, char *extra);
void M_SetConfigDir(char *dir);
void M_BindVariable(char *name, void *variable);
boolean M_SetVariable(char *name, char *value);
int M_GetIntVariable(char *name);
const char *M_GetStrVariable(char *name);
float M_GetFloatVariable(char *name);
void M_SetConfigFilenames(char *main_config, char *extra_config);
char *M_GetSaveGameDir(char *iwadname);

extern char *configdir;

#endif
