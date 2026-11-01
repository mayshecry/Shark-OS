  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                                                             
                                                                 
                                                                    
                                  
  


#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "deh_main.h"
#include "doomdef.h"
#include "doomstat.h"

#include "dstrings.h"
#include "doomfeatures.h"
#include "sounds.h"

#include "d_iwad.h"

#include "z_zone.h"
#include "w_main.h"
#include "w_wad.h"
#include "s_sound.h"
#include "v_video.h"

#include "f_finale.h"
#include "f_wipe.h"

#include "m_argv.h"
#include "m_config.h"
#include "m_controls.h"
#include "m_misc.h"
#include "m_menu.h"
#include "p_saveg.h"

#include "i_endoom.h"
#include "i_joystick.h"
#include "i_system.h"
#include "i_timer.h"
#include "i_video.h"

#include "g_game.h"

#include "hu_stuff.h"
#include "wi_stuff.h"
#include "st_stuff.h"
#include "am_map.h"
#include "net_client.h"
#include "net_dedicated.h"
#include "net_query.h"

#include "p_setup.h"
#include "r_local.h"
#include "statdump.h"

#include "d_main.h"

  
               
                                   
                                       
                                      
                         
                                                  
                                                 
  
void D_DoomLoop (void);

                                      

char *          savegamedir;

                                 

char *          iwadfile;


boolean		devparm;	                             
boolean         nomonsters;	                           
boolean         respawnparm;	                        
boolean         fastparm;	                     

                         
                        
                          

extern  boolean	inhelpscreens;

skill_t		startskill;
int             startepisode;
int		startmap;
boolean		autostart;
int             startloadgame;

boolean		advancedemo;

                                       
boolean         storedemo;

                                                                
boolean         bfgedition;

                                           
boolean         main_loop_started = false;

char		wadfile[1024];		                   
char		mapdir[1024];                                           

int             show_endoom = 1;


void D_ConnectNetGame(void);
void D_CheckNetGame(void);


  
                  
                                                                      
  
void D_ProcessEvents (void)
{
    event_t*	ev;
	
                                         
    if (storedemo)
        return;
	
    while ((ev = D_PopEvent()) != NULL)
    {
	if (M_Responder (ev))
	    continue;                                    
	G_Responder (ev);
    }
}




  
            
                                                              
  

                                                                  
gamestate_t     wipegamestate = GS_DEMOSCREEN;
extern  boolean setsizeneeded;
extern  int             showMessages;
void R_ExecuteSetViewSize (void);

void D_Display (void)
{
    static  boolean		viewactivestate = false;
    static  boolean		menuactivestate = false;
    static  boolean		inhelpscreensstate = false;
    static  boolean		fullscreen = false;
    static  gamestate_t		oldgamestate = -1;
    static  int			borderdrawcount;
    int				nowtime;
    int				tics;
    int				wipestart;
    int				y;
    boolean			done;
    boolean			wipe;
    boolean			redrawsbar;

    if (nodrawers)
    	return;                                                         
		
    redrawsbar = false;
    
                                     
    if (setsizeneeded)
    {
		R_ExecuteSetViewSize ();
		oldgamestate = -1;                                                
		borderdrawcount = 3;
    }

                                               
    if (gamestate != wipegamestate)
		{
		wipe = true;
		wipe_StartScreen(0, 0, SCREENWIDTH, SCREENHEIGHT);
    }
    else
    	wipe = false;

    if (gamestate == GS_LEVEL && gametic)
    	HU_Erase();
    
                          
    switch (gamestate)
    {
      case GS_LEVEL:
		if (!gametic)
			break;
		if (automapactive)
			AM_Drawer ();
		if (wipe || (viewheight != 200 && fullscreen) )
			redrawsbar = true;
		if (inhelpscreensstate && !inhelpscreens)
			redrawsbar = true;                                              
		ST_Drawer (viewheight == 200, redrawsbar );
		fullscreen = viewheight == 200;
		break;

      case GS_INTERMISSION:
		WI_Drawer ();
		break;

      case GS_FINALE:
		F_Drawer ();
		break;

      case GS_DEMOSCREEN:
		D_PageDrawer ();
		break;
    }
    
                                    
    I_UpdateNoBlit ();
    
                             
    if (gamestate == GS_LEVEL && !automapactive && gametic)
    	R_RenderPlayerView (&players[displayplayer]);

    if (gamestate == GS_LEVEL && gametic)
    	HU_Drawer ();
    
                            
    if (gamestate != oldgamestate && gamestate != GS_LEVEL)
    	I_SetPalette (W_CacheLumpName (DEH_String("PLAYPAL"),PU_CACHE));

                                                    
    if (gamestate == GS_LEVEL && oldgamestate != GS_LEVEL)
    {
		viewactivestate = false;                              
		R_FillBackScreen ();                                            
    }

                                                          
    if (gamestate == GS_LEVEL && !automapactive && scaledviewwidth != 320)
    {
		if (menuactive || menuactivestate || !viewactivestate)
			borderdrawcount = 3;
		if (borderdrawcount)
		{
			R_DrawViewBorder ();                           
			borderdrawcount--;
		}
    }

    if (testcontrols)
    {
                                          

        V_DrawMouseSpeedBox(testcontrols_mousespeed);
    }

    menuactivestate = menuactive;
    viewactivestate = viewactive;
    inhelpscreensstate = inhelpscreens;
    oldgamestate = wipegamestate = gamestate;
    
                     
    if (paused)
    {
		if (automapactive)
			y = 4;
		else
			y = viewwindowy+4;
		V_DrawPatchDirect(viewwindowx + (scaledviewwidth - 68) / 2, y,
							  W_CacheLumpName (DEH_String("M_PAUSE"), PU_CACHE));
    }


                                      
    M_Drawer ();                                                    
    NetUpdate ();                                         


                    
    if (!wipe)
    {
	I_FinishUpdate ();                                         
	return;
    }
    
                  
    wipe_EndScreen(0, 0, SCREENWIDTH, SCREENHEIGHT);

    wipestart = I_GetTime () - 1;

    do
    {
	do
	{
	    nowtime = I_GetTime ();
	    tics = nowtime - wipestart;
            I_Sleep(1);
	} while (tics <= 0);
        
	wipestart = nowtime;
	done = wipe_ScreenWipe(wipe_Melt
			       , 0, 0, SCREENWIDTH, SCREENHEIGHT, tics);
	I_UpdateNoBlit ();
	M_Drawer ();                                                                 
	I_FinishUpdate ();                                                 
    } while (!done);
}

  
                                            
  

