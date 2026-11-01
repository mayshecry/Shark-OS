  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                                        
  


#ifndef __P_SAVEG__
#define __P_SAVEG__

#include <stdio.h>

                                         

#define SAVESTRINGSIZE 24

                                          

char *P_TempSaveGameFile(void);

                                      

char *P_SaveGameFile(int slot);

                                            

boolean P_ReadSaveGameHeader(void);
void P_WriteSaveGameHeader(char *description);

                                            

boolean P_ReadSaveGameEOF(void);
void P_WriteSaveGameEOF(void);

                                
                                           
void P_ArchivePlayers (void);
void P_UnArchivePlayers (void);
void P_ArchiveWorld (void);
void P_UnArchiveWorld (void);
void P_ArchiveThinkers (void);
void P_UnArchiveThinkers (void);
void P_ArchiveSpecials (void);
void P_UnArchiveSpecials (void);

extern FILE *save_stream;
extern boolean savegame_error;


#endif
