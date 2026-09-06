#include "unistd.h"
#include <stddef.h>
#include <stdint.h>
static int number(const char*s,rix_pid_t*out){rix_pid_t v=0;if(!s||!s[0])return 1;for(size_t i=0;s[i];++i){if(s[i]<'0'||s[i]>'9')return 1;v=v*10u+(rix_pid_t)(s[i]-'0');}*out=v;return 0;}
int program_main(int argc,char**argv,char**envp){(void)envp;if(argc<2||argc>3){(void)write(2,"kill: expected pid [signal]\n",28);return 2;}rix_pid_t pid,sig=15;if(number(argv[1],&pid)||(argc==3&&number(argv[2],&sig))||kill(pid,(uint32_t)sig)!=0){(void)write(2,"kill: failed\n",13);return 1;}return 0;}