void D_BindVariables(void)
{
    int i;

    M_ApplyPlatformDefaults();

    I_BindVideoVariables();
    I_BindJoystickVariables();
    I_BindSoundVariables();

    M_BindBaseControls();
    M_BindWeaponControls();
    M_BindMapControls();
    M_BindMenuControls();
    M_BindChatControls(MAXPLAYERS);

    key_multi_msgplayer[0] = HUSTR_KEYGREEN;
    key_multi_msgplayer[1] = HUSTR_KEYINDIGO;
    key_multi_msgplayer[2] = HUSTR_KEYBROWN;
    key_multi_msgplayer[3] = HUSTR_KEYRED;

#ifdef FEATURE_MULTIPLAYER
    NET_BindVariables();
#endif

    M_BindVariable("mouse_sensitivity",      &mouseSensitivity);
    M_BindVariable("sfx_volume",             &sfxVolume);
    M_BindVariable("music_volume",           &musicVolume);
    M_BindVariable("show_messages",          &showMessages);
    M_BindVariable("screenblocks",           &screenblocks);
    M_BindVariable("detaillevel",            &detailLevel);
    M_BindVariable("snd_channels",           &snd_channels);
    M_BindVariable("vanilla_savegame_limit", &vanilla_savegame_limit);
    M_BindVariable("vanilla_demo_limit",     &vanilla_demo_limit);
    M_BindVariable("show_endoom",            &show_endoom);

                              

    for (i=0; i<10; ++i)
    {
        char buf[12];

        M_snprintf(buf, sizeof(buf), "chatmacro%i", i);
        M_BindVariable(buf, &chat_macros[i]);
    }
}

  
                      
  
                                                        
  

boolean D_GrabMouseCallback(void)
{
                                           

    if (drone)
        return false;

                                                                
 
    if (menuactive || paused)
        return false;

                                                          

    return (gamestate == GS_LEVEL) && !demoplayback && !advancedemo;
}

void doomgeneric_Tick()
{
                                     
    I_StartFrame ();

    TryRunTics ();                             

    S_UpdateSounds (players[consoleplayer].mo);                         

                                                      
    if (screenvisible)
    {
        D_Display ();
    }
}

  
              
  
void D_DoomLoop (void)
{
    if (bfgedition &&
        (demorecording || (gameaction == ga_playdemo) || netgame))
    {
        printf(" WARNING: You are playing using one of the Doom Classic\n"
               " IWAD files shipped with the Doom 3: BFG Edition. These are\n"
               " known to be incompatible with the regular IWAD files and\n"
               " may cause demos and network games to get out of sync.\n");
    }

    if (demorecording)
    	G_BeginRecording ();

    main_loop_started = true;

    TryRunTics();

    I_SetWindowTitle(gamedescription);
    I_GraphicsCheckCommandLine();
    I_SetGrabMouseCallback(D_GrabMouseCallback);
    I_InitGraphics();
    I_EnableLoadingDisk();

    V_RestoreBuffer();
    R_ExecuteSetViewSize();

    D_StartGameLoop();

    if (testcontrols)
    {
        wipegamestate = gamestate;
    }

    doomgeneric_Tick();
}



  
             
  
int             demosequence;
int             pagetic;
char                    *pagename;


  
               
                                       
  
