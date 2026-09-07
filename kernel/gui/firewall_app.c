/* =============================================================================
 * ZeruX OS — Firewall GUI Application (Layer 4 Interactive)
 * File: kernel/gui/firewall_app.c
 * =============================================================================
 */

#include "firewall_app.h"
#include "firewall.h"
#include "window.h"
#include "gfx2d.h"
#include "kheap.h"
#include "serial.h"
#include <stddef.h>

/* ── Renkler ─────────────────────────────────────────── */
#define FW_BG         0x1A1B26
#define FW_PANEL      0x0F111A
#define FW_ACCENT     0x7AA2F7
#define FW_RED        0xF7768E
#define FW_GREEN      0x9ECE6A
#define FW_YELLOW     0xE0AF68
#define FW_TEXT       0xE0E2EA
#define FW_SUBTEXT    0xA9B1D6
#define FW_BTN        0x292E42
#define FW_BTN_HOVER  0x3D59A1
#define FW_SEP        0x414868

/* ── Tab / sayfa sistemi ─────────────────────────────── */
typedef enum {
    TAB_STATUS = 0,
    TAB_RULES  = 1,
    TAB_ADD    = 2
} fw_tab_t;

/* Input kutusu için basit state */
typedef struct {
    char buf[64];
    int  len;
    bool focused;
} fw_input_t;

typedef struct {
    int32_t  window_id;
    fw_tab_t tab;
    int      rules_scroll;
    bool     need_redraw;

    /* ADD tab inputları */
    fw_input_t inp_ip;      /* "192.168.1.100" ya da boş = any */
    fw_input_t inp_mask;    /* "255.255.255.0" ya da boş */
    fw_input_t inp_port;    /* "80" ya da "80-443" */
    fw_input_t inp_name;    /* kural adı */
    fw_input_t inp_mac;     /* "AA:BB:CC:DD:EE:FF" */
    uint8_t    sel_proto;   /* 0=ANY 1=ICMP 6=TCP 17=UDP */
    uint8_t    sel_action;  /* 0=DROP 1=ALLOW */
    uint8_t    sel_dir;     /* 0=IN 1=OUT 2=BOTH */
    int        focused_inp; /* 0=ip 1=mask 2=port 3=name 4=mac -1=none */
} fw_ctx_t;

/* ── Helpers ─────────────────────────────────────────── */
static void u32_to_str(uint32_t v, char *out) {
    char tmp[12]; int l = 0;
    if (v == 0) { out[0]='0'; out[1]='\0'; return; }
    while (v) { tmp[l++] = '0' + (char)(v % 10); v /= 10; }
    for (int i = 0; i < l; i++) out[i] = tmp[l-1-i];
    out[l] = '\0';
}

static uint32_t str_to_u32(const char *s) {
    uint32_t v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (uint32_t)(*s - '0'); s++; }
    return v;
}

static uint32_t parse_ip(const char *s) {
    uint32_t a[4] = {0,0,0,0}; int idx = 0;
    while (*s && idx < 4) {
        a[idx] = str_to_u32(s);
        while (*s >= '0' && *s <= '9') s++;
        if (*s == '.') { s++; idx++; } else break;
    }
    return (a[0] << 24) | (a[1] << 16) | (a[2] << 8) | a[3];
}

static void fw_btn(gfx_surface_t *s, int32_t x, int32_t y, int32_t w, int32_t h,
                   const char *label, uint32_t bg, uint32_t border_col) {
    gfx_fill_round_rect(s, x, y, w, h, 4, bg);
    gfx_draw_round_rect(s, x, y, w, h, 4, border_col);
    int llen = 0; while (label[llen]) llen++;
    int32_t tx = x + (w - llen * 8) / 2;
    int32_t ty = y + (h - 16) / 2;
    gfx_draw_text(s, tx, ty, label, FW_TEXT, bg);
}

static bool btn_hit(int32_t mx, int32_t my, int32_t x, int32_t y, int32_t w, int32_t h) {
    return mx >= x && mx < x+w && my >= y && my < y+h;
}

