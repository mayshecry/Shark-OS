  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                              
                                                
                                             
                           
                                               
  


#include <stdlib.h>

#include "doomdef.h"
#include "doomstat.h"

#include "deh_main.h"
#include "i_system.h"
#include "z_zone.h"
#include "m_argv.h"
#include "m_misc.h"
#include "m_random.h"
#include "w_wad.h"

#include "r_local.h"
#include "p_local.h"

#include "g_game.h"

#include "s_sound.h"

         
#include "r_state.h"

        
#include "sounds.h"


  
                                
                                                       
  
typedef struct
{
    boolean	istexture;
    int		picnum;
    int		basepic;
    int		numpics;
    int		speed;
    
} anim_t;

  
                                   
  
typedef struct
{
    int 	istexture;	                         
    char	endname[9];
    char	startname[9];
    int		speed;
} animdef_t;



#define MAXANIMS                32

extern anim_t	anims[MAXANIMS];
extern anim_t*	lastanim;

  
                 
  

                                     
                                    
                                      
            
                                       
                                         
                                        
                 
  
animdef_t		animdefs[] =
{
    {false,	"NUKAGE3",	"NUKAGE1",	8},
    {false,	"FWATER4",	"FWATER1",	8},
    {false,	"SWATER4",	"SWATER1", 	8},
    {false,	"LAVA4",	"LAVA1",	8},
    {false,	"BLOOD3",	"BLOOD1",	8},

                               
    {false,	"RROCK08",	"RROCK05",	8},		
    {false,	"SLIME04",	"SLIME01",	8},
    {false,	"SLIME08",	"SLIME05",	8},
    {false,	"SLIME12",	"SLIME09",	8},

    {true,	"BLODGR4",	"BLODGR1",	8},
    {true,	"SLADRIP3",	"SLADRIP1",	8},

    {true,	"BLODRIP4",	"BLODRIP1",	8},
    {true,	"FIREWALL",	"FIREWALA",	8},
    {true,	"GSTFONT3",	"GSTFONT1",	8},
    {true,	"FIRELAVA",	"FIRELAV3",	8},
    {true,	"FIREMAG3",	"FIREMAG1",	8},
    {true,	"FIREBLU2",	"FIREBLU1",	8},
    {true,	"ROCKRED3",	"ROCKRED1",	8},

    {true,	"BFALL4",	"BFALL1",	8},
    {true,	"SFALL4",	"SFALL1",	8},
    {true,	"WFALL4",	"WFALL1",	8},
    {true,	"DBRAIN4",	"DBRAIN1",	8},
	
    {-1,        "",             "",             0},
};

anim_t		anims[MAXANIMS];
anim_t*		lastanim;


  
                               
  
#define MAXLINEANIMS            64

extern  short	numlinespecials;
extern  line_t*	linespeciallist[MAXLINEANIMS];



void P_InitPicAnims (void)
{
    int		i;

    
                     
    lastanim = anims;
    for (i=0 ; animdefs[i].istexture != -1 ; i++)
    {
        char *startname, *endname;

        startname = DEH_String(animdefs[i].startname);
        endname = DEH_String(animdefs[i].endname);

	if (animdefs[i].istexture)
	{
	                          
	    if (R_CheckTextureNumForName(startname) == -1)
		continue;	

	    lastanim->picnum = R_TextureNumForName(endname);
	    lastanim->basepic = R_TextureNumForName(startname);
	}
	else
	{
	    if (W_CheckNumForName(startname) == -1)
		continue;

	    lastanim->picnum = R_FlatNumForName(endname);
	    lastanim->basepic = R_FlatNumForName(startname);
	}

	lastanim->istexture = animdefs[i].istexture;
	lastanim->numpics = lastanim->picnum - lastanim->basepic + 1;

	if (lastanim->numpics < 2)
	    I_Error ("P_InitPicAnims: bad cycle from %s to %s",
		     startname, endname);
	
	lastanim->speed = animdefs[i].speed;
	lastanim++;
    }
	
}



  
            
  



  
            
                        
                                           
                                                      
  
