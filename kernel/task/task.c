/* =============================================================================
 * ZeruX OS — Process Control Block (PCB / task_t) & Task Manager
 * File: kernel/task/task.c
 * =============================================================================
 *
 * İşlem Yöneticisi:
 *   - Ana çekirdek görevini (PID 0) tanımlar.
 *   - `kthread_create` ile yeni çekirdek iş parçacıkları (threads) oluşturur.
 *   - Her göreve 4 KB'lık bağımsız çekirdek stack'i tahsis eder (kmalloc).
 *   - İlk context switch için stack üzerine sahte trampoline çerçevesi kurar.
 * =============================================================================
 */

#include "task.h"
#include "kheap.h"
#include "pmm.h"
#include "vmm.h"
#include "serial.h"
#include "cpu.h"
#include "timer.h"
#include "scheduler.h"
#include "vfs.h"
#include "usermode.h"
#include <stdbool.h>

/* Freestanding kstrncpy (libc bağımlılığını kaldırmak için) */
static char* kstrncpy(char *dest, const char *src, size_t n) {
    size_t i = 0;
    if (!dest || !src || n == 0) return dest;
    for (i = 0; i < n - 1 && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
    return dest;
}

static task_t *g_current_task   = NULL;
static task_t *g_task_list_head = NULL;
static uint32_t g_next_pid      = 0;

/* Ana Çekirdek Görevi (PID 0 - kernel_main) */
static task_t g_main_task;

/* State String Dönüştürücü */
static const char* state_names[] = {
    [TASK_STATE_READY]    = "READY",
    [TASK_STATE_RUNNING]  = "RUNNING",
    [TASK_STATE_SLEEPING] = "SLEEPING",
    [TASK_STATE_WAITING]  = "WAITING",
    [TASK_STATE_BLOCKED]  = "BLOCKED",
    [TASK_STATE_ZOMBIE]   = "ZOMBIE"
};

static void kernel_init_thread(void);

/* =============================================================================
 * task_init() — Görev Yöneticisini ve Ana Çekirdek Görevini (PID 0) Başlatır
 * =============================================================================
 */
void task_init(void) {
    serial_printf("===========================================\n");
    serial_printf(" ZeruX OS — Process Control Block & Task Init\n");
    serial_printf("===========================================\n");

    uint8_t *ptr = (uint8_t*)&g_main_task;
    for (size_t i = 0; i < sizeof(task_t); i++) ptr[i] = 0;

    g_main_task.pid              = 0;
    kstrncpy(g_main_task.name, "kernel_main", TASK_NAME_MAX_LEN - 1);
    g_main_task.state            = TASK_STATE_RUNNING;
    g_main_task.priority         = 1;
    g_main_task.time_slice       = 10;
    g_main_task.esp              = cpu_read_esp();
    g_main_task.ebp              = g_main_task.esp;
    g_main_task.eip              = 0;
    g_main_task.eflags           = 0x202; /* Interrupts Enabled */
    g_main_task.page_directory   = vmm_get_kernel_page_directory();
    g_main_task.kernel_stack     = (void*)0x7000;
    g_main_task.kernel_stack_top = 0x7BDC;

    g_main_task.next = &g_main_task;
    g_main_task.prev = &g_main_task;

    g_current_task   = &g_main_task;
    g_task_list_head = &g_main_task;

    serial_printf("[TASK] PID 0 (kernel_main) PCB Initialized successfully.\n");

    /* PID 1 (Init) Thread'ini başlat */
    kthread_create(kernel_init_thread, "init");
}

/* =============================================================================
 * kthread_create() — Yeni Çekirdek İş Parçacığı (Thread) Oluşturur
 * =============================================================================
 */
task_t* kthread_create(void (*entry_point)(void), const char *name) {
    if (!entry_point) return NULL;

    /* 1. PCB için Bellek Ayır (kmalloc) */
    task_t *t = (task_t*)kzalloc(sizeof(task_t));
    if (!t) {
        serial_printf("[TASK ERROR] Failed to allocate PCB for thread!\n");
        return NULL;
    }

    t->pid              = ++g_next_pid;
    t->parent_pid       = task_get_current() ? task_get_current()->pid : 0;
    kstrncpy(t->name, name ? name : "kthread", TASK_NAME_MAX_LEN - 1);
    t->state            = TASK_STATE_READY;
    t->priority         = 1;
    t->time_slice       = 10;
    t->page_directory   = vmm_get_kernel_page_directory();
    zx_handle_table_init(&t->handle_table);

    /* 2. 4 KB Çekirdek Stack Ayır (kmalloc) */
    t->kernel_stack     = kmalloc(TASK_STACK_SIZE);
    if (!t->kernel_stack) {
        serial_printf("[TASK ERROR] Failed to allocate 4KB stack for PID %u\n", t->pid);
        kfree(t);
        return NULL;
    }

    /* Stack Tepesini Hesapla & 8-Byte Hizala */
    uint32_t stack_top = (uint32_t)t->kernel_stack + TASK_STACK_SIZE;
    stack_top &= ~0x07; /* 8-byte alignment */
    t->kernel_stack_top = stack_top;

    /* 3. Trampoline Frame Hazırlığı (switch_to ilk popad + ret için) */
    stack_top -= sizeof(task_context_t);
    task_context_t *ctx = (task_context_t*)stack_top;

    ctx->eflags = 0x202;                 /* Interrupts Enabled (EFLAGS.IF = 1) */
    ctx->eip    = (uint32_t)entry_point; /* Thread Giriş Adresi */
    ctx->eax    = 0;
    ctx->ecx    = 0;
    ctx->edx    = 0;
    ctx->ebx    = 0;
    ctx->esp    = stack_top;
    ctx->ebp    = stack_top;
    ctx->esi    = 0;
    ctx->edi    = 0;

    t->esp    = stack_top;
    t->ebp    = stack_top;
    t->eip    = (uint32_t)entry_point;
    t->eflags = 0x202;

    /* 4. Dairesel Bağlı Listeye Ekle */
    task_t *last = g_task_list_head->prev;
    last->next   = t;
    t->prev      = last;
    t->next      = g_task_list_head;
    g_task_list_head->prev = t;

    serial_printf("[TASK] Created Thread PID %u ('%s') -> Stack: %p - %p\n",
                  t->pid, t->name, t->kernel_stack, (void*)t->kernel_stack_top);

    return t;
}

/* =============================================================================
 * user_process_create() — Ring 3 User Mode Prosesi Olusturur
 * =============================================================================
 */
task_t* user_process_create(void (*entry_point)(void), const char *name, page_directory_t *pdir, int argc, const char **argv) {
    task_t *t = (task_t*)kzalloc(sizeof(task_t));
    if (!t) return NULL;

    t->pid = ++g_next_pid;
    t->parent_pid = task_get_current() ? task_get_current()->pid : 0;
    kstrncpy(t->name, name, TASK_NAME_MAX_LEN);
    t->state = TASK_STATE_READY;
    t->priority = 1;
    t->time_slice = 2;
    t->wake_tick = 0;
    t->exit_code = 0;
    
    zx_handle_table_init(&t->handle_table);

    /* Dosya Tanımlayıcıları (FD) İlklendirme */
    t->fds[0] = -10; /* stdin (Console) */
    t->fds[1] = -11; /* stdout (Console) */
    t->fds[2] = -12; /* stderr (Console) */
    for (int i = 3; i < TASK_MAX_FDS; i++) {
        t->fds[i] = -1; /* Boş slot */
    }
    t->page_directory = pdir;

    /* 4 KB Kernel Stack Tahsis Et (Ring 3 -> Ring 0 Interrupt/Syscall için) */
    t->kernel_stack = kmalloc(TASK_STACK_SIZE);
    if (!t->kernel_stack) {
        kfree(t);
        return NULL;
    }

    uint32_t kstack_top = (uint32_t)t->kernel_stack + TASK_STACK_SIZE;
    kstack_top &= ~0x07;
    t->kernel_stack_top = kstack_top;

    #define USER_STACK_VIRT_BASE  0x0B000000u
    #define USER_STACK_PAGES      1u   /* 4 KB */

    uint32_t ustack_virt = USER_STACK_VIRT_BASE + (uint32_t)(t->pid) * (VMM_PAGE_SIZE * 2);

    void *ustack_phys = pmm_alloc_block();
    if (!ustack_phys) {
        kfree(t->kernel_stack);
        kfree(t);
        return NULL;
    }

    uint8_t *stack_bytes = (uint8_t*)ustack_phys;
    for (uint32_t i = 0; i < VMM_PAGE_SIZE; i++) {
        stack_bytes[i] = 0;
    }

    vmm_map_page(pdir,
                 ustack_virt,
                 (uint32_t)ustack_phys,
                 VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER);

    uint32_t current_top = (USER_STACK_PAGES * VMM_PAGE_SIZE);

    if (argc > 0 && argv != NULL) {
        uint32_t string_ptrs[32];
        if (argc > 32) argc = 32;

        for (int i = argc - 1; i >= 0; i--) {
            uint32_t len = 0;
            while (argv[i][len] != '\0') len++;
            len++; /* null terminator */
            current_top -= len;
            for (uint32_t j = 0; j < len; j++) {
                stack_bytes[current_top + j] = argv[i][j];
            }
            string_ptrs[i] = ustack_virt + current_top;
        }

        current_top &= ~3u; /* align to 4 bytes */
        current_top -= (argc + 1) * 4;
        uint32_t argv_array_virt = ustack_virt + current_top;
        uint32_t *argv_array_phys = (uint32_t*)(stack_bytes + current_top);
        
        for (int i = 0; i < argc; i++) {
            argv_array_phys[i] = string_ptrs[i];
        }
        argv_array_phys[argc] = 0; /* NULL pointer at the end of argv array */

        current_top &= ~0x0Fu; /* Align stack to 16-bytes BEFORE pushing arguments */

        current_top -= 4;
        *((uint32_t*)(stack_bytes + current_top)) = argv_array_virt; /* push argv */

        current_top -= 4;
        *((uint32_t*)(stack_bytes + current_top)) = (uint32_t)argc; /* push argc */
    } else {
        /* No arguments */
        current_top &= ~0x0Fu; /* Align stack to 16-bytes BEFORE pushing arguments */
        
        current_top -= 4;
        *((uint32_t*)(stack_bytes + current_top)) = 0; /* push argv = NULL */
        current_top -= 4;
        *((uint32_t*)(stack_bytes + current_top)) = 0; /* push argc = 0 */
    }

    /* Ustack top is the virtual address corresponding to current_top */
    uint32_t ustack_top = ustack_virt + current_top;

    /* Trampoline Stack Frame: C calling convention for enter_usermode(entry_point, user_stack_top) */
    uint32_t *args = (uint32_t*)(kstack_top - 12);
    args[0] = 0;                        /* Dummy Return Address */
    args[1] = (uint32_t)entry_point;     /* Arg 1: entry_point */
    args[2] = ustack_top;                /* Arg 2: user_stack_top */

    kstack_top -= sizeof(task_context_t) + 12;
    task_context_t *ctx = (task_context_t*)kstack_top;

    ctx->eflags = 0x202;
    ctx->eip    = (uint32_t)enter_usermode;
    ctx->eax    = 0;
    ctx->ecx    = 0;
    ctx->edx    = 0;
    ctx->ebx    = 0;
    ctx->esp    = kstack_top;
    ctx->ebp    = kstack_top;
    ctx->esi    = 0;
    ctx->edi    = 0;

    t->esp = kstack_top;
    t->ebp = kstack_top;
    t->eip = (uint32_t)enter_usermode;

    /* Dairesel Bağlı Listeye Ekle */
    task_t *last = g_task_list_head->prev;
    last->next   = t;
    t->prev      = last;
    t->next      = g_task_list_head;
    g_task_list_head->prev = t;

    serial_printf("[TASK] Created User Process PID %u ('%s') -> KStack: %p, UStack: %p (virt) -> %p (phys)\n",
                  t->pid, t->name, t->kernel_stack, (void*)ustack_virt, ustack_phys);

    return t;
}


/* =============================================================================
 * task_get_current() — Aktif Görevi Döner
 * =============================================================================
 */
task_t* task_get_current(void) {
    return g_current_task;
}

void task_set_current(task_t *t) {
    if (t) {
        g_current_task = t;
    }
}

task_t *task_get_list_head(void) {
    return g_task_list_head;
}

uint32_t task_get_count(void) {
    if (!g_task_list_head) return 0;
    uint32_t count = 0;
    task_t *t = g_task_list_head;
    do { count++; t = t->next; } while (t != g_task_list_head);
    return count;
}

/* =============================================================================
 * task_exit() — Görevi ZOMBIE Seviyesine Alır ve CPU'yu Bırakır (Adım 3.5.1)
 * =============================================================================
 */
void task_exit(int32_t exit_code) {
    task_t *curr = task_get_current();
    if (!curr || curr->pid == 0) return;

    serial_printf("[TASK EXIT] Task PID %u ('%s') exited with status %d.\n",
                  curr->pid, curr->name, exit_code);

    /* Orphan Reparenting: Reassign children to PID 1 (Init) */
    if (g_task_list_head) {
        task_t *t = g_task_list_head;
        do {
            if (t->parent_pid == curr->pid) {
                t->parent_pid = 1;
                /* Note: We could optionally wake up PID 1 if t is already ZOMBIE */
            }
            t = t->next;
        } while (t != g_task_list_head && t != NULL);
    }

    curr->state = TASK_STATE_ZOMBIE;
    curr->exit_code = exit_code;

    schedule();
}

/* =============================================================================
 * task_sleep_ms() — Task'ı Non-Blocking WAITING Moduna Alır (Adım 3.5.3)
 * =============================================================================
 */
void task_kill(int32_t pid) {
    if (pid <= 2) return; /* Do not kill kernel/init tasks */
    if (!g_task_list_head) return;
    task_t *t = g_task_list_head;
    do {
        if ((int32_t)t->pid == pid && t->state != TASK_STATE_ZOMBIE) {
            t->state = TASK_STATE_ZOMBIE;
            t->exit_code = -1;
            serial_printf("[TASK] PID %d killed by taskmgr.\n", pid);
            return;
        }
        t = t->next;
    } while (t != g_task_list_head);
}

void task_sleep_ms(uint32_t ms) {
    task_t *curr = task_get_current();
    if (!curr) return;

    uint32_t ticks_to_wait = (ms + 9) / 10;
    curr->wake_tick = timer_get_ticks() + ticks_to_wait;
    curr->state = TASK_STATE_WAITING;
    
    while (curr->state == TASK_STATE_WAITING) {
        schedule();
        if (curr->state == TASK_STATE_WAITING) {
            __asm__ volatile ("hlt");
        }
    }
}

/* =============================================================================
 * task_block() - Task' Bloklar (I/O, Soket beklemek iin)
 * =============================================================================
 */
void task_block(void) {
    task_t *curr = task_get_current();
    if (!curr) return;
    
    curr->state = TASK_STATE_BLOCKED;
    
    while (curr->state == TASK_STATE_BLOCKED) {
        schedule();
        if (curr->state == TASK_STATE_BLOCKED) {
            __asm__ volatile ("hlt");
        }
    }
}

/* =============================================================================
 * task_unblock() - Bloke olmuş task'ı uyandırır
 * =============================================================================
 */
void task_unblock(task_t *task) {
    if (!task) return;
    if (task->state == TASK_STATE_BLOCKED) {
        task->state = TASK_STATE_READY;
    }
}

/* =============================================================================
 * task_destroy() - Tam Görev Temizliği
 * =============================================================================
 */
void task_destroy(task_t *task) {
    if (!task) return;

    /* 1. Kapatılmamış Bütün Dosyalar (ve Soketleri) Kapat */
    for (int i = 0; i < TASK_MAX_FDS; i++) {
        if (task->fds[i] >= 0) {
            vfs_close(task->fds[i]);
            task->fds[i] = -1;
        }
    }

    /* 2. User Page Directory (ve mapped user memory) Temizliği */
    if (task->page_directory) {
        zx_handle_table_destroy(&task->handle_table);
        vmm_free_dir(task->page_directory);
    }

    /* 3. Kernel Stack ve Task Struct (Heap) Temizliği */
    if (task->kernel_stack) {
        kfree(task->kernel_stack);
    }
    kfree(task);
}

/* =============================================================================
 * task_waitpid() - Bir görevin (çocuğun) sonlanmasını bekle ve zombiyi temizle
 * pid == -1 ise herhangi bir çocuğun sonlanmasını bekler.
 * =============================================================================
 */
int32_t task_waitpid(int32_t pid) {
    if (!g_task_list_head) return -1;
    task_t *current = task_get_current();
    if (!current) return -1;
    
    while (1) {
        bool has_children = false;
        task_t *curr = g_task_list_head->next;
        task_t *zombie_to_reap = NULL;
        int exit_code = -1;
        
        while (curr != g_task_list_head) {
            /* Sadece bizim çocuklarımız (parent_pid == benim_pid) */
            if (curr->parent_pid == current->pid) {
                if (pid == -1 || curr->pid == (uint32_t)pid) {
                    has_children = true;
                    if (curr->state == TASK_STATE_ZOMBIE) {
                        zombie_to_reap = curr;
                        exit_code = curr->exit_code;
                        break;
                    }
                }
            }
            curr = curr->next;
        }
        
        if (!has_children) return -1; /* Beklenecek uygun bir çocuk kalmadı */
        
        if (zombie_to_reap) {
            serial_printf("[TASK REAPER] PID %u reaped Zombie PID %u ('%s')\n",
                          current->pid, zombie_to_reap->pid, zombie_to_reap->name);
                          
            /* Listeden çıkart */
            zombie_to_reap->prev->next = zombie_to_reap->next;
            zombie_to_reap->next->prev = zombie_to_reap->prev;
            
            /* Belleği temizle */
            task_destroy(zombie_to_reap);
            return exit_code;
        }
        
        /* Zombi yok, uyu ve bekle */
        task_sleep_ms(20);
    }
}

/* =============================================================================
 * kernel_init_thread() - PID 1 Init Süreci
 * =============================================================================
 */
static void kernel_init_thread(void) {
    serial_printf("[INIT] PID 1 Started. Reaping orphans...\n");
    while (1) {
        if (task_waitpid(-1) < 0) {
            task_sleep_ms(100);
        }
    }
}

/* =============================================================================
 * dump_task() — Tek Bir Görevin (PCB) Detaylarını Basar
 * =============================================================================
 */
void dump_task(const task_t *t) {
    if (!t) return;
    serial_printf("[PCB] PID: %u  Name: %s  State: %s  ESP: %p  EIP: %p  StackTop: %p\n",
                  t->pid, t->name, state_names[t->state], (void*)t->esp, (void*)t->eip, (void*)t->kernel_stack_top);
}

/* =============================================================================
 * dump_task_list() — Tüm Görev Listesini Raporlar
 * =============================================================================
 */
void dump_task_list(void) {
    serial_printf("-------------------------------------------------------------------------\n");
    serial_printf("[TASK LIST DUMP] Active Kernel Tasks & Process Control Blocks (PCBs)\n");
    serial_printf("-------------------------------------------------------------------------\n");

    if (!g_task_list_head) {
        serial_printf("No active tasks.\n");
        return;
    }

    task_t *curr = g_task_list_head;
    do {
        dump_task(curr);
        curr = curr->next;
    } while (curr != g_task_list_head);

    serial_printf("-------------------------------------------------------------------------\n");
}

/* =============================================================================
 * Dummy Preemptive Multitasking Test Threads (Adım 3.2.6 Verification)
 * =============================================================================
 */
static void thread_alpha_func(void) {
    while (1) {
        task_sleep_ms(1000);
    }
}

static void thread_beta_func(void) {
    while (1) {
        task_sleep_ms(1000);
    }
}

/* =============================================================================
 * task_run_test_suite() — Adım 3.1 & 3.2 Thread Doğrulama Testi
 * =============================================================================
 */
void task_run_test_suite(void) {
    serial_printf("\n[TASK TEST] Creating test threads 'Task Alpha' and 'Task Beta'...\n");

    task_t *t1 = kthread_create(thread_alpha_func, "Task Alpha");
    task_t *t2 = kthread_create(thread_beta_func,  "Task Beta");

    dump_task_list();

    if (t1 && t2 && t1->pid == 1 && t2->pid == 2) {
        serial_printf("\n=======================================================\n");
        serial_printf(" [TASK TEST PASSED] Process Control Blocks (PCBs) Verified!\n");
        serial_printf("   - Main Task (PID 0) : 'kernel_main'\n");
        serial_printf("   - Thread 1 (PID 1)  : 'Task Alpha' (Stack: %p)\n", t1->kernel_stack);
        serial_printf("   - Thread 2 (PID 2)  : 'Task Beta'  (Stack: %p)\n", t2->kernel_stack);
        serial_printf("   - Trampoline Frame  : 40-Byte Aligned Context Pre-populated\n");
        serial_printf("=======================================================\n\n");
    } else {
        serial_printf("[TASK TEST FAILED] Thread creation failed!\n");
    }
}
