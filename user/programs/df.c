#include "unistd.h"
#include <sys/statfs.h>
#include <stddef.h>
#include <stdint.h>
static size_t len(const char*s){size_t n=0;while(s&&s[n])++n;return n;}
static void put(const char*s){(void)write(1,s,len(s));}
static void num(uint64_t v){char b[21];size_t n=0;do{b[n++]=(char)('0'+v%10u);v/=10u;}while(v);while(n){char c=b[--n];(void)write(1,&c,1);}}
static void human(uint64_t bytes){
    const char*u="B";uint64_t v=bytes;
    if(bytes>=1024ULL*1024ULL*1024ULL){v=bytes/(1024ULL*1024ULL*1024ULL);u="G";}
    else if(bytes>=1024ULL*1024ULL){v=bytes/(1024ULL*1024ULL);u="M";}
    else if(bytes>=1024ULL){v=bytes/1024ULL;u="K";}
    num(v);put(u);
}
static int show(const char*path,int h){
    rix_statfs_t st;
    if(statfs(path,&st)!=0){(void)write(2,"df: failed\n",11);return 1;}
    if(st.version!=RIX_STATFS_VERSION||st.struct_size!=sizeof(st)){ (void)write(2,"df: bad statfs\n",14);return 1;}
    put("Filesystem 1K-blocks Used Available Use% Mounted on\n");
    put("rixfs ");
    uint64_t total_bytes=(uint64_t)st.total_blocks*(uint64_t)st.block_size;
    uint64_t free_bytes=(uint64_t)st.free_blocks*(uint64_t)st.block_size;
    uint64_t used_bytes=total_bytes>free_bytes?total_bytes-free_bytes:0;
    uint64_t total1k=total_bytes/1024u;
    uint64_t free1k=free_bytes/1024u;
    uint64_t used1k=used_bytes/1024u;
    uint64_t pct=total_bytes?used_bytes*100u/total_bytes:0;
    if(h){human(total_bytes);put(" ");}
    else num(total1k);
    put(" ");
    if(h){human(used_bytes);put(" ");}
    else num(used1k);
    put(" ");
    if(h){human(free_bytes);put(" ");}
    else num(free1k);
    put(" ");num(pct);put("% ");put(path);put("\n");
    return 0;
}
int program_main(int argc,char**argv,char**envp){
    (void)envp;
    int h=0;const char*path="/";
    for(int i=1;i<argc;i++){
        const char*a=argv[i];
        if(a[0]=='-'&&a[1]=='h'&&!a[2])h=1;
        else if(a[0]=='-'){ (void)write(2,"df: bad option\n",15);return 2;}
        else path=a;
    }
    return show(path,h);
}
