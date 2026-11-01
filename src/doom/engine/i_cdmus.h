  
                                           
                                        
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  

            

#ifndef __ICDMUS__
#define __ICDMUS__

#define CDERR_NOTINSTALLED   10                        
#define CDERR_NOAUDIOSUPPORT 11                                
#define CDERR_NOAUDIOTRACKS  12                                  
#define CDERR_BADDRIVE       20                    
#define CDERR_BADTRACK       21                    
#define CDERR_IOCTLBUFFMEM   22                                   
#define CDERR_DEVREQBASE     100                        

extern int cd_Error;

int I_CDMusInit(void);
void I_CDMusPrintStartup(void);
int I_CDMusPlay(int track);
int I_CDMusStop(void);
int I_CDMusResume(void);
int I_CDMusSetVolume(int volume);
int I_CDMusFirstTrack(void);
int I_CDMusLastTrack(void);
int I_CDMusTrackLength(int track);

#endif
