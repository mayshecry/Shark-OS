  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                                              
  


#ifndef __I_SOUND__
#define __I_SOUND__

#include "doomtype.h"


  
                  
  
typedef struct sfxinfo_struct	sfxinfo_t;

struct sfxinfo_struct
{
                                
    char *tagname;
    
                                                                
                                                                 

    char name[9];

                   
    int priority;

                                 
    sfxinfo_t *link;

                      
    int pitch;

                       
    int volume;

                                                   
                                                      
                                                 
    int usefulness;

                         
    int lumpnum;		

                                                                  
                
    int numchannels;

                                      
    void *driver_data;
};

  
                    
  
typedef struct
{
                             
    char *name;

                           
    int lumpnum;
    
                 
    void *data;

                                   
    void *handle;
    
} musicinfo_t;

typedef enum 
{
    SNDDEVICE_NONE = 0,
    SNDDEVICE_PCSPEAKER = 1,
    SNDDEVICE_ADLIB = 2,
    SNDDEVICE_SB = 3,
    SNDDEVICE_PAS = 4,
    SNDDEVICE_GUS = 5,
    SNDDEVICE_WAVEBLASTER = 6,
    SNDDEVICE_SOUNDCANVAS = 7,
    SNDDEVICE_GENMIDI = 8,
    SNDDEVICE_AWE32 = 9,
    SNDDEVICE_CD = 10,
} snddevice_t;

                              

typedef struct
{
                                                                

    snddevice_t *sound_devices;
    int num_sound_devices;

                              
                                               

    boolean (*Init)(boolean use_sfx_prefix);

                            

    void (*Shutdown)(void);

                                                 

    int (*GetSfxLumpNum)(sfxinfo_t *sfxinfo);

                                                   

    void (*Update)(void);

                                                      

    void (*UpdateSoundParams)(int channel, int vol, int sep);

                                                                
                        

    int (*StartSound)(sfxinfo_t *sfxinfo, int channel, int vol, int sep);

                                                   

    void (*StopSound)(int channel);

                                                       

    boolean (*SoundIsPlaying)(int channel);

                                                                 

    void (*CacheSounds)(sfxinfo_t *sounds, int num_sounds);

} sound_module_t;

void I_InitSound(boolean use_sfx_prefix);
void I_ShutdownSound(void);
int I_GetSfxLumpNum(sfxinfo_t *sfxinfo);
void I_UpdateSound(void);
void I_UpdateSoundParams(int channel, int vol, int sep);
int I_StartSound(sfxinfo_t *sfxinfo, int channel, int vol, int sep);
void I_StopSound(int channel);
boolean I_SoundIsPlaying(int channel);
void I_PrecacheSounds(sfxinfo_t *sounds, int num_sounds);

                              

typedef struct
{
                                                                

    snddevice_t *sound_devices;
    int num_sound_devices;

                                     

    boolean (*Init)(void);

                                   

    void (*Shutdown)(void);

                                     

    void (*SetMusicVolume)(int volume);

                  

    void (*PauseMusic)(void);

                     

    void (*ResumeMusic)(void);

                                       
                                                         

    void *(*RegisterSong)(void *data, int len);

                                   

    void (*UnRegisterSong)(void *handle);

                    

    void (*PlaySong)(void *handle, boolean looping);

                                     

    void (*StopSong)(void);

                                 

    boolean (*MusicIsPlaying)(void);

                                    

    void (*Poll)(void);
} music_module_t;

void I_InitMusic(void);
void I_ShutdownMusic(void);
void I_SetMusicVolume(int volume);
void I_PauseSong(void);
void I_ResumeSong(void);
void *I_RegisterSong(void *data, int len);
void I_UnRegisterSong(void *handle);
void I_PlaySong(void *handle, boolean looping);
void I_StopSong(void);
boolean I_MusicIsPlaying(void);

extern int snd_sfxdevice;
extern int snd_musicdevice;
extern int snd_samplerate;
extern int snd_cachesize;
extern int snd_maxslicetime_ms;
extern char *snd_musiccmd;

void I_BindSoundVariables(void);

                

void I_InitTimidityConfig(void);
#ifdef FEATURE_SOUND
extern sound_module_t DG_sound_module;
extern music_module_t DG_music_module;
#endif
extern sound_module_t sound_pcsound_module;
extern music_module_t music_opl_module;

                  

extern int opl_io_port;

                           

extern char *timidity_cfg_path;

#endif

