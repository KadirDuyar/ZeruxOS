/* =============================================================================
 * ZeruX OS — Interactive Shell REPL & Line Reader
 * File: kernel/shell/shell_core.c
 * =============================================================================
 */

#include "shell.h"
#include "shell_cmd.h"
#include "task.h"
#include "serial.h"
#include "keyboard.h"
#include "vbe_terminal.h"
#include "vga.h"
#include "libc.h"

#define HISTORY_MAX 10
static char g_history[HISTORY_MAX][SHELL_MAX_LINE];
static int  g_hist_count = 0;
static int  g_hist_current = -1;

static char g_line_buf[SHELL_MAX_LINE];
static uint32_t g_line_pos = 0;
static int g_esc_state = 0;

/* 0 = BOTH, 1 = UART, 2 = GUI */
static int s_shell_backend = 1;

void shell_execute_command(const char *cmdline) {
    if (!cmdline || cmdline[0] == '\0') return;

    int argc = 0;
    char *argv[SHELL_MAX_ARGS];
    char work_buf[SHELL_MAX_LINE];

    if (shell_tokenize(cmdline, &argc, argv, work_buf, sizeof(work_buf)) == 0) {
        shell_dispatch(argc, argv, NULL, 0);
    }
}

int shell_execute_command_to_buffer(const char *cmdline, char *out_buf, uint32_t max_len) {
    if (!cmdline || cmdline[0] == '\0' || !out_buf || max_len == 0) return -1;
    out_buf[0] = '\0';

    int argc = 0;
    char *argv[SHELL_MAX_ARGS];
    char work_buf[SHELL_MAX_LINE];

    if (shell_tokenize(cmdline, &argc, argv, work_buf, sizeof(work_buf)) == 0) {
        shell_dispatch(argc, argv, out_buf, max_len);
        return (int)kstrlen(out_buf);
    }
    return 0;
}

static void shell_prompt(void) {
    vbe_term_set_color(VBE_TERM_FG_PROMPT, VBE_TERM_BG_DEFAULT);
    serial_printf("zerux:%s# ", g_cwd);
    vbe_term_reset_color();
}

static void shell_task_entry(void) {
    task_sleep_ms(500);

    g_cwd[0] = '/';
    g_cwd[1] = '\0';

    s_shell_backend = 1; /* UART Mode by default */
    serial_set_vbe_mirror(0);
    serial_printf("\n[SHELL] Modular Interactive Shell Activated!\n");
    serial_printf("[SHELL REPL] Type 'help' for available commands.\n");
    shell_prompt();

    g_line_pos = 0;
    g_line_buf[0] = '\0';

    while (1) {
        char c = 0;

        if (s_shell_backend == 2 && keyboard_has_char()) c = keyboard_getchar();
        else if (s_shell_backend == 1 && serial_has_char()) c = serial_getchar();
        else if (s_shell_backend == 0) {
            if (keyboard_has_char()) c = keyboard_getchar();
            else if (serial_has_char()) c = serial_getchar();
        }

        if (c == 0) {
            task_sleep_ms(10);
            continue;
        }

        /* ANSI Escape Sequence Parser */
        if (g_esc_state == 0) {
            if (c == 27) { g_esc_state = 1; continue; }
        } else if (g_esc_state == 1) {
            if (c == '[') { g_esc_state = 2; continue; }
            else { g_esc_state = 0; }
        } else if (g_esc_state == 2) {
            g_esc_state = 0;
            if (c == 'A') { /* UP Arrow: Previous command */
                if (g_hist_count > 0) {
                    if (g_hist_current == -1) g_hist_current = g_hist_count - 1;
                    else if (g_hist_current > 0) g_hist_current--;

                    while (g_line_pos > 0) {
                        serial_putchar('\b'); serial_putchar(' '); serial_putchar('\b');
                        vga_putchar('\b');
                        g_line_pos--;
                    }
                    const char *cmd = g_history[g_hist_current];
                    while (*cmd && g_line_pos < SHELL_MAX_LINE - 1) {
                        g_line_buf[g_line_pos++] = *cmd;
                        serial_putchar(*cmd);
                        vga_putchar(*cmd);
                        cmd++;
                    }
                    g_line_buf[g_line_pos] = '\0';
                }
                continue;
            } else if (c == 'B') { /* DOWN Arrow: Next command */
                if (g_hist_count > 0 && g_hist_current != -1) {
                    if (g_hist_current < g_hist_count - 1) {
                        g_hist_current++;
                        while (g_line_pos > 0) {
                            serial_putchar('\b'); serial_putchar(' '); serial_putchar('\b');
                            vga_putchar('\b');
                            g_line_pos--;
                        }
                        const char *cmd = g_history[g_hist_current];
                        while (*cmd && g_line_pos < SHELL_MAX_LINE - 1) {
                            g_line_buf[g_line_pos++] = *cmd;
                            serial_putchar(*cmd);
                            vga_putchar(*cmd);
                            cmd++;
                        }
                        g_line_buf[g_line_pos] = '\0';
                    } else {
                        g_hist_current = -1;
                        while (g_line_pos > 0) {
                            serial_putchar('\b'); serial_putchar(' '); serial_putchar('\b');
                            vga_putchar('\b');
                            g_line_pos--;
                        }
                        g_line_buf[0] = '\0';
                    }
                }
                continue;
            }
        }

        /* Handle Enter */
        if (c == '\n' || c == '\r') {
            serial_printf("\n");
            vga_putchar('\n');
            g_line_buf[g_line_pos] = '\0';

            if (g_line_pos > 0) {
                if (g_hist_count < HISTORY_MAX) {
                    kstrcpy(g_history[g_hist_count++], g_line_buf);
                } else {
                    for (int i = 1; i < HISTORY_MAX; i++) kstrcpy(g_history[i - 1], g_history[i]);
                    kstrcpy(g_history[HISTORY_MAX - 1], g_line_buf);
                }
                g_hist_current = -1;
                shell_execute_command(g_line_buf);
            }

            g_line_pos = 0;
            g_line_buf[0] = '\0';
            shell_prompt();
            continue;
        }

        /* Handle Backspace */
        if (c == '\b' || c == 0x7F) {
            if (g_line_pos > 0) {
                g_line_pos--;
                g_line_buf[g_line_pos] = '\0';
                serial_putchar('\b');
                serial_putchar(' ');
                serial_putchar('\b');
                vga_putchar('\b');
            }
            continue;
        }

        /* Regular printable character */
        if (c >= 32 && c <= 126) {
            if (g_line_pos < SHELL_MAX_LINE - 1) {
                g_line_buf[g_line_pos++] = c;
                g_line_buf[g_line_pos] = '\0';
                serial_putchar(c);
                vga_putchar(c);
            }
        }
    }
}

void shell_init(void) {
    kthread_create(shell_task_entry, "Shell_REPL");
    serial_printf("[STEP 6] Shell REPL Task Registered with Scheduler.\n");
}
