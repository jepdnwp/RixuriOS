#include "unistd.h"
#include <stddef.h>
int program_main(int argc,char **argv,char **envp){(void)argv;(void)envp;if(argc!=1){(void)write(2,"whoami: arguments unsupported\n",31);return 2;}return write(1,"root\n",5)==5?0:1;}
int main(int argc,char **argv,char **envp){return program_main(argc,argv,envp);}