/* ── Input box helper ────────────────────────────────── */
static void fw_draw_input(gfx_surface_t *s, fw_input_t *inp,
                           int32_t x, int32_t y, int32_t w,
                           const char *placeholder) {
    uint32_t border = inp->focused ? FW_ACCENT : FW_SEP;
    gfx_fill_rect(s, x, y, w, 20, FW_PANEL);
    gfx_draw_rect(s, x, y, w, 20, border);
    const char *txt = inp->len > 0 ? inp->buf : placeholder;
    uint32_t col = inp->len > 0 ? FW_TEXT : FW_SUBTEXT;
    gfx_draw_text(s, x+4, y+2, txt, col, FW_PANEL);
    if (inp->focused) {
        int32_t cx = x + 4 + inp->len * 8;
        gfx_fill_rect(s, cx, y+3, 2, 14, FW_ACCENT);
    }
}

/* ── Tab bar ─────────────────────────────────────────── */
static void fw_draw_tabs(gfx_surface_t *s, fw_tab_t active) {
    const char *labels[] = {"Durum", "Kurallar", "Kural Ekle"};
    for (int i = 0; i < 3; i++) {
        int32_t tx = 10 + i * 120;
        bool is_active = (fw_tab_t)i == active;
        uint32_t bg = is_active ? FW_ACCENT : FW_PANEL;
        uint32_t fg = is_active ? FW_BG    : FW_TEXT;
        gfx_fill_rect(s, tx, 2, 115, 26, bg);
        gfx_draw_rect(s, tx, 2, 115, 26, FW_SEP);
        int llen = 0; while (labels[i][llen]) llen++;
        gfx_draw_text(s, tx + (115 - llen*8)/2, 7, labels[i], fg, bg);
    }
    gfx_fill_rect(s, 0, 28, s->width, 2, FW_SEP);
}

/* ── STATUS tab ──────────────────────────────────────── */
static void fw_draw_status(gfx_surface_t *s) {
    const fw_stats_t *st = fw_get_stats();
    fw_action_t pol = fw_get_default_policy();
    int y = 40;

    gfx_draw_text(s, 16, y, "Varsayilan Politika:", FW_SUBTEXT, FW_BG); y += 18;
    const char *pol_str = pol == FW_ACTION_DROP ? "DROP (Kapalı)" : "ALLOW (Açık)";
    uint32_t pol_col = pol == FW_ACTION_DROP ? FW_RED : FW_GREEN;
    gfx_draw_text(s, 24, y, pol_str, pol_col, FW_BG); y += 24;

    gfx_fill_rect(s, 10, y, s->width - 20, 1, FW_SEP); y += 10;

    /* Stats */
    char tmp[32];
    gfx_draw_text(s, 16, y, "Trafik Istatistikleri:", FW_ACCENT, FW_BG); y += 20;

    gfx_draw_text(s, 24, y, "Gelen toplam :", FW_SUBTEXT, FW_BG);
    u32_to_str(st->total_in, tmp);
    gfx_draw_text(s, 180, y, tmp, FW_TEXT, FW_BG); y += 18;

    gfx_draw_text(s, 24, y, "Giden toplam  :", FW_SUBTEXT, FW_BG);
    u32_to_str(st->total_out, tmp);
    gfx_draw_text(s, 180, y, tmp, FW_TEXT, FW_BG); y += 18;

    gfx_draw_text(s, 24, y, "Engellenen (IN):", FW_SUBTEXT, FW_BG);
    u32_to_str(st->dropped_in, tmp);
    gfx_draw_text(s, 180, y, tmp, FW_RED, FW_BG); y += 18;

    gfx_draw_text(s, 24, y, "Engellenen(OUT):", FW_SUBTEXT, FW_BG);
    u32_to_str(st->dropped_out, tmp);
    gfx_draw_text(s, 180, y, tmp, FW_RED, FW_BG); y += 18;

    gfx_draw_text(s, 24, y, "Log kaydi      :", FW_SUBTEXT, FW_BG);
    u32_to_str(st->logged_count, tmp);
    gfx_draw_text(s, 180, y, tmp, FW_YELLOW, FW_BG); y += 26;

    gfx_fill_rect(s, 10, y, s->width - 20, 1, FW_SEP); y += 10;

    /* Quick actions */
    gfx_draw_text(s, 16, y, "Hızlı İşlemler:", FW_ACCENT, FW_BG); y += 22;
    fw_btn(s, 16, y, 130, 28, "Kurallari Temizle", FW_BTN, FW_RED);
    fw_btn(s, 160, y, 130, 28, "Istatistik Sifirla", FW_BTN, FW_YELLOW);
    y += 38;
    fw_btn(s, 16, y, 130, 28, "Default: DROP", FW_BTN, FW_RED);
    fw_btn(s, 160, y, 130, 28, "Default: ALLOW", FW_BTN, FW_GREEN);

    /* Active rule count */
    int cnt = 0;
    for (int i = 0; i < fw_get_max_rules(); i++) {
        const fw_rule_t *r = fw_get_rule(i);
        if (r) cnt++;
    }
    y += 50;
    gfx_draw_text(s, 16, y, "Aktif kural:", FW_SUBTEXT, FW_BG);
    u32_to_str((uint32_t)cnt, tmp);
    gfx_draw_text(s, 120, y, tmp, FW_ACCENT, FW_BG);
}

