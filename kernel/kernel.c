/* =============================================================================
 * ZeruX OS — Kernel Entry Point
 * File: kernel/kernel.c
 * =============================================================================
 *
 * This file is the heart of ZeruX OS. The bootloader (boot.asm):
 *   1. Loaded 10 disk sectors from sector 2 onward → physical 0x10000
 *   2. After entering 32-bit protected mode, copied them to 0x100000
 *   3. Jumped to physical address 0x100000
 *
 * The linker script (linker.ld) guarantees that kernel_main() sits at the
 * very first byte of the kernel binary — so jmp 0x100000 lands here.
 *
 * Because we compile with -ffreestanding:
 *   ✗  No stdlib (no printf, no malloc, no memcpy)
 *   ✗  No runtime startup (no _start, no crt0)
 *   ✓  Full C language (structs, enums, inline functions, etc.)
 *   ✓  Access to compiler built-ins (__asm__, __attribute__, etc.)
 *   ✓  Access to <stdint.h> / <stddef.h> (compiler-provided, no OS needed)
 *      — but we define our own types in vga.h to keep it fully self-contained
 *
 * Compile:
 *   i686-elf-gcc -ffreestanding -O2 -Wall -Wextra \
 *     -I../include -c kernel.c -o ../build/kernel.o
 * =============================================================================
 */

#include "vga.h"      /* VGA driver: vga_init, vga_puts, vga_put_at, … */
#include "serial.h"   /* COM1 UART serial driver: serial_init, serial_printf */
#include "klog.h"     /* Kernel Log Ring Buffer: klog_init, klog_dump */
#include "firewall.h" /* Network Firewall / IDS: fw_init, fw_check */
#include "cpu.h"      /* CPU control primitives: cpu_read_esp, cpu_read_cr0, cpu_halt, cpu_sti */
#include "idt.h"      /* IDT setup: idt_init, idt_set_gate */
#include "isr.h"      /* ISR setup & CPU exception handling: isr_init, kernel_panic */
#include "pic.h"      /* 8259A PIC driver: pic_init, pic_send_eoi, pic_mask_irq */
#include "irq.h"      /* IRQ dispatcher & driver callback registry: irq_init, irq_install_handler */
#include "timer.h"    /* PIT Timer driver: timer_init, timer_get_ticks, timer_get_uptime_ms, timer_sleep_ms */
#include "keyboard.h" /* PS/2 Keyboard driver: keyboard_init, keyboard_getchar, keyboard_has_char */
#include "gdt.h"      /* Dynamic GDT: gdt_init, gdt_set_gate, dump_gdt */
#include "tss.h"      /* Task State Segment (TSS): tss_init, tss_set_kernel_stack, dump_tss */
#include "boot_info.h"/* BIOS E820 Memory Map & Boot Info: boot_info_init, boot_info_get */
#include "pmm.h"      /* Physical Memory Manager (Bitmap Allocator): pmm_init, pmm_alloc_block, pmm_free_block */
#include "vmm.h"      /* Virtual Memory Manager (32-bit Paging): vmm_init, vmm_map_page, vmm_unmap_page */
#include "kheap.h"    /* Kernel Heap Allocator: kheap_init, kmalloc, kfree, kzalloc */
#include "task.h"     /* Process Control Block & Task Manager: task_init, kthread_create, dump_task_list */
#include "scheduler.h"/* Preemptive Round-Robin Scheduler: scheduler_init, schedule, sys_yield */
#include "usermode.h"  /* User Mode (Ring 3): enter_usermode, test_usermode_switch */
#include "syscall.h"   /* System Calls Engine (int 0x80): syscall_init, sys_write, sys_yield */
#include "ata.h"       /* Primary ATA/IDE PIO Hard Disk Driver: ata_init, ata_read_sector */
#include "vfs.h"       /* Virtual File System (VFS Core Abstraction): vfs_init, vfs_open */
#include "devfs.h"     /* Virtual Device File System (/wire): devfs_init, /wire/serial0 */
#include "procfs.h"    /* Process File System (/live): procfs_init, /live/tasks */
#include "fat32.h"     /* FAT32 File System Driver: fat32_init, /disk/fat0/WELCOME.TXT */
#include "rootfs.h"    /* Root File System: rootfs_init, /run, /cfg, /app, /user, /core, /disk */
#include "userfs.h"    /* RAM Writable User Filesystem: userfs_init, mkdir, rm, touch, edit */
#include "shell.h"     /* Interactive VFS Shell CLI Interface: shell_init, help, cat, ls */
#include "usb.h"
#include "uhci.h"

