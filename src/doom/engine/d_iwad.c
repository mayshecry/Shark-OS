  
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                                                                   
                        
  

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "config.h"
#include "deh_str.h"
#include "doomkeys.h"
#include "d_iwad.h"
#include "i_system.h"
#include "m_argv.h"
#include "m_config.h"
#include "m_misc.h"
#include "w_wad.h"
#include "z_zone.h"

static const iwad_t iwads[] =
{
    { "doom2.wad",    doom2,     commercial, "Doom II" },
    { "plutonia.wad", pack_plut, commercial, "Final Doom: Plutonia Experiment" },
    { "tnt.wad",      pack_tnt,  commercial, "Final Doom: TNT: Evilution" },
    { "doom.wad",     doom,      retail,     "Doom" },
    { "doom1.wad",    doom,      shareware,  "Doom Shareware" },
    { "chex.wad",     pack_chex, shareware,  "Chex Quest" },
    { "hacx.wad",     pack_hacx, commercial, "Hacx" },
    { "freedm.wad",   doom2,     commercial, "FreeDM" },
    { "freedoom2.wad", doom2,    commercial, "Freedoom: Phase 2" },
    { "freedoom1.wad", doom,     retail,     "Freedoom: Phase 1" },
    { "heretic.wad",  heretic,   retail,     "Heretic" },
    { "heretic1.wad", heretic,   shareware,  "Heretic Shareware" },
    { "hexen.wad",    hexen,     commercial, "Hexen" },
                                                                                   
    { "strife1.wad",  strife,    commercial, "Strife" },
};

                                              
  
                                                              

#define MAX_IWAD_DIRS 128

static boolean iwad_dirs_built = false;
static char *iwad_dirs[MAX_IWAD_DIRS];
static int num_iwad_dirs = 0;

static void AddIWADDir(char *dir)
{
    if (num_iwad_dirs < MAX_IWAD_DIRS)
    {
        iwad_dirs[num_iwad_dirs] = dir;
        ++num_iwad_dirs;
    }
}

                                                                      
                                                                      
                                                                   
                                                                 

#if defined(_WIN32) && !defined(_WIN32_WCE)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct 
{
    HKEY root;
    char *path;
    char *value;
} registry_value_t;

#define UNINSTALLER_STRING "\\uninstl.exe /S "

                                                                      
                                                         
  
                                                             
  
                                                          

                                                                         
                                        
#if _WIN64
#define SOFTWARE_KEY "Software\\Wow6432Node"
#else
#define SOFTWARE_KEY "Software"
#endif

static registry_value_t uninstall_values[] =
{
                                                         

    {
        HKEY_LOCAL_MACHINE,
        SOFTWARE_KEY "\\Microsoft\\Windows\\CurrentVersion\\"
            "Uninstall\\Ultimate Doom for Windows 95",
        "UninstallString",
    },

                                                   

    {
        HKEY_LOCAL_MACHINE,
        SOFTWARE_KEY "\\Microsoft\\Windows\\CurrentVersion\\"
            "Uninstall\\Doom II for Windows 95",
        "UninstallString",
    },

                 

    {
        HKEY_LOCAL_MACHINE,
        SOFTWARE_KEY "\\Microsoft\\Windows\\CurrentVersion\\"
            "Uninstall\\Final Doom for Windows 95",
        "UninstallString",
    },

                        

    {
        HKEY_LOCAL_MACHINE,
        SOFTWARE_KEY "\\Microsoft\\Windows\\CurrentVersion\\"
            "Uninstall\\Doom Shareware for Windows 95",
        "UninstallString",
    },
};

                                                                  

static registry_value_t collectors_edition_value =
{
    HKEY_LOCAL_MACHINE,
    SOFTWARE_KEY "\\Activision\\DOOM Collector's Edition\\v1.0",
    "INSTALLPATH",
};

                                                                       

static char *collectors_edition_subdirs[] = 
{
    "Doom2",
    "Final Doom",
    "Ultimate Doom",
};

                                    

static registry_value_t steam_install_location =
{
    HKEY_LOCAL_MACHINE,
    SOFTWARE_KEY "\\Valve\\Steam",
    "InstallPath",
};

                                                               

