/* =============================================================================
 * ZeruX OS — Shell / Command Line Interface (CLI) Header
 * File: kernel/include/shell.h
 * =============================================================================
 *
 * ZeruX Kabuk (Shell CLI Interface):
 *   - VFS katmanı üzerinden çalışan etkileşimli komut satırı arayüzü.
 *   - Komutlar: help, ls, cat, ps, meminfo, version, clear
 * =============================================================================
 */

#ifndef SHELL_H
#define SHELL_H

#include <stdint.h>
#include <stddef.h>

void shell_init(void);
void shell_update(void);
void shell_execute_command(const char *cmdline);
int  shell_execute_command_to_buffer(const char *cmdline, char *out_buf, uint32_t max_len);

#endif /* SHELL_H */
