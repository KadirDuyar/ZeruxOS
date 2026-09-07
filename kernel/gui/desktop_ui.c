/* =============================================================================
 * ZeruX OS — Desktop UI Manager (Top Taskbar & Desktop Icons)
 * File: kernel/gui/desktop_ui.c
 * =============================================================================
 */

#include "desktop_ui.h"
#include "framebuffer.h"
#include "gfx2d.h"
#include "png.h"
#include "rtc.h"
#include "net.h"
#include "window.h"
#include "dirty_rect.h"
#include "ports.h"
#include <stddef.h>

/* ── Layout constants ──────────────────────────────── */
#define ICON_SIZE     64
#define ICON_STRIDE   96    /* vertical spacing between desktop icons */

/* ── Colours (Tokyo-Night palette) ─────────────────── */
#define TASKBAR_BG    0x1A1B26u
#define TASKBAR_EDGE  0x24283Bu
#define START_ACTIVE  0x7AA2F7u
#define TEXT_MAIN     0xC0CAF5u
#define TEXT_DIM      0x565F89u
#define MENU_BG       0x1F2335u
#define MENU_SEP      0x414868u
#define GREEN_IP      0x9ECE6Au
#define RED_POWER     0xF7768Eu

static gfx_surface_t *g_icon_browser   = NULL;
static gfx_surface_t *g_icon_terminal  = NULL;
static gfx_surface_t *g_icon_explorer  = NULL;
static gfx_surface_t *g_icon_firewall  = NULL;
static gfx_surface_t *g_icon_restart   = NULL;
static gfx_surface_t *g_icon_shutdown  = NULL;

static uint32_t g_last_rtc_sec = 0;
static bool     g_icons_loaded = false;

/* External app constructors */
extern int32_t browser_app_create(int32_t x, int32_t y, int32_t w, int32_t h, const char *url);
extern int32_t terminal_app_create(int32_t x, int32_t y, int32_t w, int32_t h);
extern int32_t explorer_app_create(int32_t x, int32_t y, int32_t w, int32_t h, const char *start_path);
extern int32_t firewall_app_create(int32_t x, int32_t y, int32_t w, int32_t h);
extern int32_t taskmgr_app_create(int32_t x, int32_t y, int32_t w, int32_t h);

/* ── Simple uint → string ───────────────────────────── */
static void u_to_s(uint32_t v, char *out) {
    char tmp[12]; int l = 0;
    if (v == 0) { out[0]='0'; out[1]='\0'; return; }
    while (v) { tmp[l++] = '0' + (char)(v % 10); v /= 10; }
    for (int i = 0; i < l; i++) out[i] = tmp[l-1-i];
    out[l] = '\0';
}

/* ── Init ─────────────────────────────────────────────── */
void desktop_ui_init(void) {
    if (g_icons_loaded) return;
    g_icons_loaded = true;
    g_icon_browser  = png_load("/disk/fat0/AURORA.PNG");
    g_icon_explorer = png_load("/disk/fat0/EXPLORER.PNG");
    g_icon_firewall = png_load("/disk/fat0/FIREWALL.PNG");
    g_icon_terminal = png_load("/disk/fat0/TERMINAL.PNG");
    g_icon_restart  = png_load("/disk/fat0/RESTART.PNG");
    g_icon_shutdown = png_load("/disk/fat0/SHUTDOWN.PNG");
}

/* ── Draw one desktop icon + label ───────────────────── */
static void draw_icon(gfx_surface_t *screen, gfx_surface_t *icon,
                      int32_t x, int32_t y, const char *label) {
    /* Icon */
    if (icon && (uint32_t)icon > 0x100000 && (uint32_t)icon < 0x80000000 && icon->pixels) {
        gfx_stretch_blit(screen, x, y, ICON_SIZE, ICON_SIZE,
                         icon, 0, 0, icon->width, icon->height);
    } else {
        uint32_t bg_col     = 0x24283B;
        uint32_t border_col = 0x7AA2F7;
        const char *badge   = "APP";

        if (label[0] == 'T' && label[1] == 'a') {        /* Tarayici */
            bg_col = 0x1E3A8A; border_col = 0x3B82F6; badge = "WEB";
        } else if (label[0] == 'D') {                   /* Dosyalar */
            bg_col = 0x78350F; border_col = 0xF59E0B; badge = "DIR";
        } else if (label[0] == 'T') {                   /* Terminal */
            bg_col = 0x064E3B; border_col = 0x10B981; badge = ">_";
        } else if (label[0] == 'G' && label[1] == 'u') { /* Guvenlik */
            bg_col = 0x7F1D1D; border_col = 0xEF4444; badge = "SEC";
        } else if (label[0] == 'G') {                   /* Gorevler */
            bg_col = 0x581C87; border_col = 0xA855F7; badge = "CPU";
        }

        gfx_fill_round_rect(screen, x, y, ICON_SIZE, ICON_SIZE, 12, bg_col);
        gfx_draw_round_rect(screen, x, y, ICON_SIZE, ICON_SIZE, 12, border_col);
        gfx_draw_round_rect(screen, x + 2, y + 2, ICON_SIZE - 4, ICON_SIZE - 4, 10, bg_col + 0x101010);

        int blen = 0; while (badge[blen]) blen++;
        int32_t bx = x + (ICON_SIZE - blen * 8) / 2;
        gfx_draw_text(screen, bx, y + 24, badge, 0xFFFFFF, bg_col);
    }
    /* Label — centred under icon */
    int llen = 0; while (label[llen]) llen++;
    int32_t tx = x + (ICON_SIZE - llen * 8) / 2;
    if (tx < x) tx = x;
    gfx_draw_text(screen, tx + 1, y + ICON_SIZE + 5, label, 0x000000, 0x000000); /* shadow */
    gfx_draw_text(screen, tx,     y + ICON_SIZE + 4, label, TEXT_MAIN, 0x000000);
}

