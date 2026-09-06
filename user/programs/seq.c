#include "unistd.h"
#include <stddef.h>
#include <stdint.h>
static void outnum(int64_t v){char b[32];size_t n=0;if(v==0){(void)write(1,"0",1);return;}if(v<0){(void)write(1,"-",1);v=-v;}while(v){b[n++]=(char)('0'+v%10);v/=10;}while(n)(void)write(1,&b[--n],1);}
static int parse(const char *s,int64_t *v){if(!s||!*s||!v)return -1;int neg=0;size_t i=0;if(s[0]=='-'){neg=1;i=1;}if(!s[i])return -1;int64_t x=0;for(;s[i];++i){if(s[i]<'0'||s[i]>'9'||x>(INT64_MAX-(s[i]-'0'))/10)return -1;x=x*10+(s[i]-'0');}*v=neg?-x:x;return 0;}
int program_main(int argc,char **argv,char **envp){(void)envp;if(argc<2||argc>4){(void)write(2,"seq: expected 1-3 integers\n",28);return 2;}int64_t first=1,last=0,step=1;if(argc==2){if(parse(argv[1],&last))return 2;}else {if(parse(argv[1],&first)||parse(argv[2],&last))return 2;if(argc==4&&parse(argv[3],&step))return 2;}if(!step||(step>0&&first>last)||(step<0&&first<last))return 0;size_t count=0;for(int64_t x=first;;x+=step){if(count++>=10000)return 1;outnum(x);if(write(1,"\n",1)!=1)return 1;if(x==last)break;if((step>0&&x>INT64_MAX-step)||(step<0&&x<INT64_MIN-step))return 1;}return 0;}
int main(int argc,char **argv,char **envp){return program_main(argc,argv,envp);}
