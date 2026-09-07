/* =============================================================================
 * ZeruX OS — System Shell Commands
 * File: kernel/commands/cmd_sys.c
 * =============================================================================
 */

#include "shell_cmd.h"
#include "vfs.h"
#include "ports.h"
#include "rtc.h"
#include "klog.h"
#include "kheap.h"
#include "vbe_terminal.h"

int cmd_help(int argc, char **argv, char *out, uint32_t max) {
    (void)argc; (void)argv;
    uint32_t count = 0;
    const shell_command_t *cmds = shell_get_commands(&count);

    cmd_printf(out, max, "\n===========================================\n");
    cmd_printf(out, max, " ZeruX OS Interactive Commands\n");
    cmd_printf(out, max, "===========================================\n");

    for (uint32_t i = 0; i < count; i++) {
        cmd_printf(out, max, "  %-12s - %s\n", cmds[i].name, cmds[i].help);
    }
    cmd_printf(out, max, "===========================================\n\n");
    return 0;
}

int cmd_clear(int argc, char **argv, char *out, uint32_t max) {
    (void)argc; (void)argv;
    extern int g_vbe_enabled;
    if (!g_vbe_enabled) {
        vbe_term_init();
    }
    cmd_printf(out, max, "\n[SHELL] Screen cleared.\n");
    return 0;
}

int cmd_version(int argc, char **argv, char *out, uint32_t max) {
    (void)argc; (void)argv;
    int fd = vfs_open("/live/version", 0);
    if (fd >= 0) {
        char buf[256] = {0};
        int32_t bytes = vfs_read(fd, buf, sizeof(buf) - 1);
        vfs_close(fd);
        if (bytes > 0) cmd_printf(out, max, "[VERSION] %s\n", buf);
    } else {
        cmd_printf(out, max, "ZeruX OS v0.1 (IA-32 Architecture)\n");
    }
    return 0;
}

int cmd_date(int argc, char **argv, char *out, uint32_t max) {
    (void)argc; (void)argv;
    rtc_time_t t;
    rtc_read_time(&t);
    cmd_printf(out, max, "%02u:%02u:%02u  %02u/%02u/20%02u\n",
               t.hour, t.minute, t.second, t.day, t.month, t.year);
    return 0;
}

int cmd_dmesg(int argc, char **argv, char *out, uint32_t max) {
    (void)argc; (void)argv;
    char *buf = (char*)kmalloc(16384);
    if (buf) {
        uint32_t len = klog_dump(buf, 16383);
        buf[len] = '\0';
        cmd_printf(out, max, "%s\n", buf);
        kfree(buf);
    } else {
        cmd_printf(out, max, "dmesg: out of memory\n");
    }
    return 0;
}

int cmd_reboot(int argc, char **argv, char *out, uint32_t max) {
    (void)argc; (void)argv;
    cmd_printf(out, max, "\n[SYSTEM] Rebooting via 8042 Keyboard Controller...\n");
    outb(0x64, 0xFE);
    while (1) { /* Wait for CPU reset */ }
    return 0;
}

int cmd_shutdown(int argc, char **argv, char *out, uint32_t max) {
    (void)argc; (void)argv;
    cmd_printf(out, max, "\n[SYSTEM] Shutting down...\n");
    outw(0x604, 0x2000);  /* QEMU ACPI shutdown */
    outw(0xB004, 0x2000); /* Bochs / alternative shutdown */
    while (1) { /* Wait for power off */ }
    return 0;
}

int cmd_echo(int argc, char **argv, char *out, uint32_t max) {
    for (int i = 1; i < argc; i++) {
        cmd_printf(out, max, "%s%s", argv[i], (i == argc - 1) ? "" : " ");
    }
    cmd_printf(out, max, "\n");
    return 0;
}

