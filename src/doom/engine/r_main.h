  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                                   
  


#ifndef __R_MAIN__
#define __R_MAIN__

#include "d_player.h"
#include "r_data.h"




  
               
  
extern fixed_t		viewcos;
extern fixed_t		viewsin;

extern int		viewwindowx;
extern int		viewwindowy;



extern int		centerx;
extern int		centery;

extern fixed_t		centerxfrac;
extern fixed_t		centeryfrac;
extern fixed_t		projection;

extern int		validcount;

extern int		linecount;
extern int		loopcount;


  
                
                                         
                                                       
  

                      
                              
#define LIGHTLEVELS	        16
#define LIGHTSEGSHIFT	         4

#define MAXLIGHTSCALE		48
#define LIGHTSCALESHIFT		12
#define MAXLIGHTZ	       128
#define LIGHTZSHIFT		20

extern lighttable_t*	scalelight[LIGHTLEVELS][MAXLIGHTSCALE];
extern lighttable_t*	scalelightfixed[MAXLIGHTSCALE];
extern lighttable_t*	zlight[LIGHTLEVELS][MAXLIGHTZ];

extern int		extralight;
extern lighttable_t*	fixedcolormap;


                                           
                                                  
#define NUMCOLORMAPS		32


                          
                
                     
extern	int		detailshift;	


  
                                                         
                                  
  
extern void		(*colfunc) (void);
extern void		(*transcolfunc) (void);
extern void		(*basecolfunc) (void);
extern void		(*fuzzcolfunc) (void);
                               
extern void		(*spanfunc) (void);


  
                     
int
R_PointOnSide
( fixed_t	x,
  fixed_t	y,
  node_t*	node );

int
R_PointOnSegSide
( fixed_t	x,
  fixed_t	y,
  seg_t*	line );

angle_t
R_PointToAngle
( fixed_t	x,
  fixed_t	y );

angle_t
R_PointToAngle2
( fixed_t	x1,
  fixed_t	y1,
  fixed_t	x2,
  fixed_t	y2 );

fixed_t
R_PointToDist
( fixed_t	x,
  fixed_t	y );


fixed_t R_ScaleFromGlobalAngle (angle_t visangle);

subsector_t*
R_PointInSubsector
( fixed_t	x,
  fixed_t	y );

void
R_AddPointToBox
( int		x,
  int		y,
  fixed_t*	box );



  
                                            
  

                      
void R_RenderPlayerView (player_t *player);

                          
void R_Init (void);

                         
void R_SetViewSize (int blocks, int detail);

#endif
