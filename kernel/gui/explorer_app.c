/* =============================================================================
 * ZeruX OS — Windowed File Explorer Application
 * File: kernel/gui/explorer_app.c
 * =============================================================================
 */

#include "explorer_app.h"
#include "window.h"
#include "gfx2d.h"
#include "vfs.h"
#include "kheap.h"
#include "serial.h"
#include <stddef.h>

#define EX_MAX_ENTRIES 128
#define EX_ROW_H       18
#define EX_HEADER_H    24
#define EX_BG          0xF5F5F5u
#define EX_HEADER_BG   0xDDDDDDu
#define EX_BTN_BG      0xCCCCCCu
#define EX_TEXT        0x000000u
#define EX_DIR_TEXT    0x0A5FBFu
#define EX_SELECT_BG   0xCCE4FFu

typedef struct {
    char     cwd[256];
    char     names[EX_MAX_ENTRIES][128];
    bool     is_dir[EX_MAX_ENTRIES];
    uint32_t sizes[EX_MAX_ENTRIES];
    int32_t  count;
    int32_t  selected;
} explorer_state_t;

/* ── Tiny local string helpers (see terminal_app.c for the same convention) ── */
static size_t ex_strlen(const char *s) { size_t n = 0; while (s && s[n]) n++; return n; }

