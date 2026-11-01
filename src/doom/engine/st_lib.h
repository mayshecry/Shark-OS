  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                               
  

#ifndef __STLIB__
#define __STLIB__


                               
#include "r_defs.h"

  
                      
  

                

typedef struct
{
                              
                                       
    int		x;
    int		y;

                                
    int width;    

                        
    int		oldnum;
    
                               
    int*	num;

                                 
                                
    boolean*	on;

                              
    patch_t**	p;

                
    int data;
    
} st_number_t;



                                            
                                                  
typedef struct
{
                         
    st_number_t		n;

                           
    patch_t*		p;
    
} st_percent_t;



                       
typedef struct
{
                                          
    int			x;
    int			y;

                       
    int			oldinum;

                              
    int*		inum;

                                 
                              
    boolean*		on;

                    
    patch_t**		p;
    
                
    int			data;
    
} st_multicon_t;




                     

typedef struct
{
                                        
    int			x;
    int			y;

                      
    boolean		oldval;

                                     
    boolean*		val;

                         
                                      
    boolean*		on;  


    patch_t*		p;	       
    int			data;               
    
} st_binicon_t;



  
                                               
  

                              
                                      
                                           
  
void STlib_init(void);



                         
void
STlib_initNum
( st_number_t*		n,
  int			x,
  int			y,
  patch_t**		pl,
  int*			num,
  boolean*		on,
  int			width );

void
STlib_updateNum
( st_number_t*		n,
  boolean		refresh );


                          
void
STlib_initPercent
( st_percent_t*		p,
  int			x,
  int			y,
  patch_t**		pl,
  int*			num,
  boolean*		on,
  patch_t*		percent );


void
STlib_updatePercent
( st_percent_t*		per,
  int			refresh );


                                
void
STlib_initMultIcon
( st_multicon_t*	mi,
  int			x,
  int			y,
  patch_t**		il,
  int*			inum,
  boolean*		on );


void
STlib_updateMultIcon
( st_multicon_t*	mi,
  boolean		refresh );

                              

void
STlib_initBinIcon
( st_binicon_t*		b,
  int			x,
  int			y,
  patch_t*		i,
  boolean*		val,
  boolean*		on );

void
STlib_updateBinIcon
( st_binicon_t*		bi,
  boolean		refresh );

#endif
