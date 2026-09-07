/* =============================================================================
 * ZeruX OS — Task Manager GUI Application
 * File: kernel/gui/taskmgr_app.c
 * =============================================================================
 */

#include "taskmgr_app.h"
#include "task.h"
#include "window.h"
#include "gfx2d.h"
#include "kheap.h"
#include "timer.h"
#include "serial.h"
#include <stddef.h>

/* ── Colours ────────────────────────────────────────── */
#define TM_BG       0x1E1E2E
#define TM_PANEL    0x24283B
#define TM_HEADER   0x1F2335
#define TM_SEP      0x292E42
#define TM_TEXT     0xC0CAF5
#define TM_DIM      0x565F89
#define TM_ACCENT   0x7AA2F7
#define TM_GREEN    0x9ECE6A
#define TM_YELLOW   0xE0AF68
#define TM_RED      0xF7768E
#define TM_CYAN     0x7DCFFF
#define TM_ORANGE   0xFF9E64

/* ── State ──────────────────────────────────────────── */
typedef struct {
    int32_t  window_id;
    uint32_t refresh_timer;
    int      scroll;
    int      selected_pid;   /* -1 = none */
    bool     need_redraw;
} tm_ctx_t;

/* ── Helpers ────────────────────────────────────────── */
static void u32s(uint32_t v, char *out) {
    char tmp[12]; int l = 0;
    if (v == 0) { out[0]='0'; out[1]='\0'; return; }
    while (v) { tmp[l++] = '0' + (char)(v % 10); v /= 10; }
    for (int i = 0; i < l; i++) out[i] = tmp[l-1-i];
    out[l] = '\0';
}

static const char *state_label(task_state_t s) {
    switch (s) {
        case TASK_STATE_READY:    return "HAZIR   ";
        case TASK_STATE_RUNNING:  return "CALISIYOR";
        case TASK_STATE_SLEEPING: return "UYUYOR  ";
        case TASK_STATE_WAITING:  return "BEKLIYOR";
        case TASK_STATE_BLOCKED:  return "BLOKLU  ";
        case TASK_STATE_ZOMBIE:   return "ZOMBI   ";
        default:                  return "BILINMEZ";
    }
}

static uint32_t state_color(task_state_t s) {
    switch (s) {
        case TASK_STATE_READY:    return TM_GREEN;
        case TASK_STATE_RUNNING:  return TM_CYAN;
        case TASK_STATE_SLEEPING: return TM_YELLOW;
        case TASK_STATE_WAITING:  return TM_YELLOW;
        case TASK_STATE_BLOCKED:  return TM_ORANGE;
        case TASK_STATE_ZOMBIE:   return TM_RED;
        default:                  return TM_DIM;
    }
}

