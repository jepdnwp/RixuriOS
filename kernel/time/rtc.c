#include "rtc.h"
#include <stdint.h>

#define CMOS_INDEX 0x70u
#define CMOS_DATA  0x71u
#define RTC_SECONDS 0x00u
#define RTC_MINUTES 0x02u
#define RTC_HOURS   0x04u
#define RTC_DAY     0x07u
#define RTC_MONTH   0x08u
#define RTC_YEAR    0x09u
#define RTC_STATUS_A 0x0Au
#define RTC_STATUS_B 0x0Bu
#define RTC_UIP 0x80u
#define RTC_BINARY 0x04u
#define RTC_24H 0x02u

static inline void outb(uint16_t port,uint8_t value){__asm__ volatile("outb %0,%1"::"a"(value),"Nd"(port));}
static inline uint8_t inb(uint16_t port){uint8_t v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"(port));return v;}
static uint8_t cmos_read(uint8_t reg){outb(CMOS_INDEX,(uint8_t)(reg|0x80u));return inb(CMOS_DATA);}
static uint8_t bcd_to_bin(uint8_t v){return (uint8_t)((v&0x0Fu)+((v>>4)*10u));}
static uint8_t leap(uint16_t y){return (uint8_t)((y%4u==0u&&y%100u!=0u)||y%400u==0u);}
static int valid(const rix_rtc_time_t*t){if(!t||t->year<1970||t->month<1||t->month>12||t->day<1||t->day>31||t->hour>23||t->minute>59||t->second>59)return -1;return 0;}
int rtc_init(void){return 0;}
int rtc_read(rix_rtc_time_t*out){
 if(!out)return -1;
 for(unsigned attempt=0;attempt<8;attempt++){
  while(cmos_read(RTC_STATUS_A)&RTC_UIP){}
  uint8_t status=cmos_read(RTC_STATUS_B),sec1=cmos_read(RTC_SECONDS),min1=cmos_read(RTC_MINUTES),hour1=cmos_read(RTC_HOURS),day1=cmos_read(RTC_DAY),mon1=cmos_read(RTC_MONTH),year1=cmos_read(RTC_YEAR);
  while(cmos_read(RTC_STATUS_A)&RTC_UIP){}
  uint8_t sec2=cmos_read(RTC_SECONDS),min2=cmos_read(RTC_MINUTES),hour2=cmos_read(RTC_HOURS),day2=cmos_read(RTC_DAY),mon2=cmos_read(RTC_MONTH),year2=cmos_read(RTC_YEAR);
  if(sec1!=sec2||min1!=min2||hour1!=hour2||day1!=day2||mon1!=mon2||year1!=year2)continue;
  uint8_t hour=hour1;
  if(!(status&RTC_BINARY)){sec1=bcd_to_bin(sec1);min1=bcd_to_bin(min1);day1=bcd_to_bin(day1);mon1=bcd_to_bin(mon1);year1=bcd_to_bin(year1);hour=(uint8_t)(bcd_to_bin((uint8_t)(hour1&0x7Fu))|(hour1&0x80u));}
  else hour=hour1;
  if(!(status&RTC_24H)){uint8_t pm=(uint8_t)(hour&0x80u);hour&=0x7Fu;if(pm&&hour<12)hour=(uint8_t)(hour+12u);if(!pm&&hour==12)hour=0;}
  rix_rtc_time_t t={.year=(uint16_t)(2000u+year1),.month=mon1,.day=day1,.hour=hour,.minute=min1,.second=sec1};
  if(valid(&t)!=0)continue;*out=t;return 0;
 }
 return -2;
}
uint64_t rtc_unix_seconds(void){
 rix_rtc_time_t t;if(rtc_read(&t)!=0)return 0;uint64_t days=0;for(uint16_t y=1970;y<t.year;y++)days+=leap(y)?366u:365u;static const uint8_t md[]={31,28,31,30,31,30,31,31,30,31,30,31};for(uint8_t m=1;m<t.month;m++)days+=(uint64_t)md[m-1]+((m==2&&leap(t.year))?1u:0u);days+=(uint64_t)t.day-1u;return days*86400ULL+(uint64_t)t.hour*3600ULL+(uint64_t)t.minute*60ULL+t.second;
}
