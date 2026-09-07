#ifndef LIBC_H
#define LIBC_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

static inline size_t kstrlen(const char *str) {
    size_t len = 0;
    while (str && str[len]) len++;
    return len;
}

static inline int kstrcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++; s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static inline int kstrncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++; s2++; n--;
    }
    if (n == 0) return 0;
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static inline char* kstrcpy(char *dest, const char *src) {
    char *d = dest;
    while (*src) *d++ = *src++;
    *d = '\0';
    return dest;
}



static inline int katoi(const char *str) {
    int res = 0;
    while (*str >= '0' && *str <= '9') {
        res = res * 10 + (*str - '0');
        str++;
    }
    return res;
}

/* Very basic ksprintf */
static inline int ksprintf(char *buf, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char *ptr = buf;
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            if (*fmt == 's') {
                const char *s = va_arg(args, const char *);
                if (!s) s = "(null)";
                while (*s) *ptr++ = *s++;
            } else if (*fmt == 'd') {
                int d = va_arg(args, int);
                if (d < 0) {
                    *ptr++ = '-';
                    d = -d;
                }
                char num[16];
                int i = 0;
                if (d == 0) num[i++] = '0';
                while (d > 0) {
                    num[i++] = (d % 10) + '0';
                    d /= 10;
                }
                while (i > 0) *ptr++ = num[--i];
            } else {
                *ptr++ = *fmt;
            }
        } else {
            *ptr++ = *fmt;
        }
        fmt++;
    }
    *ptr = '\0';
    va_end(args);
    return ptr - buf;
}

#endif
