#ifndef _ZX_NETWORK_H
#define _ZX_NETWORK_H

#include "../core/zx_types.h"
#include "transport/zx_socket.h"

/* 
 * ZX_PROTOCOL: Protocol Framework
 * HTTP, FTP, MQTT, SMTP vb. butun high-level protokoller 
 * bu arayuzu (interface) uygulayarak sisteme eklenecek.
 * (Kernel'e dokunmadan yeni protokol ekleme mimarisi)
 */
typedef struct {
    const char *name;
    
    /* Protokol Baglantisi baslat (Orn: TCP baglan, Handshake yap) */
    HANDLE (*connect)(LPCSTR host, WORD port);
    
    /* Paketlenmis veri yolla (Orn: HTTP Request, MQTT Publish) */
    BOOL   (*send)(HANDLE ctx, const void* data, DWORD size);
    
    /* Paketlenmis veri al */
    BOOL   (*recv)(HANDLE ctx, void* buffer, DWORD size);
    
    /* Baglantiyi sonlandir */
    BOOL   (*close)(HANDLE ctx);
} ZX_PROTOCOL;

#endif /* _ZX_NETWORK_H */
