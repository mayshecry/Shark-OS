  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                                                                 
  


#ifndef V_PATCH_H
#define V_PATCH_H

           
                                     
                                                        
                                                    
              

typedef struct 
{ 
    short		width;		                     
    short		height; 
    short		leftoffset;	                                
    short		topoffset;	                           
    int			columnofs[8];	                    
                                    
} PACKEDATTR patch_t;

                                             
typedef struct
{
    byte		topdelta;	                                  
    byte		length; 	                            
} PACKEDATTR post_t;

                                                              
typedef post_t	column_t;

#endif 

