/* =============================================================================
 * ZeruX OS — Windowed Terminal Application
 * File: kernel/include/terminal_app.h
 * =============================================================================
 * A self-contained, VFS-driven mini shell that lives inside a window_t
 * (window.h). It is DELIBERATELY not the same code as shell.c: shell.c's
 * ~50 commands all write straight to serial_printf()/vbe_term, which is
 * baked in throughout that file. Redirecting that output into an arbitrary
 * window surface would mean refactoring shell.c's output path everywhere —
 * a bigger, separate job. This is a small, independent command set (ls,
 * cd, pwd, cat, clear, help, echo) built directly on vfs.h and rendered
 * into the window's own content surface with gfx2d.h.
 * =============================================================================
 */

#ifndef TERMINAL_APP_H
#define TERMINAL_APP_H

#include <stdint.h>

/* Creates a terminal window at (x,y) sized (w,h) and wires up its keyboard
 * handler + destroy handler. Returns the window id (see window.h), or -1
 * on failure. */
int32_t terminal_app_create(int32_t x, int32_t y, int32_t w, int32_t h);

#endif /* TERMINAL_APP_H */
