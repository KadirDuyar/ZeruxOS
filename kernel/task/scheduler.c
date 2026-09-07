/* =============================================================================
 * ZeruX OS — Preemptive Round-Robin Scheduler Implementasyonu
 * File: kernel/task/scheduler.c
 * =============================================================================
 *
 * Preemptive Zamanlayıcı (Scheduler):
 *   - PIT IRQ0 (100 Hz / 10ms) zamanlayıcısı ile her tick'te çağrılır.
 *   - Görev listesinde sıradaki READY durumundaki görevi seçer (Round-Robin).
 *   - TSS.esp0 yazmacını yeni görevin kernel stack tepesi ile günceller.
 *   - `switch_to(old, next)` assembly stub'ı ile donanımsal context switch yapar.
 * =============================================================================
 */

#include "scheduler.h"
#include "tss.h"
#include "irq.h"
#include "timer.h"
#include "serial.h"
#include "cpu.h"

static volatile uint32_t g_schedule_count    = 0;
static volatile uint8_t  g_scheduler_enabled = 0;

/* =============================================================================
 * scheduler_enable() / scheduler_disable()
 * =============================================================================
 */
void scheduler_enable(void) {
    g_scheduler_enabled = 1;
}

void scheduler_disable(void) {
    g_scheduler_enabled = 0;
}

/* =============================================================================
 * schedule() — Preemptive Round-Robin Görev Seçici & Context Switch
 * =============================================================================
 */
/* =============================================================================
 * schedule() — Preemptive Round-Robin Görev Seçici & Context Switch
 * =============================================================================
 */
void schedule(void) {
    if (!g_scheduler_enabled) return;

    task_t *current = task_get_current();
    if (!current || !current->next) return;

    /* Dairesel Bağlı Listeden Bir Sonraki READY/RUNNING Görevi Bul */
    task_t *next = current->next;
    while (next != current) {
        /* FIX 2: Removed 'next->pid != 0' so we CAN switch to kernel_main (idle task) if needed */
        if (next->state == TASK_STATE_READY || next->state == TASK_STATE_RUNNING) {
            break;
        }
        next = next->next;
    }

    /* Çalıştırılacak başka hazır görev yoksa devam et */
    if (next == current || next->state == TASK_STATE_ZOMBIE) return;

    /* Görev Durumlarını Güncelle */
    if (current->state == TASK_STATE_RUNNING) {
        current->state = TASK_STATE_READY;
    }
    next->state = TASK_STATE_RUNNING;

    /* Ring 3 -> Ring 0 Güvenliği: TSS.esp0'ı yeni görevin stack tepesi ile güncelle! */
    tss_set_kernel_stack(next->kernel_stack_top);

    g_schedule_count++;

    task_t *old = current;

    /* Aktif görev göstericisini güncelle */
    task_set_current(next);

    /* Adım 2: Süreç İzolasyonu - Eğer görevlerin Sanal Bellek Dizinleri farklıysa CR3 değiştir */
    if (old->page_directory != next->page_directory && next->page_directory != NULL) {
        vmm_switch_page_directory(next->page_directory);
    }

    /* Assembly `switch_to` ile Register'ları ve Stack Pointer'ı Değiştir */
    switch_to(old, next);
}

/* =============================================================================
 * sys_yield() — Görevin Gönüllü Olarak CPU'yu Bırakması
 * =============================================================================
 */
void sys_yield(void) {
    schedule();
}

/* =============================================================================
 * scheduler_timer_callback() — PIT IRQ0 Timer Kesme Handler'ı
 * =============================================================================
 */
static void scheduler_timer_callback(registers_t *regs) {
    (void)regs;

    timer_increment_tick(); /* FIX: Increment the global timer ticks since we overwrote timer.c's IRQ0 handler! */

    if (!g_scheduler_enabled) return;

    /* Non-blocking Sleep Queue: WAITING durumundaki görevlerin uyandırılma zamanını kontrol et */
    uint32_t current_tick = timer_get_ticks();
    task_t *current = task_get_current();
    if (!current) return;

    task_t *curr_task = current;
    do {
        if (curr_task->state == TASK_STATE_WAITING) {
            if (current_tick >= curr_task->wake_tick) {
                curr_task->state = TASK_STATE_READY;
            }
        }
        curr_task = curr_task->next;
    } while (curr_task != NULL && curr_task != current);

    /* Time-Slice Takibi (100 Hz = her tick 10ms, 2 tick = 20ms zaman dilimi) */
    if (current->time_slice > 0) {
        current->time_slice--;
    }

    if (current->time_slice == 0) {
        current->time_slice = 2; /* 2 tick (20ms) time-slice */
        schedule();
    }
}

/* =============================================================================
 * scheduler_init() — Preemptive Scheduler'ı Hazırlar ve IRQ0 Hook'lar
 * =============================================================================
 */
void scheduler_init(void) {
    serial_printf("===========================================\n");
    serial_printf(" ZeruX OS — Preemptive Round-Robin Scheduler\n");
    serial_printf("===========================================\n");

    /* PIT IRQ0 Kesmesine Scheduler Callback'ini Bağla */
    irq_install_handler(IRQ0_TIMER, scheduler_timer_callback);

    g_scheduler_enabled = 1;
    serial_printf("[SCHEDULER] Preemptive Scheduler Hooked to IRQ0 Timer (100 Hz / 10ms Time-Slicing).\n");
}
