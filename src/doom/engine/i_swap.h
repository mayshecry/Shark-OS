  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                                                
  


#ifndef __I_SWAP__
#define __I_SWAP__

#ifdef __DJGPP__


#define SHORT(x)  ((signed short) (x))
#define LONG(x)   ((signed int) (x))

#define SYS_LITTLE_ENDIAN


#else              


#if ( __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ )
#define SYS_LITTLE_ENDIAN
#define SHORT(x)  ((signed short) (x))
#define LONG(x)   ((signed int) (x))
#elif ( __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__ )
#define SYS_BIG_ENDIAN

static inline unsigned short swapLE16(unsigned short val) {
	return ((val << 8) | (val >> 8));
}

static inline unsigned long swapLE32(unsigned long val) {
	return ((val << 24) | ((val << 8) & 0x00FF0000) | ((val >> 8) & 0x0000FF00) | (val >> 24));
}

#define SHORT(x)  ((signed short) swapLE16(x))
#define LONG(x)   ((signed int) swapLE32(x))
#else
#error "Unknown byte order"
#endif


                        
#define doom_swap_s(x) \
        ((short int)((((unsigned short int)(x) & 0x00ff) << 8) | \
                              (((unsigned short int)(x) & 0xff00) >> 8))) 

#if ( __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__ )
#define doom_wtohs(x) doom_swap_s(x)
#else
#define doom_wtohs(x) (short int)(x)
#endif


#endif              


#endif

