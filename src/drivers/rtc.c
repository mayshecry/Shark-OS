
#include "kernel.h"


#define RTC_CMD_PORT 0x70
#define RTC_DATA_PORT 0x71


#define RTC_SECONDS 0x00
#define RTC_MINUTES 0x02
#define RTC_HOURS 0x04
#define RTC_DAY 0x07
#define RTC_MONTH 0x08
#define RTC_YEAR 0x09
#define RTC_STATUS_A 0x0A


uint32_t rtc_seconds = 0;
uint32_t rtc_minutes = 0;
uint32_t rtc_hours = 0;
uint32_t rtc_day = 1;
uint32_t rtc_month = 1;
uint32_t rtc_year = 2000;


static uint8_t rtc_read(uint8_t reg) {
    outb(RTC_CMD_PORT, reg);
    return inb(RTC_DATA_PORT);
}

static bool rtc_is_updating(void) {
    outb(RTC_CMD_PORT, RTC_STATUS_A);
    return inb(RTC_DATA_PORT) & 0x80;
}

static uint8_t bcd_to_bin(uint8_t val) {
    return (val & 0x0F) + ((val >> 4) * 10);
}


void rtc_read_time(void) {
    while (rtc_is_updating());

    uint8_t seconds = rtc_read(RTC_SECONDS);
    uint8_t minutes = rtc_read(RTC_MINUTES);
    uint8_t hours = rtc_read(RTC_HOURS);
    uint8_t day = rtc_read(RTC_DAY);
    uint8_t month = rtc_read(RTC_MONTH);
    uint8_t year = rtc_read(RTC_YEAR);
    
    
    uint8_t status_b = rtc_read(0x0B);
    bool bcd_mode = !(status_b & 0x04);
    
    
    if (bcd_mode) {
        seconds = bcd_to_bin(seconds);
        minutes = bcd_to_bin(minutes);
        hours = bcd_to_bin(hours & 0x7F);
        day = bcd_to_bin(day);
        month = bcd_to_bin(month);
        year = bcd_to_bin(year);
    }
    
    
    if (!(status_b & 0x02) && (hours & 0x80)) {
        hours = ((hours & 0x7F) + 12) % 24;
    }
    
    
    rtc_seconds = seconds;
    rtc_minutes = minutes;
    rtc_hours = hours;
    rtc_day = day;
    rtc_month = month;
    rtc_year = year + 2000;
}


void rtc_init(void) {
    rtc_read_time();
}