/* Called during compositor's background phase */
void desktop_ui_draw_bg(gfx_surface_t *screen,
                        int32_t dx0, int32_t dy0, int32_t dx1, int32_t dy1) {
    (void)dx0; (void)dy0; (void)dx1; (void)dy1;
    int32_t ix = 30;
    int32_t iy = TASKBAR_HEIGHT + 24;
    draw_icon(screen, g_icon_browser,  ix, iy,                  "Tarayici");
    draw_icon(screen, g_icon_explorer, ix, iy + ICON_STRIDE,     "Dosyalar");
    draw_icon(screen, g_icon_terminal, ix, iy + ICON_STRIDE * 2, "Terminal");
    draw_icon(screen, g_icon_firewall, ix, iy + ICON_STRIDE * 3, "Guvenlik");
    draw_icon(screen, NULL,            ix, iy + ICON_STRIDE * 4, "Gorevler");
}

/* Called after all windows are painted — always on top */
void desktop_ui_draw_fg(gfx_surface_t *screen,
                        int32_t dx0, int32_t dy0, int32_t dx1, int32_t dy1) {
    (void)dx0; (void)dy0; (void)dx1; (void)dy1;

    int32_t w = (int32_t)screen->width;

    /* ── Top Taskbar background ── */
    gfx_fill_rect(screen, 0, 0, w, TASKBAR_HEIGHT, TASKBAR_BG);
    gfx_fill_rect(screen, 0, TASKBAR_HEIGHT - 1, w, 1, TASKBAR_EDGE); /* bottom edge */

    /* ── Logo & Title ── */
    gfx_draw_text(screen, 14, 10, "ZeruX OS", START_ACTIVE, TASKBAR_BG);
    gfx_fill_rect(screen, 90, 8, 1, 20, MENU_SEP);

    /* ── Kapat (Shutdown) Button on right ── */
    int32_t btn_kapat_x = w - 95;
    int32_t btn_kapat_y = 4;
    int32_t btn_kapat_w = 85;
    int32_t btn_kapat_h = 28;
    gfx_fill_round_rect(screen, btn_kapat_x, btn_kapat_y, btn_kapat_w, btn_kapat_h, 4, 0x321E28);
    gfx_draw_round_rect(screen, btn_kapat_x, btn_kapat_y, btn_kapat_w, btn_kapat_h, 4, 0x5A2D3C);
    if (g_icon_shutdown && (uint32_t)g_icon_shutdown > 0x100000 && (uint32_t)g_icon_shutdown < 0x80000000 && g_icon_shutdown->pixels) {
        gfx_stretch_blit(screen, btn_kapat_x + 6, btn_kapat_y + 4, 20, 20,
                         g_icon_shutdown, 0, 0, g_icon_shutdown->width, g_icon_shutdown->height);
        gfx_draw_text(screen, btn_kapat_x + 30, btn_kapat_y + 6, "Kapat", RED_POWER, 0x321E28);
    } else {
        gfx_draw_text(screen, btn_kapat_x + 20, btn_kapat_y + 6, "Kapat", RED_POWER, 0x321E28);
    }

    /* ── Yeniden Başlat (Restart) Button ── */
    int32_t btn_rst_x = w - 235;
    int32_t btn_rst_y = 4;
    int32_t btn_rst_w = 132;
    int32_t btn_rst_h = 28;
    gfx_fill_round_rect(screen, btn_rst_x, btn_rst_y, btn_rst_w, btn_rst_h, 4, MENU_BG);
    gfx_draw_round_rect(screen, btn_rst_x, btn_rst_y, btn_rst_w, btn_rst_h, 4, MENU_SEP);
    if (g_icon_restart && (uint32_t)g_icon_restart > 0x100000 && (uint32_t)g_icon_restart < 0x80000000 && g_icon_restart->pixels) {
        gfx_stretch_blit(screen, btn_rst_x + 6, btn_rst_y + 4, 20, 20,
                         g_icon_restart, 0, 0, g_icon_restart->width, g_icon_restart->height);
        gfx_draw_text(screen, btn_rst_x + 30, btn_rst_y + 6, "Y. Baslat", TEXT_MAIN, MENU_BG);
    } else {
        gfx_draw_text(screen, btn_rst_x + 18, btn_rst_y + 6, "Y. Baslat", TEXT_MAIN, MENU_BG);
    }

    gfx_fill_rect(screen, w - 248, 8, 1, 20, MENU_SEP);

    /* ── Clock + Date ── */
    rtc_time_t t;
    rtc_read_time(&t);

    char dt_str[24];
    dt_str[0] = '0' + (char)(t.hour / 10);
    dt_str[1] = '0' + (char)(t.hour % 10);
    dt_str[2] = ':';
    dt_str[3] = '0' + (char)(t.minute / 10);
    dt_str[4] = '0' + (char)(t.minute % 10);
    dt_str[5] = ':';
    dt_str[6] = '0' + (char)(t.second / 10);
    dt_str[7] = '0' + (char)(t.second % 10);
    dt_str[8] = ' ';
    dt_str[9] = ' ';
    dt_str[10] = '0' + (char)(t.day / 10);
    dt_str[11] = '0' + (char)(t.day % 10);
    dt_str[12] = '/';
    dt_str[13] = '0' + (char)(t.month / 10);
    dt_str[14] = '0' + (char)(t.month % 10);
    dt_str[15] = '/';
    char yr[4]; u_to_s((uint32_t)t.year, yr);
    dt_str[16] = yr[0] ? yr[0] : '0';
    dt_str[17] = yr[1] ? yr[1] : '0';
    dt_str[18] = '\0';

    gfx_draw_text(screen, w - 410, 10, dt_str, TEXT_MAIN, TASKBAR_BG);
    gfx_fill_rect(screen, w - 425, 8, 1, 20, MENU_SEP);

    /* ── IP Address ── */
    uint8_t ip[4];
    net_get_ip(ip);

    char ip_str[32];
    int p = 0;
    const char *prefix = "IP: ";
    for (int i = 0; prefix[i]; i++) ip_str[p++] = prefix[i];

    char nb[5];
    for (int i = 0; i < 4; i++) {
        u_to_s(ip[i], nb);
        for (int j = 0; nb[j]; j++) ip_str[p++] = nb[j];
        if (i < 3) ip_str[p++] = '.';
    }
    ip_str[p] = '\0';

    gfx_draw_text(screen, w - 580, 10, ip_str, GREEN_IP, TASKBAR_BG);
}

