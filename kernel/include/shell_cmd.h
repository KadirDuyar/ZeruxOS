/* =============================================================================
 * ZeruX OS — Modular Shell & Commands Interface
 * File: kernel/include/shell_cmd.h
 * =============================================================================
 */

#ifndef SHELL_CMD_H
#define SHELL_CMD_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define SHELL_MAX_ARGS  32
#define SHELL_MAX_LINE  256

/* Command handler signature:
 * argc, argv: parsed arguments (argv[0] is command name)
 * out_buf, out_max: optional buffer for output (used by GUI terminal_app)
 * Returns 0 on success, non-zero on error.
 */
typedef int (*cmd_handler_t)(int argc, char **argv, char *out_buf, uint32_t out_max);

typedef struct {
    const char    *name;
    cmd_handler_t  handler;
    const char    *help;
} shell_command_t;

/* Output helper: prints to out_buf if provided, or to serial_printf if out_buf is NULL */
void cmd_printf(char *out_buf, uint32_t out_max, const char *fmt, ...);

/* Central Tokenizer: breaks cmdline into argc / argv */
int shell_tokenize(const char *cmdline, int *argc, char **argv, char *buf, uint32_t buf_sz);

/* Dispatcher: finds and runs command from registry or falls back to /disk/fat0/BIN/app.ELF */
int shell_dispatch(int argc, char **argv, char *out_buf, uint32_t out_max);

/* Command Table Access */
const shell_command_t* shell_get_commands(uint32_t *count);
const shell_command_t* shell_find_command(const char *name);

/* CWD (Current Working Directory) Global State */
extern char g_cwd[256];
void path_join(const char *base, const char *child, char *out, size_t out_sz);
void resolve_path(const char *input, char *out, size_t out_sz);
int  is_fat32_cwd(void);
int  is_user_cwd(void);
uint32_t fat32_cwd_cluster(void);

/* Execution helpers */
void shell_execute_command(const char *cmdline);
int  shell_execute_command_to_buffer(const char *cmdline, char *out_buf, uint32_t max_len);

/* Command Handlers: System */
int cmd_help(int argc, char **argv, char *out, uint32_t max);
int cmd_clear(int argc, char **argv, char *out, uint32_t max);
int cmd_echo(int argc, char **argv, char *out, uint32_t max);
int cmd_version(int argc, char **argv, char *out, uint32_t max);
int cmd_date(int argc, char **argv, char *out, uint32_t max);
int cmd_dmesg(int argc, char **argv, char *out, uint32_t max);
int cmd_reboot(int argc, char **argv, char *out, uint32_t max);
int cmd_shutdown(int argc, char **argv, char *out, uint32_t max);

/* Command Handlers: Task */
int cmd_ps(int argc, char **argv, char *out, uint32_t max);
int cmd_meminfo(int argc, char **argv, char *out, uint32_t max);
int cmd_service(int argc, char **argv, char *out, uint32_t max);

/* Command Handlers: Filesystem */
int cmd_pwd(int argc, char **argv, char *out, uint32_t max);
int cmd_cd(int argc, char **argv, char *out, uint32_t max);
int cmd_ls(int argc, char **argv, char *out, uint32_t max);
int cmd_cat(int argc, char **argv, char *out, uint32_t max);
int cmd_mkdir(int argc, char **argv, char *out, uint32_t max);
int cmd_touch(int argc, char **argv, char *out, uint32_t max);
int cmd_rm(int argc, char **argv, char *out, uint32_t max);
int cmd_edit(int argc, char **argv, char *out, uint32_t max);
int cmd_lspci(int argc, char **argv, char *out, uint32_t max);
int cmd_run(int argc, char **argv, char *out, uint32_t max);

/* Command Handlers: Network */
int cmd_ifconfig(int argc, char **argv, char *out, uint32_t max);
int cmd_dhcp(int argc, char **argv, char *out, uint32_t max);
int cmd_ping(int argc, char **argv, char *out, uint32_t max);
int cmd_netstat(int argc, char **argv, char *out, uint32_t max);
int cmd_host(int argc, char **argv, char *out, uint32_t max);
int cmd_wget(int argc, char **argv, char *out, uint32_t max);
int cmd_httpserver(int argc, char **argv, char *out, uint32_t max);
int cmd_tcp(int argc, char **argv, char *out, uint32_t max);
int cmd_tcpdump(int argc, char **argv, char *out, uint32_t max);
int cmd_firewall(int argc, char **argv, char *out, uint32_t max);
int cmd_aurora(int argc, char **argv, char *out, uint32_t max);

#endif /* SHELL_CMD_H */