void D_PageTicker (void)
{
    if (--pagetic < 0)
	D_AdvanceDemo ();
}



  
               
  
void D_PageDrawer (void)
{
    V_DrawPatch (0, 0, W_CacheLumpName(pagename, PU_CACHE));
}


  
                
                                                        
  
void D_AdvanceDemo (void)
{
    advancedemo = true;
}


  
                                          
                                          
  
void D_DoAdvanceDemo (void)
{
    players[consoleplayer].playerstate = PST_LIVE;               
    advancedemo = false;
    usergame = false;                                         
    paused = false;
    gameaction = ga_nothing;

                                                                    
                                                               
                                                                 
                                                                
                                                       

                                                                
                                   

    if (gameversion == exe_ultimate || gameversion == exe_final)
      demosequence = (demosequence+1)%7;
    else
      demosequence = (demosequence+1)%6;
    
    switch (demosequence)
    {
      case 0:
	if ( gamemode == commercial )
	    pagetic = TICRATE * 11;
	else
	    pagetic = 170;
	gamestate = GS_DEMOSCREEN;
	pagename = DEH_String("TITLEPIC");
	if ( gamemode == commercial )
	  S_StartMusic(mus_dm2ttl);
	else
	  S_StartMusic (mus_intro);
	break;
      case 1:
	G_DeferedPlayDemo(DEH_String("demo1"));
	break;
      case 2:
	pagetic = 200;
	gamestate = GS_DEMOSCREEN;
	pagename = DEH_String("CREDIT");
	break;
      case 3:
	G_DeferedPlayDemo(DEH_String("demo2"));
	break;
      case 4:
	gamestate = GS_DEMOSCREEN;
	if ( gamemode == commercial)
	{
	    pagetic = TICRATE * 11;
	    pagename = DEH_String("TITLEPIC");
	    S_StartMusic(mus_dm2ttl);
	}
	else
	{
	    pagetic = 200;

	    if ( gamemode == retail )
	      pagename = DEH_String("CREDIT");
	    else
	      pagename = DEH_String("HELP2");
	}
	break;
      case 5:
	G_DeferedPlayDemo(DEH_String("demo3"));
	break;
                                                   
      case 6:
	G_DeferedPlayDemo(DEH_String("demo4"));
	break;
    }

                                                                   
                                                            
    if (bfgedition && !strcasecmp(pagename, "TITLEPIC")
        && W_CheckNumForName("titlepic") < 0)
    {
        pagename = DEH_String("INTERPIC");
    }
}



  
               
  
void D_StartTitle (void)
{
    gameaction = ga_nothing;
    demosequence = -1;
    D_AdvanceDemo ();
}

                                                          
  
                                                               
                                   

static char *banners[] =
{
                
    "                         "
    "DOOM 2: Hell on Earth v%i.%i"
    "                           ",
                
    "                            "
    "DOOM Shareware Startup v%i.%i"
    "                           ",
               
    "                            "
    "DOOM Registered Startup v%i.%i"
    "                           ",
                                
    "                          "
    "DOOM System Startup v%i.%i"
    "                          ",
                               
    "                         "
    "The Ultimate DOOM Startup v%i.%i"
    "                        ",
              
    "                     "
    "DOOM 2: TNT - Evilution v%i.%i"
    "                           ",
                   
    "                   "
    "DOOM 2: Plutonia Experiment v%i.%i"
    "                           ",
};

  
                                                                    
                                
   

static char *GetGameName(char *gamename)
{
    size_t i;
    char *deh_sub;
    
    for (i=0; i<arrlen(banners); ++i)
    {
                                        

        deh_sub = DEH_String(banners[i]);
        
        if (deh_sub != banners[i])
        {
            size_t gamename_size;
            int version;

                                 
                                                                              
                                                                   

            gamename_size = strlen(deh_sub) + 10;
            gamename = Z_Malloc(gamename_size, PU_STATIC, 0);
            version = G_VanillaVersionCode();
            M_snprintf(gamename, gamename_size, deh_sub,
                       version / 100, version % 100);

            while (gamename[0] != '\0' && isspace((int)gamename[0]))
            {
                memmove(gamename, gamename + 1, gamename_size - 1);
            }

            while (gamename[0] != '\0' && isspace((int)gamename[strlen(gamename)-1]))
            {
                gamename[strlen(gamename) - 1] = '\0';
            }

            return gamename;
        }
    }

    return gamename;
}

