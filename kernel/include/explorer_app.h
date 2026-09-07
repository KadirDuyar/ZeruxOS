/* =============================================================================
 * ZeruX OS — Windowed File Explorer Application
 * File: kernel/include/explorer_app.h
 * =============================================================================
 * A VFS-driven directory browser living inside a window_t (window.h).
 * Click a folder row to navigate into it, click ".." to go up, click a
 * file to select it. Built on gfx2d.h + vfs.h, same pattern as
 * terminal_app.h.
 * =============================================================================
 */

#ifndef EXPLORER_APP_H
#define EXPLORER_APP_H

#include <stdint.h>

/* Creates a file explorer window at (x,y) sized (w,h), starting at
 * start_path (e.g. "/"). Returns the window id (window.h), or -1 on
 * failure. */
int32_t explorer_app_create(int32_t x, int32_t y, int32_t w, int32_t h, const char *start_path);

#endif /* EXPLORER_APP_H */
