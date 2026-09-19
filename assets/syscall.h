#ifndef _USER_SYSCALL_H
#define _USER_SYSCALL_H

#include <stdint.h>

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

/* Phase 2 Networking (userland <-> kernel/arch/syscall.c: sys_socket_handler..sys_connect_handler) */
#define SYS_SOCKET  14
#define SYS_BIND    15
#define SYS_LISTEN  16
#define SYS_ACCEPT  17
#define SYS_CONNECT 18

/* FTP daemon destegi: /disk/fat0 kok dizininde (flat, alt klasorsuz)
 * yeni bir dosya yaratir (kernel/arch/syscall.c: sys_creat_handler). */
#define SYS_CREAT   19

/* Dosya tanimlayicilari */
#define STDIN  0
#define STDOUT 1
#define STDERR 2

/* open() flag'leri (kernel/include/fcntl.h ile birebir ayni) */
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0100
#define O_TRUNC     0x1000
#define O_APPEND    0x2000

/* ---- Minimal POSIX Socket API (kernel/include/socket.h ile birebir) ---- */
#define AF_INET     2
#define SOCK_STREAM 1
#define SOCK_DGRAM  2

struct sockaddr {
    unsigned short sa_family;
    char           sa_data[14];
};

struct sockaddr_in {
    unsigned short sin_family;
    unsigned short sin_port;   /* network byte order (buyuk-endian) olmali */
    unsigned int   sin_addr;   /* network byte order (buyuk-endian) olmali */
    char           sin_zero[8];
};

int syscall0(int eax);
int syscall1(int eax, int ebx);
int syscall2(int eax, int ebx, int ecx);
int syscall3(int eax, int ebx, int ecx, int edx);

/* Temel POSIX-benzeri fonksiyonlar */
int write(int fd, const void *buf, int count);
int getpid(void);
void yield(void);
void sleep(int ms);
void exit(int status);
int spawn(const char *path, const char **argv);
int waitpid(int pid);
int open(const char *path, int flags);
int read(int fd, void *buf, int count);
int close(int fd);
struct dirent;
struct dirent *readdir(int fd, int index);

/* Socket sarmalayicilari */
int socket(int domain, int type, int protocol);
int bind(int sockfd, const struct sockaddr *addr, unsigned int addrlen);
int listen(int sockfd, int backlog);
int accept(int sockfd, struct sockaddr *addr, unsigned int *addrlen);
int connect(int sockfd, const struct sockaddr *addr, unsigned int addrlen);

/* /disk/fat0 kokunde (flat) yeni dosya yaratir. Basariysa 0, hata olursa -1 doner. */
int creat_fat0(const char *path);

#endif
