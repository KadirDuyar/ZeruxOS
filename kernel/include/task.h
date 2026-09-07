/* =============================================================================
 * ZeruX OS — Process Control Block (PCB / task_t) Header
 * File: kernel/include/task.h
 * =============================================================================
 *
 * İşlem Yönetimi (Process Management) ve Kernel Thread yapıları.
 * Her bir görevin (task/thread) durumunu (READY, RUNNING, SLEEPING, ZOMBIE),
 * stack adreslerini, CPU register bağlamını (context) ve PID bilgilerini saklar.
 * =============================================================================
 */

#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include <stddef.h>
#include "vmm.h"
#include "object.h"

#define TASK_NAME_MAX_LEN 32
#define TASK_STACK_SIZE   16384  /* Her task için 16 KB Çekirdek Stack (aurora-fetch gibi derin callstack'ler için) */
#define TASK_MAX_FDS      16    /* Maksimum açık dosya sayısı (per-process) */

/* Görev Durumu (Task State Enum) */
typedef enum {
    TASK_STATE_READY = 0,   /* Çalışmaya hazır */
    TASK_STATE_RUNNING,     /* Şu an CPU'da çalışıyor */
    TASK_STATE_SLEEPING,    /* Uyuyor / Zamanlayıcı bekliyor */
    TASK_STATE_WAITING,     /* Non-blocking sleep queue bekliyor */
    TASK_STATE_BLOCKED,     /* I/O (Soket vs) bekliyor, wakeup gerektirir */
    TASK_STATE_ZOMBIE       /* Sonlandı / Temizlenecek */
} task_state_t;

/* Context Switch sırasında stack'e kaydedilen 40-Baytlık CPU Register çerçevesi */
typedef struct {
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t eflags;
    uint32_t eip;
} __attribute__((packed)) task_context_t;

/* Process Control Block (PCB / task_t Yapısı) */
typedef struct task {
    uint32_t            pid;                /* Benzersiz Görev Kimliği (Process ID) */
    uint32_t            parent_pid;         /* Ebeveyn Sürecin PID'si (PID 0 veya 1 = Init/Kernel) */
    char                name[TASK_NAME_MAX_LEN]; /* Görev İsmi (örn. "Task A") */
    task_state_t        state;              /* READY, RUNNING, SLEEPING, ZOMBIE */
    uint32_t            priority;           /* Öncelik Seviyesi (default 1) */
    uint32_t            time_slice;         /* Kalan zaman dilimi (tick sayısı) */
    uint32_t            esp;                /* Görevin kaydedilmiş ESP stack pointer'ı */
    uint32_t            ebp;                /* Görevin EBP pointer'ı */
    uint32_t            eip;                /* Görevin başlangıç/devam adresi */
    uint32_t            eflags;             /* EFLAGS register'ı (0x202 = Interrupts Enabled) */
    page_directory_t   *page_directory;     /* Görevin Sanal Bellek Dizini (Paging CR3) */
    void               *kernel_stack;       /* kmalloc ile ayrılan 4 KB Çekirdek Stack tabanı */
    uint32_t            kernel_stack_top;   /* Çekirdek stack tepesi (tepe noktası) */
    uint32_t            wake_tick;          /* Görevin uyanacağı PIT tick zamanı (Non-blocking sleep queue) */
    int32_t             exit_code;          /* sys_exit() ile dönen çıkış kodu */
    int32_t             fds[TASK_MAX_FDS];  /* Dosya Tanımlayıcıları (File Descriptors) */
    
    /* ZXAPI Handle Table (Phase 0) */
    zx_handle_table_t   handle_table;

    /* User Heap (sbrk) Pointers */
    uint32_t            heap_start;
    uint32_t            heap_end;

    /* Çift Yönlü Bağlı Liste (Scheduler) */
    struct task        *next;               /* Dairesel bağlı liste için sonraki görev */
    struct task        *prev;               /* Dairesel bağlı liste için önceki görev */
} task_t;

/* Public API Bildirimleri */
void     task_init(void);
task_t*  kthread_create(void (*entry_point)(void), const char *name);
task_t*  user_process_create(void (*entry_point)(void), const char *name, page_directory_t *pdir, int argc, const char **argv);
task_t*  task_get_current(void);
void     task_set_current(task_t *t);
void     task_exit(int32_t exit_code);
void     task_kill(int32_t pid);
void     task_sleep_ms(uint32_t ms);
void     task_block(void);
void     task_unblock(task_t *task);
int32_t  task_waitpid(int32_t pid);
void     dump_task(const task_t *t);
void     dump_task_list(void);

/* Adım 3.1 Doğrulama Testi */
void task_run_test_suite(void);

/* Task listesini dolaşmak için (read-only) */
task_t *task_get_list_head(void);
uint32_t task_get_count(void);

#endif /* TASK_H */
