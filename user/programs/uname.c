#include "unistd.h"
#include <stddef.h>
static size_t len(const char*s){size_t n=0;while(s&&s[n])++n;return n;}
static int is_match(const char *a, const char *b) {
    size_t i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i] && a[i] == b[i]) ++i;
    return a[i] == 0 && b[i] == 0;
}
int program_main(int argc,char**argv,char**envp){(void)envp;if(argc==1){return write(1,"RixuriOS\n",len("RixuriOS\n"))==9?0:1;}if(argc==2&&(is_match(argv[1],"-a")||is_match(argv[1],"-s")||is_match(argv[1],"-n")||is_match(argv[1],"-m"))){const char*text=is_match(argv[1],"-a")?"RixuriOS rixurios x86_64 1\n":is_match(argv[1],"-n")?"rixurios\n":is_match(argv[1],"-m")?"x86_64\n":"RixuriOS\n";return write(1,text,len(text))==(rix_ssize_t)len(text)?0:1;}(void)write(2,"uname: arguments unsupported\n",29);return 2;}