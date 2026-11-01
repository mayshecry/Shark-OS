  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                           
  



#include <string.h>

#include "doomtype.h"
#include "m_cheat.h"

  
                         
  

  
                                                      
                                                        
  
int
cht_CheckCheat
( cheatseq_t*	cht,
  char		key )
{
                                                                    
                                                       

    if (cht->parameter_chars > 0 && strlen(cht->sequence) < cht->sequence_len)
        return false;
    
    if (cht->chars_read < strlen(cht->sequence))
    {
                                                       
                                                       
                            

        if (key == cht->sequence[cht->chars_read])
            ++cht->chars_read;
        else
            cht->chars_read = 0;
        
        cht->param_chars_read = 0;
    }
    else if (cht->param_chars_read < cht->parameter_chars)
    {
                                                                
                                   
        
        cht->parameter_buf[cht->param_chars_read] = key;
        
        ++cht->param_chars_read;
    }

    if (cht->chars_read >= strlen(cht->sequence)
     && cht->param_chars_read >= cht->parameter_chars)
    {
        cht->chars_read = cht->param_chars_read = 0;

        return true;
    }
    
                            

    return false;
}

void
cht_GetParam
( cheatseq_t*	cht,
  char*		buffer )
{
    memcpy(buffer, cht->parameter_buf, cht->parameter_chars);
}


