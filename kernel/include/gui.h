#ifndef GUI_H
#define GUI_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int x, y, w, h;
    char title[32];
    bool active;
    int type; // 1 = Shell, 2 = File Explorer
} gui_window_t;

extern bool g_gui_active;
extern bool g_desktop_ready;
extern int g_active_window;

void gui_init(void);
void gui_render(void);
void gui_handle_events(void);

int gui_open_window(const char *title, int type);
void gui_close_window(int id);
bool gui_is_window_open(int type);
bool gui_is_shell_active(void);

#endif