#include "pci.h"
#include "rtc.h"

#include "vbe.h"
#include "framebuffer.h"
#include "gfx2d.h"
#include "png.h"
#include "mouse.h"
#include <stddef.h>
#include "gui.h"
#include "tests.h"    /* Debug exception test suite: trigger_divide_error, ... */

/* Linker script symbols */
extern uint8_t _kernel_start[];
extern uint8_t _kernel_end[];
extern uint8_t __bss_start[];
extern uint8_t __bss_end[];

/* =============================================================================
 * SECTION PLACEMENT
 * =============================================================================
 * We mark kernel_main with __attribute__((section(".entry"))) so the linker
 * script can place it at the very beginning of the .text segment.
 * This ensures the bootloader's `jmp 0x100000` hits kernel_main directly.
 *
 * Without this, the linker might place helper functions before kernel_main
 * and the CPU would execute garbage as if it were kernel_main code.
 */
void kernel_main(boot_info_t *boot_info) __attribute__((section(".entry")));

/* =============================================================================
 * INTERNAL HELPERS
 * =============================================================================
 * These are forward-declared static functions so they are private to this
 * translation unit and the compiler can optimize / inline them freely.
 */

/* =============================================================================
 * kernel_main — OS Entry Point
 * =============================================================================
 */
void kernel_main(boot_info_t *boot_info) {

    /* ══ ERKEN UART PROBE ══════════════════════════════════════════════════════
     * serial_init() cagrismadan ONCE dogrudan port 0x3F8'e yaz.
     * Bunu gormek kernel'in baslayip baslamadigini kanitlar.
     * QEMU -serial stdio modunda BIOS zaten UART'i hazir birakir.
     * ══════════════════════════════════════════════════════════════════════════ */
    __asm__ volatile (
        "mov $0x3F8, %%dx\n\t"
        "mov $'[', %%al\n\t"  "out %%al, %%dx\n\t"
        "mov $'K', %%al\n\t"  "out %%al, %%dx\n\t"
        "mov $'M', %%al\n\t"  "out %%al, %%dx\n\t"
        "mov $']', %%al\n\t"  "out %%al, %%dx\n\t"
        "mov $0x0D, %%al\n\t" "out %%al, %%dx\n\t"
        "mov $0x0A, %%al\n\t" "out %%al, %%dx\n\t"
        : : : "eax", "edx"
    );

    /* ── BSS Sıfırlama: Uninitialized static/global alanları garantiye al ──── */
    for (uint8_t *p = __bss_start; p < __bss_end; p++) {
        *p = 0;
    }

    /* 🚀 Step -1: Klog init */
    klog_init();

    /* 🚀 Step 0: Serial port başlat */
    serial_init();
    serial_printf("\n");
    serial_printf("  +--------------------------------------------------------------+\n");
    serial_printf("  |                                                              |\n");
    serial_printf("  |   ZZZZZZZ  EEEEE  RRRRR   U   U  X   X                       |\n");
    serial_printf("  |        Z   E      R    R  U   U   X X                        |\n");
    serial_printf("  |       Z    EEE    RRRRR   U   U    X                         |\n");
    serial_printf("  |      Z     E      R  R    U   U   X X                        |\n");
    serial_printf("  |   ZZZZZZZ  EEEEE  R   R    UUU   X   X                       |\n");
    serial_printf("  |                                                              |\n");
    serial_printf("  |              ZeruX Operating System v0.1                     |\n");
    serial_printf("  |          IA-32 Educational Research Kernel                   |\n");
    serial_printf("  |                                                              |\n");
    serial_printf("  |          Build        : Jul 2026                             |\n");
    serial_printf("  |          Author       :                                      |\n");
    serial_printf("  |          Architecture : IA-32 Protected Mode                 |\n");
    serial_printf("  |                                                              |\n");
    serial_printf("  +--------------------------------------------------------------+\n\n");
    serial_printf("[STEP 0] Serial initialized successfully.\n");

    /* ── Step 3.0: Dynamic GDT Revision & Task State Segment (TSS / TR=0x28) ─ */
    gdt_init();

    /* ── Step 2.0: BIOS E820 Physical Memory Map Parser ───────────────────── */
    boot_info_init(boot_info);

    /* ── Step 2.1: Physical Memory Manager (PMM / Bitmap Allocator) ──────── */
    pmm_init(boot_info);
    pmm_run_test_suite();

    /* ── Step 2.2: Virtual Memory Manager (VMM / 32-bit Paging) ──────────── */
    vmm_init(boot_info);
    vmm_run_test_suite();

    /* ── Step 2.3: Kernel Heap Allocator (kmalloc / kfree) ────────────────── */
    kheap_init(boot_info);
    kheap_run_test_suite();

    /* ── Step 3.1 & 3.2: Process Control Block (PCB / task_t) & Task Manager ────── */
    task_init();
    task_run_test_suite();

    /* ── Step 1: CPU Protection Infrastructure (IDT & ISR 0..31) ──────────── */
    idt_init();      /* 256 IDT gate tablosunu sıfırla ve lidt yükle */
    serial_printf("[STEP 1a] IDT loaded.\n");

    isr_init();      /* 0..31 arası CPU Exception handler'larını kaydet */
    serial_printf("[STEP 1b] ISR handlers registered.\n");

    /* ── Step 1.4: 8259A PIC Remapping & Hardware IRQ Dispatcher (0..15) ───── */
    irq_init();      /* PIC1 (0x20) & PIC2 (0x28) remap, IRQ 0..15 IDT gates (32..47) */
    serial_printf("[STEP 1c] 8259A PIC remapped & Hardware IRQs 0..15 registered.\n");

    /* ── Step 1.5: Hardware Drivers (PIT Timer 100Hz & PS/2 Keyboard IRQ1) ─ */
    timer_init(100);    /* PIT Channel 0 @ 100 Hz (10 ms per tick) */
    keyboard_init();    /* PS/2 Keyboard IRQ1 hooked */

    /* ── Step 3.2: Preemptive Round-Robin Scheduler (100 Hz Time-Slicing) ─── */
    scheduler_init();

    /* 🎯 Step 3.4: System Call Engine (int 0x80 Gate DPL=3 & Dispatcher) 🛡️🔌 */
    syscall_init();

    /* ── Step 1.6: Hardware Buses & Storage (PCI Bus Scan & ATA/IDE Hard Disk) */
    pci_init();
    ata_init();

    /* Initialize GUI Subsystem */
    vbe_init(1024, 768, 32);
    fb_init();
    gui_init();

    if (g_vbe_enabled) {
        vbe_fill_rect(0, 0, g_vbe_width, g_vbe_height, 0x1E1E2E);
        vbe_draw_string(20, 20, "ZeruX OS v0.1 -- Booting...", 0xFFFFFF, 0x1E1E2E);
        vbe_refresh_screen();
        serial_printf("[VBE] Framebuffer Active (%ux%u).\n", g_vbe_width, g_vbe_height);
    }

    extern void wm_init(void);
    
    wm_init();
    
    mouse_init();       /* PS/2 Mouse IRQ12 hooked (must be after VBE init) */
    usb_init();         /* Generic USB core (device table reset) */
    uhci_init();        /* UHCI USB 1.1 — probes PCI, enumerates USB HID mouse */


    /* ── Step 1.8: Mount Filesystems ─────────────────────────────────────── */
    vfs_init();
    vfs_run_test_suite();

    /* ── Step 4.2b: Root File System (/) ───────────────────────────────────── */
    rootfs_init();

    /* ── Step 4.2c: RAM-based Writable User Filesystem (/user) ─────────────── */
    userfs_init();

    /* ── Step 4.3: Virtual Device File System (devfs - /dev/serial0, /dev/kbd) */
    devfs_init();
    devfs_run_test_suite();

    /* ── Step 4.4: Process File System (procfs - /proc/tasks, /proc/meminfo) ── */
    procfs_init();
    procfs_run_test_suite();

    /* ── Step 4.5: FAT32 Interoperable File System (/disk/fat0) ───────────── */
    fat32_init();
    fat32_run_test_suite();

    /* ── Step 1.7: Initialize Network (requires FAT32/VFS for config) ────── */
    extern bool rtl8168_init(void);
    extern void rtl8139_init(void);
    extern void net_init(void);
    extern void udp_init(void);
    extern void tcp_init(void);
    extern void dhcp_init(void);
    extern void dns_init(void);
    extern void net_load_config(void);
    
    if (!rtl8168_init()) {
        rtl8139_init();
    }
    fw_init();
    net_init();
    udp_init();
    tcp_init();
    dhcp_init();
    dns_init();
    
    /* Now that all protocols are initialized, load config (which may trigger DHCP) */
    net_load_config();
    


    /* (Shell init delayed until after GUI init to prevent splash screen overlapping) */

    /* ── Dynamic CPU state inspection ───────────────────────────────────── */
    uint32_t current_esp = cpu_read_esp();
    uint32_t cr0_val     = cpu_read_cr0();
    int is_protected     = (cr0_val & CR0_PE) ? 1 : 0;

    /* ── İlk debug mesajları — kernel yaşıyor kanıtı ─────────────────────── */
    serial_printf("\n");
    serial_printf("===========================================\n");
    serial_printf(" ZeruX OS v0.1  |  Kernel Alive\n");
    serial_printf("===========================================\n");
    serial_printf("[BOOT] Kernel loaded at  : %p (symbol: %p)\n", (void*)0x100000, (void*)_kernel_start);
    serial_printf("[BOOT] VGA buffer at     : %p\n", (void*)0xB8000);
    serial_printf("[BOOT] Stack pointer     : 0x%x (actual ESP register)\n", current_esp);
    serial_printf("[BOOT] CPU mode          : %s (CR0: 0x%x)\n",
                  is_protected ? "32-bit Protected Mode" : "Real Mode / Unknown", cr0_val);
    serial_printf("[BOOT] Serial port       : COM1 @ 0x%x (38400 baud, 8N1)\n", 0x3F8);
    serial_printf("[BOOT] IDT & ISR status  : Active (256 gates, CPU Exceptions 0..31 + IRQs 32..47)\n");
    serial_printf("===========================================\n\n");

    /* ── Step 2: Ekranı tamamen maviye boyat ─────────────────────────────── */
    serial_printf("[STEP 4] Enabling CPU hardware interrupts (sti)...\n");
    cpu_sti();              /* Set EFLAGS.IF = 1 to begin accepting IRQ0 & IRQ1 */

    /* Show Graphical Boot Logo for 2 seconds */
    if (g_vbe_enabled) {
        gfx_surface_t *boot_img = png_load("/disk/fat0/BOOT.PNG");
        if (boot_img) {
            gfx_surface_t *screen = fb_get_screen_surface();
            if (screen) {
                gfx_stretch_blit(screen, 0, 0, screen->width, screen->height, boot_img, 0, 0, boot_img->width, boot_img->height);
                /* fb_present takes dirty rects, but here we can just do dirty_mark_all */
                extern void dirty_mark_all(void);
                extern void fb_present(void);
                dirty_mark_all();
                fb_present();
            }
            timer_sleep_ms(2000);
            surface_destroy(boot_img);
        }
    }

    /* [Regression Tests] - Phase 4.1 */
    extern void kernel_selftest(void);
    kernel_selftest();

    /* ── Step 6: ZeruX OS Interactive VFS Shell CLI Interface ────────────── */
    shell_init();

    /* ── Step 7: Auto-Start WebOS Services ────────────── */
    serial_printf("[STEP 7] Starting WebOS HTTP & WebSocket Server...\n");
    extern void http_server_run(int argc, char **argv);
    char *http_args[] = {"httpserver", "80"};
    http_server_run(2, http_args);

    /* (User mode apps are executed from /bin/*.elf via shell/API) */
    /* run_usermode_app_demo(); */

    serial_printf("[STEP 5] Entering Kernel Main Idle Loop.\n");

    /* 🚀 Kernel Idle Loop - Serves hardware interrupts efficiently 🚀 */
    uint32_t last_uptime_sec = 0xFFFFFFFF;
    uint32_t last_gui_tick = 0;
    
    extern void net_poll(void);
    extern void tcp_timer_poll(void);
    
    bool g_wm_active = true;   /* yeni: WM'i deneme modu */
    
    while (1) {
        if (timer_get_ticks() - last_gui_tick >= 3) {
            if (g_wm_active) {
                extern void wm_update(void);
                extern void compositor_compose(void);
                wm_update();          /* mouse: drag / focus / close */
                compositor_compose(); /* shadow buffer'a çiz          */
                vbe_refresh_screen(); /* cursor overlay + VRAM'a bas   */
            } else if (g_gui_active) {
                gui_handle_events();
                gui_render();
            }
            last_gui_tick = timer_get_ticks();
        }
        
        /* Process deferred network packets (Soft-IRQ) */
        net_poll();
        /* Process TCP Retransmissions and Keep-Alives */
        tcp_timer_poll();

        cpu_hlt();  /* HLT instruction — awaits next hardware interrupt */

        /* Her 1 saniyede bir canlı uptime ve tick bilgisini hem VGA hem Seri Port'a yaz */
        uint32_t current_sec = timer_get_uptime_ms() / 1000;
        if (current_sec != last_uptime_sec) {
            last_uptime_sec = current_sec;

            /* VGA Status bar (satır 22) üzerindeki zamanlayıcıyı canlı güncelle */
            char status_buf[64];
            status_buf[0] = '[';
            status_buf[1] = ' ';
            status_buf[2] = 'S';
            status_buf[3] = 'y';
            status_buf[4] = 's';
            status_buf[5] = 't';
            status_buf[6] = 'e';
            status_buf[7] = 'm';
            status_buf[8] = ' ';
            status_buf[9] = 'R';
            status_buf[10] = 'u';
            status_buf[11] = 'n';
            status_buf[12] = 'n';
            status_buf[13] = 'i';
            status_buf[14] = 'n';
            status_buf[15] = 'g';
            status_buf[16] = ' ';
            status_buf[17] = '|';
            status_buf[18] = ' ';
            status_buf[19] = 'U';
            status_buf[20] = 'p';
            status_buf[21] = 't';
            status_buf[22] = 'i';
            status_buf[23] = 'm';
            status_buf[24] = 'e';
            status_buf[25] = ':';
            status_buf[26] = ' ';

            /* Uptime saniye rakamlarını yerleştir */
            char num_buf[16];
            uint32_t val = current_sec;
            int n_len = 0;
            if (val == 0) {
                num_buf[n_len++] = '0';
            } else {
                char tmp[16];
                int t_len = 0;
                while (val > 0) {
                    tmp[t_len++] = (char)('0' + (val % 10));
                    val /= 10;
                }
                for (int i = t_len - 1; i >= 0; i--) {
                    num_buf[n_len++] = tmp[i];
                }
            }
            num_buf[n_len] = '\0';

            int p = 27;
            for (int i = 0; i < n_len; i++) {
                status_buf[p++] = num_buf[i];
            }
            status_buf[p++] = 's';
            status_buf[p++] = ' ';
            status_buf[p++] = ']';
            status_buf[p] = '\0';

            vga_puts_centered(22, status_buf, vga_attr(COLOR_LIGHT_GREEN, COLOR_BLUE));

            /* Periodically flush logs to disk every 5 seconds for bare metal debugging */
            if (current_sec % 5 == 0) {
                klog_flush_to_disk();
            }
        }
    }
}

