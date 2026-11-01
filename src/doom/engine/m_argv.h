  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
        
      


#ifndef __M_ARGV__
#define __M_ARGV__

#include "doomtype.h"

  
       
  
extern  int	myargc;
extern  char**	myargv;

                                              
                                    
int M_CheckParm (char* check);

                                                                        
                                    
int M_CheckParmWithArgs(char *check, int num_args);

void M_FindResponseFile(void);

                                

boolean M_ParmExists(char *check);

                                                   

char *M_GetExecutableName(void);

#endif