static char *steam_install_subdirs[] =
{
    "steamapps\\common\\doom 2\\base",
    "steamapps\\common\\final doom\\base",
    "steamapps\\common\\ultimate doom\\base",
    "steamapps\\common\\heretic shadow of the serpent riders\\base",
    "steamapps\\common\\hexen\\base",
    "steamapps\\common\\hexen deathkings of the dark citadel\\base",

                                

    "steamapps\\common\\DOOM 3 BFG Edition\\base\\wads",
};

#define STEAM_BFG_GUS_PATCHES \
    "steamapps\\common\\DOOM 3 BFG Edition\\base\\classicmusic\\instruments"

static char *GetRegistryString(registry_value_t *reg_val)
{
    HKEY key;
    DWORD len;
    DWORD valtype;
    char *result;

                                                         

    if (RegOpenKeyEx(reg_val->root, reg_val->path,
                     0, KEY_READ, &key) != ERROR_SUCCESS)
    {
        return NULL;
    }

    result = NULL;

                                                                       

    if (RegQueryValueEx(key, reg_val->value,
                        NULL, &valtype, NULL, &len) == ERROR_SUCCESS
     && valtype == REG_SZ)
    {
                                                             

        result = malloc(len);

        if (RegQueryValueEx(key, reg_val->value, NULL, &valtype,
                            (unsigned char *) result, &len) != ERROR_SUCCESS)
        {
            free(result);
            result = NULL;
        }
    }

                    

    RegCloseKey(key);

    return result;
}

                                                       

static void CheckUninstallStrings(void)
{
    unsigned int i;

    for (i=0; i<arrlen(uninstall_values); ++i)
    {
        char *val;
        char *path;
        char *unstr;

        val = GetRegistryString(&uninstall_values[i]);

        if (val == NULL)
        {
            continue;
        }

        unstr = strstr(val, UNINSTALLER_STRING);

        if (unstr == NULL)
        {
            free(val);
        }
        else
        {
            path = unstr + strlen(UNINSTALLER_STRING);

            AddIWADDir(path);
        }
    }
}

                                      

static void CheckCollectorsEdition(void)
{
    char *install_path;
    char *subpath;
    unsigned int i;

    install_path = GetRegistryString(&collectors_edition_value);

    if (install_path == NULL)
    {
        return;
    }

    for (i=0; i<arrlen(collectors_edition_subdirs); ++i)
    {
        subpath = M_StringJoin(install_path, DIR_SEPARATOR_S,
                               collectors_edition_subdirs[i], NULL);

        AddIWADDir(subpath);
    }

    free(install_path);
}


                                      

static void CheckSteamEdition(void)
{
    char *install_path;
    char *subpath;
    size_t i;

    install_path = GetRegistryString(&steam_install_location);

    if (install_path == NULL)
    {
        return;
    }

    for (i=0; i<arrlen(steam_install_subdirs); ++i)
    {
        subpath = M_StringJoin(install_path, DIR_SEPARATOR_S,
                               steam_install_subdirs[i], NULL);

        AddIWADDir(subpath);
    }

    free(install_path);
}

                                                                         
                                    

static void CheckSteamGUSPatches(void)
{
    const char *current_path;
    char *install_path;
    char *patch_path;
    int len;

                                                             
    current_path = M_GetStrVariable("gus_patch_path");
    if (current_path != NULL && strlen(current_path) > 0)
    {
        return;
    }

    install_path = GetRegistryString(&steam_install_location);

    if (install_path == NULL)
    {
        return;
    }

    len = strlen(install_path) + strlen(STEAM_BFG_GUS_PATCHES) + 20;
    patch_path = malloc(len);
    M_snprintf(patch_path, len, "%s\\%s\\ACBASS.PAT",
               install_path, STEAM_BFG_GUS_PATCHES);

                                                             
    if (M_FileExists(patch_path))
    {
        M_snprintf(patch_path, len, "%s\\%s",
                   install_path, STEAM_BFG_GUS_PATCHES);
        M_SetVariable("gus_patch_path", patch_path);
    }

    free(patch_path);
    free(install_path);
}

                                           

