/* =============================================================================
 * ZeruX OS — Central Shell Dispatcher & Command Registry
 * File: kernel/shell/shell_dispatch.c
 * =============================================================================
 */

#include "shell_cmd.h"
#include "elf.h"
#include "task.h"
#include "libc.h"

/* Central Command Registry Table */
static const shell_command_t g_commands[] = {
    /* System Commands */
    { "help",        cmd_help,        "Show this command help menu" },
    { "clear",       cmd_clear,       "Clear screen" },
    { "echo",        cmd_echo,        "Print text arguments to output" },
    { "version",     cmd_version,     "Print operating system version" },
    { "date",        cmd_date,        "Display current hardware RTC time/date" },
    { "dmesg",       cmd_dmesg,       "Dump kernel log ring buffer" },
    { "reboot",      cmd_reboot,      "Reboot computer via 8042 controller" },
    { "shutdown",    cmd_shutdown,    "Power off computer via ACPI/APM" },

    /* Task & Service Commands */
    { "ps",          cmd_ps,          "List running kernel tasks and processes" },
    { "meminfo",     cmd_meminfo,     "Show physical and virtual memory usage" },
    { "service",     cmd_service,     "Manage services (network restart | desktop start/stop)" },

    /* Filesystem Commands */
    { "pwd",         cmd_pwd,         "Print current working directory" },
    { "cd",          cmd_cd,          "Change directory" },
    { "ls",          cmd_ls,          "List directory contents" },
    { "cat",         cmd_cat,         "Display file content" },
    { "mkdir",       cmd_mkdir,       "Create directory" },
    { "touch",       cmd_touch,       "Create empty file" },
    { "rm",          cmd_rm,          "Delete file or directory" },
    { "edit",        cmd_edit,        "Simple in-terminal text editor" },
    { "ls-pci",      cmd_lspci,       "Dump discovered PCI devices" },
    { "run",         cmd_run,         "Execute user ELF binary (e.g. run /disk/fat0/BIN/HELLO.ELF)" },

    /* Network Commands */
    { "ifconfig",    cmd_ifconfig,    "Show network interface IP, MAC, gateway" },
    { "dhcp",        cmd_dhcp,        "Request IP configuration via DHCP" },
    { "ping",        cmd_ping,        "Send ICMP echo request to IP/host" },
    { "netstat",     cmd_netstat,     "Show active network sockets & statistics" },
    { "host",        cmd_host,        "Resolve hostname via DNS" },
    { "wget",        cmd_wget,        "Download file over HTTP" },
    { "httpserver",  cmd_httpserver,  "Start built-in WebOS HTTP server" },
    { "tcp",         cmd_tcp,         "Test TCP connection to host:port" },
    { "tcpdump",     cmd_tcpdump,     "Inspect raw captured network packets" },
    { "firewall",    cmd_firewall,    "Manage Layer-4 packet filter rules" },
    { "aurora",      cmd_aurora,      "Open graphical Aurora web browser" }
};

#define COMMAND_COUNT (sizeof(g_commands) / sizeof(g_commands[0]))

const shell_command_t* shell_get_commands(uint32_t *count) {
    if (count) *count = COMMAND_COUNT;
    return g_commands;
}

const shell_command_t* shell_find_command(const char *name) {
    if (!name) return NULL;
    for (uint32_t i = 0; i < COMMAND_COUNT; i++) {
        if (kstrcmp(g_commands[i].name, name) == 0) {
            return &g_commands[i];
        }
    }
    return NULL;
}

int shell_dispatch(int argc, char **argv, char *out_buf, uint32_t out_max) {
    if (argc <= 0 || !argv || !argv[0] || argv[0][0] == '\0') {
        return 0;
    }

    /* 1. Check registered commands */
    const shell_command_t *cmd = shell_find_command(argv[0]);
    if (cmd) {
        return cmd->handler(argc, argv, out_buf, out_max);
    }

    /* 2. Fallback: Search in /disk/fat0/BIN/<NAME>.ELF */
    char bin_path[128] = "/disk/fat0/BIN/";
    int name_len = 0;
    while (argv[0][name_len] && name_len < 32) name_len++;

    if (name_len > 0 && name_len < 32) {
        for (int i = 0; i < name_len; i++) {
            char c = argv[0][i];
            if (c >= 'a' && c <= 'z') c -= 32; /* Upper case for FAT32 */
            bin_path[15 + i] = c;
        }
        bin_path[15 + name_len]     = '.';
        bin_path[15 + name_len + 1] = 'E';
        bin_path[15 + name_len + 2] = 'L';
        bin_path[15 + name_len + 3] = 'F';
        bin_path[15 + name_len + 4] = '\0';

        int pid = elf32_load_and_exec(bin_path, argc, (const char**)argv);
        if (pid > 0) {
            int status = task_waitpid(pid);
            return status;
        }
    }

    cmd_printf(out_buf, out_max, "zerux: command not found: '%s' (type 'help' for available commands)\n", argv[0]);
    return -1;
}
