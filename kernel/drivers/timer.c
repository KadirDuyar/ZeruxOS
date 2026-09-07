/* =============================================================================
 * ZeruX OS — Programmable Interval Timer (PIT) Driver Implementasyonu
 * File: kernel/drivers/timer.c
 * =============================================================================
 *
 * Bu dosya PIT Channel 0 donanımını konfigüre eder, IRQ0 kesmelerini işler,
 * sistem uptime süresini tutar ve milisaniye hassasiyetli `timer_sleep_ms` sunar.
 * =============================================================================
 */

#include "timer.h"
#include "irq.h"
#include "ports.h"
#include "serial.h"
#include "cpu.h"
#include "uhci.h"

static volatile uint32_t g_timer_ticks   = 0;
static uint32_t          g_timer_freq_hz = KERNEL_HERTZ;
static uint32_t          g_ms_per_tick   = 10;

/* =============================================================================
 * timer_callback() — IRQ0 Interrupt Service Routine (ISR)
 * =============================================================================
 * Her 10 ms'de (100 Hz modunda) donanım tarafından çağrılır.
 */
void timer_increment_tick(void) {
    g_timer_ticks++;
}

static void timer_callback(registers_t *regs) {
    (void)regs;
    timer_increment_tick();
    uhci_poll(); /* USB HID mouse polling — no-op if UHCI not present */
}


/* =============================================================================
 * timer_init() — PIT Donanımını Programla & IRQ0 Handler Kaydet
 * =============================================================================
 */
void timer_init(uint32_t frequency_hz) {
    if (frequency_hz == 0) frequency_hz = 100;
    g_timer_freq_hz = frequency_hz;
    g_ms_per_tick   = 1000 / frequency_hz;

    /* Divisor hesabı: 1193182 / frekans (örn: 100 Hz için 11931) */
    uint32_t divisor = PIT_BASE_FREQUENCY / frequency_hz;
    if (divisor > 65535) divisor = 65535;

    /* PIT Komut Baytı: 0x36
     *   Bit 7-6 : 00 -> Channel 0
     *   Bit 5-4 : 11 -> Access Mode: LSB sonra MSB oku/yaz
     *   Bit 3-1 : 011-> Operating Mode: Mode 3 (Square Wave Generator)
     *   Bit 0   : 0  -> 16-bit Binary Counter
     */
    outb_p(PIT_COMMAND_PORT, 0x36);

    /* Divisor değerini LSB ve MSB olarak 0x40 portuna gönder */
    outb_p(PIT_CHANNEL0_DATA, (uint8_t)(divisor & 0xFF));
    outb_p(PIT_CHANNEL0_DATA, (uint8_t)((divisor >> 8) & 0xFF));

    /* IRQ0 (Timer) kesmesini IRQ dispatcher'a kaydet */
    irq_install_handler(IRQ0_TIMER, timer_callback);

    serial_printf("[TIMER] PIT Channel 0 initialized at %u Hz (tick interval: %u ms)\n",
                  frequency_hz, g_ms_per_tick);
}

/* =============================================================================
 * timer_get_ticks() — Toplam System Tick Sayısını Döner
 * =============================================================================
 */
uint32_t timer_get_ticks(void) {
    return g_timer_ticks;
}

/* =============================================================================
 * timer_get_uptime_ms() — Geçen Uptime Süresini (ms) Döner
 * =============================================================================
 */
uint32_t timer_get_uptime_ms(void) {
    return g_timer_ticks * g_ms_per_tick;
}

/* =============================================================================
 * timer_sleep_ms() — Milisaniye Cinsinden Bekleme (CPU Halt Uykusu)
 * =============================================================================
 * Meşgul döngü (busy-loop) yerine CPU'yu HLT modunda tutarak enerji tasarrufu sağlar.
 */
void timer_sleep_ms(uint32_t ms) {
    uint32_t ticks_to_wait = (ms * g_timer_freq_hz) / 1000;
    if (ticks_to_wait == 0) ticks_to_wait = 1;

    uint32_t target_ticks = g_timer_ticks + ticks_to_wait;
    while (g_timer_ticks < target_ticks) {
        cpu_halt();
    }
}
