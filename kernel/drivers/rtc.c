/* =============================================================================
 * ZeruX OS — RTC (Real Time Clock) & CMOS Driver
 * File: kernel/drivers/rtc.c
 * =============================================================================
 */

#include "rtc.h"
#include "ports.h"
#include "serial.h"

/* I/O Port Helpers using inline assembly */
static inline void rtc_outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t rtc_inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static int get_update_in_progress_flag(void) {
    rtc_outb(CMOS_ADDRESS, 0x0A);
    return (rtc_inb(CMOS_DATA) & 0x80);
}

static uint8_t get_rtc_register(int reg) {
    rtc_outb(CMOS_ADDRESS, reg);
    return rtc_inb(CMOS_DATA);
}

void rtc_read_time(rtc_time_t *time) {
    uint8_t last_second, last_minute, last_hour, last_day, last_month, last_year, last_century;
    uint8_t registerB;
    uint8_t century = 0;

    /* Wait until update is finished */
    while (get_update_in_progress_flag());
    
    time->second = get_rtc_register(0x00);
    time->minute = get_rtc_register(0x02);
    time->hour   = get_rtc_register(0x04);
    time->day    = get_rtc_register(0x07);
    time->month  = get_rtc_register(0x08);
    time->year   = get_rtc_register(0x09);
    century      = get_rtc_register(0x32); /* ACPI Century byte, might not be supported but we try */

    /* Read again until we get stable values */
    do {
        last_second  = time->second;
        last_minute  = time->minute;
        last_hour    = time->hour;
        last_day     = time->day;
        last_month   = time->month;
        last_year    = time->year;
        last_century = century;

        while (get_update_in_progress_flag());
        time->second = get_rtc_register(0x00);
        time->minute = get_rtc_register(0x02);
        time->hour   = get_rtc_register(0x04);
        time->day    = get_rtc_register(0x07);
        time->month  = get_rtc_register(0x08);
        time->year   = get_rtc_register(0x09);
        century      = get_rtc_register(0x32);

    } while ((last_second != time->second) || (last_minute != time->minute) || (last_hour != time->hour) ||
             (last_day != time->day) || (last_month != time->month) || (last_year != time->year) ||
             (last_century != century));

    registerB = get_rtc_register(0x0B);

    /* Convert BCD to binary values if necessary */
    if (!(registerB & 0x04)) {
        time->second = (time->second & 0x0F) + ((time->second / 16) * 10);
        time->minute = (time->minute & 0x0F) + ((time->minute / 16) * 10);
        time->hour   = ((time->hour & 0x0F) + (((time->hour & 0x70) / 16) * 10)) | (time->hour & 0x80);
        time->day    = (time->day & 0x0F) + ((time->day / 16) * 10);
        time->month  = (time->month & 0x0F) + ((time->month / 16) * 10);
        time->year   = (time->year & 0x0F) + ((time->year / 16) * 10);
        if(century != 0) {
            century = (century & 0x0F) + ((century / 16) * 10);
        }
    }

    /* Convert 12 hour clock to 24 hour clock if necessary */
    if (!(registerB & 0x02) && (time->hour & 0x80)) {
        time->hour = ((time->hour & 0x7F) + 12) % 24;
    }

    /* Calculate the full year */
    if (century != 0) {
        time->year += century * 100;
    } else {
        time->year += (time->year > 69) ? 1900 : 2000; /* simple heuristics */
    }
}

void rtc_init(void) {
    serial_printf("===========================================\n");
    serial_printf(" ZeruX OS — RTC (Real Time Clock) init\n");
    serial_printf("===========================================\n");
    rtc_time_t now;
    rtc_read_time(&now);
    
    char h1, h2, m1, m2, s1, s2;
    char d1, d2, mo1, mo2;
    h1  = '0' + (now.hour   / 10); h2  = '0' + (now.hour   % 10);
    m1  = '0' + (now.minute / 10); m2  = '0' + (now.minute % 10);
    s1  = '0' + (now.second / 10); s2  = '0' + (now.second % 10);
    d1  = '0' + (now.day    / 10); d2  = '0' + (now.day    % 10);
    mo1 = '0' + (now.month  / 10); mo2 = '0' + (now.month  % 10);

    /* year digits */
    uint32_t yr = now.year;
    char y1 = '0' + (yr / 1000) % 10;
    char y2 = '0' + (yr / 100)  % 10;
    char y3 = '0' + (yr / 10)   % 10;
    char y4 = '0' + (yr)        % 10;

    serial_printf("[RTC] Current System Time: %c%c:%c%c:%c%c  %c%c/%c%c/%c%c%c%c\n",
                  h1, h2, m1, m2, s1, s2, d1, d2, mo1, mo2, y1, y2, y3, y4);
    serial_printf("===========================================\n\n");
}

void rtc_print_time(void) {
    rtc_time_t now;
    rtc_read_time(&now);
    char h1, h2, m1, m2, s1, s2, d1, d2, mo1, mo2, y1, y2, y3, y4;
    h1 = '0' + (now.hour / 10); h2 = '0' + (now.hour % 10);
    m1 = '0' + (now.minute / 10); m2 = '0' + (now.minute % 10);
    s1 = '0' + (now.second / 10); s2 = '0' + (now.second % 10);
    d1 = '0' + (now.day / 10); d2 = '0' + (now.day % 10);
    mo1 = '0' + (now.month / 10); mo2 = '0' + (now.month % 10);
    
    uint32_t yr = now.year;
    y1 = '0' + (yr / 1000) % 10;
    y2 = '0' + (yr / 100)  % 10;
    y3 = '0' + (yr / 10)   % 10;
    y4 = '0' + (yr)        % 10;

    serial_printf("%c%c%c%c-%c%c-%c%c %c%c:%c%c:%c%c\n", 
                  y1, y2, y3, y4, mo1, mo2, d1, d2, h1, h2, m1, m2, s1, s2);
}
