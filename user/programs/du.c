#include "unistd.h"
#include <stddef.h>
#include <stdint.h>
static size_t len(const char*s){size_t n=0;while(s&&s[n])++n;return n;}
static void num(uint64_t v){char b[21];size_t n=0;do{b[n++]=(char)('0'+v%10u);v/=10u;}while(v);while(n){char c=b[--n];(void)write(1,&c,1);}}
int program_main(int argc,char**argv,char**envp){(void)envp;if(argc!=2){(void)write(2,"du: expected path\n",18);return 2;}rix_stat_t st;if(stat(argv[1],&st)!=0){(void)write(2,"du: failed\n",12);return 1;}num((st.size+511u)/512u);(void)write(1,"\t",1);(void)write(1,argv[1],len(argv[1]));(void)write(1,"\n",1);return 0;}
