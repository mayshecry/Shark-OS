#ifndef SHARKSCRIPT_H
#define SHARKSCRIPT_H


void shs_run_script(const char* script_content, const char* filename);
void shs_run_file(const char* path);
void shs_init_engine(void);

#endif