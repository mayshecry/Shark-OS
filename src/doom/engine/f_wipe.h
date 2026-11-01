  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                                                   
   


#ifndef __F_WIPE_H__
#define __F_WIPE_H__

  
                                            
  

enum
{
                                                 
    wipe_ColorXForm,
    
                        
    wipe_Melt,	

    wipe_NUMWIPES
};

int
wipe_StartScreen
( int		x,
  int		y,
  int		width,
  int		height );


int
wipe_EndScreen
( int		x,
  int		y,
  int		width,
  int		height );


int
wipe_ScreenWipe
( int		wipeno,
  int		x,
  int		y,
  int		width,
  int		height,
  int		ticks );

#endif