static void SetMissionForPackName(char *pack_name)
{
    int i;
    static const struct
    {
        char *name;
        int mission;
    } packs[] = {
        { "doom2",    doom2 },
        { "tnt",      pack_tnt },
        { "plutonia", pack_plut },
    };

    for (i = 0; i < arrlen(packs); ++i)
    {
        if (!strcasecmp(pack_name, packs[i].name))
        {
            gamemission = packs[i].mission;
            return;
        }
    }

    printf("Valid mission packs are:\n");

    for (i = 0; i < arrlen(packs); ++i)
    {
        printf("\t%s\n", packs[i].name);
    }

    I_Error("Unknown mission pack name: %s", pack_name);
}

  
                                            
  

void D_IdentifyVersion(void)
{
                                                                 
                                                     
                                                               
                                                           
                                

    if (gamemission == none)
    {
        unsigned int i;

        for (i=0; i<numlumps; ++i)
        {
            if (!strncasecmp(lumpinfo[i].name, "MAP01", 8))
            {
                gamemission = doom2;
                break;
            } 
            else if (!strncasecmp(lumpinfo[i].name, "E1M1", 8))
            {
                gamemission = doom;
                break;
            }
        }

        if (gamemission == none)
        {
                                                                   

            I_Error("Unknown or invalid IWAD file.");
        }
    }

                                             

    if (logical_gamemission == doom)
    {
                                      

        if (W_CheckNumForName("E4M1") > 0)
        {
                            

            gamemode = retail;
        } 
        else if (W_CheckNumForName("E3M1") > 0)
        {
            gamemode = registered;
        }
        else
        {
            gamemode = shareware;
        }
    }
    else
    {
        int p;

                               
        gamemode = commercial;

                                                                        
                                                                     
                                                       

           
                      
          
                                                                            
                                                                         
                                
          
        p = M_CheckParmWithArgs("-pack", 1);
        if (p > 0)
        {
            SetMissionForPackName(myargv[p + 1]);
        }
    }
}

                                 

void D_SetGameDescription(void)
{
    boolean is_freedoom = W_CheckNumForName("FREEDOOM") >= 0,
            is_freedm = W_CheckNumForName("FREEDM") >= 0;

    gamedescription = "Unknown";

    if (logical_gamemission == doom)
    {
                                      

        if (is_freedoom)
        {
            gamedescription = GetGameName("Freedoom: Phase 1");
        }
        else if (gamemode == retail)
        {
                            

            gamedescription = GetGameName("The Ultimate DOOM");
        }
        else if (gamemode == registered)
        {
            gamedescription = GetGameName("DOOM Registered");
        }
        else if (gamemode == shareware)
        {
            gamedescription = GetGameName("DOOM Shareware");
        }
    }
    else
    {
                                                   

        if (is_freedoom)
        {
            if (is_freedm)
            {
                gamedescription = GetGameName("FreeDM");
            }
            else
            {
                gamedescription = GetGameName("Freedoom: Phase 2");
            }
        }
        else if (logical_gamemission == doom2)
        {
            gamedescription = GetGameName("DOOM 2: Hell on Earth");
        }
        else if (logical_gamemission == pack_plut)
        {
            gamedescription = GetGameName("DOOM 2: Plutonia Experiment"); 
        }
        else if (logical_gamemission == pack_tnt)
        {
            gamedescription = GetGameName("DOOM 2: TNT - Evilution");
        }
    }
}

                                          
char            title[128];

static boolean D_AddFile(char *filename)
{
    wad_file_t *handle;

    printf(" adding %s\n", filename);
    handle = W_AddFile(filename);

    return handle != NULL;
}

                            
                                                                           
                        

static char *copyright_banners[] =
{
    "===========================================================================\n"
    "ATTENTION:  This version of DOOM has been modified.  If you would like to\n"
    "get a copy of the original game, call 1-800-IDGAMES or see the readme file.\n"
    "        You will not receive technical support for modified games.\n"
    "                      press enter to continue\n"
    "===========================================================================\n",

    "===========================================================================\n"
    "                 Commercial product - do not distribute!\n"
    "         Please report software piracy to the SPA: 1-800-388-PIR8\n"
    "===========================================================================\n",

    "===========================================================================\n"
    "                                Shareware!\n"
    "===========================================================================\n"
};

                                                             

