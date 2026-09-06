#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

static size_t length(const char *s) { size_t n = 0; while (s && s[n]) ++n; return n; }
static void text(const char *s) { (void)write(1, s, length(s)); }
static void number(uint64_t value) { char b[21]; size_t n=0; do { b[n++]=(char)('0'+value%10u); value/=10u; } while(value); while(n) { char c=b[--n]; (void)write(1,&c,1); } }
int program_main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc < 2) { text("printf: expected format\n"); return 2; }
    const char *format=argv[1]; int arg=2;
    for (size_t i=0; format[i]; ++i) {
        if (format[i]!='%') { char c=format[i]; (void)write(1,&c,1); continue; }
        ++i; if (!format[i]) break;
        if (format[i]=='%') { text("%"); }
        else if (format[i]=='s') { if (arg>=argc) return 2; text(argv[arg++]); }
        else if (format[i]=='d') { if (arg>=argc) return 2; uint64_t v=0; const char *s=argv[arg++]; for(size_t j=0;s[j]>='0'&&s[j]<='9';++j)v=v*10u+(uint64_t)(s[j]-'0'); number(v); }
        else { text("printf: unsupported format\n"); return 2; }
    }
    return 0;
}
