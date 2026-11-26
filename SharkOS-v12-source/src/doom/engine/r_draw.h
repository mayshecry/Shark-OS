  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                                   
  


#ifndef __R_DRAW__
#define __R_DRAW__




extern lighttable_t*	dc_colormap;
extern int		dc_x;
extern int		dc_yl;
extern int		dc_yh;
extern fixed_t		dc_iscale;
extern fixed_t		dc_texturemid;

                          
extern byte*		dc_source;		


                               
                                           
         
void 	R_DrawColumn (void);
void 	R_DrawColumnLow (void);

                                   
void 	R_DrawFuzzColumn (void);
void 	R_DrawFuzzColumnLow (void);

                                      
                                
                                 
void	R_DrawTranslatedColumn (void);
void	R_DrawTranslatedColumnLow (void);

void
R_VideoErase
( unsigned	ofs,
  int		count );

extern int		ds_y;
extern int		ds_x1;
extern int		ds_x2;

extern lighttable_t*	ds_colormap;

extern fixed_t		ds_xfrac;
extern fixed_t		ds_yfrac;
extern fixed_t		ds_xstep;
extern fixed_t		ds_ystep;

                              
extern byte*		ds_source;		

extern byte*		translationtables;
extern byte*		dc_translation;


                                         
                            
void 	R_DrawSpan (void);

                                
void 	R_DrawSpanLow (void);


void
R_InitBuffer
( int		width,
  int		height );


                                       
                             
void	R_InitTranslationTables (void);



                      
void R_FillBackScreen (void);

                                                                 
void R_DrawViewBorder (void);



#endif
