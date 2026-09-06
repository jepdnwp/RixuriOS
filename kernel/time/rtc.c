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

static inline void outb(uint16_t port, uint8_t value) { __asm__ volatile("outb %0,%1" :: "a"(value), "Nd"(port)); }
static inline uint8_t inb(uint16_t port) { uint8_t v; __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(port)); return v; }
static uint8_t cmos_read(uint8_t reg) { outb(CMOS_INDEX, (uint8_t)(reg | 0x80u)); return inb(CMOS_DATA); }
static uint8_t bcd_to_bin(uint8_t v) { return (uint8_t)((v & 0x0Fu) + ((v >> 4) * 10u)); }
static int valid(const rix_rtc_time_t *t) {
    if (!t || t->month < 1 || t->month > 12 || t->day < 1 || t->day > 31 || t->hour > 23 || t->minute > 59 || t->second > 59) return -1;
    return 0;
}
int rtc_init(void) {
    uint8_t b = cmos_read(RTC_STATUS_B);
    if (!(b & RTC_24H)) return -1;
    return 0;
}
int rtc_read(rix_rtc_time_t *out) {
    if (!out) return -1;
    rix_rtc_time_t a, b;
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        while (cmos_read(RTC_STATUS_A) & RTC_UIP) { }
        uint8_t status = cmos_read(RTC_STATUS_B);
        a.second = cmos_read(RTC_SECONDS); a.minute = cmos_read(RTC_MINUTES); a.hour = cmos_read(RTC_HOURS);
        a.day = cmos_read(RTC_DAY); a.month = cmos_read(RTC_MONTH); a.year = cmos_read(RTC_YEAR);
        while (cmos_read(RTC_STATUS_A) & RTC_UIP) { }
        b.second = cmos_read(RTC_SECONDS); b.minute = cmos_read(RTC_MINUTES); b.hour = cmos_read(RTC_HOURS);
        b.day = cmos_read(RTC_DAY); b.month = cmos_read(RTC_MONTH); b.year = cmos_read(RTC_YEAR);
        if (a.second != b.second || a.minute != b.minute || a.hour != b.hour || a.day != b.day || a.month != b.month || a.year != b.year) continue;
        if (!(status & RTC_BINARY)) { a.second=bcd_to_bin(a.second); a.minute=bcd_to_bin(a.minute); a.hour=(uint8_t)(bcd_to_bin((uint8_t)(a.hour & 0x7Fu)) | (a.hour & 0x80u)); a.day=bcd_to_bin(a.day); a.month=bcd_to_bin(a.month); a.year=bcd_to_bin(a.year); }
        if (a.hour & 0x80u) a.hour=(uint8_t)((a.hour & 0x7Fu)+12u);
        a.year=(uint16_t)(2000u+a.year);
        if (valid(&a)) return -2;
        *out=a; return 0;
    }
    return -3;
}
static uint8_t leap(uint16_t y) { return (uint8_t)((y%4u==0u && y%100u!=0u) || y%400u==0u); }
uint64_t rtc_unix_seconds(void) {
    rix_rtc_time_t t; if (rtc_read(&t) != 0 || t.year < 1970) return 0;
    uint64_t days=0; for (uint16_t y=1970; y<t.year; ++y) days += leap(y) ? 366u : 365u;
    static const uint16_t md[]={31,28,31,30,31,30,31,31,30,31,30,31};
    for (uint8_t m=1; m<t.month; ++m) days += md[m-1] + ((m==2 && leap(t.year)) ? 1u : 0u);
    days += (uint64_t)t.day - 1u;
    return days*86400ULL + (uint64_t)t.hour*3600ULL + (uint64_t)t.minute*60ULL + t.second;
}