side_t*
getSide
( int		currentSector,
  int		line,
  int		side )
{
    return &sides[ (sectors[currentSector].lines[line])->sidenum[side] ];
}


  
              
                          
                                           
                                                     
  
sector_t*
getSector
( int		currentSector,
  int		line,
  int		side )
{
    return sides[ (sectors[currentSector].lines[line])->sidenum[side] ].sector;
}


  
             
                                               
                                                          
  
int
twoSided
( int	sector,
  int	line )
{
    return (sectors[sector].lines[line])->flags & ML_TWOSIDED;
}




  
                  
                                               
                             
  
sector_t*
getNextSector
( line_t*	line,
  sector_t*	sec )
{
    if (!(line->flags & ML_TWOSIDED))
	return NULL;
		
    if (line->frontsector == sec)
	return line->backsector;
	
    return line->frontsector;
}



  
                                 
                                                  
  
fixed_t	P_FindLowestFloorSurrounding(sector_t* sec)
{
    int			i;
    line_t*		check;
    sector_t*		other;
    fixed_t		floor = sec->floorheight;
	
    for (i=0 ;i < sec->linecount ; i++)
    {
	check = sec->lines[i];
	other = getNextSector(check,sec);

	if (!other)
	    continue;
	
	if (other->floorheight < floor)
	    floor = other->floorheight;
    }
    return floor;
}



  
                                  
                                                   
  
fixed_t	P_FindHighestFloorSurrounding(sector_t *sec)
{
    int			i;
    line_t*		check;
    sector_t*		other;
    fixed_t		floor = -500*FRACUNIT;
	
    for (i=0 ;i < sec->linecount ; i++)
    {
	check = sec->lines[i];
	other = getNextSector(check,sec);
	
	if (!other)
	    continue;
	
	if (other->floorheight > floor)
	    floor = other->floorheight;
    }
    return floor;
}



  
                         
                                                 
                                                 

                                                         

                            
#define MAX_ADJOINING_SECTORS     20

