/* =============================================================================
 * ZeruX OS — Graphical Desktop Environment & Window Manager
 * File: kernel/gui/gui.c
 * =============================================================================
 */

#include "gui.h"
#include "vbe.h"
#include "vbe_terminal.h"
#include "mouse.h"
#include "keyboard.h"
#include "serial.h"
#include "timer.h"
#include "rtc.h"
#include "ports.h"
#include "vfs.h"
#include <stdbool.h>

#define GUI_BG_COLOR     0x808080 // Gray background
#define TASKBAR_COLOR    0x000080 // Dark blue taskbar
#define START_BTN_COLOR  0xC0C0C0 // Silver start button
#define WIN_TITLE_COLOR  0x0000A0 // Window title bar
#define WIN_BG_COLOR     0xFFFFFF // Window background
#define WIN_BORDER_COLOR 0x000000 // Black border

bool g_gui_active = false;
bool g_desktop_ready = false;
bool g_start_menu_open = false;

/* Window Management */
#define MAX_WINDOWS 4
gui_window_t g_windows[MAX_WINDOWS];
int g_active_window = -1;

/* File Explorer Cache */
#define MAX_EXPLORER_FILES 32
static struct dirent g_explorer_files[MAX_EXPLORER_FILES];
static int g_explorer_file_count = 0;

void gui_init(void) {
    g_gui_active = true;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        g_windows[i].active = false;
    }
    serial_printf("[GUI] Graphical User Interface Initialized.\n");
    
    /* Boot Splash Screen (Just black for now, kernel.c draws the actual PNG later) */
    vbe_fill_rect(0, 0, g_vbe_width, g_vbe_height, 0x000000);
    vbe_refresh_screen();
    
    /* Wait 1.5 seconds (1500 ms)
       Removed since interrupts aren't enabled here, timer_get_uptime_ms() will never increment.
       Also we have another splash screen drawn in kernel.c anyway.
    */
    
    g_desktop_ready = true;
}

static void draw_start_menu(void) {
    if (!g_start_menu_open) return;
    
    int menu_w = 200;
    int menu_h = 100;
    int menu_x = 0;
    int menu_y = g_vbe_height - 40 - menu_h; /* Taskbar is 40px high */
    
    vbe_fill_rect(menu_x, menu_y, menu_w, menu_h, 0x404040);
    vbe_draw_rect(menu_x, menu_y, menu_w, menu_h, 0x000000);
    
    /* Menu Items */
    vbe_draw_string(menu_x + 10, menu_y + 10, "1. Oturumu Kapat", 0xFFFFFF, 0x404040);
    vbe_draw_string(menu_x + 10, menu_y + 40, "2. Yeniden Baslat", 0xFFFFFF, 0x404040);
    vbe_draw_string(menu_x + 10, menu_y + 70, "3. Kapat", 0xFFFFFF, 0x404040);
}

static void draw_taskbar(void) {
    int taskbar_h = 40;
    int taskbar_y = g_vbe_height - taskbar_h;
    
    vbe_fill_rect(0, taskbar_y, g_vbe_width, taskbar_h, TASKBAR_COLOR);
    vbe_draw_line(0, taskbar_y, g_vbe_width, taskbar_y, 0xFFFFFF);
    
    /* Start Button */
    vbe_fill_rect(2, taskbar_y + 2, 80, taskbar_h - 4, START_BTN_COLOR);
    vbe_draw_rect(2, taskbar_y + 2, 80, taskbar_h - 4, 0xFFFFFF); // Highlight
    vbe_draw_string(14, taskbar_y + 12, "BASLAT", 0x000000, START_BTN_COLOR);
    
    /* Clock */
    char time_str[32];
    rtc_time_t t;
    rtc_read_time(&t);
    
    time_str[0] = '0' + (t.hour / 10);
    time_str[1] = '0' + (t.hour % 10);
    time_str[2] = ':';
    time_str[3] = '0' + (t.minute / 10);
    time_str[4] = '0' + (t.minute % 10);
    time_str[5] = ':';
    time_str[6] = '0' + (t.second / 10);
    time_str[7] = '0' + (t.second % 10);
    time_str[8] = '\0';
    
    vbe_draw_string(g_vbe_width - 80, taskbar_y + 12, time_str, 0xFFFFFF, TASKBAR_COLOR);
}

