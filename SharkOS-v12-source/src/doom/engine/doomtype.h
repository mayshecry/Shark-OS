  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                                                         
                       
      


#ifndef __DOOMTYPE__
#define __DOOMTYPE__

                                                          
                                                       


#ifdef _MSC_VER

#define strcasecmp _stricmp
#define strncasecmp _strnicmp

#else

#include <strings.h>
#include <stdbool.h>
#endif


  
                                                                        
                                                                          
                                                                      
                                                                          
                                                                          
           
  

#ifdef __GNUC__
#define PACKEDATTR __attribute__((packed))
#else
#define PACKEDATTR
#endif

                                                                  
                                                               

                                                                     
                                                                
                                                                
                                                              

#include <inttypes.h>

#if defined(__cplusplus) || defined(__bool_true_false_are_defined)

                                                                                 
typedef unsigned int boolean;

#else

#ifndef __bool_true_false_are_defined 
typedef enum 
{
    false	= 0,
    true	= 1,
	undef	= 0xFFFFFFFF
} boolean;
#endif

#endif

typedef uint8_t byte;

#include <limits.h>

#if defined(_WIN32) || defined(__DJGPP__)

#define DIR_SEPARATOR '\\'
#define DIR_SEPARATOR_S "\\"
#define PATH_SEPARATOR ';'

#else

#define DIR_SEPARATOR '/'
#define DIR_SEPARATOR_S "/"
#define PATH_SEPARATOR ':'

#endif

#define arrlen(array) (sizeof(array) / sizeof(*array))

#endif

