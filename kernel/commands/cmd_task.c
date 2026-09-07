/* =============================================================================
 * ZeruX OS — Process & Service Shell Commands
 * File: kernel/commands/cmd_task.c
 * =============================================================================
 */

#include "shell_cmd.h"
#include "vfs.h"
#include "libc.h"

int cmd_ps(int argc, char **argv, char *out, uint32_t max) {
    (void)argc; (void)argv;
    int fd = vfs_open("/live/tasks", 0);
    if (fd >= 0) {
        char buf[512] = {0};
        int32_t bytes = vfs_read(fd, buf, sizeof(buf) - 1);
        vfs_close(fd);
        if (bytes > 0) {
            cmd_printf(out, max, "\n%s\n", buf);
            return 0;
        }
    }
    cmd_printf(out, max, "ps: failed to read /live/tasks\n");
    return -1;
}

int cmd_meminfo(int argc, char **argv, char *out, uint32_t max) {
    (void)argc; (void)argv;
    int fd = vfs_open("/live/meminfo", 0);
    if (fd >= 0) {
        char buf[512] = {0};
        int32_t bytes = vfs_read(fd, buf, sizeof(buf) - 1);
        vfs_close(fd);
        if (bytes > 0) {
            cmd_printf(out, max, "\n%s\n", buf);
            return 0;
        }
    }
    cmd_printf(out, max, "meminfo: failed to read /live/meminfo\n");
    return -1;
}

int cmd_service(int argc, char **argv, char *out, uint32_t max) {
    if (argc < 3) {
        cmd_printf(out, max, "Usage: service <name> <action>\n");
        cmd_printf(out, max, "Examples:\n");
        cmd_printf(out, max, "  service network restart\n");
        cmd_printf(out, max, "  service desktop start\n");
        cmd_printf(out, max, "  service desktop stop\n");
        return -1;
    }

    const char *service = argv[1];
    const char *action  = argv[2];

    if (kstrcmp(service, "network") == 0 && kstrcmp(action, "restart") == 0) {
        cmd_printf(out, max, "[SERVICE] Restarting Network Stack...\n");
        extern void net_load_config(void);
        net_load_config();
        return 0;
    }

    if (kstrcmp(service, "desktop") == 0) {
        if (kstrcmp(action, "start") == 0) {
            cmd_printf(out, max, "[SERVICE] Starting Graphical Desktop Environment...\n");
            extern void gui_init(void);
            gui_init();
            return 0;
        } else if (kstrcmp(action, "stop") == 0) {
            cmd_printf(out, max, "[SERVICE] Stopping Graphical Desktop Environment...\n");
            extern bool g_gui_active;
            g_gui_active = false;
            return 0;
        }
    }

    cmd_printf(out, max, "service: Unknown service '%s' or action '%s'\n", service, action);
    return -1;
}