fixed_t
P_FindNextHighestFloor
( sector_t* sec,
  int       currentheight )
{
    int         i;
    int         h;
    int         min;
    line_t*     check;
    sector_t*   other;
    fixed_t     height = currentheight;
    fixed_t     heightlist[MAX_ADJOINING_SECTORS + 2];

    for (i=0, h=0; i < sec->linecount; i++)
    {
        check = sec->lines[i];
        other = getNextSector(check,sec);

        if (!other)
            continue;
        
        if (other->floorheight > height)
        {
                                                   
            if (h == MAX_ADJOINING_SECTORS + 1)
            {
                height = other->floorheight;
            }
            else if (h == MAX_ADJOINING_SECTORS + 2)
            {
                                                              
                I_Error("Sector with more than 22 adjoining sectors. "
                        "Vanilla will crash here");
            }

            heightlist[h++] = other->floorheight;
        }
    }
    
                                 
    if (!h)
    {
        return currentheight;
    }
        
    min = heightlist[0];
    
                       
    for (i = 1; i < h; i++)
    {
        if (heightlist[i] < min)
        {
            min = heightlist[i];
        }
    }

    return min;
}

  
                                                 
  
fixed_t
P_FindLowestCeilingSurrounding(sector_t* sec)
{
    int			i;
    line_t*		check;
    sector_t*		other;
    fixed_t		height = INT_MAX;
	
    for (i=0 ;i < sec->linecount ; i++)
    {
	check = sec->lines[i];
	other = getNextSector(check,sec);

	if (!other)
	    continue;

	if (other->ceilingheight < height)
	    height = other->ceilingheight;
    }
    return height;
}


  
                                                  
  
fixed_t	P_FindHighestCeilingSurrounding(sector_t* sec)
{
    int		i;
    line_t*	check;
    sector_t*	other;
    fixed_t	height = 0;
	
    for (i=0 ;i < sec->linecount ; i++)
    {
	check = sec->lines[i];
	other = getNextSector(check,sec);

	if (!other)
	    continue;

	if (other->ceilingheight > height)
	    height = other->ceilingheight;
    }
    return height;
}



  
                                               
  
int
P_FindSectorFromLineTag
( line_t*	line,
  int		start )
{
    int	i;
	
    for (i=start+1;i<numsectors;i++)
	if (sectors[i].tag == line->tag)
	    return i;
    
    return -1;
}




  
                                             
  
int
P_FindMinSurroundingLight
( sector_t*	sector,
  int		max )
{
    int		i;
    int		min;
    line_t*	line;
    sector_t*	check;
	
    min = max;
    for (i=0 ; i < sector->linecount ; i++)
    {
	line = sector->lines[i];
	check = getNextSector(line,sector);

	if (!check)
	    continue;

	if (check->lightlevel < min)
	    min = check->lightlevel;
    }
    return min;
}



  
         
                                                      
                                                   
  

  
                               
                                            
                                         
  
void
P_CrossSpecialLine
( int		linenum,
  int		side,
  mobj_t*	thing )
{
    line_t*	line;
    int		ok;

    line = &lines[linenum];
    
                                              
    if (!thing->player)
    {
	                                             
	switch(thing->type)
	{
	  case MT_ROCKET:
	  case MT_PLASMA:
	  case MT_BFG:
	  case MT_TROOPSHOT:
	  case MT_HEADSHOT:
	  case MT_BRUISERSHOT:
	    return;
	    break;
	    
	  default: break;
	}
		
	ok = 0;
	switch(line->special)
	{
	  case 39:	                   
	  case 97:	                     
	  case 125:	                               
	  case 126:	                                 
	  case 4:	             
	  case 10:	                                 
	  case 88:	                                   
	    ok = 1;
	    break;
	}
	if (!ok)
	    return;
    }

    
                                         
    switch (line->special)
    {
	            
	                               
      case 2:
	            
	EV_DoDoor(line,vld_open);
	line->special = 0;
	break;

      case 3:
	             
	EV_DoDoor(line,vld_close);
	line->special = 0;
	break;

      case 4:
	             
	EV_DoDoor(line,vld_normal);
	line->special = 0;
	break;
	
      case 5:
	              
	EV_DoFloor(line,raiseFloor);
	line->special = 0;
	break;
	
      case 6:
	                             
	EV_DoCeiling(line,fastCrushAndRaise);
	line->special = 0;
	break;
	
      case 8:
	               
	EV_BuildStairs(line,build8);
	line->special = 0;
	break;
	
      case 10:
	                 
	EV_DoPlat(line,downWaitUpStay,0);
	line->special = 0;
	break;
	
      case 12:
	                                 
	EV_LightTurnOn(line,0);
	line->special = 0;
	break;
	
      case 13:
	                    
	EV_LightTurnOn(line,255);
	line->special = 0;
	break;
	
      case 16:
	                
	EV_DoDoor(line,vld_close30ThenOpen);
	line->special = 0;
	break;
	
      case 17:
	                       
	EV_StartLightStrobing(line);
	line->special = 0;
	break;
	
      case 19:
	              
	EV_DoFloor(line,lowerFloor);
	line->special = 0;
	break;
	
      case 22:
	                                                   
	EV_DoPlat(line,raiseToNearestAndChange,0);
	line->special = 0;
	break;
	
      case 25:
	                          
	EV_DoCeiling(line,crushAndRaise);
	line->special = 0;
	break;
	
      case 30:
	                                         
	                            
	EV_DoFloor(line,raiseToTexture);
	line->special = 0;
	break;
	
      case 35:
	                   
	EV_LightTurnOn(line,35);
	line->special = 0;
	break;
	
      case 36:
	                      
	EV_DoFloor(line,turboLower);
	line->special = 0;
	break;
	
      case 37:
	                 
	EV_DoFloor(line,lowerAndChange);
	line->special = 0;
	break;
	
      case 38:
	                        
	EV_DoFloor( line, lowerFloorToLowest );
	line->special = 0;
	break;
	
      case 39:
	            
	EV_Teleport( line, side, thing );
	line->special = 0;
	break;

      case 40:
	                         
	EV_DoCeiling( line, raiseToHighest );
	EV_DoFloor( line, lowerFloorToLowest );
	line->special = 0;
	break;
	
      case 44:
	                
	EV_DoCeiling( line, lowerAndCrush );
	line->special = 0;
	break;
	
      case 52:
	        
	G_ExitLevel ();
	break;
	
      case 53:
	                           
	EV_DoPlat(line,perpetualRaise,0);
	line->special = 0;
	break;
	
      case 54:
	                
	EV_StopPlat(line);
	line->special = 0;
	break;

      case 56:
	                    
	EV_DoFloor(line,raiseFloorCrush);
	line->special = 0;
	break;

      case 57:
	                     
	EV_CeilingCrushStop(line);
	line->special = 0;
	break;
	
      case 58:
	                 
	EV_DoFloor(line,raiseFloor24);
	line->special = 0;
	break;

      case 59:
	                            
	EV_DoFloor(line,raiseFloor24AndChange);
	line->special = 0;
	break;
	
      case 104:
	                                 
	EV_TurnTagLightsOff(line);
	line->special = 0;
	break;
	
      case 108:
	                                          
	EV_DoDoor (line,vld_blazeRaise);
	line->special = 0;
	break;
	
      case 109:
	                                         
	EV_DoDoor (line,vld_blazeOpen);
	line->special = 0;
	break;
	
      case 100:
	                        
	EV_BuildStairs(line,turbo16);
	line->special = 0;
	break;
	
      case 110:
	                                          
	EV_DoDoor (line,vld_blazeClose);
	line->special = 0;
	break;

      case 119:
	                                     
	EV_DoFloor(line,raiseFloorToNearest);
	line->special = 0;
	break;
	
      case 121:
	                             
	EV_DoPlat(line,blazeDWUS,0);
	line->special = 0;
	break;
	
      case 124:
	              
	G_SecretExitLevel ();
	break;
		
      case 125:
	                       
	if (!thing->player)
	{
	    EV_Teleport( line, side, thing );
	    line->special = 0;
	}
	break;
	
      case 130:
	                    
	EV_DoFloor(line,raiseFloorTurbo);
	line->special = 0;
	break;
	
      case 141:
	                               
	EV_DoCeiling(line,silentCrushAndRaise);
	line->special = 0;
	break;
	
	                                       
      case 72:
	                
	EV_DoCeiling( line, lowerAndCrush );
	break;

      case 73:
	                          
	EV_DoCeiling(line,crushAndRaise);
	break;

      case 74:
	                     
	EV_CeilingCrushStop(line);
	break;
	
      case 75:
	             
	EV_DoDoor(line,vld_close);
	break;
	
      case 76:
	                
	EV_DoDoor(line,vld_close30ThenOpen);
	break;
	
      case 77:
	                             
	EV_DoCeiling(line,fastCrushAndRaise);
	break;
	
      case 79:
	                   
	EV_LightTurnOn(line,35);
	break;
	
      case 80:
	                                 
	EV_LightTurnOn(line,0);
	break;
	
      case 81:
	                    
	EV_LightTurnOn(line,255);
	break;
	
      case 82:
	                        
	EV_DoFloor( line, lowerFloorToLowest );
	break;
	
      case 83:
	              
	EV_DoFloor(line,lowerFloor);
	break;

      case 84:
	                 
	EV_DoFloor(line,lowerAndChange);
	break;

      case 86:
	            
	EV_DoDoor(line,vld_open);
	break;
	
      case 87:
	                           
	EV_DoPlat(line,perpetualRaise,0);
	break;
	
      case 88:
	                 
	EV_DoPlat(line,downWaitUpStay,0);
	break;
	
      case 89:
	                
	EV_StopPlat(line);
	break;
	
      case 90:
	             
	EV_DoDoor(line,vld_normal);
	break;
	
      case 91:
	              
	EV_DoFloor(line,raiseFloor);
	break;
	
      case 92:
	                 
	EV_DoFloor(line,raiseFloor24);
	break;
	
      case 93:
	                            
	EV_DoFloor(line,raiseFloor24AndChange);
	break;
	
      case 94:
	                    
	EV_DoFloor(line,raiseFloorCrush);
	break;
	
      case 95:
	                                
	                      
	EV_DoPlat(line,raiseToNearestAndChange,0);
	break;
	
      case 96:
	                                         
	                           
	EV_DoFloor(line,raiseToTexture);
	break;
	
      case 97:
	            
	EV_Teleport( line, side, thing );
	break;
	
      case 98:
	                      
	EV_DoFloor(line,turboLower);
	break;

      case 105:
	                                          
	EV_DoDoor (line,vld_blazeRaise);
	break;
	
      case 106:
	                                         
	EV_DoDoor (line,vld_blazeOpen);
	break;

      case 107:
	                                          
	EV_DoDoor (line,vld_blazeClose);
	break;

      case 120:
	                              
	EV_DoPlat(line,blazeDWUS,0);
	break;
	
      case 126:
	                        
	if (!thing->player)
	    EV_Teleport( line, side, thing );
	break;
	
      case 128:
	                         
	EV_DoFloor(line,raiseFloorToNearest);
	break;
	
      case 129:
	                    
	EV_DoFloor(line,raiseFloorTurbo);
	break;
    }
}



  
                                       
                                             
  