/* ── RULES tab ───────────────────────────────────────── */
static void fw_draw_rules(gfx_surface_t *s, int scroll) {
    /* Column headers */
    int y = 38;
    gfx_fill_rect(s, 0, y, s->width, 18, FW_PANEL);
    gfx_draw_text(s, 4,  y+1, "#", FW_SUBTEXT, FW_PANEL);
    gfx_draw_text(s, 20, y+1, "Eylem", FW_SUBTEXT, FW_PANEL);
    gfx_draw_text(s, 80, y+1, "Yon", FW_SUBTEXT, FW_PANEL);
    gfx_draw_text(s, 120,y+1, "Proto", FW_SUBTEXT, FW_PANEL);
    gfx_draw_text(s, 170,y+1, "IP/CIDR", FW_SUBTEXT, FW_PANEL);
    gfx_draw_text(s, 300,y+1, "Port", FW_SUBTEXT, FW_PANEL);
    gfx_draw_text(s, 350,y+1, "Isab.", FW_SUBTEXT, FW_PANEL);
    gfx_draw_text(s, 400,y+1, "Ad", FW_SUBTEXT, FW_PANEL);
    y += 20;

    int row = 0;
    char tmp[32];
    for (int i = 0; i < fw_get_max_rules(); i++) {
        const fw_rule_t *r = fw_get_rule(i);
        if (!r) continue;
        if (row < scroll) { row++; continue; }
        if (y > (int32_t)s->height - 40) break;

        uint32_t row_bg = (row % 2 == 0) ? FW_BG : FW_PANEL;
        gfx_fill_rect(s, 0, y, s->width, 18, row_bg);

        /* Rule index */
        u32_to_str((uint32_t)i, tmp);
        gfx_draw_text(s, 4,  y+1, tmp, FW_SUBTEXT, row_bg);

        /* Action */
        const char *act = r->action == FW_ACTION_DROP ? "DROP" :
                          r->action == FW_ACTION_LOG  ? "LOG " : "ALLOW";
        uint32_t act_col = r->action == FW_ACTION_DROP ? FW_RED :
                           r->action == FW_ACTION_LOG  ? FW_YELLOW : FW_GREEN;
        gfx_draw_text(s, 20, y+1, act, act_col, row_bg);

        /* Direction */
        const char *dir = r->direction == FW_DIR_IN  ? "IN  " :
                          r->direction == FW_DIR_OUT ? "OUT " : "BOTH";
        gfx_draw_text(s, 80, y+1, dir, FW_TEXT, row_bg);

        /* Protocol */
        const char *proto = r->protocol == 0  ? "ANY " :
                            r->protocol == 1  ? "ICMP" :
                            r->protocol == 6  ? "TCP " : "UDP ";
        gfx_draw_text(s, 120, y+1, proto, FW_TEXT, row_bg);

        /* IP */
        if (r->src_ip == 0 && r->src_mask == 0) {
            gfx_draw_text(s, 170, y+1, "*.*.*.*", FW_SUBTEXT, row_bg);
        } else {
            char ip_s[20]; int p2 = 0;
            uint8_t b0 = (r->src_ip>>24)&0xFF, b1=(r->src_ip>>16)&0xFF,
                    b2 = (r->src_ip>>8)&0xFF,  b3=r->src_ip&0xFF;
            char nb[4];
            u32_to_str(b0,nb); int nl=0; while(nb[nl])ip_s[p2++]=nb[nl++]; ip_s[p2++]='.';
            u32_to_str(b1,nb); nl=0; while(nb[nl])ip_s[p2++]=nb[nl++]; ip_s[p2++]='.';
            u32_to_str(b2,nb); nl=0; while(nb[nl])ip_s[p2++]=nb[nl++]; ip_s[p2++]='.';
            u32_to_str(b3,nb); nl=0; while(nb[nl])ip_s[p2++]=nb[nl++];
            ip_s[p2] = '\0';
            gfx_draw_text(s, 170, y+1, ip_s, FW_TEXT, row_bg);
        }

        /* Port */
        if (r->port_min == 0 && r->port_max == 0) {
            gfx_draw_text(s, 300, y+1, "any", FW_SUBTEXT, row_bg);
        } else {
            u32_to_str(r->port_min, tmp);
            gfx_draw_text(s, 300, y+1, tmp, FW_TEXT, row_bg);
            if (r->port_max && r->port_max != r->port_min) {
                gfx_draw_text(s, 330, y+1, "-", FW_TEXT, row_bg);
                u32_to_str(r->port_max, tmp);
                gfx_draw_text(s, 338, y+1, tmp, FW_TEXT, row_bg);
            }
        }

        /* Hit count */
        u32_to_str(r->hit_count, tmp);
        gfx_draw_text(s, 350, y+1, tmp, FW_YELLOW, row_bg);

        /* Name */
        gfx_draw_text(s, 400, y+1, r->name, FW_SUBTEXT, row_bg);

        /* Delete button */
        fw_btn(s, (int32_t)s->width - 40, y+2, 34, 14, "SIL", FW_BTN, FW_RED);

        y += 18;
        row++;
    }

    if (row == 0) {
        gfx_draw_text(s, 20, 80, "Hic kural yok. 'Kural Ekle' sekmesini kullan.", FW_SUBTEXT, FW_BG);
    }

    /* Scroll arrows */
    if (scroll > 0)
        fw_btn(s, (int32_t)s->width/2 - 20, (int32_t)s->height - 30, 38, 20, "^ Yukari", FW_BTN, FW_ACCENT);
    fw_btn(s, (int32_t)s->width/2 + 22, (int32_t)s->height - 30, 38, 20, "v Asagi", FW_BTN, FW_ACCENT);
}