static void CheckDOSDefaults(void)
{
                                                                  
                         

    AddIWADDir("\\doom2");                        
    AddIWADDir("\\plutonia");                        
    AddIWADDir("\\tnt");
    AddIWADDir("\\doom_se");                            
    AddIWADDir("\\doom");                                             
    AddIWADDir("\\dooms");                                   
    AddIWADDir("\\doomsw");

    AddIWADDir("\\heretic");                      
    AddIWADDir("\\hrtic_se");                                               

    AddIWADDir("\\hexen");                      
    AddIWADDir("\\hexendk");                                                   

    AddIWADDir("\\strife");                      
}

#endif

                                                         
                         

static boolean DirIsFile(char *path, char *filename)
{
    size_t path_len;
    size_t filename_len;

    path_len = strlen(path);
    filename_len = strlen(filename);

    return path_len >= filename_len + 1
        && path[path_len - filename_len - 1] == DIR_SEPARATOR
        && !strcasecmp(&path[path_len - filename_len], filename);
}

                                                               
                                                              
                

static char *CheckDirectoryHasIWAD(char *dir, char *iwadname)
{
    char *filename; 

                                                                  
                                                                  

    if (DirIsFile(dir, iwadname) && M_FileExists(dir))
    {
        return strdup(dir);
    }

                                                              
                                              

    if (!strcmp(dir, "."))
    {
        filename = strdup(iwadname);
    }
    else
    {
        filename = M_StringJoin(dir, DIR_SEPARATOR_S, iwadname, NULL);
    }

    printf("Trying IWAD file:%s\n", filename);

    if (M_FileExists(filename))
    {
        return filename;
    }

    free(filename);

    return NULL;
}

                                            
                                                             

static char *SearchDirectoryForIWAD(char *dir, int mask, GameMission_t *mission)
{
    char *filename;
    size_t i;

    for (i=0; i<arrlen(iwads); ++i) 
    {
        if (((1 << iwads[i].mission) & mask) == 0)
        {
            continue;
        }

        filename = CheckDirectoryHasIWAD(dir, DEH_String(iwads[i].name));

        if (filename != NULL)
        {
            *mission = iwads[i].mission;

            return filename;
        }
    }

    return NULL;
}

                                                 
                                      

static GameMission_t IdentifyIWADByName(char *name, int mask)
{
    size_t i;
    GameMission_t mission;
    char *p;

    p = strrchr(name, DIR_SEPARATOR);

    if (p != NULL)
    {
        name = p + 1;
    }

    mission = none;

    for (i=0; i<arrlen(iwads); ++i)
    {
                                                   

                                       

        if (((1 << iwads[i].mission) & mask) == 0)
            continue;

                                              

        if (!strcasecmp(name, iwads[i].name))
        {
            mission = iwads[i].mission;
            break;
        }
    }

    return mission;
}

#if ORIGCODE
  
                                                                         
  

static void AddDoomWadPath(void)
{
    char *doomwadpath;
    char *p;

                                                  

    doomwadpath = getenv("DOOMWADPATH");

    if (doomwadpath == NULL)
    {
        return;
    }

    doomwadpath = strdup(doomwadpath);

                                

    AddIWADDir(doomwadpath);

                                                  

    p = doomwadpath;

    for (;;)
    {
        p = strchr(p, PATH_SEPARATOR);

        if (p != NULL)
        {
                                                                   
                                  
  
            *p = '\0';
            p += 1;

            AddIWADDir(p);
        }
        else
        {
            break;
        }
    }
}

#endif

  
                             
  

