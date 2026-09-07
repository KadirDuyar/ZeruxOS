#include "stdio.h"
#include "syscall.h"
#include "string.h"

void putchar(char c) {
    write(STDOUT, &c, 1);
}

static void print_int(int val, int base) {
    if (val == 0) {
        putchar('0');
        return;
    }
    
    if (val < 0 && base == 10) {
        putchar('-');
        val = -val;
    }
    
    char buf[32];
    int i = 0;
    while (val > 0) {
        int rem = val % base;
        buf[i++] = (rem < 10) ? (rem + '0') : (rem - 10 + 'a');
        val /= base;
    }
    
    while (i > 0) {
        putchar(buf[--i]);
    }
}

void printf(const char *format, ...) {
    va_list args;
    va_start(args, format);

    while (*format) {
        if (*format == '%') {
            format++;
            switch (*format) {
                case 's': {
                    char *s = va_arg(args, char *);
                    if (!s) s = "(null)";
                    write(STDOUT, s, strlen(s));
                    break;
                }
                case 'd': {
                    int i = va_arg(args, int);
                    print_int(i, 10);
                    break;
                }
                case 'u': {
                    unsigned int u = va_arg(args, unsigned int);
                    print_int((int)u, 10);
                    break;
                }
                case 'x': {
                    int i = va_arg(args, int);
                    print_int(i, 16);
                    break;
                }
                case 'c': {
                    char c = (char)va_arg(args, int);
                    putchar(c);
                    break;
                }
                default:
                    putchar('%');
                    putchar(*format);
                    break;
            }
        } else {
            putchar(*format);
        }
        format++;
    }

    va_end(args);
}