void PrintDehackedBanners(void)
{
    size_t i;

    for (i=0; i<arrlen(copyright_banners); ++i)
    {
        char *deh_s;

        deh_s = DEH_String(copyright_banners[i]);

        if (deh_s != copyright_banners[i])
        {
            printf("%s", deh_s);

                                                                                
                                                                

            if (deh_s[strlen(deh_s) - 1] != '\n')
            {
                printf("\n");
            }
        }
    }
}

static struct 
{
    char *description;
    char *cmdline;
    GameVersion_t version;
} gameversions[] = {
    {"Doom 1.666",           "1.666",      exe_doom_1_666},
    {"Doom 1.7/1.7a",        "1.7",        exe_doom_1_7},
    {"Doom 1.8",             "1.8",        exe_doom_1_8},
    {"Doom 1.9",             "1.9",        exe_doom_1_9},
    {"Hacx",                 "hacx",       exe_hacx},
    {"Ultimate Doom",        "ultimate",   exe_ultimate},
    {"Final Doom",           "final",      exe_final},
    {"Final Doom (alt)",     "final2",     exe_final2},
    {"Chex Quest",           "chex",       exe_chex},
    { NULL,                  NULL,         0},
};

                              

static void InitGameVersion(void)
{
    int p;
    int i;

        
                     
                       
      
                                                                   
                                                        
      

    p = M_CheckParmWithArgs("-gameversion", 1);

    if (p)
    {
        for (i=0; gameversions[i].description != NULL; ++i)
        {
            if (!strcmp(myargv[p+1], gameversions[i].cmdline))
            {
                gameversion = gameversions[i].version;
                break;
            }
        }
        
        if (gameversions[i].description == NULL) 
        {
            printf("Supported game versions:\n");

            for (i=0; gameversions[i].description != NULL; ++i)
            {
                printf("\t%s (%s)\n", gameversions[i].cmdline,
                        gameversions[i].description);
            }
            
            I_Error("Unknown game version '%s'", myargv[p+1]);
        }
    }
    else
    {
                                  

        if (gamemission == pack_chex)
        {
                                                     

            gameversion = exe_chex;
        }
        else if (gamemission == pack_hacx)
        {
                                                    

            gameversion = exe_hacx;
        }
        else if (gamemode == shareware || gamemode == registered)
        {
                       

            gameversion = exe_doom_1_9;

                                                         
        }
        else if (gamemode == retail)
        {
            gameversion = exe_ultimate;
        }
        else if (gamemode == commercial)
        {
            if (gamemission == doom2)
            {
                gameversion = exe_doom_1_9;
            }
            else
            {
                                              
                                                                         
                                                                        
                                                                     
                                        

                gameversion = exe_final;
            }
        }
    }
    
                                                                           

    if (gameversion < exe_ultimate && gamemode == retail)
    {
        gamemode = registered;
    }

                                                                   

    if (gameversion < exe_final && gamemode == commercial
     && (gamemission == pack_tnt || gamemission == pack_plut))
    {
        gamemission = doom2;
    }
}

void PrintGameVersion(void)
{
    int i;

    for (i=0; gameversions[i].description != NULL; ++i)
    {
        if (gameversions[i].version == gameversion)
        {
            printf("Emulating the behavior of the "
                   "'%s' executable.\n", gameversions[i].description);
            break;
        }
    }
}

                                                       

static void D_Endoom(void)
{
    byte *endoom;

                                                                 
                                                                 
                                 

    if (!show_endoom || !main_loop_started
     || screensaver_mode || M_CheckParm("-testcontrols") > 0)
    {
        return;
    }

    endoom = W_CacheLumpName(DEH_String("ENDOOM"), PU_STATIC);

    I_Endoom(endoom);

	exit(0);
}

#if ORIGCODE
                                                  
