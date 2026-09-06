#pragma once
#include <stdint.h>

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} rix_rtc_time_t;

int rtc_init(void);
int rtc_read(rix_rtc_time_t *out);
uint64_t rtc_unix_seconds(void);
