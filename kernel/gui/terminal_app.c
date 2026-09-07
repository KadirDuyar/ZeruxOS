/* =============================================================================
 * ZeruX OS — Windowed Terminal Application
 * File: kernel/gui/terminal_app.c
 * =============================================================================
 */

#include "terminal_app.h"
#include "window.h"
#include "gfx2d.h"
#include "vfs.h"
#include "kheap.h"
#include "serial.h"
#include "shell.h"
#include "shell_cmd.h"
#include <stddef.h>

#define TA_MAX_ROWS  200
#define TA_LINE_LEN  128
#define TA_BG        0x0C0C0Cu
#define TA_TEXT      0x00FF66u
#define TA_PROMPT    0x66CCFFu
#define TA_INPUT     0xFFFFFFu
#define TA_ERROR     0xFF5555u

typedef struct {
    char     lines[TA_MAX_ROWS][TA_LINE_LEN];
    int32_t  line_count;
    char     cwd[256];
    char     input[TA_LINE_LEN];
    uint32_t input_len;
    int32_t  max_cols;
} terminal_state_t;

/* ── Tiny local string helpers ───── */
static size_t ta_strlen(const char *s) { size_t n = 0; while (s && s[n]) n++; return n; }

static void ta_strcpy_bounded(char *dst, const char *src, size_t maxlen) {
    size_t i = 0;
    for (; i < maxlen - 1 && src && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static int ta_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

/* ── Scrollback ──────────────────────────────────────────────────────── */
static void ta_println(terminal_state_t *t, const char *s) {
    if (!s) return;
    size_t len = ta_strlen(s);
    size_t max_c = (t->max_cols > 0 && t->max_cols < TA_LINE_LEN) ? t->max_cols : TA_LINE_LEN - 1;
    
    size_t processed = 0;
    while (processed < len || len == 0) {
        if (t->line_count >= TA_MAX_ROWS) {
            for (int i = 1; i < TA_MAX_ROWS; i++) ta_strcpy_bounded(t->lines[i - 1], t->lines[i], TA_LINE_LEN);
            t->line_count = TA_MAX_ROWS - 1;
        }
        
        size_t chunk = len - processed;
        if (chunk > max_c) chunk = max_c;
        
        for (size_t i = 0; i < chunk; i++) {
            t->lines[t->line_count][i] = s[processed + i];
        }
        t->lines[t->line_count][chunk] = '\0';
        t->line_count++;
        processed += chunk;
        
        if (len == 0) break; /* Handle empty string case */
    }
}

/* ── Rendering ───────────────────────────────────────────────────────── */
static void terminal_redraw(window_t *win) {
    terminal_state_t *t = (terminal_state_t *)win->app_data;
    gfx_surface_t *surf = win->content;
    if (!t || !surf) return;

    surface_clear(surf, TA_BG);

    int32_t rows_visible = (int32_t)(surf->height / 16);
    if (rows_visible < 1) rows_visible = 1;
    int32_t history_rows = rows_visible - 1; /* last row reserved for the input line */
    if (history_rows < 0) history_rows = 0;

    int32_t start = (t->line_count > history_rows) ? (t->line_count - history_rows) : 0;
    int32_t y = 0;
    for (int32_t i = start; i < t->line_count; i++) {
        gfx_draw_text(surf, 4, y, t->lines[i], TA_TEXT, TA_BG);
        y += 16;
    }

    int32_t px = 4;
    gfx_draw_text(surf, px, y, t->cwd, TA_PROMPT, TA_BG);
    px += 8 * (int32_t)ta_strlen(t->cwd);
    gfx_draw_text(surf, px, y, "> ", TA_INPUT, TA_BG);
    px += 16;
    gfx_draw_text(surf, px, y, t->input, TA_INPUT, TA_BG);

    wm_invalidate(win->id);
}

/* ── Command Execution ────────────────────────────────────────────────── */
static void terminal_execute(terminal_state_t *t, const char *cmdline) {
    char history[TA_LINE_LEN];
    char tmp[TA_LINE_LEN];
    ta_strcpy_bounded(tmp, t->cwd, sizeof(tmp));
    size_t l = ta_strlen(tmp);
    if (l < sizeof(tmp) - 3) { tmp[l] = '>'; tmp[l + 1] = ' '; tmp[l + 2] = '\0'; l += 2; }
    ta_strcpy_bounded(history, tmp, sizeof(history));
    size_t hl = ta_strlen(history);
    for (size_t i = 0; cmdline[i] && hl < TA_LINE_LEN - 1; i++) history[hl++] = cmdline[i];
    history[hl] = '\0';
    ta_println(t, history);

    if (!cmdline[0]) return;

    /* Trim leading whitespace */
    const char *p = cmdline;
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) return;

    /* Fast-path clear screen */
    if (ta_strcmp(p, "clear") == 0) {
        t->line_count = 0;
        return;
    }

    /* Synchronize global CWD with terminal CWD */
    ta_strcpy_bounded(g_cwd, t->cwd, sizeof(g_cwd));

    /* Static buffer for command output */
    static char s_term_buf[8192];
    s_term_buf[0] = '\0';

    int res = shell_execute_command_to_buffer(p, s_term_buf, sizeof(s_term_buf));

    /* Synchronize terminal CWD back in case 'cd' was executed */
    ta_strcpy_bounded(t->cwd, g_cwd, sizeof(t->cwd));

    /* Route buffer output into terminal scrollback lines */
    if (s_term_buf[0] != '\0') {
        char line[TA_LINE_LEN];
        uint32_t lp = 0;
        for (uint32_t i = 0; s_term_buf[i]; i++) {
            char c = s_term_buf[i];
            if (c == '\n' || lp >= TA_LINE_LEN - 1) {
                line[lp] = '\0';
                ta_println(t, line);
                lp = 0;
                if (c != '\n') line[lp++] = c;
            } else if (c != '\r') {
                line[lp++] = c;
            }
        }
        if (lp > 0) {
            line[lp] = '\0';
            ta_println(t, line);
        }
    } else if (res < 0) {
        ta_println(t, "[!] Komut calistirilamadi");
    }
}

/* ── window.h callback glue ──────────────────────────────────────────── */
static void terminal_on_key(window_t *win, char c) {
    terminal_state_t *t = (terminal_state_t *)win->app_data;
    if (!t) return;

    if (c == '\n') {
        terminal_execute(t, t->input);
        t->input[0] = '\0';
        t->input_len = 0;
    } else if (c == '\b') {
        if (t->input_len > 0) t->input[--t->input_len] = '\0';
    } else if (c >= 32 && c < 127) {
        if (t->input_len < TA_LINE_LEN - 1) {
            t->input[t->input_len++] = c;
            t->input[t->input_len] = '\0';
        }
    }
    terminal_redraw(win);
}

static void terminal_on_destroy(window_t *win) {
    if (win->app_data) {
        kfree(win->app_data);
        win->app_data = NULL;
    }
}

int32_t terminal_app_create(int32_t x, int32_t y, int32_t w, int32_t h) {
    int32_t id = wm_create_window("Terminal", x, y, w, h);
    if (id < 0) return -1;

    terminal_state_t *t = (terminal_state_t *)kzalloc(sizeof(terminal_state_t));
    if (!t) { wm_destroy_window(id); return -1; }
    
    window_t *win = wm_get_window(id);
    if (win && win->content) {
        t->max_cols = (win->content->width - 8) / 8;
    } else {
        t->max_cols = 50;
    }

    ta_strcpy_bounded(t->cwd, (g_cwd[0] != '\0') ? g_cwd : "/", sizeof(t->cwd));
    ta_println(t, "ZeruX Terminal - 'help' ile komutlari gor");

    wm_set_app_data(id, t);
    wm_set_key_handler(id, terminal_on_key);
    wm_set_destroy_handler(id, terminal_on_destroy);
    wm_set_resize_handler(id, terminal_redraw);

    terminal_redraw(wm_get_window(id));
    return id;
}
