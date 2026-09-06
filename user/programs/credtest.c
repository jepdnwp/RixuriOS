#include "unistd.h"
#include <stdint.h>
static int outnum(uint32_t value){char b[16];size_t n=0;if(!value)b[n++]='0';while(value){b[n++]=(char)('0'+value%10u);value/=10u;}for(size_t i=n;i-- > 0;)if(write(1,&b[i],1)!=1)return -1;return 0;}
int program_main(int argc,char **argv,char **envp){(void)argv;(void)envp;uint32_t groups[2]={1000,2000};if(argc!=1)return 2;if(write(1,"before uid=",11)!=11||outnum(getuid())!=0||write(1," gid=",5)!=5||outnum(getgid())!=0||write(1,"\n",1)!=1)return 1;if(setgroups(2,groups)!=0||setgid(1000)!=0||setuid(1000)!=0)return 1;if(write(1,"after uid=",10)!=10||outnum(getuid())!=0||write(1," gid=",5)!=5||outnum(getgid())!=0||write(1,"\n",1)!=1)return 1;if(getgroups(0,(uint32_t*)0)!=2||setgroups(0,(const uint32_t*)0)==0)return 1;if(mkdir("/phase20-denied",0755)==0)return 1;if(getuid()!=1000||getgid()!=1000)return 1;return 0;}
int main(int argc,char **argv,char **envp){return program_main(argc,argv,envp);}
