  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                                                
  




#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomtype.h"

#include "config.h"
#include "d_iwad.h"
#include "i_swap.h"
#include "i_system.h"
#include "i_video.h"
#include "m_misc.h"
#include "z_zone.h"

#include "w_wad.h"

typedef struct
{
                                  
    char		identification[4];		
    int			numlumps;
    int			infotableofs;
} PACKEDATTR wadinfo_t;


typedef struct
{
    int			filepos;
    int			size;
    char		name[8];
} PACKEDATTR filelump_t;

  
          
  

                                 

lumpinfo_t *lumpinfo;		
unsigned int numlumps = 0;

                              

static lumpinfo_t **lumphash;

                                     

unsigned int W_LumpNameHash(const char *s)
{
                                                                       
                                       

    unsigned int result = 5381;
    unsigned int i;

    for (i=0; i < 8 && s[i] != '\0'; ++i)
    {
        result = ((result << 5) ^ result ) ^ toupper((int)s[i]);
    }

    return result;
}

                                                                   
static void ExtendLumpInfo(int newnumlumps)
{
    lumpinfo_t *newlumpinfo;
    unsigned int i;

    newlumpinfo = calloc(newnumlumps, sizeof(lumpinfo_t));

    if (newlumpinfo == NULL)
    {
	I_Error ("Couldn't realloc lumpinfo");
    }

                                                                    
                                                               
                                    
    for (i = 0; i < numlumps && i < newnumlumps; ++i)
    {
        memcpy(&newlumpinfo[i], &lumpinfo[i], sizeof(lumpinfo_t));

        if (newlumpinfo[i].cache != NULL)
        {
            Z_ChangeUser(newlumpinfo[i].cache, &newlumpinfo[i].cache);
        }

                                                                            
                                           
        if (lumpinfo[i].next != NULL)
        {
            int nextlumpnum = lumpinfo[i].next - lumpinfo;
            newlumpinfo[i].next = &newlumpinfo[nextlumpnum];
        }
    }

                
    free(lumpinfo);
    lumpinfo = newlumpinfo;
    numlumps = newnumlumps;
}

  
                       
  

  
            
                                                        
                                                    
                                                
                        
                                                      
                      

wad_file_t *W_AddFile (char *filename)
{
    wadinfo_t header;
    lumpinfo_t *lump_p;
    unsigned int i;
    wad_file_t *wad_file;
    int length;
    int startlump;
    filelump_t *fileinfo;
    filelump_t *filerover;
    int newnumlumps;

                                         

    wad_file = W_OpenFile(filename);

    if (wad_file == NULL)
    {
		printf (" couldn't open %s\n", filename);
		return NULL;
    }

    newnumlumps = numlumps;

    if (strcasecmp(filename+strlen(filename)-3 , "wad" ) )
    {
    	                   

                                                                      
                                                                       
                                                                            
                                            

		fileinfo = Z_Malloc(sizeof(filelump_t), PU_STATIC, 0);
		fileinfo->filepos = LONG(0);
		fileinfo->size = LONG(wad_file->length);

                                                                    
                      

		M_ExtractFileBase (filename, fileinfo->name);
		newnumlumps++;
    }
    else 
    {
    	           
        W_Read(wad_file, 0, &header, sizeof(header));

		if (strncmp(header.identification,"IWAD",4))
		{
			                   
			if (strncmp(header.identification,"PWAD",4))
			{
			I_Error ("Wad file %s doesn't have IWAD "
				 "or PWAD id\n", filename);
			}

			                          
		}

		header.numlumps = LONG(header.numlumps);
		header.infotableofs = LONG(header.infotableofs);
		length = header.numlumps*sizeof(filelump_t);
		fileinfo = Z_Malloc(length, PU_STATIC, 0);

        W_Read(wad_file, header.infotableofs, fileinfo, length);
        newnumlumps += header.numlumps;
    }

                                                                  
    startlump = numlumps;
    ExtendLumpInfo(newnumlumps);

    lump_p = &lumpinfo[startlump];

    filerover = fileinfo;

    for (i=startlump; i<numlumps; ++i)
    {
		lump_p->wad_file = wad_file;
		lump_p->position = LONG(filerover->filepos);
		lump_p->size = LONG(filerover->size);
			lump_p->cache = NULL;
		strncpy(lump_p->name, filerover->name, 8);

			++lump_p;
			++filerover;
    }

    Z_Free(fileinfo);

    if (lumphash != NULL)
    {
        Z_Free(lumphash);
        lumphash = NULL;
    }

    return wad_file;
}



  
             
  
int W_NumLumps (void)
{
    return numlumps;
}



  
                    
                                
  

int W_CheckNumForName (char* name)
{
    lumpinfo_t *lump_p;
    int i;

                                   

    if (lumphash != NULL)
    {
        int hash;
        
                            

        hash = W_LumpNameHash(name) % numlumps;
        
        for (lump_p = lumphash[hash]; lump_p != NULL; lump_p = lump_p->next)
        {
            if (!strncasecmp(lump_p->name, name, 8))
            {
                return lump_p - lumpinfo;
            }
        }
    } 
    else
    {
                                                                     
           
                                                             

        for (i=numlumps-1; i >= 0; --i)
        {
            if (!strncasecmp(lumpinfo[i].name, name, 8))
            {
                return i;
            }
        }
    }

                      

    return -1;
}




  
                  
                                                       
  
int W_GetNumForName (char* name)
{
    int	i;

    i = W_CheckNumForName (name);

    if (i < 0)
    {
        I_Error ("W_GetNumForName: %s not found!", name);
    }
 
    return i;
}


  
               
                                                         
  