/* ── Draw ───────────────────────────────────────────── */
static void tm_draw(window_t *win) {
    tm_ctx_t *ctx = (tm_ctx_t *)win->app_data;
    if (!ctx || !win->content) return;

    gfx_surface_t *s = win->content;
    int32_t W = (int32_t)s->width;

    surface_clear(s, TM_BG);

    /* Title bar area */
    gfx_fill_rect(s, 0, 0, W, 28, TM_PANEL);
    gfx_draw_text(s, 10, 6, "ZeruX Gorev Yoneticisi", TM_ACCENT, TM_PANEL);

    /* Stats line */
    uint32_t total = task_get_count();
    char tmp[32];
    gfx_draw_text(s, 10, 32, "Toplam gorev:", TM_DIM, TM_BG);
    u32s(total, tmp);
    gfx_draw_text(s, 120, 32, tmp, TM_TEXT, TM_BG);

    gfx_draw_text(s, 170, 32, "Uptime(ms):", TM_DIM, TM_BG);
    u32s(timer_get_uptime_ms(), tmp);
    gfx_draw_text(s, 260, 32, tmp, TM_TEXT, TM_BG);

    /* Column header */
    int32_t hy = 50;
    gfx_fill_rect(s, 0, hy, W, 18, TM_HEADER);
    gfx_draw_text(s, 4,   hy+1, "PID", TM_DIM, TM_HEADER);
    gfx_draw_text(s, 40,  hy+1, "Ad", TM_DIM, TM_HEADER);
    gfx_draw_text(s, 200, hy+1, "Durum", TM_DIM, TM_HEADER);
    gfx_draw_text(s, 310, hy+1, "Pri", TM_DIM, TM_HEADER);
    gfx_draw_text(s, 345, hy+1, "PPID", TM_DIM, TM_HEADER);
    gfx_draw_text(s, 395, hy+1, "Stack(KB)", TM_DIM, TM_HEADER);

    gfx_fill_rect(s, 0, hy + 18, W, 1, TM_SEP);

    /* Task rows */
    int32_t y = hy + 20;
    int row = 0;
    task_t *head = task_get_list_head();
    if (!head) goto no_tasks;

    {
        task_t *t = head;
        do {
            if (row < ctx->scroll) { row++; t = t->next; continue; }
            if (y > (int32_t)s->height - 36) break;

            bool selected = (ctx->selected_pid >= 0 && (uint32_t)ctx->selected_pid == t->pid);
            uint32_t row_bg = selected ? TM_ACCENT :
                               (row % 2 == 0 ? TM_BG : TM_PANEL);
            uint32_t row_fg = selected ? TM_BG : TM_TEXT;

            gfx_fill_rect(s, 0, y, W, 18, row_bg);

            /* PID */
            u32s(t->pid, tmp);
            gfx_draw_text(s, 4, y+1, tmp, selected ? row_bg : TM_CYAN, row_bg);

            /* Name (max 18 chars) */
            char name_buf[20];
            int ni = 0;
            while (t->name[ni] && ni < 18) { name_buf[ni] = t->name[ni]; ni++; }
            name_buf[ni] = '\0';
            gfx_draw_text(s, 40, y+1, name_buf, row_fg, row_bg);

            /* State */
            gfx_draw_text(s, 200, y+1, state_label(t->state),
                          selected ? TM_BG : state_color(t->state), row_bg);

            /* Priority */
            u32s(t->priority, tmp);
            gfx_draw_text(s, 310, y+1, tmp, row_fg, row_bg);

            /* Parent PID */
            u32s(t->parent_pid, tmp);
            gfx_draw_text(s, 345, y+1, tmp, selected ? TM_BG : TM_DIM, row_bg);

            /* Stack size in KB */
            u32s(TASK_STACK_SIZE / 1024, tmp);
            gfx_draw_text(s, 395, y+1, tmp, row_fg, row_bg);

            y += 18;
            row++;
            t = t->next;
        } while (t != head);
    }

    if (row == 0) {
no_tasks:;
        gfx_draw_text(s, 20, 90, "Gorev listesi bos.", TM_DIM, TM_BG);
    }

    /* Scroll bar indicator */
    {
        gfx_fill_rect(s, W - 8, hy + 20, 6, (int32_t)s->height - hy - 56, TM_SEP);
        /* thumb */
        uint32_t cnt = total > 0 ? total : 1;
        int32_t track_h = (int32_t)s->height - hy - 56;
        int32_t thumb_h = track_h / (int32_t)cnt;
        if (thumb_h < 10) thumb_h = 10;
        int32_t thumb_y = hy + 20 + (int32_t)ctx->scroll * track_h / (int32_t)cnt;
        gfx_fill_rect(s, W - 8, thumb_y, 6, thumb_h, TM_ACCENT);
    }

    /* Bottom action bar */
    int32_t bar_y = (int32_t)s->height - 32;
    gfx_fill_rect(s, 0, bar_y, W, 32, TM_PANEL);
    gfx_fill_rect(s, 0, bar_y, W, 1, TM_SEP);

    /* Scroll buttons */
    gfx_fill_round_rect(s, 6, bar_y + 5, 60, 22, 4, TM_HEADER);
    gfx_draw_round_rect(s, 6, bar_y + 5, 60, 22, 4, TM_DIM);
    gfx_draw_text(s, 12, bar_y + 9, "^ Yukari", TM_TEXT, TM_HEADER);

    gfx_fill_round_rect(s, 72, bar_y + 5, 60, 22, 4, TM_HEADER);
    gfx_draw_round_rect(s, 72, bar_y + 5, 60, 22, 4, TM_DIM);
    gfx_draw_text(s, 78, bar_y + 9, "v Asagi", TM_TEXT, TM_HEADER);

    gfx_fill_round_rect(s, 140, bar_y + 5, 140, 22, 4, TM_HEADER);
    gfx_draw_round_rect(s, 140, bar_y + 5, 140, 22, 4, TM_RED);
    gfx_draw_text(s, 148, bar_y + 9, "Gorevi Sonlandir", TM_RED, TM_HEADER);

    gfx_fill_round_rect(s, W - 80, bar_y + 5, 74, 22, 4, TM_HEADER);
    gfx_draw_round_rect(s, W - 80, bar_y + 5, 74, 22, 4, TM_RED);
    gfx_draw_text(s, W - 72, bar_y + 9, "Yenile", TM_RED, TM_HEADER);

    wm_invalidate(win->id);
}

