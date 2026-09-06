#include "unistd.h"
#include <stddef.h>
static size_t length(const char *s){size_t n=0;while(s&&s[n])++n;return n;}
static void out(const char *s){(void)write(2,s,length(s));}
int program_main(int argc,char **argv,char **envp){(void)envp;if(argc!=2){out("rm: expected path\n");return 2;}if(unlink(argv[1])!=0){out("rm: failed\n");return 1;}return 0;}
