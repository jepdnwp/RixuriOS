#include "unistd.h"
#include <stddef.h>

static size_t length(const char *s){size_t n=0;while(s&&s[n])++n;return n;}
static void out(const char *s){(void)write(1,s,length(s));}

int program_main(int argc,char **argv,char **envp){
    (void)argc; (void)argv; (void)envp;
    int passed = 1;
    if (openat(-100,(const char *)(uintptr_t)1u,0,0) >= 0) passed = 0;
    int fd = openat(-100,"/",0,0);
    if (fd < 0) passed = 0;
    size_t count = 0;
    if (fd >= 0) {
        if (getdents(fd,(rix_dirent_t *)(uintptr_t)1u,1u,&count) >= 0) passed = 0;
        (void)close(fd);
    }
    if (nanosleep((const rix_timespec_t *)(uintptr_t)1u,NULL) >= 0) passed = 0;
    out(passed ? "negative_abi=PASS\n" : "negative_abi=FAIL\n");
    return passed ? 0 : 1;
}
