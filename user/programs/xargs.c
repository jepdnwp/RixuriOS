#include "unistd.h"
#include <stddef.h>
#include <stdint.h>
#define ARG_CAP 16
#define WORD_CAP 128u
static size_t len(const char*s){size_t n=0;while(s&&s[n])++n;return n;}
static void out(const char*s){(void)write(2,s,len(s));}
static int run(char **base,int count,char words[ARG_CAP][WORD_CAP],int word_count){char *av[ARG_CAP+1];char *envp[1]={0};for(int i=0;i<count;++i)av[i]=base[i];for(int i=0;i<word_count;++i)av[count+i]=words[i];av[count+word_count]=0;rix_pid_t p=fork();if(p==(rix_pid_t)-1)return 1;if(p==0){(void)execve(av[0],av,envp);_exit(127);}uint64_t st=1;if(wait(p,&st)==(rix_pid_t)-1)return 1;return st?1:0;}
int program_main(int argc,char**argv,char**envp){(void)envp;if(argc<2||argc>ARG_CAP){out("xargs: usage xargs COMMAND\n");return 2;}char *base[ARG_CAP];for(int i=1;i<argc;++i)base[i-1]=argv[i];int bc=argc-1,wc=0,status=0;char words[ARG_CAP][WORD_CAP];size_t used=0;for(;;){char c;rix_ssize_t n=read(0,&c,1);if(n<0)return 1;if(n==0){if(used){if(wc>=ARG_CAP-bc)return 1;words[wc][used]=0;++wc;}break;}if(c==' '||c=='\n'||c=='\t'){if(used){if(wc>=ARG_CAP-bc)return 1;words[wc][used]=0;++wc;used=0;}}else if(used+1u<WORD_CAP)words[wc][used++]=c;else return 1;}if(wc==0){words[0][0]=0;wc=1;}if(run(base,bc,words,wc)!=0)status=1;return status;}
