#include "zx_socket.h"
#include "../../core/zx_error.h"
#include "../../core/zx_handle.h"

#define SYS_WRITE   1
#define SYS_READ    7
#define SYS_SOCKET  14
#define SYS_CONNECT 18

extern int syscall3(int sys_num, int arg1, int arg2, int arg3);
extern int syscall1(int sys_num, int arg1);

/* Basit htons / inet_addr implementasyonlari stub */
static WORD host_to_net_short(WORD v) { return (v >> 8) | (v << 8); }

static DWORD parse_ip(const char* ip_str) {
    /* Basit IP parsing stub (Kernel tarafindaki arp/ip stack'e baglanacak).
       Ornek: "192.168.1.10" -> 0x0A01A8C0 (little endian)
       Simdilik dummy bir IP donelim (0x0). */
    (void)ip_str;
    return 0x0; 
}

HANDLE SocketCreate(DWORD socket_type) {
    int domain = 2; /* AF_INET */
    int type = 1;   /* SOCK_STREAM (TCP default) */
    
    if (socket_type == ZX_SOCKET_UDP) type = 2; /* SOCK_DGRAM */
    
    int res = syscall3(SYS_SOCKET, domain, type, 0);
    if (res < 0) { SetLastError((DWORD)(-res)); return NULL_HANDLE; }
    
    /* İleride Handle Manager'a gececek. Simdilik dogrudan fd id'si dönüyor. */
    return (HANDLE)res; 
}

BOOL SocketConnect(HANDLE socket, LPCSTR ip, WORD port) {
    /* sockaddr_in (16 byte) */
    struct {
        WORD sin_family;
        WORD sin_port;
        DWORD sin_addr;
        char sin_zero[8];
    } addr;
    
    addr.sin_family = 2; /* AF_INET */
    addr.sin_port = host_to_net_short(port);
    addr.sin_addr = parse_ip(ip);
    
    int res = syscall3(SYS_CONNECT, (uint32_t)socket, (uint32_t)&addr, 16);
    if (res < 0) { SetLastError((DWORD)(-res)); return FALSE; }
    return TRUE;
}

BOOL SocketSend(HANDLE socket, const void* data, DWORD size, DWORD* bytes_sent) {
    int res = syscall3(SYS_WRITE, (uint32_t)socket, (uint32_t)data, size);
    if (res < 0) { SetLastError((DWORD)(-res)); return FALSE; }
    if (bytes_sent) *bytes_sent = (DWORD)res;
    return TRUE;
}

BOOL SocketRecv(HANDLE socket, void* buffer, DWORD size, DWORD* bytes_received) {
    int res = syscall3(SYS_READ, (uint32_t)socket, (uint32_t)buffer, size);
    if (res < 0) { SetLastError((DWORD)(-res)); return FALSE; }
    if (bytes_received) *bytes_received = (DWORD)res;
    return TRUE;
}

BOOL SocketClose(HANDLE socket) {
    return CloseHandle(socket);
}
