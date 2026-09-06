#include "unistd.h"
#include <stddef.h>
static size_t len(const char*s){size_t n=0;while(s&&s[n])++n;return n;}
static void num(rix_pid_t v){char b[21];size_t n=0;do{b[n++]=(char)('0'+v%10u);v/=10u;}while(v);while(n){char c=b[--n];(void)write(1,&c,1);}}
int program_main(int argc,char**argv,char**envp){(void)argv;(void)envp;if(argc!=1){(void)write(2,"ps: arguments unsupported\n",26);return 2;}const char*h="PID\n";(void)write(1,h,len(h));num(getpid());(void)write(1,"\n",1);return 0;}