void
P_ShootSpecialLine
( mobj_t*	thing,
  line_t*	line )
{
    int		ok;
    
                                              
    if (!thing->player)
    {
	ok = 0;
	switch(line->special)
	{
	  case 46:
	                       
	    ok = 1;
	    break;
	}
	if (!ok)
	    return;
    }

    switch(line->special)
    {
      case 24:
	              
	EV_DoFloor(line,raiseFloor);
	P_ChangeSwitchTexture(line,0);
	break;
	
      case 46:
	            
	EV_DoDoor(line,vld_open);
	P_ChangeSwitchTexture(line,1);
	break;
	
      case 47:
	                              
	EV_DoPlat(line,raiseToNearestAndChange,0);
	P_ChangeSwitchTexture(line,0);
	break;
    }
}



  
                          
                         
                                                 
  
void P_PlayerInSpecialSector (player_t* player)
{
    sector_t*	sector;
	
    sector = player->mo->subsector->sector;

                                         
    if (player->mo->z != sector->floorheight)
	return;	

                         
    switch (sector->special)
    {
      case 5:
	                   
	if (!player->powers[pw_ironfeet])
	    if (!(leveltime&0x1f))
		P_DamageMobj (player->mo, NULL, NULL, 10);
	break;
	
      case 7:
	                
	if (!player->powers[pw_ironfeet])
	    if (!(leveltime&0x1f))
		P_DamageMobj (player->mo, NULL, NULL, 5);
	break;
	
      case 16:
	                         
      case 4:
	              
	if (!player->powers[pw_ironfeet]
	    || (P_Random()<5) )
	{
	    if (!(leveltime&0x1f))
		P_DamageMobj (player->mo, NULL, NULL, 20);
	}
	break;
			
      case 9:
	                
	player->secretcount++;
	sector->special = 0;
	break;
			
      case 11:
	                                       
	player->cheats &= ~CF_GODMODE;

	if (!(leveltime&0x1f))
	    P_DamageMobj (player->mo, NULL, NULL, 20);

	if (player->health <= 10)
	    G_ExitLevel();
	break;
			
      default:
	I_Error ("P_PlayerInSpecialSector: "
		 "unknown special %i",
		 sector->special);
	break;
    };
}




  
                   
                                     
  