/* ── ADD tab ─────────────────────────────────────────── */
static void fw_draw_add(gfx_surface_t *s, fw_ctx_t *ctx) {
    int y = 38;

    gfx_draw_text(s, 16, y, "Kaynak IP (bos=herhangi):", FW_SUBTEXT, FW_BG);
    gfx_draw_text(s, 240, y, "MAC (bos=herhangi):", FW_SUBTEXT, FW_BG); y += 14;
    fw_draw_input(s, &ctx->inp_ip,   16, y, 200, "orn: 192.168.1.100");
    fw_draw_input(s, &ctx->inp_mac,  240, y, 200, "orn: AA:BB:CC:DD:EE:FF"); y += 26;

    gfx_draw_text(s, 16, y, "Alt Ag Maskesi (bos=host):", FW_SUBTEXT, FW_BG); y += 14;
    fw_draw_input(s, &ctx->inp_mask, 16, y, 200, "orn: 255.255.255.0"); y += 26;

    gfx_draw_text(s, 16, y, "Port / Aralik (bos=herhangi):", FW_SUBTEXT, FW_BG); y += 14;
    fw_draw_input(s, &ctx->inp_port, 16, y, 200, "orn: 80 veya 8000-9000"); y += 26;

    gfx_draw_text(s, 16, y, "Kural Adi:", FW_SUBTEXT, FW_BG); y += 14;
    fw_draw_input(s, &ctx->inp_name, 16, y, 200, "orn: block-ssh"); y += 30;

    /* Protocol selector */
    gfx_draw_text(s, 16, y, "Protokol:", FW_SUBTEXT, FW_BG); y += 14;
    const char *protos[] = {"ANY","ICMP","TCP","UDP"};
    uint8_t proto_vals[] = {0, 1, 6, 17};
    for (int i = 0; i < 4; i++) {
        bool sel = (ctx->sel_proto == proto_vals[i]);
        fw_btn(s, 16 + i*60, y, 55, 22, protos[i],
               sel ? FW_ACCENT : FW_BTN,
               sel ? FW_BG     : FW_TEXT);
    }
    y += 32;

    /* Action selector */
    gfx_draw_text(s, 16, y, "Eylem:", FW_SUBTEXT, FW_BG); y += 14;
    fw_btn(s, 16,  y, 70, 22, "ENGELLE", ctx->sel_action == 0 ? FW_RED   : FW_BTN,
           ctx->sel_action == 0 ? FW_BG : FW_RED);
    fw_btn(s, 96,  y, 70, 22, "IZIN VER", ctx->sel_action == 1 ? FW_GREEN : FW_BTN,
           ctx->sel_action == 1 ? FW_BG : FW_GREEN);
    fw_btn(s, 176, y, 70, 22, "LOG", ctx->sel_action == 2 ? FW_YELLOW : FW_BTN,
           ctx->sel_action == 2 ? FW_BG : FW_YELLOW);
    y += 32;

    /* Direction selector */
    gfx_draw_text(s, 16, y, "Yon:", FW_SUBTEXT, FW_BG); y += 14;
    const char *dirs[] = {"GELEN","GIDEN","IKISI"};
    for (int i = 0; i < 3; i++) {
        bool sel = (ctx->sel_dir == (uint8_t)i);
        fw_btn(s, 16 + i*80, y, 75, 22, dirs[i],
               sel ? FW_ACCENT : FW_BTN,
               sel ? FW_BG     : FW_TEXT);
    }
    y += 38;

    /* Apply button */
    fw_btn(s, 16, y, 180, 28, "Kurali Uygula", FW_GREEN, FW_BG);
}