/* =============================================================================
 * kernel_selftest - Regression Tests (Phase 4.1)
 * =============================================================================
 */
void kernel_selftest(void) {
    serial_printf("\n[SELF-TEST] Starting Kernel Regression Tests...\n");
    
    /* 1. Memory Leak Test (PMM) */
    serial_printf("[SELF-TEST] PMM Leak Test: ");
    void *ptr1 = pmm_alloc_block();
    void *ptr2 = pmm_alloc_block();
    if (ptr1 && ptr2 && ptr1 != ptr2) {
        pmm_free_block(ptr2);
        pmm_free_block(ptr1);
        serial_printf("PASS\n");
    } else {
        serial_printf("FAIL\n");
    }
    
    /* 2. TCP Socket Allocation Test */
    serial_printf("[SELF-TEST] TCP Socket Alloc Test: ");
    extern int tcp_socket_open(void);
    extern void tcp_close(int);
    int sock_id = tcp_socket_open();
    if (sock_id >= 0) {
        tcp_close(sock_id);
        serial_printf("PASS\n");
    } else {
        serial_printf("FAIL\n");
    }
    
    /* 3. Advanced Network Tests */
    extern void test_net_all(void);
    test_net_all();
    
    serial_printf("[SELF-TEST] All Tests Completed.\n\n");
}

