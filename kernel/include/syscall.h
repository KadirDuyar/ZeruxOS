/* =============================================================================
 * ZeruX OS — System Call (int 0x80) Interface & Table
 * File: kernel/include/syscall.h
 * =============================================================================
 *
 * Ring 3 (User Mode) uygulamalarının çekirdek (Ring 0) hizmetlerine güvenli
 * erişimini sağlayan `int 0x80` sistem çağrıları altyapısı.
 *
 * Calling Convention:
 *   EAX = Syscall Number
 *   EBX = Parameter 1
 *   ECX = Parameter 2
 *   EDX = Parameter 3
 *   Return value in EAX
 * =============================================================================
 */

#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#include <stddef.h>
#include "isr.h"

/* Syscall Numaraları */
#define SYS_WRITE   1
#define SYS_GETPID  2
#define SYS_YIELD   3
#define SYS_SLEEP   4
#define SYS_EXIT    5
#define SYS_SPAWN   6
#define SYS_WAITPID 7
#define SYS_OPEN    8
#define SYS_READ    9
#define SYS_CLOSE   10
#define SYS_READDIR 11
#define SYS_PIPE    12
#define SYS_SBRK    13

#define MAX_SYSCALLS 64

/* Public API Bildirimleri */
void syscall_init(void);
int32_t syscall_handler_c(registers_t *regs);

/* kstrlen: Freestanding string length (no libc) */
static inline uint32_t kstrlen(const char *s) {
    uint32_t len = 0;
    while (s && s[len]) len++;
    return len;
}

/* user_write_str: compile-time literal OR runtime pointer — both work */
#define user_write_str(fd, s) user_write((fd), (s), kstrlen(s))

static inline int32_t user_write(int fd, const char *buf, uint32_t len) {
    int32_t ret;
    __asm__ __volatile__ (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_WRITE), "b"(fd), "c"(buf), "d"(len)
        : "memory"
    );
    return ret;
}

static inline uint32_t user_getpid(void) {
    uint32_t ret;
    __asm__ __volatile__ (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_GETPID)
        : "memory"
    );
    return ret;
}

static inline void user_yield(void) {
    __asm__ __volatile__ (
        "int $0x80"
        :
        : "a"(SYS_YIELD)
        : "memory"
    );
}

static inline void user_sleep(uint32_t ms) {
    __asm__ __volatile__ (
        "int $0x80"
        :
        : "a"(SYS_SLEEP), "b"(ms)
        : "memory"
    );
}

static inline void user_exit(int32_t status) {
    __asm__ __volatile__ (
        "int $0x80"
        :
        : "a"(SYS_EXIT), "b"(status)
        : "memory"
    );
}

static inline void user_close(int fd) {
    __asm__ __volatile__("int $0x80" : : "a"(SYS_CLOSE), "b"(fd) : "memory");
}

/* User Mode Wrapper: Pipe (Boru Hattı) */
static inline int user_pipe(int fds[2]) {
    int ret;
    __asm__ __volatile__("int $0x80" : "=a"(ret) : "a"(SYS_PIPE), "b"(fds) : "memory");
    return ret;
}

/* User Mode Wrapper: Waitpid */
static inline int user_waitpid(int pid) {
    int ret;
    __asm__ __volatile__("int $0x80" : "=a"(ret) : "a"(SYS_WAITPID), "b"(pid) : "memory");
    return ret;
}

/* User Mode Wrapper: Sbrk */
static inline void* user_sbrk(int increment) {
    void* ret;
    __asm__ __volatile__("int $0x80" : "=a"(ret) : "a"(SYS_SBRK), "b"(increment) : "memory");
    return ret;
}

#endif /* SYSCALL_H */
