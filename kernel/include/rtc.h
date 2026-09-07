/* =============================================================================
 * ZeruX OS — RTC (Real Time Clock) & CMOS Driver Header
 * File: kernel/include/rtc.h
 * =============================================================================
 */

#ifndef RTC_H
#define RTC_H

#include <stdint.h>

#define CMOS_ADDRESS 0x70
#define CMOS_DATA    0x71

typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint32_t year;
} rtc_time_t;

void rtc_init(void);
void rtc_read_time(rtc_time_t *time);
void rtc_print_time(void);

#endif /* RTC_H */
