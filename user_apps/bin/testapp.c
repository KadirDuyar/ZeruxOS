#include "../api/zeruxapi.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("\n========================================\n");
    printf("   ZXAPI Native Test Program Started\n");
    printf("========================================\n\n");

    /* 1. Sistem Bilgisi Testi */
    SYSTEM_INFO sys;
    if (GetSystemInfo(&sys)) {
        printf("[Phase 8] GetSystemInfo() SUCCESS:\n");
        printf("  - OS Version : %s\n", sys.kernel_version);
        printf("  - Total RAM  : %u MB\n", sys.total_memory / (1024*1024));
        printf("  - Free RAM   : %u MB\n", sys.free_memory / (1024*1024));
        printf("  - Uptime     : %u seconds\n\n", sys.uptime_seconds);
    } else {
        printf("[Phase 8] GetSystemInfo() FAILED. Error: %u\n", GetLastError());
    }

    /* 2. Zaman Testi */
    SYSTEMTIME t;
    if (GetSystemTime(&t)) {
        printf("[Phase 8] GetSystemTime() SUCCESS:\n");
        printf("  - Date: %04d-%02d-%02d\n\n", t.year, t.month, t.day);
    }

    /* 3. Dosya I/O (Handle Manager) Testi */
    printf("[Phase 4/0] Creating /disk/fat0/TEST.TXT...\n");
    HANDLE hFile = CreateFile("/disk/fat0/TEST.TXT", ZX_GENERIC_WRITE, ZX_CREATE_ALWAYS);
    
    if (hFile != NULL_HANDLE) {
        printf("[Phase 4/0] CreateFile() SUCCESS. Handle ID: %u\n", (uint32_t)hFile);
        
        const char *msg = "Hello from ZXAPI Native Handle!\n";
        DWORD written = 0;
        if (WriteFile(hFile, msg, strlen(msg), &written)) {
            printf("[Phase 4/0] WriteFile() SUCCESS. Bytes written: %u\n", written);
        } else {
            printf("[Phase 4/0] WriteFile() FAILED. Error: %u\n", GetLastError());
        }
        
        if (CloseHandle(hFile)) {
            printf("[Phase 4/0] CloseHandle() SUCCESS.\n\n");
        }
    } else {
        printf("[Phase 4/0] CreateFile() FAILED. Error: %u\n\n", GetLastError());
    }

    /* 4. GUI Window Testi */
    printf("[Phase 6] Creating Native GUI Window...\n");
    HWND hwnd = CreateWindowEx(0, "WindowClass", "ZXAPI Test", 
                               ZX_WS_OVERLAPPEDWINDOW, 
                               100, 100, 400, 300, 
                               NULL_HANDLE, NULL_HANDLE, NULL_HANDLE, NULL);
    
    if (hwnd != NULL_HANDLE) {
        printf("[Phase 6] CreateWindowEx() SUCCESS. HWND: %u\n", (uint32_t)hwnd);
    } else {
        printf("[Phase 6] CreateWindowEx() FAILED. Error: %u\n", GetLastError());
    }

    printf("========================================\n");
    printf("   ZXAPI Test Completed!\n");
    printf("========================================\n");
    return 0;
}