static void ex_strcpy_bounded(char *dst, const char *src, size_t maxlen) {
    size_t i = 0;
    for (; i < maxlen - 1 && src && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static void ex_path_join(const char *base, const char *child, char *out, size_t out_sz) {
    size_t blen = ex_strlen(base);
    int need_slash = (blen > 0 && base[blen - 1] != '/');
    size_t i = 0;
    while (i < out_sz - 1 && base[i]) { out[i] = base[i]; i++; }
    if (need_slash && i < out_sz - 1) out[i++] = '/';
    size_t j = 0;
    while (i < out_sz - 1 && child[j]) out[i++] = child[j++];
    out[i] = '\0';
}

/* In-place: strip the last path segment ("/a/b/c" -> "/a/b", "/a" -> "/") */
static void ex_path_parent(char *path) {
    size_t len = ex_strlen(path);
    if (len <= 1) { path[0] = '/'; path[1] = '\0'; return; }
    if (path[len - 1] == '/') { len--; path[len] = '\0'; }
    while (len > 0 && path[len - 1] != '/') len--;
    if (len == 0) len = 1; /* keep the leading '/' */
    path[len] = '\0';
}

static void explorer_refresh(window_t *win);

static void explorer_redraw(window_t *win) {
    explorer_state_t *ex = (explorer_state_t *)win->app_data;
    gfx_surface_t *surf = win->content;
    if (!ex || !surf) return;

    surface_clear(surf, EX_BG);

    /* Header background */
    gfx_fill_rect(surf, 0, 0, (int32_t)surf->width, EX_HEADER_H, EX_HEADER_BG);

    /* Back Button: 4,4 to 60,20 */
    gfx_fill_rect(surf, 4, 4, 56, 16, EX_BTN_BG);
    gfx_draw_text(surf, 8, 4, "< Geri", EX_TEXT, EX_BTN_BG);

    /* Refresh Button: 64,4 to 128,20 */
    gfx_fill_rect(surf, 64, 4, 64, 16, EX_BTN_BG);
    gfx_draw_text(surf, 68, 4, "Yenile", EX_TEXT, EX_BTN_BG);

    /* Current Path */
    gfx_draw_text(surf, 136, 4, ex->cwd, EX_TEXT, EX_HEADER_BG);

    int32_t y = EX_HEADER_H;
    int32_t row = 0;

    for (int32_t i = 0; i < ex->count; i++) {
        if (y + EX_ROW_H > (int32_t)surf->height) break; /* no scrolling yet — Stage 2 */

        if (ex->selected == i) {
            gfx_fill_rect(surf, 0, y, (int32_t)surf->width, EX_ROW_H, EX_SELECT_BG);
        }

        char label[160];
        if (ex->is_dir[i]) {
            ex_strcpy_bounded(label, "[DIR] ", sizeof(label));
            size_t l = ex_strlen(label);
            for (size_t j = 0; ex->names[i][j] && l < sizeof(label) - 1; j++) label[l++] = ex->names[i][j];
            label[l] = '\0';
            gfx_draw_text(surf, 4, y, label, EX_DIR_TEXT, ex->selected == i ? EX_SELECT_BG : EX_BG);
        } else {
            gfx_draw_text(surf, 4, y, ex->names[i], EX_TEXT, ex->selected == i ? EX_SELECT_BG : EX_BG);
        }
        y += EX_ROW_H;
        row++;
    }
    (void)row;

    wm_invalidate(win->id);
}

static void explorer_refresh(window_t *win) {
    explorer_state_t *ex = (explorer_state_t *)win->app_data;
    if (!ex) return;

    ex->count = 0;
    ex->selected = -1;

    int32_t fd = vfs_open(ex->cwd, 0);
    if (fd < 0) {
        serial_printf("[Explorer] Could not open '%s'\n", ex->cwd);
        explorer_redraw(win);
        return;
    }

    for (uint32_t i = 0; ex->count < EX_MAX_ENTRIES; i++) {
        struct dirent *de = vfs_readdir(fd, i);
        if (!de) break;

        char child[256];
        ex_path_join(ex->cwd, de->name, child, sizeof(child));
        vfs_node_t *cn = vfs_lookup(child);

        ex_strcpy_bounded(ex->names[ex->count], de->name, sizeof(ex->names[ex->count]));
        ex->is_dir[ex->count] = cn ? ((cn->flags & VFS_DIRECTORY) != 0) : false;
        ex->sizes[ex->count]  = cn ? cn->length : 0;
        ex->count++;
    }
    vfs_close(fd);

    explorer_redraw(win);
}

static void explorer_on_click(window_t *win, int32_t local_x, int32_t local_y) {
    explorer_state_t *ex = (explorer_state_t *)win->app_data;
    if (!ex) return;

    if (local_y < EX_HEADER_H) {
        if (local_x >= 4 && local_x <= 60) {
            /* Back Button */
            if (ex_strlen(ex->cwd) > 1) {
                ex_path_parent(ex->cwd);
                explorer_refresh(win);
            }
        } else if (local_x >= 64 && local_x <= 128) {
            /* Refresh Button */
            explorer_refresh(win);
        } else {
            /* Path Bar */
            explorer_refresh(win);
        }
        return;
    }

    int32_t row = (local_y - EX_HEADER_H) / EX_ROW_H;

    if (row < 0 || row >= ex->count) return;

    if (ex->is_dir[row]) {
        char next[256];
        ex_path_join(ex->cwd, ex->names[row], next, sizeof(next));
        ex_strcpy_bounded(ex->cwd, next, sizeof(ex->cwd));
        explorer_refresh(win);
    } else {
        ex->selected = row;
        explorer_redraw(win);
    }
}

static void explorer_on_destroy(window_t *win) {
    if (win->app_data) {
        kfree(win->app_data);
        win->app_data = NULL;
    }
}

int32_t explorer_app_create(int32_t x, int32_t y, int32_t w, int32_t h, const char *start_path) {
    int32_t id = wm_create_window("Dosya Gezgini", x, y, w, h);
    if (id < 0) return -1;

    explorer_state_t *ex = (explorer_state_t *)kzalloc(sizeof(explorer_state_t));
    if (!ex) { wm_destroy_window(id); return -1; }

    ex_strcpy_bounded(ex->cwd, (start_path && start_path[0]) ? start_path : "/", sizeof(ex->cwd));
    ex->selected = -1;

    wm_set_app_data(id, ex);
    wm_set_click_handler(id, explorer_on_click);
    wm_set_destroy_handler(id, explorer_on_destroy);
    wm_set_resize_handler(id, explorer_redraw);

    explorer_refresh(wm_get_window(id));
    return id;
}
