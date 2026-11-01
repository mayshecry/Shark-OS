  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                                              
  


#ifndef __S_SOUND__
#define __S_SOUND__

#include "p_mobj.h"
#include "sounds.h"

  
                                            
                                       
                                                
  

void S_Init(int sfxVolume, int musicVolume);


                   

void S_Shutdown(void);



  
                          
                                          
                                           
  

void S_Start(void);

  
                                    
                                  
  

void S_StartSound(void *origin, int sound_id);

                                   
void S_StopSound(mobj_t *origin);


                                             
void S_StartMusic(int music_id);

                                              
                           
void S_ChangeMusic(int music_id, int looping);

                            
boolean S_MusicPlaying(void);

                            
void S_StopMusic(void);

                                            
void S_PauseSound(void);
void S_ResumeSound(void);


  
                         
  
void S_UpdateSounds(mobj_t *listener);

void S_SetMusicVolume(int volume);
void S_SetSfxVolume(int volume);

extern int snd_channels;

#endif