boolean		levelTimer;
int		levelTimeCount;

void P_UpdateSpecials (void)
{
    anim_t*	anim;
    int		pic;
    int		i;
    line_t*	line;

    
                  
    if (levelTimer == true)
    {
	levelTimeCount--;
	if (!levelTimeCount)
	    G_ExitLevel();
    }
    
                                          
    for (anim = anims ; anim < lastanim ; anim++)
    {
	for (i=anim->basepic ; i<anim->basepic+anim->numpics ; i++)
	{
	    pic = anim->basepic + ( (leveltime/anim->speed + i)%anim->numpics );
	    if (anim->istexture)
		texturetranslation[i] = pic;
	    else
		flattranslation[i] = pic;
	}
    }

    
                            
    for (i = 0; i < numlinespecials; i++)
    {
	line = linespeciallist[i];
	switch(line->special)
	{
	  case 48:
	                               
	    sides[line->sidenum[0]].textureoffset += FRACUNIT;
	    break;
	}
    }

    
                 
    for (i = 0; i < MAXBUTTONS; i++)
	if (buttonlist[i].btimer)
	{
	    buttonlist[i].btimer--;
	    if (!buttonlist[i].btimer)
	    {
		switch(buttonlist[i].where)
		{
		  case top:
		    sides[buttonlist[i].line->sidenum[0]].toptexture =
			buttonlist[i].btexture;
		    break;
		    
		  case middle:
		    sides[buttonlist[i].line->sidenum[0]].midtexture =
			buttonlist[i].btexture;
		    break;
		    
		  case bottom:
		    sides[buttonlist[i].line->sidenum[0]].bottomtexture =
			buttonlist[i].btexture;
		    break;
		}
		S_StartSound(&buttonlist[i].soundorg,sfx_swtchn);
		memset(&buttonlist[i],0,sizeof(button_t));
	    }
	}
}


  
                          
  
                                                                            
               
  

