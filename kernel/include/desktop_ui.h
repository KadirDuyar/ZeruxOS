/* =============================================================================
 * ZeruX OS — Desktop UI Manager (Taskbar, Start Menu, Desktop Icons)
 * File: kernel/include/desktop_ui.h
 * =============================================================================
 */

#ifndef DESKTOP_UI_H
#define DESKTOP_UI_H

#include <stdint.h>
#include <stdbool.h>
#include "surface.h"

#define TASKBAR_HEIGHT 36

void desktop_ui_init(void);

/* Draws desktop background elements (icons) to the screen */
void desktop_ui_draw_bg(gfx_surface_t *screen, int32_t dx0, int32_t dy0, int32_t dx1, int32_t dy1);

/* Draws foreground elements (taskbar, start menu) to the screen */
void desktop_ui_draw_fg(gfx_surface_t *screen, int32_t dx0, int32_t dy0, int32_t dx1, int32_t dy1);

/* Called every tick to check for time/network updates and trigger redraws */
void desktop_ui_update(void);

/* Returns true if the click was absorbed by the desktop UI (taskbar, menu, icons) */
bool desktop_ui_handle_click(int32_t x, int32_t y);

#endif /* DESKTOP_UI_H */
