#include "unistd.h"
#include <stddef.h>
static size_t len(const char *s){size_t n=0;while(s&&s[n])++n;return n;}
static void err(const char *s){(void)write(2,s,len(s));}
int program_main(int argc,char **argv,char **envp){(void)envp;if(argc!=2){err("tee: expected one file\n");return 2;}int fd=openat(-100,argv[1],1u|4u|16u,0644u);if(fd<0){err("tee: open failed\n");return 1;}char b[256];int status=0;for(;;){rix_ssize_t n=read(0,b,sizeof(b));if(n<0){status=1;break;}if(n==0)break;size_t done=0;while(done<(size_t)n){rix_ssize_t w=write(1,b+done,(size_t)n-done);if(w<=0){status=1;break;}done+=(size_t)w;}done=0;while(done<(size_t)n){rix_ssize_t w=write(fd,b+done,(size_t)n-done);if(w<=0){status=1;break;}done+=(size_t)w;}if(status)break;}if(close(fd)!=0)status=1;return status;}
int main(int argc,char **argv,char **envp){return program_main(argc,argv,envp);}
