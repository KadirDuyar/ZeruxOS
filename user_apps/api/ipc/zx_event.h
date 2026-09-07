#ifndef _ZX_EVENT_H
#define _ZX_EVENT_H

#include "../core/zx_types.h"

HANDLE CreateEvent(BOOL manual_reset, BOOL initial_state, LPCSTR name);
BOOL   SetEvent(HANDLE event);
BOOL   ResetEvent(HANDLE event);

#endif /* _ZX_EVENT_H */