/* ── Per-frame update (clock tick) ────────────────────── */
void desktop_ui_update(void) {
    rtc_time_t t;
    rtc_read_time(&t);
    if ((uint32_t)t.second != g_last_rtc_sec) {
        g_last_rtc_sec = (uint32_t)t.second;
        gfx_surface_t *screen = fb_get_screen_surface();
        if (screen) dirty_mark(0, 0, (int32_t)screen->width, TASKBAR_HEIGHT);
    }
}

/* ── Click handler ─────────────────────────────────────── */
bool desktop_ui_handle_click(int32_t x, int32_t y) {
    gfx_surface_t *screen = fb_get_screen_surface();
    int32_t w = screen ? (int32_t)screen->width : 1024;

    /* ── Clicks inside top taskbar ── */
    if (y < TASKBAR_HEIGHT) {
        /* Kapat (Shutdown) button clicked */
        int32_t btn_kapat_x = w - 95;
        if (x >= btn_kapat_x && x <= btn_kapat_x + 85 && y >= 4 && y <= 32) {
            outw(0xB004, 0x2000); /* Bochs / older QEMU */
            outw(0x604, 0x2000);  /* QEMU ACPI shutdown */
            outw(0x4004, 0x3400); /* VirtualBox shutdown */
            return true;
        }

        /* Yeniden Başlat (Restart) button clicked */
        int32_t btn_rst_x = w - 235;
        if (x >= btn_rst_x && x <= btn_rst_x + 132 && y >= 4 && y <= 32) {
            outb(0x64, 0xFE); /* PS/2 keyboard controller reset pulse */
            return true;
        }

        return true; /* Always absorb clicks on top taskbar */
    }

    /* ── Check if click is on an active window ── */
    extern int32_t wm_window_at(int32_t x, int32_t y);
    if (wm_window_at(x, y) >= 0) return false;

    /* ── Desktop icon clicks ── */
    int32_t ix = 30;
    int32_t iy = TASKBAR_HEIGHT + 24;
    if (x >= ix && x < ix + ICON_SIZE) {
        for (int s = 0; s < 5; s++) {
            int32_t top = iy + s * ICON_STRIDE;
            if (y >= top && y < top + ICON_SIZE) {
                switch (s) {
                    case 0: browser_app_create(120, TASKBAR_HEIGHT + 30, 600, 400, ""); break;
                    case 1: explorer_app_create(140, TASKBAR_HEIGHT + 40, 600, 400, "/"); break;
                    case 2: terminal_app_create(160, TASKBAR_HEIGHT + 50, 600, 400); break;
                    case 3: firewall_app_create(180, TASKBAR_HEIGHT + 60, 660, 480); break;
                    case 4: taskmgr_app_create(200, TASKBAR_HEIGHT + 70, 680, 480); break;
                    default: break;
                }
                return true;
            }
        }
    }

    return false;
}
