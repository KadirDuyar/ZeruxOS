#include "zx_error.h"

/* Guncel process/thread'e ait son hata kodu. 
 * İleride multi-threading tam desteklenince bu degisken 
 * Thread Local Storage (TLS) icine tasinacaktir.
 */
static DWORD g_last_error = ZX_SUCCESS;

DWORD GetLastError(void) {
    return g_last_error;
}

void SetLastError(DWORD err_code) {
    g_last_error = err_code;
}

LPCSTR GetErrorString(DWORD err_code) {
    switch (err_code) {
        case ZX_SUCCESS:                 return "Islem basarili (Success)";
        case ZX_ERROR_NOT_FOUND:         return "Dosya veya obje bulunamadi";
        case ZX_ERROR_PATH_NOT_FOUND:    return "Yol (Path) bulunamadi";
        case ZX_ERROR_ACCESS_DENIED:     return "Erisim engellendi";
        case ZX_ERROR_INVALID_HANDLE:    return "Gecersiz Handle";
        case ZX_ERROR_NO_MEMORY:         return "Yetersiz bellek";
        case ZX_ERROR_NOT_SUPPORTED:     return "Bu islem desteklenmiyor";
        case ZX_ERROR_INVALID_PARAMETER: return "Gecersiz parametre";
        case ZX_ERROR_IO_DEVICE:         return "I/O Aygit hatasi";
        case ZX_ERROR_TIMEOUT:           return "Zaman asimi (Timeout)";
        default:                         return "Bilinmeyen hata";
    }
}
