#include "syscall.h"

int syscall0(int eax) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(eax));
    return ret;
}

int syscall1(int eax, int ebx) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(eax), "b"(ebx));
    return ret;
}

int syscall2(int eax, int ebx, int ecx) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(eax), "b"(ebx), "c"(ecx));
    return ret;
}

int syscall3(int eax, int ebx, int ecx, int edx) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx));
    return ret;
}

int write(int fd, const void *buf, int count) {
    return syscall3(SYS_WRITE, fd, (int)buf, count);
}

int getpid(void) {
    return syscall0(SYS_GETPID);
}

void yield(void) {
    syscall0(SYS_YIELD);
}

void sleep(int ms) {
    syscall1(SYS_SLEEP, ms);
}

void exit(int status) {
    syscall1(SYS_EXIT, status);
    while (1);
}

int spawn(const char *path, const char **argv) {
    return syscall2(SYS_SPAWN, (int)path, (int)argv);
}

int waitpid(int pid) {
    return syscall1(SYS_WAITPID, pid);
}

int open(const char *path, int flags) {
    return syscall2(SYS_OPEN, (int)path, flags);
}

int read(int fd, void *buf, int count) {
    return syscall3(SYS_READ, fd, (int)buf, count);
}

int close(int fd) {
    return syscall1(SYS_CLOSE, fd);
}
struct dirent *readdir(int fd, int index) { return (struct dirent *)syscall2(SYS_READDIR, fd, index); }

int socket(int domain, int type, int protocol) {
    return syscall3(SYS_SOCKET, domain, type, protocol);
}

int bind(int sockfd, const struct sockaddr *addr, unsigned int addrlen) {
    return syscall3(SYS_BIND, sockfd, (int)addr, (int)addrlen);
}

int listen(int sockfd, int backlog) {
    return syscall2(SYS_LISTEN, sockfd, backlog);
}

int accept(int sockfd, struct sockaddr *addr, unsigned int *addrlen) {
    return syscall3(SYS_ACCEPT, sockfd, (int)addr, (int)addrlen);
}

int connect(int sockfd, const struct sockaddr *addr, unsigned int addrlen) {
    return syscall3(SYS_CONNECT, sockfd, (int)addr, (int)addrlen);
}

int creat_fat0(const char *path) {
    return syscall1(SYS_CREAT, (int)path);
}