/* ── Main draw ───────────────────────────────────────── */
static void fw_app_draw(window_t *win) {
    fw_ctx_t *ctx = (fw_ctx_t *)win->app_data;
    if (!ctx || !win->content) return;

    gfx_surface_t *s = win->content;
    surface_clear(s, FW_BG);

    fw_draw_tabs(s, ctx->tab);

    switch (ctx->tab) {
        case TAB_STATUS: fw_draw_status(s); break;
        case TAB_RULES:  fw_draw_rules(s, ctx->rules_scroll); break;
        case TAB_ADD:    fw_draw_add(s, ctx); break;
    }

    wm_invalidate(win->id);
}

/* ── Update ──────────────────────────────────────────── */
static void fw_app_update(window_t *win) {
    fw_ctx_t *ctx = (fw_ctx_t *)win->app_data;
    if (!ctx) return;
    static uint32_t timer = 0;
    timer++;
    if (ctx->tab == TAB_STATUS && (timer % 60 == 0)) {
        ctx->need_redraw = true;
    }
    if (ctx->need_redraw) {
        fw_app_draw(win);
        ctx->need_redraw = false;
    }
}

/* ── Key handler for ADD tab ─────────────────────────── */
static void fw_app_key(window_t *win, char c) {
    fw_ctx_t *ctx = (fw_ctx_t *)win->app_data;
    if (!ctx || ctx->tab != TAB_ADD || ctx->focused_inp < 0) return;

    fw_input_t *inputs[] = {&ctx->inp_ip, &ctx->inp_mask, &ctx->inp_port, &ctx->inp_name, &ctx->inp_mac};
    fw_input_t *inp = inputs[ctx->focused_inp];

    if (c == '\b' || c == 127) {
        if (inp->len > 0) inp->buf[--inp->len] = '\0';
    } else if (c == '\t') {
        ctx->focused_inp = (ctx->focused_inp + 1) % 5;
        for (int i = 0; i < 5; i++) inputs[i]->focused = (i == ctx->focused_inp);
    } else if (c >= 32 && c < 127 && inp->len < 63) {
        inp->buf[inp->len++] = c;
        inp->buf[inp->len] = '\0';
    }
    ctx->need_redraw = true;
}

