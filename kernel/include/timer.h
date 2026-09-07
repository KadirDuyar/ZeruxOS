/* =============================================================================
 * ZeruX OS — Programmable Interval Timer (PIT 8253/8254) Header
 * File: kernel/include/timer.h
 * =============================================================================
 *
 * PIT, x86 işlemcisinde donanımsal zamanlama (system tick) ve milisaniye bazlı
 * zaman gecikmesi (sleep) sağlayan donanım bileşenidir.
 * =============================================================================
 */

#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

/* PIT Port Sabitleri */
#define PIT_CHANNEL0_DATA  0x40  /* Channel 0 Data Port (IRQ0 üretir) */
#define PIT_CHANNEL1_DATA  0x41  /* Channel 1 Data Port (RAM refresh, eski) */
#define PIT_CHANNEL2_DATA  0x42  /* Channel 2 Data Port (PC Speaker) */
#define PIT_COMMAND_PORT   0x43  /* Mode / Command Register Port */

/* PIT Donanım Frekansı */
#define PIT_BASE_FREQUENCY 1193182  /* 1.193182 MHz ana kristal frekansı */

/* Varsayılan Kernel Ritim Frekansı */
#define KERNEL_HERTZ       100      /* 100 Hz = Her 10 ms'de 1 tick */

/* Public API Bildirimleri */
void     timer_init(uint32_t frequency_hz);
void     timer_increment_tick(void);
uint32_t timer_get_ticks(void);
uint32_t timer_get_uptime_ms(void);
void     timer_sleep_ms(uint32_t ms);

#endif /* TIMER_H */
