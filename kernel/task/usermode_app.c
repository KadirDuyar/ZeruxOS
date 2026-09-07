/* =============================================================================
 * ZeruX OS — User Mode Application Demonstration
 * File: kernel/task/usermode_app.c
 * =============================================================================
 *
 * Ring 3 (User Mode) seviyesinde çalışan ve `int 0x80` sistem çağrılarını
 * (sys_write, sys_getpid, sys_yield) kullanarak ekran/seri porta çıktı veren
 * örnek kullanıcı programı.
 * =============================================================================
 */

#include "usermode.h"
#include "syscall.h"
#include "kheap.h"
#include "serial.h"
#include "task.h"

void user_space_app_entry(void) {
    /* 1. Syscall 1: user_write_str() ile mesaj yaz */
    user_write_str(1, "\n=======================================================\n");
    user_write_str(1, " [RING 3 USER APP] Hello from Ring 3 User Mode!\n");
    user_write_str(1, " [RING 3 USER APP] Executed via int 0x80 Syscall Engine!\n");
    user_write_str(1, "=======================================================\n");

    /* 2. Syscall 2: user_getpid() ile PID öğren */
    uint32_t pid = user_getpid();
    (void)pid;

    /* 3. Syscall 3: user_yield() ile CPU'yu gönüllü zamanlayıcıya bırak */
    user_write_str(1, " [RING 3 USER APP] Voluntarily calling user_yield()...\n");
    user_yield();

    user_write_str(1, " [RING 3 USER APP] Resumed execution after user_yield()!\n");

    /* 4. Syscall 4: user_sleep(100) ile non-blocking 100ms uykuya yat */
    user_write_str(1, " [RING 3 USER APP] Calling Non-Blocking user_sleep(100ms)...\n");
    user_sleep(100);
    user_write_str(1, " [RING 3 USER APP] Woke up from Non-Blocking user_sleep(100ms)!\n");

    /* 5. Syscall 5: user_exit(0) ile görevi ZOMBIE yapıp sonlandır */
    user_write_str(1, " [RING 3 USER APP] Terminating task via user_exit(0)...\n\n");
    user_exit(0);

    /* Güvenlik kilidi: user_exit asla return etmemeli.  
     * Eğer return ederse burada sonsuza kadar bekle (scheduler interrupt gelene kadar). */
    while (1) {
        __asm__ volatile ("hlt"); /* Ring 3'te hlt #GP atar — scheduler tekrar devralır */
    }
}

void run_usermode_app_demo(void) {
    serial_printf("[USERMODE DEMO] Creating Ring 3 User Mode Application Process via PCB Manager...\n");
    user_process_create(user_space_app_entry, "User App", vmm_get_kernel_page_directory(), 0, NULL);
}
