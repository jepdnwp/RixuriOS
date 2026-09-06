#include "unistd.h"
#include <stddef.h>

static size_t length(const char *s){size_t n=0;while(s&&s[n])++n;return n;}
static int candidate(const char *dir,const char *name,char *out,size_t cap){size_t n=0;while(dir[n]&&n+1<cap){out[n]=dir[n];++n;}if(n+1<cap&&n&&out[n-1]!='/')out[n++]='/';size_t i=0;while(name[i]&&n+1<cap)out[n++]=name[i++];if(name[i]||n>=cap)return 1;out[n]=0;return 0;}
int program_main(int argc,char **argv,char **envp){(void)envp;if(argc!=2){(void)write(2,"which: expected command\n",24);return 2;}const char *dirs[]={"/bin","/usr/bin","/sbin","/usr/sbin"};char path[4096];for(size_t i=0;i<4;++i){if(!candidate(dirs[i],argv[1],path,sizeof(path))){rix_stat_t st;if(stat(path,&st)==0){(void)write(1,path,length(path));(void)write(1,"\n",1);return 0;}}}(void)write(2,"which: not found\n",17);return 1;}