/* ── Click handler ───────────────────────────────────── */
static void fw_app_click(window_t *win, int32_t lx, int32_t ly) {
    fw_ctx_t *ctx = (fw_ctx_t *)win->app_data;
    if (!ctx) return;

    /* Tab bar */
    if (ly >= 2 && ly <= 28) {
        if (btn_hit(lx, ly, 10, 2, 115, 26))  ctx->tab = TAB_STATUS;
        else if (btn_hit(lx, ly, 130, 2, 115, 26)) ctx->tab = TAB_RULES;
        else if (btn_hit(lx, ly, 250, 2, 115, 26)) ctx->tab = TAB_ADD;
        ctx->need_redraw = true;
        return;
    }

    /* ── STATUS tab actions ── */
    if (ctx->tab == TAB_STATUS) {
        int y = 40 + 18 + 24 + 10 + 10 + 20 + 18*5 + 26 + 10 + 10 + 22;
        if (btn_hit(lx, ly, 16, y, 130, 28)) fw_flush();
        else if (btn_hit(lx, ly, 160, y, 130, 28)) fw_reset_stats();
        y += 38;
        if (btn_hit(lx, ly, 16, y, 130, 28))  fw_set_default_policy(FW_ACTION_DROP);
        else if (btn_hit(lx, ly, 160, y, 130, 28)) fw_set_default_policy(FW_ACTION_ALLOW);
        ctx->need_redraw = true;
        return;
    }

    /* ── RULES tab: delete buttons & scroll ── */
    if (ctx->tab == TAB_RULES) {
        int y = 38 + 20;
        int row = 0;
        for (int i = 0; i < fw_get_max_rules(); i++) {
            const fw_rule_t *r = fw_get_rule(i);
            if (!r) continue;
            if (row < ctx->rules_scroll) { row++; continue; }
            if (y > (int32_t)win->content->height - 40) break;
            int32_t btn_x = (int32_t)win->content->width - 40;
            if (btn_hit(lx, ly, btn_x, y+2, 34, 14)) {
                fw_del_rule(i);
                ctx->need_redraw = true;
                return;
            }
            y += 18; row++;
        }
        /* scroll */
        int32_t sw = (int32_t)win->content->width;
        int32_t sh = (int32_t)win->content->height;
        if (btn_hit(lx, ly, sw/2 - 20, sh - 30, 38, 20) && ctx->rules_scroll > 0) {
            ctx->rules_scroll--;
            ctx->need_redraw = true;
        }
        if (btn_hit(lx, ly, sw/2 + 22, sh - 30, 38, 20)) {
            ctx->rules_scroll++;
            ctx->need_redraw = true;
        }
        return;
    }

    /* ── ADD tab ── */
    if (ctx->tab == TAB_ADD) {
        fw_input_t *inputs[] = {&ctx->inp_ip, &ctx->inp_mask, &ctx->inp_port, &ctx->inp_name, &ctx->inp_mac};
        int y_bases[] = {38+14, 38+14+26+14, 38+14+26+14+26+14, 38+14+26+14+26+14+26+14, 38+14};
        int x_bases[] = {16, 16, 16, 16, 240};
        for (int i = 0; i < 5; i++) {
            if (btn_hit(lx, ly, x_bases[i], y_bases[i], 200, 20)) {
                ctx->focused_inp = i;
                for (int j = 0; j < 5; j++) inputs[j]->focused = (j == i);
                ctx->need_redraw = true;
                return;
            }
        }

        /* Protocol */
        uint8_t proto_vals[] = {0, 1, 6, 17};
        int y_proto = 38 + 14+26 + 14+26 + 14+26 + 14+30 + 14;
        for (int i = 0; i < 4; i++) {
            if (btn_hit(lx, ly, 16 + i*60, y_proto, 55, 22)) {
                ctx->sel_proto = proto_vals[i];
                ctx->need_redraw = true; return;
            }
        }

        /* Action */
        int y_act = y_proto + 32 + 14;
        if (btn_hit(lx, ly, 16,  y_act, 70, 22)) { ctx->sel_action = 0; ctx->need_redraw = true; return; }
        if (btn_hit(lx, ly, 96,  y_act, 70, 22)) { ctx->sel_action = 1; ctx->need_redraw = true; return; }
        if (btn_hit(lx, ly, 176, y_act, 70, 22)) { ctx->sel_action = 2; ctx->need_redraw = true; return; }

        /* Direction */
        int y_dir = y_act + 32 + 14;
        for (int i = 0; i < 3; i++) {
            if (btn_hit(lx, ly, 16 + i*80, y_dir, 75, 22)) {
                ctx->sel_dir = (uint8_t)i;
                ctx->need_redraw = true; return;
            }
        }

        /* Apply */
        int y_apply = y_dir + 38;
        if (btn_hit(lx, ly, 16, y_apply, 180, 28)) {
            /* Parse inputs */
            uint32_t ip   = (ctx->inp_ip.len > 0)   ? parse_ip(ctx->inp_ip.buf)   : 0;
            uint32_t mask = (ctx->inp_mask.len > 0)  ? parse_ip(ctx->inp_mask.buf) : 0;

            uint16_t port_min = 0, port_max = 0;
            if (ctx->inp_port.len > 0) {
                const char *ps = ctx->inp_port.buf;
                port_min = (uint16_t)str_to_u32(ps);
                while (*ps >= '0' && *ps <= '9') ps++;
                if (*ps == '-') { ps++; port_max = (uint16_t)str_to_u32(ps); }
                else port_max = port_min;
            }

            const char *name = ctx->inp_name.len > 0 ? ctx->inp_name.buf : "kural";
            fw_action_t act  = (fw_action_t)ctx->sel_action;
            fw_direction_t dir = (fw_direction_t)ctx->sel_dir;

            uint8_t mac_addr[6] = {0};
            uint8_t *mac_ptr = NULL;
            if (ctx->inp_mac.len > 0) {
                const char *ms = ctx->inp_mac.buf;
                for(int i=0; i<6; i++) {
                    uint8_t val = 0;
                    for(int j=0; j<2; j++) {
                        char c = ms[i*3 + j];
                        val *= 16;
                        if (c >= '0' && c <= '9') val += c - '0';
                        else if (c >= 'a' && c <= 'f') val += c - 'a' + 10;
                        else if (c >= 'A' && c <= 'F') val += c - 'A' + 10;
                    }
                    mac_addr[i] = val;
                }
                mac_ptr = mac_addr;
            }

            int res = fw_add_rule_full(act, dir, ip, mask, mac_ptr,
                                       ctx->sel_proto, port_min, port_max, name);
            if (res >= 0) {
                /* Clear form on success */
                ctx->inp_ip.buf[0]='\0'; ctx->inp_ip.len=0;
                ctx->inp_mask.buf[0]='\0'; ctx->inp_mask.len=0;
                ctx->inp_port.buf[0]='\0'; ctx->inp_port.len=0;
                ctx->inp_name.buf[0]='\0'; ctx->inp_name.len=0;
                ctx->inp_mac.buf[0]='\0';  ctx->inp_mac.len=0;
                ctx->tab = TAB_RULES;
            }
            ctx->need_redraw = true;
        }
    }
}