int W_LumpLength (unsigned int lump)
{
    if (lump >= numlumps)
    {
	I_Error ("W_LumpLength: %i >= numlumps", lump);
    }

    return lumpinfo[lump].size;
}



  
             
                                        
                                    
  
void W_ReadLump(unsigned int lump, void *dest)
{
    int c;
    lumpinfo_t *l;
	
    if (lump >= numlumps)
    {
	I_Error ("W_ReadLump: %i >= numlumps", lump);
    }

    l = lumpinfo+lump;
	
    I_BeginRead ();
	
    c = W_Read(l->wad_file, l->position, dest, l->size);

    if (c < l->size)
    {
	I_Error ("W_ReadLump: only read %i of %i on lump %i",
		 c, l->size, lump);	
    }

    I_EndRead ();
}




  
                 
  
                                                                      
                 
  
                                                                   
                                                              
                                                               
                                                  
  

void *W_CacheLumpNum(int lumpnum, int tag)
{
    byte *result;
    lumpinfo_t *lump;

    if ((unsigned)lumpnum >= numlumps)
    {
	I_Error ("W_CacheLumpNum: %i >= numlumps", lumpnum);
    }

    lump = &lumpinfo[lumpnum];

                                                                    
                                                                     
                                                                  
                                                      

    if (lump->wad_file->mapped != NULL)
    {
                                                              

        result = lump->wad_file->mapped + lump->position;
    }
    else if (lump->cache != NULL)
    {
                                                       

        result = lump->cache;
        Z_ChangeTag(lump->cache, tag);
    }
    else
    {
                                         

        lump->cache = Z_Malloc(W_LumpLength(lumpnum), tag, &lump->cache);
	W_ReadLump (lumpnum, lump->cache);
        result = lump->cache;
    }
	
    return result;
}



  
                  
  
void *W_CacheLumpName(char *name, int tag)
{
    return W_CacheLumpNum(W_GetNumForName(name), tag);
}

   
                                                                    
                                                                      
                           
  
                                                              
                                                                 
                  
  

void W_ReleaseLumpNum(int lumpnum)
{
    lumpinfo_t *lump;

    if ((unsigned)lumpnum >= numlumps)
    {
	I_Error ("W_ReleaseLumpNum: %i >= numlumps", lumpnum);
    }

    lump = &lumpinfo[lumpnum];

    if (lump->wad_file->mapped != NULL)
    {
                                                                
    }
    else
    {
        Z_ChangeTag(lump->cache, PU_CACHE);
    }
}

void W_ReleaseLumpName(char *name)
{
    W_ReleaseLumpNum(W_GetNumForName(name));
}

#if 0

  
            
  
int		info[2500][10];
int		profilecount;

void W_Profile (void)
{
    int		i;
    memblock_t*	block;
    void*	ptr;
    char	ch;
    FILE*	f;
    int		j;
    char	name[9];
	
	
    for (i=0 ; i<numlumps ; i++)
    {	
	ptr = lumpinfo[i].cache;
	if (!ptr)
	{
	    ch = ' ';
	    continue;
	}
	else
	{
	    block = (memblock_t *) ( (byte *)ptr - sizeof(memblock_t));
	    if (block->tag < PU_PURGELEVEL)
		ch = 'S';
	    else
		ch = 'P';
	}
	info[i][profilecount] = ch;
    }
    profilecount++;
#if ORIGCODE
    f = fopen ("waddump.txt","w");
    name[8] = 0;

    for (i=0 ; i<numlumps ; i++)
    {
	memcpy (name,lumpinfo[i].name,8);

	for (j=0 ; j<8 ; j++)
	    if (!name[j])
		break;

	for ( ; j<8 ; j++)
	    name[j] = ' ';

	fprintf (f,"%s ",name);

	for (j=0 ; j<profilecount ; j++)
	    fprintf (f,"    %c",info[i][j]);

	fprintf (f,"\n");
    }
    fclose (f);
#endif
}


#endif

                                         

void W_GenerateHashTable(void)
{
    unsigned int i;

                                               

    if (lumphash != NULL)
    {
        Z_Free(lumphash);
    }

                          
    if (numlumps > 0)
    {
        lumphash = Z_Malloc(sizeof(lumpinfo_t *) * numlumps, PU_STATIC, NULL);
        memset(lumphash, 0, sizeof(lumpinfo_t *) * numlumps);

        for (i=0; i<numlumps; ++i)
        {
            unsigned int hash;

            hash = W_LumpNameHash(lumpinfo[i].name) % numlumps;

                                       

            lumpinfo[i].next = lumphash[hash];
            lumphash[hash] = &lumpinfo[i];
        }
    }

                
}

                                                                          
                                                                
                                  
static const struct
{
    GameMission_t mission;
    char *lumpname;
} unique_lumps[] = {
    { doom,    "POSSA1" },
    { heretic, "IMPXA1" },
    { hexen,   "ETTNA1" },
    { strife,  "AGRDA1" },
};

void W_CheckCorrectIWAD(GameMission_t mission)
{
    int i;
    int lumpnum;

    for (i = 0; i < arrlen(unique_lumps); ++i)
    {
        if (mission != unique_lumps[i].mission)
        {
            lumpnum = W_CheckNumForName(unique_lumps[i].lumpname);

            if (lumpnum >= 0)
            {
                I_Error("\nYou are trying to use a %s IWAD file with "
                        "the %s%s binary.\nThis isn't going to work.\n"
                        "You probably want to use the %s%s binary.",
                        D_SuggestGameName(unique_lumps[i].mission,
                                          indetermined),
                        PROGRAM_PREFIX,
                        D_GameMissionString(mission),
                        PROGRAM_PREFIX,
                        D_GameMissionString(unique_lumps[i].mission));
            }
        }
    }
}

