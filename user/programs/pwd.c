#include "unistd.h"
#include <stddef.h>
static size_t length(const char *s){size_t n=0;while(s&&s[n])++n;return n;}
int program_main(int argc,char **argv,char **envp){(void)argv;(void)envp;if(argc!=1){(void)write(2,"pwd: arguments unsupported\n",27);return 2;}return write(1,"/\n",length("/\n"))==2?0:1;}