static void fw_app_destroy(window_t *win) {
    if (win->app_data) kfree(win->app_data);
}

int32_t firewall_app_create(int32_t x, int32_t y, int32_t w, int32_t h) {
    int32_t existing = wm_find_window_by_title("ZeruX Guvenlik Duvari");
    if (existing >= 0) {
        wm_focus_window(existing);
        return existing;
    }

    int32_t win_id = wm_create_window("ZeruX Guvenlik Duvari", x, y, w, h);
    if (win_id < 0) return -1;

    fw_ctx_t *ctx = (fw_ctx_t *)kzalloc(sizeof(fw_ctx_t));
    if (!ctx) { wm_destroy_window(win_id); return -1; }

    ctx->window_id = win_id;
    ctx->tab = TAB_STATUS;
    ctx->focused_inp = -1;
    ctx->need_redraw = true;

    wm_set_app_data(win_id, ctx);
    wm_set_update_handler(win_id, fw_app_update);
    wm_set_click_handler(win_id, fw_app_click);
    wm_set_key_handler(win_id, fw_app_key);
    wm_set_destroy_handler(win_id, fw_app_destroy);
    wm_set_resize_handler(win_id, fw_app_draw);

    window_t *win = wm_get_window(win_id);
    if (win) {
        fw_app_draw(win);
        wm_show_window(win_id, true);
        wm_focus_window(win_id);
    }
    return win_id;
}
