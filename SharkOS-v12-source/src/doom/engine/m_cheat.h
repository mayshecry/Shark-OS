  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                       
  


#ifndef __M_CHEAT__
#define __M_CHEAT__

  
                         
  

                    

#define CHEAT(value, parameters) \
    { value, sizeof(value) - 1, parameters, 0, 0, "" }

#define MAX_CHEAT_LEN 25
#define MAX_CHEAT_PARAMS 5

typedef struct
{
                              

    char sequence[MAX_CHEAT_LEN];
    size_t sequence_len;
    int parameter_chars;

                                 

    size_t chars_read;
    int param_chars_read;
    char parameter_buf[MAX_CHEAT_PARAMS];
} cheatseq_t;

int
cht_CheckCheat
( cheatseq_t*		cht,
  char			key );


void
cht_GetParam
( cheatseq_t*		cht,
  char*			buffer );


#endif
