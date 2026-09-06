#include "unistd.h"
#include <stddef.h>
#include <stdint.h>
static void num(uint64_t v){char b[24];size_t n=0;if(!v){(void)write(1,"0",1);return;}while(v){b[n++]=(char)('0'+v%10);v/=10;}while(n)(void)write(1,&b[--n],1);}
static int leap(uint64_t y){return (y%4==0&&y%100!=0)||y%400==0;}
int program_main(int argc,char **argv,char **envp){(void)argv;(void)envp;if(argc!=1){(void)write(2,"date: arguments unsupported\n",28);return 2;}rix_timespec_t t;if(clock_gettime(&t)!=0)return 1;uint64_t days=t.sec/86400ULL,rem=t.sec%86400ULL,y=1970;while(days>=(uint64_t)(leap(y)?366:365)){days-=leap(y)?366:365;++y;}uint64_t month=1;static const uint8_t md[]={31,28,31,30,31,30,31,31,30,31,30,31};while(month<=12){uint64_t d=md[month-1]+(month==2&&leap(y));if(days<d)break;days-=d;++month;}num(y);write(1,"-",1);if(month<10)write(1,"0",1);num(month);write(1,"-",1);if(days+1<10)write(1,"0",1);num(days+1);write(1," ",1);uint64_t h=rem/3600;rem%=3600;uint64_t m=rem/60,s=rem%60;if(h<10)write(1,"0",1);num(h);write(1,":",1);if(m<10)write(1,"0",1);num(m);write(1,":",1);if(s<10)write(1,"0",1);num(s);return write(1,"\n",1)==1?0:1;}
int main(int argc,char **argv,char **envp){return program_main(argc,argv,envp);}
