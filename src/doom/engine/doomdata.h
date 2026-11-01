  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                                     
                                                                    
                                                            
  

#ifndef __DOOMDATA__
#define __DOOMDATA__

                                            
#include "doomtype.h"

                                                
#include "doomdef.h"



  
                   
                                                             
                                      
  

                                                            
                                                    
enum
{
  ML_LABEL,		                                   
  ML_THINGS,		                    
  ML_LINEDEFS,		                         
  ML_SIDEDEFS,		                         
  ML_VERTEXES,		                                            
  ML_SEGS,		                                       
  ML_SSECTORS,		                               
  ML_NODES,		            
  ML_SECTORS,		                        
  ML_REJECT,		                                 
  ML_BLOCKMAP		                                           
};


                   
typedef struct
{
  short		x;
  short		y;
} PACKEDATTR mapvertex_t;


                                                       
                                   
typedef struct
{
  short		textureoffset;
  short		rowoffset;
  char		toptexture[8];
  char		bottomtexture[8];
  char		midtexture[8];
                                  
  short		sector;
} PACKEDATTR mapsidedef_t;



                                               
                      
typedef struct
{
  short		v1;
  short		v2;
  short		flags;
  short		special;
  short		tag;
                                       
  short		sidenum[2];		
} PACKEDATTR maplinedef_t;


  
                      
  

                         
#define ML_BLOCKING		1

                        
#define ML_BLOCKMONSTERS	2

                                      
                     
#define ML_TWOSIDED		4

                                                
                                              
                                                 
                                                  
                                  
                                                  
                                                    
                                                 

                         
#define ML_DONTPEGTOP		8

                         
#define ML_DONTPEGBOTTOM	16	

                                                     
#define ML_SECRET		32

                                                       
#define ML_SOUNDBLOCK		64

                                    
#define ML_DONTDRAW		128

                                              
#define ML_MAPPED		256




                                   
typedef	struct
{
  short		floorheight;
  short		ceilingheight;
  char		floorpic[8];
  char		ceilingpic[8];
  short		lightlevel;
  short		special;
  short		tag;
} PACKEDATTR mapsector_t;

                                  
typedef struct
{
  short		numsegs;
                                                      
  short		firstseg;	
} PACKEDATTR mapsubsector_t;


                                           
                                                 
typedef struct
{
  short		v1;
  short		v2;
  short		angle;		
  short		linedef;
  short		side;
  short		offset;
} PACKEDATTR mapseg_t;



                      

                   
#define	NF_SUBSECTOR	0x8000

typedef struct
{
                                            
  short		x;
  short		y;
  short		dx;
  short		dy;

                                 
                               
  short		bbox[2][4];

                                     
                                         
  unsigned short	children[2];

} PACKEDATTR mapnode_t;




                                                    
                                              
typedef struct
{
    short		x;
    short		y;
    short		angle;
    short		type;
    short		options;
} PACKEDATTR mapthing_t;





#endif			               
