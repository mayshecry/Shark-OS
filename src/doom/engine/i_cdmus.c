  
                                           
                                        
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
  
                                                
  

#include <stdio.h>

#ifdef ORIGCODE
#include "SDL2/SDL.h"
#include "SDL2/SDL_cdrom.h"
#endif

#include "doomtype.h"

#include "i_cdmus.h"

#ifdef ORIGCODE
static SDL_CD *cd_handle = NULL;
static char *startup_error = NULL;
static const char *cd_name = NULL;
#endif

int cd_Error;

int I_CDMusInit(void)
{
#ifdef ORIGCODE
    int drive_num = 0;

                                                                         
                                                                       
                               

    if (cd_handle == NULL)
    {
        if (SDL_Init(SDL_INIT_CDROM) < 0)
        {
            startup_error = "Failed to init CD subsystem.";
            cd_Error = 1;
            return -1;
        }

                                                         

        cd_handle = SDL_CDOpen(drive_num);

        if (cd_handle == NULL)
        {
            startup_error = "Failed to open CD-ROM drive.";
            cd_Error = 1;
            return -1;
        }

        cd_name = SDL_CDName(drive_num);
    }

    if (SDL_CDStatus(cd_handle) == CD_ERROR)
    {
        startup_error = "Failed to read CD status.";
        cd_Error = 1;
        return -1;
    }

    if (!CD_INDRIVE(cd_handle->status))
    {
        startup_error = "No CD in drive.";
        cd_Error = 1;
        return -1;
    }

    cd_Error = 0;
#endif
    return 0;
}

                                                                   
                                                    

void I_CDMusPrintStartup(void)
{
#ifdef ORIGCODE
    if (cd_name != NULL)
    {
        printf("I_CDMusInit: Using CD-ROM drive: %s\n", cd_name);
    }

    if (startup_error != NULL)
    {
        fprintf(stderr, "I_CDMusInit: %s\n", startup_error);
    }
#endif
}

int I_CDMusPlay(int track)
{
#ifdef ORIGCODE
    int result;

    if (cd_handle == NULL)
    {
        cd_Error = 1;
        return -1;
    }

                     
                               

    result = SDL_CDPlayTracks(cd_handle, track - 1, 0, 1, 0);

    cd_Error = 0;
    return result;
#else
	return 0;
#endif
}

int I_CDMusStop(void)
{
#ifdef ORIGCODE
    int result;

    result = SDL_CDStop(cd_handle);

    cd_Error = 0;

    return result;
#else
	return 0;
#endif
}

int I_CDMusResume(void)
{
#ifdef ORIGCODE
    int result;

    result = SDL_CDResume(cd_handle);

    cd_Error = 0;

    return result;
#else
	return 0;
#endif
}

int I_CDMusSetVolume(int volume)
{
                           

    cd_Error = 0;

    return 0;
}

int I_CDMusFirstTrack(void)
{
#ifdef ORIGCODE
    int i;

    if (cd_handle == NULL)
    {
        cd_Error = 1;
        return -1;
    }

                                  

    for (i=0; i<cd_handle->numtracks; ++i) 
    {
        if (cd_handle->track[i].type == SDL_AUDIO_TRACK)
        {
            cd_Error = 0;

                                         
            return i + 1;
        }
    }

                  
    cd_Error = 1;

    return -1;
#else
	return 0;
#endif
}

int I_CDMusLastTrack(void)
{
#ifdef ORIGCODE
    if (cd_handle == NULL)
    {
        cd_Error = 1;
        return -1;
    }

    cd_Error = 0;

    return cd_handle->numtracks;
#else
	return 0;
#endif
}

int I_CDMusTrackLength(int track_num)
{
#ifdef ORIGCODE
    SDL_CDtrack *track;

    if (cd_handle == NULL || track_num < 1 || track_num > cd_handle->numtracks)
    {
        cd_Error = 1;
        return -1;
    }

                                      

    track = &cd_handle->track[track_num - 1];

                                  

    cd_Error = 0;

    return (track->length + CD_FPS - 1) / CD_FPS;
#else
	return 0;
#endif
}

