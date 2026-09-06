#include "unistd.h"
#include <stddef.h>
int program_main(int argc,char **argv,char **envp){(void)argv;(void)envp;if(argc!=1){(void)write(2,"id: arguments unsupported\n",26);return 2;}return write(1,"uid=0 gid=0\n",12)==12?0:1;}
int main(int argc,char **argv,char **envp){return program_main(argc,argv,envp);}
