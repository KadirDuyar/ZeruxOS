/* =============================================================================
 * ZeruX OS — Shell Command Line Parser & Formatter
 * File: kernel/shell/shell_parser.c
 * =============================================================================
 */

#include "shell_cmd.h"
#include "serial.h"
#include "libc.h"
#include <stdarg.h>

/* Central command line tokenizer:
 * Splits input string into words separated by spaces/tabs.
 * Handles quoted arguments ("hello world").
 * Fills argv array and sets argc.
 */
int shell_tokenize(const char *cmdline, int *argc, char **argv, char *buf, uint32_t buf_sz) {
    if (!cmdline || !argc || !argv || !buf || buf_sz == 0) return -1;

    *argc = 0;
    argv[0] = NULL;

    /* Copy cmdline into working buffer */
    uint32_t len = 0;
    while (cmdline[len] && len < buf_sz - 1) {
        buf[len] = cmdline[len];
        len++;
    }
    buf[len] = '\0';

    char *p = buf;
    int count = 0;

    while (*p && count < SHELL_MAX_ARGS - 1) {
        /* Skip leading whitespace */
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            *p++ = '\0';
        }
        if (*p == '\0') break;

        /* Check for quotes */
        if (*p == '"') {
            p++; /* Skip opening quote */
            argv[count++] = p;
            while (*p && *p != '"') p++;
            if (*p == '"') {
                *p++ = '\0'; /* Terminate quoted string */
            }
        } else {
            argv[count++] = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
        }
    }

    argv[count] = NULL;
    *argc = count;
    return (count > 0) ? 0 : -1;
}

/* Output helper:
 * If out_buf is NULL, prints directly to serial_printf.
 * If out_buf is provided, appends formatted string into out_buf (up to out_max).
 */
void cmd_printf(char *out_buf, uint32_t out_max, const char *fmt, ...) {
    char temp[1024];
    va_list args;
    va_start(args, fmt);

    /* Format string using libc helper */
    char *ptr = temp;
    char *end = temp + sizeof(temp) - 1;

    while (*fmt && ptr < end) {
        if (*fmt == '%') {
            fmt++;
            if (*fmt == 's') {
                const char *s = va_arg(args, const char *);
                if (!s) s = "(null)";
                while (*s && ptr < end) *ptr++ = *s++;
            } else if (*fmt == 'd' || *fmt == 'i') {
                int d = va_arg(args, int);
                if (d < 0) {
                    if (ptr < end) *ptr++ = '-';
                    d = -d;
                }
                char num_buf[16];
                int n_idx = 0;
                if (d == 0) num_buf[n_idx++] = '0';
                else {
                    while (d > 0 && n_idx < 15) {
                        num_buf[n_idx++] = (char)('0' + (d % 10));
                        d /= 10;
                    }
                }
                for (int i = n_idx - 1; i >= 0 && ptr < end; i--) *ptr++ = num_buf[i];
            } else if (*fmt == 'u') {
                uint32_t u = va_arg(args, uint32_t);
                char num_buf[16];
                int n_idx = 0;
                if (u == 0) num_buf[n_idx++] = '0';
                else {
                    while (u > 0 && n_idx < 15) {
                        num_buf[n_idx++] = (char)('0' + (u % 10));
                        u /= 10;
                    }
                }
                for (int i = n_idx - 1; i >= 0 && ptr < end; i--) *ptr++ = num_buf[i];
            } else if (*fmt == 'x' || *fmt == 'X') {
                uint32_t x = va_arg(args, uint32_t);
                const char *hex = (*fmt == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
                char num_buf[16];
                int n_idx = 0;
                if (x == 0) num_buf[n_idx++] = '0';
                else {
                    while (x > 0 && n_idx < 15) {
                        num_buf[n_idx++] = hex[x & 0x0F];
                        x >>= 4;
                    }
                }
                for (int i = n_idx - 1; i >= 0 && ptr < end; i--) *ptr++ = num_buf[i];
            } else if (*fmt == 'c') {
                char c = (char)va_arg(args, int);
                if (ptr < end) *ptr++ = c;
            } else if (*fmt == '%') {
                if (ptr < end) *ptr++ = '%';
            }
            fmt++;
        } else {
            *ptr++ = *fmt++;
        }
    }
    *ptr = '\0';
    va_end(args);

    if (out_buf && out_max > 0) {
        /* Append to out_buf */
        uint32_t cur_len = 0;
        while (out_buf[cur_len] && cur_len < out_max - 1) cur_len++;

        uint32_t src_idx = 0;
        while (temp[src_idx] && cur_len < out_max - 1) {
            out_buf[cur_len++] = temp[src_idx++];
        }
        out_buf[cur_len] = '\0';
    } else {
        /* Direct output to serial terminal */
        serial_printf("%s", temp);
    }
}