static void draw_desktop(void) {
    /* If shell is active window and fullscreen, we don't draw the desktop background
       because it would overwrite the terminal pixels in the shadow buffer. */
    if (g_active_window >= 0 && g_windows[g_active_window].type == 1) {
        return;
    }

    /* Gradient Background (from top to bottom) */
    for (int y = 48; y < g_vbe_height - 40; y++) {
        /* Interpolate from 0x203060 (Dark Blue) to 0x4060A0 (Lighter Blue) */
        int r = 0x20 + (0x40 - 0x20) * (y - 48) / (g_vbe_height - 88);
        int g = 0x30 + (0x60 - 0x30) * (y - 48) / (g_vbe_height - 88);
        int b = 0x60 + (0xA0 - 0x60) * (y - 48) / (g_vbe_height - 88);
        uint32_t color = (r << 16) | (g << 8) | b;
        vbe_draw_line(0, y, g_vbe_width, y, color);
    }
    
    /* Draw centered text on desktop with shadow */
    char *desk_txt = "ZeruX OS";
    int dw = 8 * 8; /* 8 chars * 8 px width */
    int dh = 16;
    int dx = (g_vbe_width - dw) / 2;
    int dy = (g_vbe_height - dh) / 2;
    
    /* Shadow */
    vbe_draw_string(dx + 2, dy + 2, desk_txt, 0x101010, 0x000000); // Transparent-ish shadow since we don't have alpha blending, wait string draws rects.
    /* Since vbe_draw_string fills the rect with bg color, we should use a custom transparent string drawing or just draw the text. */
    /* Wait, vbe_draw_string doesn't have transparency, it fills the background. I'll just draw the text without shadow or with a solid background */
    vbe_draw_string(dx, dy, desk_txt, 0xFFFFFF, 0x304880);
    
    /* Desktop Icons */
    /* Icon 1: Shell */
    vbe_fill_rect(20, 60, 48, 48, 0x000000);
    vbe_draw_string(24, 76, ">_", 0x00FF00, 0x000000);
    vbe_draw_string(20, 110, "Shell", 0xFFFFFF, GUI_BG_COLOR);
    
    /* Icon 2: File Explorer */
    vbe_fill_rect(20, 140, 48, 48, 0xFFD700);
    vbe_fill_rect(24, 150, 40, 34, 0xFAFAD2);
    vbe_draw_string(10, 190, "Dosyalar", 0xFFFFFF, GUI_BG_COLOR);
}

static void draw_windows(void) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].active) {
            gui_window_t *win = &g_windows[i];
            
            /* Window Background */
            if (win->type != 1) {
                vbe_fill_rect(win->x, win->y, win->w, win->h, WIN_BG_COLOR);
            }
            vbe_draw_rect(win->x, win->y, win->w, win->h, WIN_BORDER_COLOR);
            
            /* Title Bar */
            vbe_fill_rect(win->x, win->y, win->w, 24, WIN_TITLE_COLOR);
            vbe_draw_string(win->x + 8, win->y + 4, win->title, 0xFFFFFF, WIN_TITLE_COLOR);
            
            /* Close Button (X) */
            int close_x = win->x + win->w - 24;
            vbe_fill_rect(close_x, win->y + 2, 20, 20, 0xFF0000);
            vbe_draw_string(close_x + 6, win->y + 4, "X", 0xFFFFFF, 0xFF0000);
            
            /* File Explorer Contents */
            if (win->type == 2) {
                int y_offset = 40;
                vbe_draw_string(win->x + 10, win->y + y_offset, "--- /disk/fat0 Icerigi ---", 0x000000, WIN_BG_COLOR);
                y_offset += 30;
                
                for (int i = 0; i < g_explorer_file_count; i++) {
                    vbe_fill_rect(win->x + 20, win->y + y_offset, 16, 16, 0x0000A0);
                    vbe_draw_string(win->x + 44, win->y + y_offset + 4, g_explorer_files[i].name, 0x000000, WIN_BG_COLOR);
                    y_offset += 24;
                    if (y_offset > win->h - 30) break;
                }
                if (g_explorer_file_count == 0) {
                    vbe_draw_string(win->x + 10, win->y + 80, "Klasor bos veya okunamadi.", 0xFF0000, WIN_BG_COLOR);
                }
            }
        }
    }
}

void gui_render(void) {
    if (!g_gui_active) return;
    
    /* Draw everything into the shadow buffer */
    draw_desktop();
    draw_windows();
    draw_taskbar();
    draw_start_menu();
    
    /* Draw mouse cursor onto shadow buffer as the final layer */
    extern int32_t mouse_x, mouse_y;
    vbe_draw_mouse_cursor(mouse_x, mouse_y);
    
    /* Swap shadow buffer to actual screen */
    vbe_refresh_screen();
}

