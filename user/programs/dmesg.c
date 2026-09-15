#include "unistd.h"
#include <sys/klog.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
static void put(const char*s){size_t n=0;while(s&&s[n])++n;(void)write(1,s,n);}
int program_main(int argc,char**argv,char**envp){
    (void)envp;
    for(int i=1;i<argc;i++){ (void)write(2,"dmesg: bad option\n",18); (void)argv; return 2; }
    static uint8_t buf[4096];
    uint64_t cursor=0,next=0;
    for(;;){
        rix_ssize_t n=klog_read(buf,sizeof(buf),cursor,&next);
        if(n<0){ (void)write(2,"dmesg: failed\n",14); return 1; }
        if(n==0)break;
        size_t done=0;
        while(done<(size_t)n){
            rix_ssize_t w=write(1,buf+done,(size_t)n-done);
            if(w<=0){ (void)write(2,"dmesg: write failed\n",20); return 1; }
            done+=(size_t)w;
        }
        if(next<=cursor)break;
        cursor=next;
        if(n<(rix_ssize_t)sizeof(buf))break;
    }
    (void)put("");
    return 0;
}
