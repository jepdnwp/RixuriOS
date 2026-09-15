#include "unistd.h"
#include <sys/sysinfo.h>
#include <stddef.h>
#include <stdint.h>
static void put(const char*s){size_t n=0;while(s&&s[n])++n;(void)write(1,s,n);}
static void num(uint64_t v){char b[21];size_t n=0;do{b[n++]=(char)('0'+v%10u);v/=10u;}while(v);while(n){char c=b[--n];(void)write(1,&c,1);}}
int program_main(int argc,char**argv,char**envp){
    (void)argc;(void)argv;(void)envp;
    rix_sysinfo_t st;
    if(sysinfo(&st)!=0){(void)write(2,"free: failed\n",13);return 1;}
    if(st.version!=RIX_SYSINFO_VERSION||st.struct_size!=sizeof(st)||!st.page_size){(void)write(2,"free: bad sysinfo\n",17);return 1;}
    uint64_t total_kb=(uint64_t)st.total_pages*(st.page_size/1024u);
    uint64_t free_kb=(uint64_t)st.free_pages*(st.page_size/1024u);
    uint64_t used_kb=total_kb>free_kb?total_kb-free_kb:0;
    put("MemTotal: ");num(total_kb);put(" kB\n");
    put("MemFree: ");num(free_kb);put(" kB\n");
    put("MemUsed: ");num(used_kb);put(" kB\n");
    put("Reserved: ");num(st.reserved_pages);put(" pages\n");
    put("Uptime: ");num(st.uptime_sec);put(" s\n");
    return 0;
}