#define DONUT_FLOORHEIGHT_DEFAULT 0x00000000
#define DONUT_FLOORPIC_DEFAULT 0x16

static void DonutOverrun(fixed_t *s3_floorheight, short *s3_floorpic,
                         line_t *line, sector_t *pillar_sector)
{
    static int first = 1;
    static int tmp_s3_floorheight;
    static int tmp_s3_floorpic;

    extern int numflats;

    if (first)
    {
        int p;

                                                         
        first = 0;

                         
        tmp_s3_floorheight = DONUT_FLOORHEIGHT_DEFAULT;
        tmp_s3_floorpic = DONUT_FLOORPIC_DEFAULT;

           
                           
                       
          
                                                                        
                                                                 
                                                                     
                                                                       
                                                              

        p = M_CheckParmWithArgs("-donut", 2);

        if (p > 0)
        {
                                                                             
              
                        
                     
              
                        
                                                             
                       
                                                             
                     
                                                             
                               
                                                             

            M_StrToInt(myargv[p + 1], &tmp_s3_floorheight);
            M_StrToInt(myargv[p + 2], &tmp_s3_floorpic);

            if (tmp_s3_floorpic >= numflats)
            {
                fprintf(stderr,
                        "DonutOverrun: The second parameter for \"-donut\" "
                        "switch should be greater than 0 and less than number "
                        "of flats (%d). Using default value (%d) instead. \n",
                        numflats, DONUT_FLOORPIC_DEFAULT);
                tmp_s3_floorpic = DONUT_FLOORPIC_DEFAULT;
            }
        }
    }

      
                   
                                       
                                                        
                                                    
                                                       
       

    *s3_floorheight = (fixed_t) tmp_s3_floorheight;
    *s3_floorpic = (short) tmp_s3_floorpic;
}


  
                                            
  
int EV_DoDonut(line_t*	line)
{
    sector_t*		s1;
    sector_t*		s2;
    sector_t*		s3;
    int			secnum;
    int			rtn;
    int			i;
    floormove_t*	floor;
    fixed_t s3_floorheight;
    short s3_floorpic;

    secnum = -1;
    rtn = 0;
    while ((secnum = P_FindSectorFromLineTag(line,secnum)) >= 0)
    {
	s1 = &sectors[secnum];

	                                        
	if (s1->specialdata)
	    continue;

	rtn = 1;
	s2 = getNextSector(s1->lines[0],s1);

                                                                        
                                                                       
                                                              
                                                                  
          
                                                                        
                                                       
                                           

        if (s2 == NULL)
        {
            fprintf(stderr,
                    "EV_DoDonut: linedef had no second sidedef! "
                    "Unexpected behavior may occur in Vanilla Doom. \n");
	    break;
        }

	for (i = 0; i < s2->linecount; i++)
	{
	    s3 = s2->lines[i]->backsector;

	    if (s3 == s1)
		continue;

            if (s3 == NULL)
            {
                      
                                 
                                                         
                                                       
                                    

                fprintf(stderr,
                        "EV_DoDonut: WARNING: emulating buffer overrun due to "
                        "NULL back sector. "
                        "Unexpected behavior may occur in Vanilla Doom.\n");

                DonutOverrun(&s3_floorheight, &s3_floorpic, line, s1);
            }
            else
            {
                s3_floorheight = s3->floorheight;
                s3_floorpic = s3->floorpic;
            }

	                         
	    floor = Z_Malloc (sizeof(*floor), PU_LEVSPEC, 0);
	    P_AddThinker (&floor->thinker);
	    s2->specialdata = floor;
	    floor->thinker.function.acp1 = (actionf_p1) T_MoveFloor;
	    floor->type = donutRaise;
	    floor->crush = false;
	    floor->direction = 1;
	    floor->sector = s2;
	    floor->speed = FLOORSPEED / 2;
	    floor->texture = s3_floorpic;
	    floor->newspecial = 0;
	    floor->floordestheight = s3_floorheight;
	    
	                                
	    floor = Z_Malloc (sizeof(*floor), PU_LEVSPEC, 0);
	    P_AddThinker (&floor->thinker);
	    s1->specialdata = floor;
	    floor->thinker.function.acp1 = (actionf_p1) T_MoveFloor;
	    floor->type = lowerFloor;
	    floor->crush = false;
	    floor->direction = -1;
	    floor->sector = s1;
	    floor->speed = FLOORSPEED / 2;
	    floor->floordestheight = s3_floorheight;
	    break;
	}
    }
    return rtn;
}



  
                   
  

  
                  
                                                   
                       
  
short		numlinespecials;
line_t*		linespeciallist[MAXLINEANIMS];


                                  
void P_SpawnSpecials (void)
{
    sector_t*	sector;
    int		i;

                                   

    if (timelimit > 0 && deathmatch)
    {
        levelTimer = true;
        levelTimeCount = timelimit * 60 * TICRATE;
    }
    else
    {
	levelTimer = false;
    }

                            
    sector = sectors;
    for (i=0 ; i<numsectors ; i++, sector++)
    {
	if (!sector->special)
	    continue;
	
	switch (sector->special)
	{
	  case 1:
	                        
	    P_SpawnLightFlash (sector);
	    break;

	  case 2:
	                  
	    P_SpawnStrobeFlash(sector,FASTDARK,0);
	    break;
	    
	  case 3:
	                  
	    P_SpawnStrobeFlash(sector,SLOWDARK,0);
	    break;
	    
	  case 4:
	                              
	    P_SpawnStrobeFlash(sector,FASTDARK,0);
	    sector->special = 4;
	    break;
	    
	  case 8:
	                    
	    P_SpawnGlowingLight(sector);
	    break;
	  case 9:
	                    
	    totalsecret++;
	    break;
	    
	  case 10:
	                               
	    P_SpawnDoorCloseIn30 (sector);
	    break;
	    
	  case 12:
	                       
	    P_SpawnStrobeFlash (sector, SLOWDARK, 1);
	    break;

	  case 13:
	                       
	    P_SpawnStrobeFlash (sector, FASTDARK, 1);
	    break;

	  case 14:
	                              
	    P_SpawnDoorRaiseIn5Mins (sector, i);
	    break;
	    
	  case 17:
	    P_SpawnFireFlicker(sector);
	    break;
	}
    }

    
                        
    numlinespecials = 0;
    for (i = 0;i < numlines; i++)
    {
	switch(lines[i].special)
	{
	  case 48:
            if (numlinespecials >= MAXLINEANIMS)
            {
                I_Error("Too many scrolling wall linedefs! "
                        "(Vanilla limit is 64)");
            }
	                              
	    linespeciallist[numlinespecials] = &lines[i];
	    numlinespecials++;
	    break;
	}
    }

    
                            
    for (i = 0;i < MAXCEILINGS;i++)
	activeceilings[i] = NULL;

    for (i = 0;i < MAXPLATS;i++)
	activeplats[i] = NULL;
    
    for (i = 0;i < MAXBUTTONS;i++)
	memset(&buttonlist[i],0,sizeof(button_t));

                                    
                                 
}
