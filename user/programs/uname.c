#include "unistd.h"
#include <stddef.h>
static size_t len(const char*s){size_t n=0;while(s&&s[n])++n;return n;}
int program_main(int argc,char**argv,char**envp){(void)argv;(void)envp;if(argc!=1){(void)write(2,"uname: arguments unsupported\n",29);return 2;}return write(1,"RixuriOS\n",len("RixuriOS\n"))==9?0:1;}
