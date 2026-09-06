#include "unistd.h"
#include <stddef.h>
static size_t len(const char *s){size_t n=0;while(s&&s[n])++n;return n;}
int program_main(int argc,char **argv,char **envp){(void)envp;if(argc!=2){(void)write(2,"dirname: expected path\n",23);return 2;}const char *p=argv[1];size_t n=len(p);while(n>1&&p[n-1]=='/')--n;size_t slash=n;while(slash>0&&p[slash-1]!='/')--slash;if(slash==0){(void)write(1,".\n",2);return 0;}while(slash>1&&p[slash-1]=='/')--slash;if(write(1,p,slash)!= (rix_ssize_t)slash)return 1;return write(1,"\n",1)==1?0:1;}
int main(int argc,char **argv,char **envp){return program_main(argc,argv,envp);}
