  
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
  
                                      
  

#ifndef DEH_MAIN_H
#define DEH_MAIN_H

#include "doomtype.h"
#include "doomfeatures.h"
#include "deh_str.h"
#include "sha1.h"

                                                                          
                                                                             
                          

#define DEH_VANILLA_NUMSTATES 966
#define DEH_VANILLA_NUMSFX 107

void DEH_ParseCommandLine(void);
int DEH_LoadFile(char *filename);
int DEH_LoadLump(int lumpnum, boolean allow_long, boolean allow_error);
int DEH_LoadLumpByName(char *name, boolean allow_long, boolean allow_error);

boolean DEH_ParseAssignment(char *line, char **variable_name, char **value);

void DEH_Checksum(sha1_digest_t digest);

extern boolean deh_allow_extended_strings;
extern boolean deh_allow_long_strings;
extern boolean deh_allow_long_cheats;
extern boolean deh_apply_cheats;

#endif                         