/* ── Update ─────────────────────────────────────────── */
static void tm_update(window_t *win) {
    tm_ctx_t *ctx = (tm_ctx_t *)win->app_data;
    if (!ctx) return;
    ctx->refresh_timer++;
    /* Auto-refresh every ~90 frames (~3 sec at 30fps) */
    if (ctx->refresh_timer >= 90 || ctx->need_redraw) {
        ctx->refresh_timer = 0;
        ctx->need_redraw = false;
        tm_draw(win);
    }
}

/* ── Click ──────────────────────────────────────────── */
static void tm_click(window_t *win, int32_t lx, int32_t ly) {
    tm_ctx_t *ctx = (tm_ctx_t *)win->app_data;
    if (!ctx || !win->content) return;

    int32_t bar_y = (int32_t)win->content->height - 32;
    int32_t W     = (int32_t)win->content->width;

    /* Scroll up */
    if (lx >= 6 && lx <= 66 && ly >= bar_y + 5 && ly <= bar_y + 27) {
        if (ctx->scroll > 0) { ctx->scroll--; ctx->need_redraw = true; }
        return;
    }
    /* Scroll down */
    if (lx >= 72 && lx <= 132 && ly >= bar_y + 5 && ly <= bar_y + 27) {
        ctx->scroll++;
        ctx->need_redraw = true;
        return;
    }
    /* Kill */
    if (lx >= 140 && lx <= 280 && ly >= bar_y + 5 && ly <= bar_y + 27) {
        if (ctx->selected_pid >= 0) {
            task_kill(ctx->selected_pid);
            ctx->need_redraw = true;
        }
        return;
    }
    /* Refresh */
    if (lx >= W - 80 && lx <= W - 6 && ly >= bar_y + 5 && ly <= bar_y + 27) {
        ctx->scroll = 0;
        ctx->selected_pid = -1;
        ctx->need_redraw = true;
        return;
    }

    /* Row click — select task */
    int32_t hy = 50;
    int32_t row_area_top = hy + 20;
    if (ly >= row_area_top && ly < bar_y) {
        int row_idx = (ly - row_area_top) / 18 + ctx->scroll;
        task_t *head = task_get_list_head();
        if (head) {
            int r = 0;
            task_t *t = head;
            do {
                if (r == row_idx) {
                    ctx->selected_pid = (int)t->pid;
                    ctx->need_redraw = true;
                    return;
                }
                r++; t = t->next;
            } while (t != head);
        }
    }
}

static void tm_destroy(window_t *win) {
    if (win->app_data) kfree(win->app_data);
}

/* ── Create ─────────────────────────────────────────── */
int32_t taskmgr_app_create(int32_t x, int32_t y, int32_t w, int32_t h) {
    int32_t existing = wm_find_window_by_title("ZeruX Gorev Yoneticisi");
    if (existing >= 0) {
        wm_focus_window(existing);
        return existing;
    }

    int32_t win_id = wm_create_window("ZeruX Gorev Yoneticisi", x, y, w, h);
    if (win_id < 0) return -1;

    tm_ctx_t *ctx = (tm_ctx_t *)kzalloc(sizeof(tm_ctx_t));
    if (!ctx) { wm_destroy_window(win_id); return -1; }
    ctx->window_id   = win_id;
    ctx->selected_pid = -1;
    ctx->need_redraw  = true;

    wm_set_app_data(win_id, ctx);
    wm_set_update_handler(win_id, tm_update);
    wm_set_click_handler(win_id, tm_click);
    wm_set_destroy_handler(win_id, tm_destroy);
    wm_set_resize_handler(win_id, tm_draw);

    window_t *win = wm_get_window(win_id);
    if (win) {
        tm_draw(win);
        wm_show_window(win_id, true);
        wm_focus_window(win_id);
    }
    return win_id;
}
