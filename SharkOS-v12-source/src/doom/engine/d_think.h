  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                                                           
                                                            
                                                         
  


#ifndef __D_THINK__
#define __D_THINK__





  
                      
                                           
                                      
                             
  
typedef  void (*actionf_v)();
typedef  void (*actionf_p1)( void* );
typedef  void (*actionf_p2)( void*, void* );

typedef union
{
  actionf_v	acv;
  actionf_p1	acp1;
  actionf_p2	acp2;

} actionf_t;





                                         
                                           
             
typedef actionf_t  think_t;


                                
typedef struct thinker_s
{
    struct thinker_s*	prev;
    struct thinker_s*	next;
    think_t		function;
    
} thinker_t;



#endif
