#include "unistd.h"
#include <stddef.h>
int program_main(int argc,char **argv,char **envp){(void)argv;(void)envp;if(argc!=1){(void)write(2,"pwd: arguments unsupported\n",27);return 2;}char path[256];if(getcwd(path,sizeof(path))<0)return 1;size_t n=0;while(path[n])++n;if(write(1,path,n)!=(rix_ssize_t)n)return 1;return write(1,"\n",1)==1?0:1;}