static void BuildIWADDirList(void)
{
#if ORIGCODE
    char *doomwaddir;

    if (iwad_dirs_built)
    {
        return;
    }

                                                             

    AddIWADDir(".");

                                                 

    doomwaddir = getenv("DOOMWADDIR");

    if (doomwaddir != NULL)
    {
        AddIWADDir(doomwaddir);
    }        

                                

    AddDoomWadPath();

#ifdef _WIN32

                                                                    

    CheckUninstallStrings();
    CheckCollectorsEdition();
    CheckSteamEdition();
    CheckDOSDefaults();

                                                            

    CheckSteamGUSPatches();

#else

                                                                 

    AddIWADDir("/usr/share/games/doom");
    AddIWADDir("/usr/local/share/games/doom");

#endif
#else
    AddIWADDir (FILES_DIR);

                                     

    iwad_dirs_built = true;
#endif
}

  
                                                                 
   

char *D_FindWADByName(char *name)
{
    char *path;
    int i;
    
                     

    if (M_FileExists(name))
    {
        return name;
    }

    BuildIWADDirList();

                                                                    

    for (i=0; i<num_iwad_dirs; ++i)
    {
                                                                      
                                                                 
                

        if (DirIsFile(iwad_dirs[i], name) && M_FileExists(iwad_dirs[i]))
        {
            return strdup(iwad_dirs[i]);
        }

                                               

        path = M_StringJoin(iwad_dirs[i], DIR_SEPARATOR_S, name, NULL);

        if (M_FileExists(path))
        {
            return path;
        }

        free(path);
    }

                     

    return NULL;
}

  
                 
  
                                                                     
                
  

char *D_TryFindWADByName(char *filename)
{
    char *result;

    result = D_FindWADByName(filename);

    if (result != NULL)
    {
        return result;
    }
    else
    {
        return filename;
    }
}

  
           
                                             
                                                      
                                              
  

char *D_FindIWAD(int mask, GameMission_t *mission)
{
    char *result;
    char *iwadfile;
    int iwadparm;
    int i;

                                    

       
                                   
      
                  
      

    iwadparm = M_CheckParmWithArgs("-iwad", 1);

    if (iwadparm)
    {
                                                                    

        iwadfile = myargv[iwadparm + 1];

        result = D_FindWADByName(iwadfile);

        if (result == NULL)
        {
            I_Error("IWAD file '%s' not found!", iwadfile);
        }
        
        *mission = IdentifyIWADByName(result, mask);
    }
    else
    {
                                                       

        printf("-iwad not specified, trying a few iwad names\n");

        result = NULL;

        BuildIWADDirList();
    
        for (i=0; result == NULL && i<num_iwad_dirs; ++i)
        {
            result = SearchDirectoryForIWAD(iwad_dirs[i], mask, mission);
        }
    }

    return result;
}

                                                                  

const iwad_t **D_FindAllIWADs(int mask)
{
    const iwad_t **result;
    int result_len;
    char *filename;
    int i;

    result = malloc(sizeof(iwad_t *) * (arrlen(iwads) + 1));
    result_len = 0;

                            

    for (i=0; i<arrlen(iwads); ++i)
    {
        if (((1 << iwads[i].mission) & mask) == 0)
        {
            continue;
        }

        filename = D_FindWADByName(iwads[i].name);

        if (filename != NULL)
        {
            result[result_len] = &iwads[i];
            ++result_len;
        }
    }

                  

    result[result_len] = NULL;

    return result;
}

  
                                        
  

char *D_SaveGameIWADName(GameMission_t gamemission)
{
    size_t i;

                                                    
                                                                     
      
                                                                  
                                                                
                         

    for (i=0; i<arrlen(iwads); ++i)
    {
        if (gamemission == iwads[i].mission)
        {
            return iwads[i].name;
        }
    }

                        

    return "unknown.wad";
}

char *D_SuggestIWADName(GameMission_t mission, GameMode_t mode)
{
    int i;

    for (i = 0; i < arrlen(iwads); ++i)
    {
        if (iwads[i].mission == mission && iwads[i].mode == mode)
        {
            return iwads[i].name;
        }
    }

    return "unknown.wad";
}

char *D_SuggestGameName(GameMission_t mission, GameMode_t mode)
{
    int i;

    for (i = 0; i < arrlen(iwads); ++i)
    {
        if (iwads[i].mission == mission
         && (mode == indetermined || iwads[i].mode == mode))
        {
            return iwads[i].description;
        }
    }

    return "Unknown game?";
}

