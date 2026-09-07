#ifndef _ZX_SOCKET_H
#define _ZX_SOCKET_H

#include "../../core/zx_types.h"

/* Socket Turleri */
#define ZX_SOCKET_TCP 1
#define ZX_SOCKET_UDP 2
#define ZX_SOCKET_TLS 3

/* Native Transport API */
HANDLE SocketCreate(DWORD socket_type);
BOOL   SocketConnect(HANDLE socket, LPCSTR ip, WORD port);
BOOL   SocketSend(HANDLE socket, const void* data, DWORD size, DWORD* bytes_sent);
BOOL   SocketRecv(HANDLE socket, void* buffer, DWORD size, DWORD* bytes_received);
BOOL   SocketClose(HANDLE socket);

#endif /* _ZX_SOCKET_H */
