#include "unistd.h"
#include <stddef.h>
static size_t len(const char *s){size_t n=0;while(s&&s[n])++n;return n;}
static void out(const char *s){(void)write(1,s,len(s));}
int program_main(int argc,char **argv,char **envp){(void)envp;if(argc<2||argc>3){(void)write(2,"basename: expected path [suffix]\n",33);return 2;}const char *p=argv[1];size_t n=len(p);while(n&&p[n-1]=='/')--n;size_t start=n;while(start&&p[start-1]!='/')--start;size_t end=n;if(argc==3){size_t sl=len(argv[2]);if(sl<=end-start){size_t i=0;while(i<sl&&p[end-sl+i]==argv[2][i])++i;if(i==sl&&sl<end-start)end-=sl;}}char b[256];size_t m=end-start;if(m>=sizeof(b))return 1;for(size_t i=0;i<m;++i)b[i]=p[start+i];b[m]=0;out(b);out("\n");return 0;}
int main(int argc,char **argv,char **envp){return program_main(argc,argv,envp);}
