#ifndef _ZX_ENV_H
#define _ZX_ENV_H

#include "../core/zx_types.h"

LPCSTR GetEnvironmentVariable(LPCSTR name);
BOOL   SetEnvironmentVariable(LPCSTR name, LPCSTR value);
LPCSTR GetCurrentDirectory(void);
BOOL   SetCurrentDirectory(LPCSTR path);
LPCSTR GetCommandLine(void);

#endif /* _ZX_ENV_H */