int gui_open_window(const char *title, int type) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!g_windows[i].active) {
            g_windows[i].active = true;
            g_windows[i].type = type;
            
            /* Copy title */
            int j = 0;
            while (title[j] && j < 31) {
                g_windows[i].title[j] = title[j];
                j++;
            }
            g_windows[i].title[j] = '\0';
            
            /* Full screen EXCEPT top bar (48) and taskbar (40) */
            g_windows[i].x = 0;
            g_windows[i].y = 48;
            g_windows[i].w = g_vbe_width;
            g_windows[i].h = g_vbe_height - 48 - 40;
            
            g_active_window = i;
            
            if (type == 1) {
                vbe_term_clear();
                /* Inject ENTER key so shell reprints the prompt */
                keyboard_inject_char('\n');
            } else if (type == 2) {
                /* Load directory entries once into cache */
                g_explorer_file_count = 0;
                int fd = vfs_open("/disk/fat0", 0);
                if (fd >= 0) {
                    struct dirent *dir;
                    while ((dir = vfs_readdir(fd, g_explorer_file_count)) != 0) {
                        int c = 0;
                        while(dir->name[c] && c < VFS_MAX_NAME_LEN - 1) {
                            g_explorer_files[g_explorer_file_count].name[c] = dir->name[c];
                            c++;
                        }
                        g_explorer_files[g_explorer_file_count].name[c] = '\0';
                        g_explorer_files[g_explorer_file_count].ino = dir->ino;
                        
                        g_explorer_file_count++;
                        if (g_explorer_file_count >= MAX_EXPLORER_FILES) break;
                    }
                    vfs_close(fd);
                }
            }
            
            return i;
        }
    }
    return -1; /* No free windows */
}

void gui_close_window(int id) {
    if (id >= 0 && id < MAX_WINDOWS) {
        g_windows[id].active = false;
        if (g_active_window == id) {
            g_active_window = -1;
        }
    }
}

bool gui_is_window_open(int type) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].active && g_windows[i].type == type) {
            return true;
        }
    }
    return false;
}

bool gui_is_shell_active(void) {
    if (!g_gui_active) return false;
    if (g_active_window >= 0 && g_windows[g_active_window].type == 1) {
        return true;
    }
    return false;
}

static bool prev_left_btn = false;

void gui_handle_events(void) {
    if (!g_gui_active) return;
    
    bool click = (mouse_left_btn && !prev_left_btn);
    prev_left_btn = mouse_left_btn;
    
    if (click) {
        /* Check Start Button */
        if (mouse_x >= 2 && mouse_x <= 82 && mouse_y >= g_vbe_height - 40) {
            g_start_menu_open = !g_start_menu_open;
            return;
        }
        
        /* Check Start Menu Items */
        if (g_start_menu_open && mouse_x >= 0 && mouse_x <= 200 && 
            mouse_y >= g_vbe_height - 140 && mouse_y <= g_vbe_height - 40) {
            
            if (mouse_y < g_vbe_height - 110) {
                /* Log out */
                g_start_menu_open = false;
                for (int i=0; i<MAX_WINDOWS; i++) g_windows[i].active = false;
            } else if (mouse_y < g_vbe_height - 80) {
                /* Reboot */
                outb(0x64, 0xFE);
            } else {
                /* Shutdown (QEMU ACPI or Bochs magic) */
                outw(0xB004, 0x2000);
            }
            return;
        }
        
        g_start_menu_open = false; /* Close menu if clicked outside */
        
        /* Check Window Close Buttons */
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (g_windows[i].active) {
                int close_x = g_windows[i].x + g_windows[i].w - 24;
                int close_y = g_windows[i].y + 2;
                if (mouse_x >= close_x && mouse_x <= close_x + 20 &&
                    mouse_y >= close_y && mouse_y <= close_y + 20) {
                    gui_close_window(i);
                    return;
                }
            }
        }
        
        /* Check Desktop Icons (Only if no window covers them, but windows are fullscreen here) */
        if (g_active_window == -1) {
            /* Shell Icon */
            if (mouse_x >= 20 && mouse_x <= 68 && mouse_y >= 60 && mouse_y <= 108) {
                gui_open_window("Terminal", 1);
            }
            /* File Explorer Icon */
            else if (mouse_x >= 20 && mouse_x <= 68 && mouse_y >= 140 && mouse_y <= 188) {
                gui_open_window("Dosya Gezgini", 2);
            }
        }
    }
    /* Discard keyboard input if shell is not active, to prevent terminal overflow */
    if (g_active_window < 0 || g_windows[g_active_window].type != 1) {
        while (keyboard_has_char()) {
            keyboard_getchar(); /* discard */
        }
    }
}
