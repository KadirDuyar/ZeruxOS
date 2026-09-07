#ifndef _ZERUXAPI_H
#define _ZERUXAPI_H

/* 
 * ZXAPI (ZeruX Native API) 
 * Ustanin Yoneticisi (Master Header)
 */

#define ZXAPI_VERSION_MAJOR 1
#define ZXAPI_VERSION_MINOR 0

/* Faz 1: Core API'ler */
#include "core/zx_types.h"
#include "core/zx_error.h"
#include "core/zx_handle.h"

/* Faz 8: System, Time, Env */
#include "system/zx_time.h"
#include "system/zx_system.h"
#include "system/zx_env.h"

/* Faz 7: Network (Transport & Protocol) */
#include "network/zx_network.h"

/* Faz 6: GUI ve Cizim */
#include "gui/zx_window.h"
#include "gui/zx_msg.h"
#include "gui/zx_graphics.h"

/* Faz 4: I/O ve Device */
#include "io/zx_io.h"
#include "io/zx_device.h"

/* Faz 3: Process, Thread ve IPC */
#include "process/zx_thread.h"
#include "process/zx_sync.h"
#include "ipc/zx_sharedmem.h"
#include "ipc/zx_pipe.h"
#include "ipc/zx_event.h"

/* Faz 2: Memory API */
#include "process/zx_process.h"

/* Faz 8: System, Time, Env */
#include "system/zx_time.h"
#include "system/zx_system.h"
#include "system/zx_env.h"

/* Faz 7: Network (Transport & Protocol) */
#include "network/zx_network.h"

/* Faz 6: GUI ve Cizim */
#include "gui/zx_window.h"
#include "gui/zx_msg.h"
#include "gui/zx_graphics.h"

/* Faz 4: I/O ve Device */
#include "io/zx_io.h"
#include "io/zx_device.h"

/* Faz 3: Process, Thread ve IPC */
#include "process/zx_thread.h"
#include "process/zx_sync.h"
#include "ipc/zx_sharedmem.h"
#include "ipc/zx_pipe.h"
#include "ipc/zx_event.h"

/* Faz 2: Memory API */
#include "memory/zx_memory.h"

/* Diger fazlar yapildikca buraya eklenecektir. */

#endif /* _ZERUXAPI_H */
