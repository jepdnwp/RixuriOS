#include "unistd.h"
#include <stddef.h>
#include <stdint.h>
#define PATH_CAP 256u
static size_t len(const char *s){size_t n=0;while(s&&s[n])++n;return n;}
static int eq(const char*a,const char*b){size_t i=0;while(a[i]&&b[i]&&a[i]==b[i])++i;return a[i]==b[i];}
static void out(const char*s){(void)write(1,s,len(s));}
static int join(const char*base,const char*name,char*outp){size_t a=len(base),b=len(name),p=0;if(a+b+2u>PATH_CAP)return -1;for(size_t i=0;i<a;++i)outp[p++]=base[i];if(p>1u&&outp[p-1u]=='/'){}else outp[p++]='/';for(size_t i=0;i<b;++i)outp[p++]=name[i];outp[p]=0;return 0;}
static int walk(const char*path,const char*name){rix_stat_t st;if(stat(path,&st)!=0)return 1;size_t n=len(path),start=n;while(start&&path[start-1u]!='/')--start;const char*base=path+start;if(!name||eq(name,"*")||eq(base,name)){out(path);out("\n");}if(st.type!=1u)return 0;int fd=openat(-100,path,0,0);if(fd<0)return 1;rix_dirent_t e[16];size_t count=0;int rc=0;if(getdents(fd,e,16,&count)!=0){(void)close(fd);return 1;}for(size_t i=0;i<count;++i){if(eq(e[i].name,".")||eq(e[i].name,".."))continue;char child[PATH_CAP];if(join(path,e[i].name,child)!=0){rc=1;continue;}if(walk(child,name)!=0)rc=1;}(void)close(fd);return rc;}
int program_main(int argc,char**argv,char**envp){(void)envp;if(argc<2||argc>3){out("find: usage find PATH [NAME]\n");return 2;}return walk(argv[1],argc==3?argv[2]:0);}
