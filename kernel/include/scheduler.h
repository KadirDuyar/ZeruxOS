/* =============================================================================
 * ZeruX OS — Preemptive Round-Robin Scheduler Header
 * File: kernel/include/scheduler.h
 * =============================================================================
 *
 * Çok Görevli (Multitasking) Zamanlayıcı Sürücüsü:
 *   - `schedule()`: Preemptive Round-Robin görev seçici.
 *   - `sys_yield()`: Görevin CPU'yu gönüllü olarak bırakması.
 *   - `switch_to(old, next)`: Assembly seviyesinde context switch.
 * =============================================================================
 */

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "task.h"

/* Public API Bildirimleri */
void scheduler_init(void);
void schedule(void);
void sys_yield(void);
void scheduler_enable(void);
void scheduler_disable(void);

/* Low-level Assembly Context Switch Stub'ı (switch_asm.asm'de tanımlı) */
extern void switch_to(task_t *old, task_t *next);

#endif /* SCHEDULER_H */