static void LoadIwadDeh(void)
{
                                                                  
    if (W_CheckNumForName("FREEDOOM") >= 0)
    {
                                                                             
                                                                           
                          
        DEH_LoadLumpByName("DEHACKED", false, true);
    }

                                                                   
    if (gameversion == exe_hacx)
    {
        if (!DEH_LoadLumpByName("DEHACKED", true, false))
        {
            I_Error("DEHACKED lump not found.  Please check that this is the "
                    "Hacx v1.2 IWAD.");
        }
    }

                                                                          
                                      
    if (gameversion == exe_chex)
    {
        char *chex_deh = NULL;
        char *sep;

                                                                    
        sep = strrchr(iwadfile, DIR_SEPARATOR);

        if (sep != NULL)
        {
            size_t chex_deh_len = strlen(iwadfile) + 9;
            chex_deh = malloc(chex_deh_len);
            M_StringCopy(chex_deh, iwadfile, chex_deh_len);
            chex_deh[sep - iwadfile + 1] = '\0';
            M_StringConcat(chex_deh, "chex.deh", chex_deh_len);
        }
        else
        {
            chex_deh = strdup("chex.deh");
        }

                                                                   
                                                    
        if (!M_FileExists(chex_deh))
        {
            free(chex_deh);
            chex_deh = D_FindWADByName("chex.deh");
        }

                           
        if (chex_deh == NULL)
        {
            I_Error("Unable to find Chex Quest dehacked file (chex.deh).\n"
                    "The dehacked file is required in order to emulate\n"
                    "chex.exe correctly.  It can be found in your nearest\n"
                    "/idgames repository mirror at:\n\n"
                    "   utils/exe_edit/patches/chexdeh.zip");
        }

        if (!DEH_LoadFile(chex_deh))
        {
            I_Error("Failed to load chex.deh needed for emulating chex.exe.");
        }
    }
}
#endif

  
             
  
void D_DoomMain (void)
{
    int p;
    char file[256];
    char demolumpname[9];
#if ORIGCODE
    int numiwadlumps;
#endif

    I_AtExit(D_Endoom, false);

                   

    I_PrintBanner(PACKAGE_STRING);

    DEH_printf("Z_Init: Init zone memory allocation daemon. \n");
    Z_Init ();

#ifdef FEATURE_MULTIPLAYER
       
                    
      
                                                                      
                          
      

    if (M_CheckParm("-dedicated") > 0)
    {
        printf("Dedicated server mode.\n");
        NET_DedicatedServer();

                        
    }

       
                    
      
                                                                   
               
      

    if (M_CheckParm("-search"))
    {
        NET_MasterQuery();
        exit(0);
    }

       
                     
                    
      
                                                             
               
      

    p = M_CheckParmWithArgs("-query", 1);

    if (p)
    {
        NET_QueryAddress(myargv[p+1]);
        exit(0);
    }

       
                    
      
                                                
      

    if (M_CheckParm("-localsearch"))
    {
        NET_LANQuery();
        exit(0);
    }

#endif

       
               
      
                        
      
	
    nomonsters = M_CheckParm ("-nomonsters");

       
               
      
                                           
      

    respawnparm = M_CheckParm ("-respawn");

       
               
      
                            
      

    fastparm = M_CheckParm ("-fast");

        
               
      
                                                                    
                 
      

    devparm = M_CheckParm ("-devparm");

    I_DisplayFPSDots(devparm);

       
                    
               
      
                               
      

    if (M_CheckParm ("-deathmatch"))
	deathmatch = 1;

       
                    
               
      
                                                                     
                                          
      

    if (M_CheckParm ("-altdeath"))
	deathmatch = 2;

    if (devparm)
	DEH_printf(D_DEVSTR);
    
                                             

#ifdef _WIN32

       
                        
               
      
                                                            
                             
      

    if (M_ParmExists("-cdrom"))
    {
        printf(D_CDROM);

        M_SetConfigDir("c:\\doomdata\\");
    }
    else
#endif
    {
                                             

        M_SetConfigDir(NULL);
    }

       
               
               
      
                                                                            
                                                                       
      

    if ( (p=M_CheckParm ("-turbo")) )
    {
	int     scale = 200;
	extern int forwardmove[2];
	extern int sidemove[2];
	
	if (p<myargc-1)
	    scale = atoi (myargv[p+1]);
	if (scale < 10)
	    scale = 10;
	if (scale > 400)
	    scale = 400;
        DEH_printf("turbo scale: %i%%\n", scale);
	forwardmove[0] = forwardmove[0]*scale/100;
	forwardmove[1] = forwardmove[1]*scale/100;
	sidemove[0] = sidemove[0]*scale/100;
	sidemove[1] = sidemove[1]*scale/100;
    }
    
                      
    DEH_printf("V_Init: allocate screens.\n");
    V_Init ();

                                                                     
    DEH_printf("M_LoadDefaults: Load system defaults.\n");
    M_SetConfigFilenames("default.cfg", PROGRAM_PREFIX "doom.cfg");
    D_BindVariables();
    M_LoadDefaults();

                                  
    I_AtExit(M_SaveDefaults, false);

                                       
    iwadfile = D_FindIWAD(IWAD_MASK_DOOM, &gamemission);

                  

    if (iwadfile == NULL)
    {
        I_Error("Game mode indeterminate.  No IWAD file was found.  Try\n"
                "specifying one with the '-iwad' command line parameter.\n");
    }

    modifiedgame = false;

    DEH_printf("W_Init: Init WADfiles.\n");
    D_AddFile(iwadfile);
#if ORIGCODE
    numiwadlumps = numlumps;
#endif

    W_CheckCorrectIWAD(doom);

                                                                         
                                                                          
    D_IdentifyVersion();
    InitGameVersion();

#if ORIGCODE
       
                    
      
                                                                
                  
      
    if (!M_ParmExists("-nodeh"))
    {
                                                                      
                                      
        LoadIwadDeh();
    }
#endif

                                                                    
                                                                    
                                                                       
                                                                       
                                                 
                                                                      
                                                               

    if (W_CheckNumForName("dmenupic") >= 0)
    {
        printf("BFG Edition: Using workarounds as needed.\n");
        bfgedition = true;

                                                                
                                                                  
                                                                 
                                                                  
                                                   

        DEH_AddStringReplacement(HUSTR_31, "level 31: idkfa");
        DEH_AddStringReplacement(HUSTR_32, "level 32: keen");
        DEH_AddStringReplacement(PHUSTR_1, "level 33: betray");

                                                                          
                                                                          
                                                                          
                                                                          
                        
          
                                                                          
                                                                          
                                                                      

        DEH_AddStringReplacement("M_GDHIGH", "M_MSGON");
        DEH_AddStringReplacement("M_GDLOW", "M_MSGOFF");
    }

#ifdef FEATURE_DEHACKED
                                                                     
                                                                      
                                                        
                                 
                                                             
                                                   
    DEH_ParseCommandLine();
#endif

                       
    modifiedgame = W_ParseCommandLine();

             
                         

       
                  
                     
               
      
                                         
      

    p = M_CheckParmWithArgs ("-playdemo", 1);

    if (!p)
    {
           
                      
                         
                   
          
                                                                       
                         
          
	p = M_CheckParmWithArgs("-timedemo", 1);

    }

    if (p)
    {
                                                                       
                                  
        if (M_StringEndsWith(myargv[p + 1], ".lmp"))
        {
            M_StringCopy(file, myargv[p + 1], sizeof(file));
        }
        else
        {
            DEH_snprintf(file, sizeof(file), "%s.lmp", myargv[p+1]);
        }

        if (D_AddFile(file))
        {
            M_StringCopy(demolumpname, lumpinfo[numlumps - 1].name,
                         sizeof(demolumpname));
        }
        else
        {
                                                                    
                                                                    
                                                      

            M_StringCopy(demolumpname, myargv[p + 1], sizeof(demolumpname));
        }

        printf("Playing demo %s.\n", file);
    }

    I_AtExit((atexit_func_t) G_CheckDemoStatus, true);

                                                           
    W_GenerateHashTable();

                                                                         
                              

#if ORIGCODE
       
                    
      
                                                                        
                         
      
    if (M_ParmExists("-dehlump"))
    {
        int i, loaded = 0;

        for (i = numiwadlumps; i < numlumps; ++i)
        {
            if (!strncmp(lumpinfo[i].name, "DEHACKED", 8))
            {
                DEH_LoadLump(i, false, false);
                loaded++;
            }
        }

        printf("  loaded %i DEHACKED lumps from PWAD files.\n", loaded);
    }
#endif

                                                                     
                                               
    D_SetGameDescription();

#ifdef _WIN32
                                                                            
    if (M_ParmExists("-cdrom"))
    {
        savegamedir = configdir;
    }
    else
#endif
    {
        savegamedir = M_GetSaveGameDir(D_SaveGameIWADName(gamemission));
    }

                                   
    if (modifiedgame)
    {
	                                                    
	                                                        
	char name[23][8]=
	{
	    "e2m1","e2m2","e2m3","e2m4","e2m5","e2m6","e2m7","e2m8","e2m9",
	    "e3m1","e3m3","e3m3","e3m4","e3m5","e3m6","e3m7","e3m8","e3m9",
	    "dphoof","bfgga0","heada1","cybra1","spida1d1"
	};
	int i;
	
	if ( gamemode == shareware)
	    I_Error(DEH_String("\nYou cannot -file with the shareware "
			       "version. Register!"));

	                                       
	                                                    
	if (gamemode == registered)
	    for (i = 0;i < 23; i++)
		if (W_CheckNumForName(name[i])<0)
		    I_Error(DEH_String("\nThis is not the registered version."));
    }

    if (W_CheckNumForName("SS_START") >= 0
     || W_CheckNumForName("FF_END") >= 0)
    {
        I_PrintDivider();
        printf(" WARNING: The loaded WAD file contains modified sprites or\n"
               " floor textures.  You may want to use the '-merge' command\n"
               " line option instead of '-file'.\n");
    }

    I_PrintStartupBanner(gamedescription);
    PrintDehackedBanners();

                                                                     
                                                                    
                                              
    if (W_CheckNumForName("FREEDOOM") >= 0 && W_CheckNumForName("FREEDM") < 0)
    {
        printf(" WARNING: You are playing using one of the Freedoom IWAD\n"
               " files, which might not work in this port. See this page\n"
               " for more information on how to play using Freedoom:\n"
               "   http://www.chocolate-doom.org/wiki/index.php/Freedoom\n");
        I_PrintDivider();
    }

    DEH_printf("I_Init: Setting up machine state.\n");
    I_CheckIsScreensaver();
    I_InitTimer();
    I_InitJoystick();
    I_InitSound(true);
    I_InitMusic();

#ifdef FEATURE_MULTIPLAYER
    printf ("NET_Init: Init network subsystem.\n");
    NET_Init ();
#endif

                                                      
    D_ConnectNetGame();

                                           
    startskill = sk_medium;
    startepisode = 1;
    startmap = 1;
    autostart = false;

       
                   
               
      
                                                                    
                               
      

    p = M_CheckParmWithArgs("-skill", 1);

    if (p)
    {
	startskill = myargv[p+1][0]-'1';
	autostart = true;
    }

       
               
               
      
                                       
      

    p = M_CheckParmWithArgs("-episode", 1);

    if (p)
    {
	startepisode = myargv[p+1][0]-'0';
	startmap = 1;
	autostart = true;
    }
	
    timelimit = 0;

        
               
                    
               
      
                                                              
      

    p = M_CheckParmWithArgs("-timer", 1);

    if (p)
    {
	timelimit = atoi(myargv[p+1]);
    }

       
                    
               
      
                                                          
      

    p = M_CheckParm ("-avg");

    if (p)
    {
	timelimit = 20;
    }

       
                            
               
      
                                                                  
               
      

    p = M_CheckParmWithArgs("-warp", 1);

    if (p)
    {
        if (gamemode == commercial)
            startmap = atoi (myargv[p+1]);
        else
        {
            startepisode = myargv[p+1][0]-'0';

            if (p + 2 < myargc)
            {
                startmap = myargv[p+2][0]-'0';
            }
            else
            {
                startmap = 1;
            }
        }
        autostart = true;
    }

                    
                                             

    p = M_CheckParm("-testcontrols");

    if (p > 0)
    {
        startepisode = 1;
        startmap = 1;
        autostart = true;
        testcontrols = true;
    }

                                    
                                                                         
                                                              

       
               
               
      
                               
      

    p = M_CheckParmWithArgs("-loadgame", 1);
    
    if (p)
    {
        startloadgame = atoi(myargv[p+1]);
    }
    else
    {
                             
        startloadgame = -1;
    }

    DEH_printf("M_Init: Init miscellaneous info.\n");
    M_Init ();

    DEH_printf("R_Init: Init DOOM refresh daemon - ");
    R_Init ();

    DEH_printf("\nP_Init: Init Playloop state.\n");
    P_Init ();

    DEH_printf("S_Init: Setting up sound.\n");
    S_Init (sfxVolume * 8, musicVolume * 8);

    DEH_printf("D_CheckNetGame: Checking network game status.\n");
    D_CheckNetGame ();

    PrintGameVersion();

    DEH_printf("HU_Init: Setting up heads up display.\n");
    HU_Init ();

    DEH_printf("ST_Init: Init status bar.\n");
    ST_Init ();

                                                             
                                                               
                        

    if (gamemode == commercial && W_CheckNumForName("map01") < 0)
        storedemo = true;

    if (M_CheckParmWithArgs("-statdump", 1))
    {
        I_AtExit(StatDump, true);
        DEH_printf("External statistics registered.\n");
    }

       
               
                     
               
      
                                 
      

    p = M_CheckParmWithArgs("-record", 1);

    if (p)
    {
		G_RecordDemo (myargv[p+1]);
		autostart = true;
    }

    p = M_CheckParmWithArgs("-playdemo", 1);
    if (p)
    {
		singledemo = true;                                    
		G_DeferedPlayDemo (demolumpname);
		D_DoomLoop ();
        return;
    }

    p = M_CheckParmWithArgs("-timedemo", 1);
    if (p)
    {
		G_TimeDemo (demolumpname);
		D_DoomLoop ();
        return;
    }

    if (startloadgame >= 0)
    {
        M_StringCopy(file, P_SaveGameFile(startloadgame), sizeof(file));
        G_LoadGame(file);
    }

    if (gameaction != ga_loadgame )
    {
		if (autostart || netgame)
			G_InitNew (startskill, startepisode, startmap);
		else
			D_StartTitle ();                                      
    }

    D_DoomLoop ();
}

