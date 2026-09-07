/* =============================================================================
 * ZeruX OS - Aurora Browser Application
 * File: kernel/gui/browser_app.c
 * =============================================================================
 * DOM agacini alip gfx2d.h ile window icine cizen, kaydirma ve adres cubugu
 * destekleyen tam GUI tarayicisi. terminal_app.c / explorer_app.c deseni.
 * =============================================================================
 */

#include "browser_app.h"
#include "png.h"
#include "window.h"
#include "gfx2d.h"
#include "kheap.h"
#include "serial.h"
#include "task.h"
#include "../../apps/aurora/dom.h"
#include "../../apps/aurora/html_parser.h"
#include "../../apps/aurora/http_client.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* ---- Renk paleti --------------------------------------------------------- */
#define BR_BG          0xFAFAFAu  /* sayfa arkaplan */
#define BR_TEXT        0x1A1A1Au  /* normal metin */
#define BR_H1          0x0D0D2Eu  /* h1 */
#define BR_H2          0x1A2E4Au  /* h2 */
#define BR_H3          0x1A4A2Eu  /* h3 */
#define BR_H4          0x2E1A4Au  /* h4 */
#define BR_H5          0x555577u  /* h5 */
#define BR_H6          0x777799u  /* h6 */
#define BR_LINK        0x0645ADu  /* <a> linki */
#define BR_LINK_HOV    0xCC3300u  /* hover (tiklanmis) */
#define BR_BULLET      0x555555u  /* liste noktasi */
#define BR_HR          0xCCCCCCu  /* yatay cizgi */
#define BR_BOLD        0x000000u  /* <b> */
#define BR_CODE_BG     0xF0F0F0u  /* <code> arkaplan */
#define BR_CODE_TXT    0xCC2200u  /* <code> metin rengi */
#define BR_MARK_BG     0xFFFF88u  /* <mark> arka plan */
#define BR_MARK_TXT    0x111111u  /* <mark> metin */
#define BR_QUOTE_BAR   0xAAAAABu  /* <blockquote> sol kenar cizgi */
#define BR_QUOTE_BG    0xF5F5F5u  /* <blockquote> arkaplan */
#define BR_QUOTE_TXT   0x444444u  /* <blockquote> metin */
#define BR_TH_BG       0xE0E8F0u  /* <th> arkaplan */
#define BR_TABLE_BORDER 0xCCCCCCu /* tablo kenarlari */
#define BR_DT_TXT      0x000000u  /* <dt> metin (bold) */
#define BR_DD_TXT      0x333333u  /* <dd> metin */
#define BR_SMALL_TXT   0x666666u  /* <small> metin */
#define BR_ADDRBAR_BG  0xE8E8E8u  /* adres cubugu arkaplan */
#define BR_ADDRBAR_TXT 0x111111u  /* adres cubugu metin */
#define BR_ADDRBAR_CUR 0x0066CCu  /* adres cubugu kursor */
#define BR_BTN_BG      0xD0D0D0u  /* Geri/Yenile buton arkaplan */
#define BR_BTN_TXT     0x222222u  /* buton metin */
#define BR_STATUS_BG   0xDDDDDDu  /* status bar arkaplan */
#define BR_STATUS_TXT  0x333333u  /* status bar metin */
#define BR_SEL_BG      0xBBDDFFu  /* secili link arkaplan */

/* ---- Layout sabitleri ---------------------------------------------------- */
#define BR_TOOLBAR_H   26   /* adres cubugu + buton yuksekligi */
#define BR_STATUS_H    18   /* alt status bar */
#define BR_MARGIN_X    12   /* sol kenar bogluğu */
#define BR_LINE_H      18   /* satir yuksekligi (metin icin) */
#define BR_H1_H        24   /* h1 yuksekligi */
#define BR_H2_H        22   /* h2 yuksekligi */
#define BR_H3_H        20   /* h3 yuksekligi */
#define BR_CHAR_W       8   /* glyph genisligi (8x16 bitmap font) */
#define BR_CHAR_H      16   /* glyph yuksekligi */
#define BR_MAX_LINKS  128   /* sayfa basina max tiklanabilir link */
#define BR_URL_MAX    256   /* max URL uzunlugu */
#define BR_STATUS_MAX 128   /* status bar max */
#define BR_HISTORY_MAX 16   /* max gecmis stack boyutu */
#define BR_SCROLLBAR_W 12   /* kaydirma cubugu genisligi */

/* ---- Durum makinesi ------------------------------------------------------ */
typedef enum {
    BR_STATE_IDLE = 0,  /* bekleniyor */
    BR_STATE_LOADING,   /* HTTP fetch devam ediyor (thread'de) */
    BR_STATE_DONE,      /* sayfa gosteriliyor */
    BR_STATE_ADDR_INPUT /* adres cubuguna yaziliyor */
} browser_state_t;

/* Fetch thread ile paylasilan tek-seferlik arguman yapisi */
typedef struct {
    window_t  *win;               /* browser penceresi */
    char       url[BR_URL_MAX];   /* kopyalanmis URL */
} fetch_args_t;

typedef struct {
    int32_t  y;        /* content koordinati (toolbar hizmali) */
    int32_t  x;
    int32_t  w;        /* genislik (tiklanabilir alan) */
    char     href[BR_URL_MAX];
} link_rect_t;

typedef struct {
    /* Pencere id'si (wm) */
    int32_t  win_id;

    /* URL ve durum */
    char     url[BR_URL_MAX];
    char     addr_input[BR_URL_MAX]; /* adres cubugu yazilirken */
    uint32_t addr_len;
    browser_state_t state;
    char     status[BR_STATUS_MAX];

    /* DOM agaci (html_parser.h ile parse edilir) */
    dom_node_t *dom_root;

    /* Gecmis (History) Stack'i */
    char     history[BR_HISTORY_MAX][BR_URL_MAX];
    int32_t  history_count;
    int32_t  history_idx; /* su an gosterilen index */

    /* Kaydirma */
    int32_t  scroll_y;    /* kac piksel asagi kaydirildi */
    int32_t  content_h;   /* son render'da olculen toplam sayfa yuksekligi */

    /* Tiklanabilir link tablosu */
    link_rect_t links[BR_MAX_LINKS];
    int32_t  link_count;
    int32_t  hover_link; /* -1 = yok */

    /* Yeniden cizim istegi (fetch thread'i set eder, on_update okur) */
    volatile int redraw_needed;

    /* Thread durumu ve Yikim yonetimi */
    volatile bool fetch_running;
    volatile bool window_closed;
    
    /* Klavye escape dizisi durumu (ok tuslari vb.) */
    int escape_state;

    /* Pencere iceriği boyutu */
    int32_t  surf_w;
    int32_t  surf_h;
} browser_state;

/* ---- Tiny string helpers ------------------------------------------------- */
static size_t br_strlen(const char *s) {
    size_t n = 0; while (s && s[n]) n++; return n;
}
static void br_strcpy(char *dst, const char *src, size_t max) {
    size_t i = 0;
    for (; i < max - 1 && src && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}
static int br_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static int br_strncmp(const char *a, const char *b, size_t n) {
    while (n && *a && (*a == *b)) { a++; b++; n--; }
    return n ? ((unsigned char)*a - (unsigned char)*b) : 0;
}
static void br_strcat_bounded(char *dst, const char *src, size_t max) {
    size_t n = br_strlen(dst);
    size_t i = 0;
    while (n < max - 1 && src[i]) dst[n++] = src[i++];
    dst[n] = '\0';
}

/* ---- UTF-8 decoder + Türkçe/Latin fallback --------------------------------
 * Tek UTF-8 karakter decode eder; *consumed = kaç byte tüketildi.
 * Desteklenmeyen codepoint için en yakın ASCII fallback döndürür.
 * --------------------------------------------------------------------------- */
static char br_utf8_decode(const char *p, int *consumed) {
    unsigned char c0 = (unsigned char)p[0];

    /* ASCII (0x00-0x7F): direkt döndür */
    if (c0 < 0x80) { *consumed = 1; return (char)c0; }

    /* 2-byte sequence: 110xxxxx 10xxxxxx */
    if ((c0 & 0xE0) == 0xC0 && (unsigned char)p[1] >= 0x80) {
        unsigned int cp = ((c0 & 0x1F) << 6) | ((unsigned char)p[1] & 0x3F);
        *consumed = 2;
        /* Latin Extended + Türkçe harfler → ASCII fallback */
        switch (cp) {
        /* Türkçe özel */
        case 0x011F: case 0x011E: return 'g'; /* ğ Ğ */
        case 0x0131:               return 'i'; /* ı */
        case 0x0130:               return 'I'; /* İ */
        case 0x015F: case 0x015E: return 's'; /* ş Ş */
        case 0x00E7:               return 'c'; /* ç */
        case 0x00C7:               return 'C'; /* Ç */
        case 0x00FC:               return 'u'; /* ü */
        case 0x00DC:               return 'U'; /* Ü */
        case 0x00F6:               return 'o'; /* ö */
        case 0x00D6:               return 'O'; /* Ö */
        /* Genel Latin-1 */
        case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3:
        case 0x00E4: case 0x00E5: return 'a';
        case 0x00C0: case 0x00C1: case 0x00C2: case 0x00C3:
        case 0x00C4: case 0x00C5: return 'A';
        case 0x00E8: case 0x00E9: case 0x00EA: case 0x00EB: return 'e';
        case 0x00C8: case 0x00C9: case 0x00CA: case 0x00CB: return 'E';
        case 0x00EC: case 0x00ED: case 0x00EE: case 0x00EF: return 'i';
        case 0x00CC: case 0x00CD: case 0x00CE: case 0x00CF: return 'I';
        case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F5: return 'o';
        case 0x00D2: case 0x00D3: case 0x00D4: case 0x00D5: return 'O';
        case 0x00F9: case 0x00FA: case 0x00FB: return 'u';
        case 0x00D9: case 0x00DA: case 0x00DB: return 'U';
        case 0x00FD: case 0x00FF: return 'y';
        case 0x00F1: return 'n'; /* ñ */
        case 0x00D1: return 'N';
        case 0x00DF: return 's'; /* ß */
        case 0x00A0: return ' '; /* nbsp */
        case 0x00AD: return '-'; /* shy */
        default: return '?';
        }
    }
    /* 3-byte sequence: skip entirely */
    if ((c0 & 0xF0) == 0xE0) { *consumed = 3; return '?'; }
    /* 4-byte sequence: skip */
    if ((c0 & 0xF8) == 0xF0) { *consumed = 4; return '?'; }
    /* Invalid byte */
    *consumed = 1; return '?';
}

/* UTF-8 string'i decode ederek ASCII buffer'a yazar.
 * dst_max: hedef buffer boyutu. Döndürülen string null-terminate edilir. */
static void br_utf8_to_ascii(const char *src, char *dst, size_t dst_max) {
    size_t di = 0;
    while (*src && di < dst_max - 1) {
        int consumed = 1;
        char ch = br_utf8_decode(src, &consumed);
        dst[di++] = ch;
        src += consumed;
    }
    dst[di] = '\0';
}

/* URL'den host'u ayikla: "http://host:port/path" -> "host" */
static void __attribute__((unused)) br_extract_host(const char *url, char *out, size_t max) {
    const char *p = url;
    if (br_strncmp(p, "http://", 7) == 0) p += 7;
    size_t i = 0;
    while (*p && *p != '/' && *p != ':' && i < max - 1) out[i++] = *p++;
    out[i] = '\0';
}

/* Goreceli href'i mutlak URL'e cevir */
static void br_resolve_href(const char *base_url, const char *href,
                              char *out, size_t max) {
    /* Eger href zaten mutlak ise dogrudan kullan */
    if (br_strncmp(href, "http://", 7) == 0 ||
        br_strncmp(href, "https://", 8) == 0) {
        br_strcpy(out, href, max);
        return;
    }
    /* Eger / ile baslarsa ana host'a ekle */
    if (href[0] == '/') {
        char host_part[128] = {0};
        const char *p = base_url;
        if (br_strncmp(p, "http://", 7) == 0) p += 7;
        /* host + port */
        const char *h = p;
        while (*p && *p != '/') p++;
        size_t hlen = (size_t)(p - h);
        if (hlen >= sizeof(host_part) - 8) hlen = sizeof(host_part) - 9;
        host_part[0] = 'h'; host_part[1] = 't'; host_part[2] = 't';
        host_part[3] = 'p'; host_part[4] = ':'; host_part[5] = '/';
        host_part[6] = '/';
        for (size_t i = 0; i < hlen; i++) host_part[7 + i] = h[i];
        host_part[7 + hlen] = '\0';
        br_strcpy(out, host_part, max);
        br_strcat_bounded(out, href, max);
        return;
    }
    /* Goreceli: base URL'in son '/' a kadar al, sonra href ekle */
    br_strcpy(out, base_url, max);
    char *p = out;
    if (br_strncmp(p, "http://", 7) == 0) p += 7;
    while (*p && *p != '/') p++;
    
    if (*p == '\0') {
        /* Base URL'de '/' yok, yani sadece host. Araya '/' koy ve href ekle */
        br_strcat_bounded(out, "/", max);
        br_strcat_bounded(out, href, max);
        return;
    }
    
    /* Base URL'de '/' var, sondaki slash'e kadar kes */
    size_t len = br_strlen(out);
    size_t host_end = (size_t)(p - out);
    while (len > host_end && out[len - 1] != '/') { len--; }
    out[len] = '\0';
    br_strcat_bounded(out, href, max);
}

/* ---- Render -------------------------------------------------------------- */



/* ---- LAYOUT ENGINE (Box Model) ------------------------------------------- */
typedef enum {
    BOX_BLOCK,
    BOX_INLINE,
    BOX_IMAGE
} box_type_t;

typedef struct layout_box {
    box_type_t type;
    dom_node_t *node;

    int32_t x, y;
    int32_t w, h;

    int32_t margin_top, margin_bottom, margin_left, margin_right;
    int32_t padding_top, padding_bottom, padding_left, padding_right;
    
    int32_t content_x, content_y;
    int32_t content_w, content_h;

    gfx_surface_t *image;

    struct layout_box *first_child;
    struct layout_box *next_sibling;
} layout_box_t;

#define MAX_LAYOUT_BOXES 1024
static layout_box_t g_box_pool[MAX_LAYOUT_BOXES];
static int g_box_pool_idx = 0;

static layout_box_t *br_alloc_box(box_type_t type, dom_node_t *node) {
    if (g_box_pool_idx >= MAX_LAYOUT_BOXES) return NULL;
    layout_box_t *b = &g_box_pool[g_box_pool_idx++];
    b->type = type;
    b->node = node;
    b->x = 0; b->y = 0; b->w = 0; b->h = 0;
    b->margin_top = 0; b->margin_bottom = 0; b->margin_left = 0; b->margin_right = 0;
    b->padding_top = 0; b->padding_bottom = 0; b->padding_left = 0; b->padding_right = 0;
    b->content_x = 0; b->content_y = 0; b->content_w = 0; b->content_h = 0;
    b->image = NULL;
    b->first_child = NULL;
    b->next_sibling = NULL;
    return b;
}

static void br_reset_layout(void) {
    g_box_pool_idx = 0;
}

/* ---- Inline Layout Context -----------------------------------------------
 * Inline elementler (TEXT, B, I, A, CODE, MARK, SMALL, S, SPAN) bu context'i
 * paylaşır. Block elementler (P, H1-H6, LI, DIV, TABLE...) yeni bir context
 * başlatır.
 * --------------------------------------------------------------------------- */
typedef struct {
    int32_t  cx;       /* mevcut X imleci (block-start'tan itibaren) */
    int32_t  y;        /* mevcut içerik-Y (scroll eklenmemiş) */
    int32_t  x0;       /* satır başlangıç X */
    int32_t  max_x;    /* sağ sınır */
    int32_t  scroll_y; /* ekran kaydırması (çizim için) */
    int32_t  surf_h;   /* surface yüksekliği */
    bool     pending_newline; /* satır sonu bekleniyor mu */
} layout_ctx_t;

/* Kelime bazlı satır sayısı (UTF-8 aware). x_start'tan başlayarak hesaplar. */
static int32_t __attribute__((unused)) br_text_line_count(const char *text, int32_t x_start,
                                   int32_t max_x) {
    if (!text || !*text) return 0;
    /* Önce ASCII'ye dönüştür, sonra say */
    char buf[512];
    br_utf8_to_ascii(text, buf, sizeof(buf));
    int32_t cx = x_start;
    int32_t lines = 1;
    const char *p = buf;
    while (*p) {
        const char *we = p;
        while (*we && *we != ' ') we++;
        int32_t ww = (int32_t)((we - p) * BR_CHAR_W);
        if (cx > x_start && cx + ww > max_x) { cx = x_start; lines++; }
        cx += ww;
        p = we;
        if (*p == ' ') {
            if (cx + BR_CHAR_W > max_x) { cx = x_start; lines++; }
            else cx += BR_CHAR_W;
            p++;
        }
    }
    return lines;
}

/* Bir kelimeyi (veya boşlukla ayrılmış token'ı) layout context içine çizer.
 * UTF-8 farkındalıklı, underline ve background desteği var. */
static void br_lctx_emit_word(gfx_surface_t *surf, layout_ctx_t *ctx,
                               const char *word, int32_t wlen,
                               uint32_t fg, uint32_t bg,
                               bool underline, bool strikethrough) {
    /* Ekranın altında mı? */
    if (surf && ctx->y - ctx->scroll_y >= ctx->surf_h) return;

    int32_t draw_x = ctx->cx;
    int32_t draw_y = ctx->y;
    int32_t vy = draw_y - ctx->scroll_y;

    if (surf && (vy + BR_CHAR_H <= BR_TOOLBAR_H)) return; /* toolbar altında gizli */
    if (surf && (vy >= ctx->surf_h)) return;

    const char *p = word;
    int chars_left = (int)wlen;
    int32_t lx = draw_x;

    while (chars_left > 0) {
        int consumed = 1;
        char ch = br_utf8_decode(p, &consumed);
        p += consumed;
        chars_left -= consumed;

        vy = ctx->y - ctx->scroll_y;
        if (surf && (vy >= ctx->surf_h || vy + BR_CHAR_H <= BR_TOOLBAR_H)) {
            lx += BR_CHAR_W;
            continue;
        }
        if (lx + BR_CHAR_W > ctx->max_x) break; /* sağ sınır aşıldı */

        if (surf) gfx_draw_char(surf, lx, vy, ch, fg, bg);

        if (surf && underline) {
            int32_t uly = vy + BR_CHAR_H - 2;
            for (int32_t ux = lx; ux < lx + BR_CHAR_W && ux < ctx->max_x; ux++)
                gfx_put_pixel(surf, ux, uly, fg);
        }
        if (surf && strikethrough) {
            int32_t sly = vy + BR_CHAR_H / 2;
            for (int32_t ux = lx; ux < lx + BR_CHAR_W && ux < ctx->max_x; ux++)
                gfx_put_pixel(surf, ux, sly, fg);
        }
        lx += BR_CHAR_W;
    }
    ctx->cx = lx;
}

/* Metin stringini kelime kelime layout context'e yaz (UTF-8 aware, word-wrap). */
static void br_lctx_emit_text(gfx_surface_t *surf, layout_ctx_t *ctx,
                               const char *text,
                               uint32_t fg, uint32_t bg,
                               bool underline, bool strikethrough) {
    const char *p = text;
    while (*p) {
        /* Boşluk karakteri */
        if (*p == ' ' || *p == '\t' || *p == '\n') {
            if (ctx->cx > ctx->x0) {
                /* Boşluk sığıyor mu? */
                if (ctx->cx + BR_CHAR_W > ctx->max_x) {
                    /* Satır sonu */
                    ctx->cx = ctx->x0;
                    ctx->y += BR_LINE_H;
                } else {
                    int32_t vy = ctx->y - ctx->scroll_y;
                    if (surf && vy + BR_CHAR_H > BR_TOOLBAR_H && vy < ctx->surf_h)
                        gfx_draw_char(surf, ctx->cx, vy, ' ', fg, bg);
                    ctx->cx += BR_CHAR_W;
                }
            }
            p++;
            continue;
        }

        /* Kelimenin sonunu bul (byte olarak) */
        const char *word_start = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n') {
            int consumed = 1;
            br_utf8_decode(p, &consumed);
            p += consumed;
        }
        int32_t word_bytes = (int32_t)(p - word_start);

        /* Kelime genişliğini karakter sayısından hesapla */
        int32_t char_count = 0;
        const char *tmp = word_start;
        while (tmp < p) {
            int consumed = 1;
            br_utf8_decode(tmp, &consumed);
            tmp += consumed;
            char_count++;
        }
        int32_t word_w = char_count * BR_CHAR_W;

        /* Kelime satıra sığmıyor ve başta değiliz: yeni satır */
        if (ctx->cx > ctx->x0 && ctx->cx + word_w > ctx->max_x) {
            ctx->cx = ctx->x0;
            ctx->y += BR_LINE_H;
        }

        /* Kelimeyi çiz */
        br_lctx_emit_word(surf, ctx, word_start, word_bytes,
                          fg, bg, underline, strikethrough);
    }
}

/* Context'te bekleyen satırı kapat (block elementten önce çağrılır) */
static void __attribute__((unused)) br_lctx_newline(layout_ctx_t *ctx) {
    if (ctx->cx > ctx->x0) {
        ctx->y += BR_LINE_H;
        ctx->cx = ctx->x0;
    }
}

/* Satır içi (inline) öğeleri dolaşarak metinleri çizer ve linkleri toplar */
static void br_render_inline(gfx_surface_t *surf, dom_node_t *node, layout_ctx_t *ctx,
                             link_rect_t *links, int32_t *link_count, int32_t max_links,
                             const char *base_url, bool in_bold) {
    if (!node) return;
    
    bool is_link = (node->tag == DOM_TAG_A);
    bool is_bold = in_bold || (node->tag == DOM_TAG_B);
    
    if (node->tag == DOM_TAG_TEXT) {
        uint32_t fg = 0x000000;
        if (is_link) fg = 0x0000FF;
        br_lctx_emit_text(surf, ctx, node->text, fg, 0xFFFFFF, is_link, false);
    }
    
    if (is_link && surf && links && link_count && *link_count < max_links && node->attr[0] != '\0') {
        br_resolve_href(base_url, node->attr, links[*link_count].href, 256);
        links[*link_count].x = ctx->cx;
        links[*link_count].y = ctx->y;
        links[*link_count].w = 100;
        (*link_count)++;
    }
    
    for (dom_node_t *c = node->first_child; c; c = c->next_sibling) {
        br_render_inline(surf, c, ctx, links, link_count, max_links, base_url, is_bold);
    }
}

/* ---- LAYOUT TREE BUILDING ------------------------------------------------ */
static void br_layout_process_node(layout_box_t *parent, dom_node_t *node, int32_t *y_cursor, int32_t max_x, int32_t list_depth) {
    if (!node) return;
    
    /* Skip non-visible tags or scripts */
    if (node->tag == DOM_TAG_UNKNOWN || node->tag == DOM_TAG_HEAD) {
        return;
    }

    /* Basic block elements */
    bool is_block = false;
    bool is_image = false;
    
    switch (node->tag) {
        case DOM_TAG_H1: case DOM_TAG_H2: case DOM_TAG_H3:
        case DOM_TAG_H4: case DOM_TAG_H5: case DOM_TAG_H6:
        case DOM_TAG_P: case DOM_TAG_DIV: case DOM_TAG_BLOCKQUOTE: case DOM_TAG_PRE:
        case DOM_TAG_UL: case DOM_TAG_OL: case DOM_TAG_LI:
        case DOM_TAG_TABLE: case DOM_TAG_TR: case DOM_TAG_HR:
        case DOM_TAG_HEADER: case DOM_TAG_MAIN: case DOM_TAG_SECTION:
        case DOM_TAG_ARTICLE: case DOM_TAG_ASIDE: case DOM_TAG_FOOTER:
        case DOM_TAG_THEAD: case DOM_TAG_TBODY: case DOM_TAG_TFOOT:
        case DOM_TAG_CAPTION: case DOM_TAG_TH: case DOM_TAG_TD:
        case DOM_TAG_DL: case DOM_TAG_DT: case DOM_TAG_DD: case DOM_TAG_NAV:
            is_block = true;
            break;
        case DOM_TAG_IMG:
            is_image = true;
            break;
        default:
            is_block = false;
            break;
    }

    if (node->tag == DOM_TAG_HTML || node->tag == DOM_TAG_BODY) {
        is_block = true; /* Treat document roots as block */
    }

    layout_box_t *box = NULL;

    if (is_image) {
        box = br_alloc_box(BOX_IMAGE, node);
        if (box) {
            box->w = node->attr_w > 0 ? node->attr_w : 32;
            box->h = node->attr_h > 0 ? node->attr_h : 32;
            box->margin_bottom = 4;
            
            /* Try to load the PNG if it's local */
            if (node->attr[0] == '/' || (node->attr[0] && node->attr[0] != 'h' && node->attr[0] != 'H')) {
                char final_path[256];
                const char *prefix = "/disk/fat0";
                int k = 0;
                while (prefix[k]) { final_path[k] = prefix[k]; k++; }
                
                int src_idx = 0;
                if (node->attr[0] != '/') {
                    final_path[k++] = '/';
                }
                
                while (node->attr[src_idx] && k < 255) {
                    char c = node->attr[src_idx++];
                    if (c >= 'a' && c <= 'z') c -= 32;
                    final_path[k++] = c;
                }
                final_path[k] = '\0';
                
                box->image = png_load(final_path);
                if (box->image) {
                    box->w = node->attr_w > 0 ? node->attr_w : (int32_t)box->image->width;
                    box->h = node->attr_h > 0 ? node->attr_h : (int32_t)box->image->height;
                }
            }
        }
    } else if (is_block) {
        box = br_alloc_box(BOX_BLOCK, node);
        if (box) {
            box->margin_bottom = 4;
            if (node->tag == DOM_TAG_LI) {
                box->margin_left = list_depth * 16;
            } else if (node->tag == DOM_TAG_BLOCKQUOTE) {
                box->margin_left = 16;
                box->padding_left = 8;
            }
        }
    } else {
        /* Inline element - we don't calculate height here yet, we defer to Paint */
        box = br_alloc_box(BOX_INLINE, node);
    }

    if (box) {
        if (!parent->first_child) {
            parent->first_child = box;
        } else {
            layout_box_t *sib = parent->first_child;
            while (sib->next_sibling) sib = sib->next_sibling;
            sib->next_sibling = box;
        }

        /* Calculate position for block/image */
        if (is_block || is_image) {
            *y_cursor += box->margin_top;
            box->x = parent->content_x + box->margin_left;
            box->y = *y_cursor;
            box->content_x = box->x + box->padding_left;
            box->content_y = box->y + box->padding_top;
            
            box->w = (max_x - parent->content_x) - box->margin_left - box->margin_right;
            box->content_w = box->w - box->padding_left - box->padding_right;
            
            int32_t child_y = box->content_y;
            
            /* Recurse children */
            bool has_inline = false;
            for (dom_node_t *c = node->first_child; c; c = c->next_sibling) {
                if (c->tag == DOM_TAG_TEXT || c->tag == DOM_TAG_B || c->tag == DOM_TAG_I || c->tag == DOM_TAG_A) {
                    has_inline = true;
                }
                br_layout_process_node(box, c, &child_y, max_x, node->tag == DOM_TAG_UL || node->tag == DOM_TAG_OL ? list_depth + 1 : list_depth);
            }
            
            if (has_inline) {
                layout_ctx_t ctx;
                ctx.x0 = box->content_x;
                ctx.cx = box->content_x;
                ctx.y = box->content_y;
                ctx.max_x = box->content_x + box->content_w;
                ctx.scroll_y = 0;
                ctx.surf_h = 0x7FFFFFFF; /* don't clip */
                ctx.pending_newline = false;
                for (dom_node_t *c = node->first_child; c; c = c->next_sibling) {
                    if (c->tag != DOM_TAG_IMG && c->tag != DOM_TAG_P && c->tag != DOM_TAG_DIV) {
                        br_render_inline(NULL, c, &ctx, NULL, NULL, 0, NULL, false);
                    }
                }
                int32_t inline_y = ctx.y + (ctx.cx > ctx.x0 ? BR_LINE_H : 0);
                if (inline_y > child_y) child_y = inline_y;
            }
            
            box->content_h = child_y - box->content_y;
            if (is_image) box->content_h = box->h; /* Images have fixed content h */
            
            box->h = box->content_h + box->padding_top + box->padding_bottom;
            *y_cursor = box->y + box->h + box->margin_bottom;
        } else {
            /* Inline Box */
            for (dom_node_t *c = node->first_child; c; c = c->next_sibling) {
                br_layout_process_node(box, c, y_cursor, max_x, list_depth);
            }
        }
    }
}

static layout_box_t *br_build_layout_tree(dom_node_t *root, int32_t start_y, int32_t max_x) {
    br_reset_layout();
    
    layout_box_t *root_box = br_alloc_box(BOX_BLOCK, root);
    if (!root_box) return NULL;
    
    root_box->x = 0;
    root_box->y = start_y;
    root_box->w = max_x;
    root_box->content_x = 0;
    root_box->content_y = start_y;
    root_box->content_w = max_x;
    
    int32_t y_cursor = start_y;
    for (dom_node_t *c = root->first_child; c; c = c->next_sibling) {
        br_layout_process_node(root_box, c, &y_cursor, max_x, 1);
    }
    
    root_box->content_h = y_cursor - start_y;
    root_box->h = root_box->content_h;
    
    return root_box;
}

/* ---- PAINT LAYOUT TREE --------------------------------------------------- */
static void br_paint_layout_tree(gfx_surface_t *surf, layout_box_t *box, int32_t scroll_y, int32_t surf_h,
                                 link_rect_t *links, int *link_count, int max_links,
                                 const char *base_url) {
    if (!box) return;

    int32_t vy = box->y - scroll_y;
    int32_t cvy = box->content_y - scroll_y;

    /* Y ekseninde gorunurluk testi (Culling) */
    if (vy + box->h < BR_TOOLBAR_H || vy > surf_h) {
        /* Gorunmez ama icinde cok buyuk bir eleman olabilir, yine de cocuklari gez */
    }

    if (box->type == BOX_IMAGE) {
        if (box->image && vy + box->h > BR_TOOLBAR_H && vy < surf_h) {
            gfx_stretch_blit(surf, box->content_x, cvy, box->content_w, box->content_h,
                             box->image, 0, 0, box->image->width, box->image->height);
        } else if (!box->image && vy + box->h > BR_TOOLBAR_H && vy < surf_h) {
            /* Placeholder */
            gfx_fill_rect(surf, box->content_x, cvy, box->content_w, box->content_h, 0xDDDDDD);
            gfx_draw_rect(surf, box->content_x, cvy, box->content_w, box->content_h, 0xAAAAAA);
            gfx_draw_text(surf, box->content_x + 4, cvy + 4, "[IMG]", 0x555555, 0xDDDDDD);
            if (box->node && box->node->attr2[0]) {
                gfx_draw_text(surf, box->content_x + 4, cvy + 20, box->node->attr2, 0x333333, 0xDDDDDD);
            }
        }
    } else if (box->type == BOX_BLOCK) {
        /* Ozel arka planlar veya kenarliklar (Orn: Tablo th, blockquote) */
        if (box->node && box->node->tag == DOM_TAG_BLOCKQUOTE) {
            if (vy + box->h > BR_TOOLBAR_H && vy < surf_h) {
                int32_t dy0 = vy < BR_TOOLBAR_H ? BR_TOOLBAR_H : vy;
                int32_t dy1 = (vy + box->h) > surf_h ? surf_h : (vy + box->h);
                gfx_fill_rect(surf, box->x, dy0, 4, dy1 - dy0, BR_QUOTE_BAR);
            }
        }
        
        /* Liste noktalari */
        if (box->node && box->node->tag == DOM_TAG_LI) {
            if (cvy + BR_CHAR_H > BR_TOOLBAR_H && cvy < surf_h) {
                if (box->node->list_index > 0) {
                    char nl[8]; uint16_t n = box->node->list_index; int pos = 0;
                    if (n >= 10) nl[pos++] = (char)('0' + n / 10);
                    nl[pos++] = (char)('0' + n % 10); nl[pos++] = '.'; nl[pos++] = ' '; nl[pos] = '\0';
                    gfx_draw_text(surf, box->content_x - 24, cvy, nl, BR_BULLET, BR_BG);
                } else {
                    gfx_fill_circle(surf, box->content_x - 8, cvy + BR_CHAR_H / 2, 2, BR_BULLET);
                }
            }
        }

        /* Eger bu bir bloksa ve icinde dogrudan metin/inline varsa eski akisi kullan (Hibrit Gecis) */
        bool has_inline = false;
        for (dom_node_t *c = box->node->first_child; c; c = c->next_sibling) {
            if (c->tag == DOM_TAG_TEXT || c->tag == DOM_TAG_B || c->tag == DOM_TAG_I || c->tag == DOM_TAG_A) {
                has_inline = true; break;
            }
        }
        
        if (has_inline) {
            /* Inline akisi Paint asamasinda yap */
            layout_ctx_t ctx;
            ctx.x0 = box->content_x;
            ctx.cx = box->content_x;
            ctx.y = box->content_y;
            ctx.max_x = box->content_x + box->content_w;
            ctx.scroll_y = scroll_y;
            ctx.surf_h = surf_h;
            ctx.pending_newline = false;
            
            for (dom_node_t *c = box->node->first_child; c; c = c->next_sibling) {
                /* Sadece text/inline cocuklari isliyoruz (Block ve img'ler kendi kutusundan islenir) */
                if (c->tag != DOM_TAG_IMG && c->tag != DOM_TAG_P && c->tag != DOM_TAG_DIV) {
                    br_render_inline(surf, c, &ctx, links, link_count, max_links, base_url, false);
                }
            }
        }
    }

    /* Cocuk kutulari (Child Boxes) gez */
    for (layout_box_t *c = box->first_child; c; c = c->next_sibling) {
        br_paint_layout_tree(surf, c, scroll_y, surf_h, links, link_count, max_links, base_url);
    }
}

/* ---- Toolbar cizimi ------------------------------------------------------ */
static void br_draw_toolbar(gfx_surface_t *surf, browser_state *bs) {
    int32_t w = bs->surf_w;

    /* Arkaplan */
    gfx_fill_rect(surf, 0, 0, w, BR_TOOLBAR_H, BR_ADDRBAR_BG);
    /* Alt sinir cizgi */
    gfx_draw_line(surf, 0, BR_TOOLBAR_H - 1, w, BR_TOOLBAR_H - 1, 0xAAAAAA);

    /* Geri butonu: 4,4 -> 52,20 */
    gfx_fill_rect(surf, 4, 4, 48, 18, BR_BTN_BG);
    gfx_draw_rect(surf, 4, 4, 48, 18, 0xAAAAAA);
    gfx_draw_text(surf, 8, 5, "< Geri", BR_BTN_TXT, BR_BTN_BG);

    /* Yenile butonu: 56,4 -> 104,20 */
    gfx_fill_rect(surf, 56, 4, 48, 18, BR_BTN_BG);
    gfx_draw_rect(surf, 56, 4, 48, 18, 0xAAAAAA);
    gfx_draw_text(surf, 60, 5, "Yenile", BR_BTN_TXT, BR_BTN_BG);

    /* Adres cubugu: 108,4 -> (w-4),20 */
    int32_t addr_x = 108;
    int32_t addr_w = w - addr_x - 4;
    gfx_fill_rect(surf, addr_x, 4, addr_w, 18, 0xFFFFFF);
    gfx_draw_rect(surf, addr_x, 4, addr_w, 18, 0x888888);

    /* Adres metni */
    const char *show = (bs->state == BR_STATE_ADDR_INPUT)
                       ? bs->addr_input : bs->url;
    gfx_draw_text(surf, addr_x + 4, 5, show, BR_ADDRBAR_TXT, 0xFFFFFF);

    /* Yazma moddayken kursor */
    if (bs->state == BR_STATE_ADDR_INPUT) {
        int32_t cx = addr_x + 4 + (int32_t)(bs->addr_len * BR_CHAR_W);
        gfx_draw_line(surf, cx, 5, cx, 5 + BR_CHAR_H, BR_ADDRBAR_CUR);
    }
}

/* ---- Status bar cizimi --------------------------------------------------- */
static void br_draw_statusbar(gfx_surface_t *surf, browser_state *bs) {
    int32_t w = bs->surf_w;
    int32_t h = bs->surf_h;
    int32_t sy = h - BR_STATUS_H;
    gfx_fill_rect(surf, 0, sy, w, BR_STATUS_H, BR_STATUS_BG);
    gfx_draw_line(surf, 0, sy, w, sy, 0xAAAAAA);
    gfx_draw_text(surf, 4, sy + 1, bs->status, BR_STATUS_TXT, BR_STATUS_BG);
}

/* ---- Tam sayfa yeniden ciz ----------------------------------------------- */
static void br_redraw(window_t *win) {
    browser_state *bs = (browser_state *)win->app_data;
    gfx_surface_t *surf = win->content;
    if (!bs || !surf) return;

    bs->surf_w = (int32_t)surf->width;
    bs->surf_h = (int32_t)surf->height;

    /* Arkaplan */
    surface_clear(surf, BR_BG);

    /* Toolbar */
    br_draw_toolbar(surf, bs);

    /* Icerik alani: toolbar alti, statusbar ustu */
    int32_t content_area_h = bs->surf_h - BR_TOOLBAR_H - BR_STATUS_H;
    (void)content_area_h;

    int32_t max_x = bs->surf_w - BR_MARGIN_X;

    /* Link tablosunu temizle */
    bs->link_count = 0;

    if (bs->state == BR_STATE_LOADING) {
        gfx_draw_text(surf, BR_MARGIN_X, BR_TOOLBAR_H + 20,
                      "Yukleniyor...", BR_TEXT, BR_BG);
    } else if (bs->dom_root) {
        /* DOM'u ciz */
        int32_t y = BR_TOOLBAR_H + 4;
        
        /* 1. Layout Tree Olustur (Box Model) */
        layout_box_t *root_box = br_build_layout_tree(bs->dom_root, y, max_x);
        
        /* 2. Boyama (Paint) */
        if (root_box) {
            br_paint_layout_tree(surf, root_box, bs->scroll_y, bs->surf_h - BR_STATUS_H,
                                 bs->links, &bs->link_count, BR_MAX_LINKS, bs->url);
            bs->content_h = root_box->content_h;
        } else {
            bs->content_h = y;
        }
    } else if (bs->url[0]) {
        gfx_draw_text(surf, BR_MARGIN_X, BR_TOOLBAR_H + 20,
                      "Sayfa yuklenemedi.", BR_TEXT, BR_BG);
    } else {
        /* Hos geldiniz ekrani */
        gfx_draw_text(surf, BR_MARGIN_X, BR_TOOLBAR_H + 30,
                      "Aurora Browser - ZeruX OS", BR_H1, BR_BG);
        gfx_draw_text(surf, BR_MARGIN_X, BR_TOOLBAR_H + 55,
                      "Adres cubuguna URL girerek baslayin.", BR_TEXT, BR_BG);
        gfx_draw_text(surf, BR_MARGIN_X, BR_TOOLBAR_H + 80,
                      "Ornek: 192.168.1.1:8080", BR_LINK, BR_BG);
    }

    /* Scrollbar cizimi */
    if (bs->content_h > content_area_h) {
        int32_t sb_x = bs->surf_w - BR_SCROLLBAR_W;
        /* Scroll track */
        gfx_fill_rect(surf, sb_x, BR_TOOLBAR_H, BR_SCROLLBAR_W, content_area_h, 0xEEEEEE);
        gfx_draw_line(surf, sb_x, BR_TOOLBAR_H, sb_x, BR_TOOLBAR_H + content_area_h - 1, 0xCCCCCC);

        /* Scroll thumb */
        int32_t thumb_h = (content_area_h * content_area_h) / bs->content_h;
        if (thumb_h < 10) thumb_h = 10; /* min thumb height */
        int32_t thumb_y = BR_TOOLBAR_H + (bs->scroll_y * content_area_h) / bs->content_h;
        /* bounds check for thumb_y */
        if (thumb_y + thumb_h > BR_TOOLBAR_H + content_area_h) {
            thumb_y = BR_TOOLBAR_H + content_area_h - thumb_h;
        }

        gfx_fill_rect(surf, sb_x + 2, thumb_y, BR_SCROLLBAR_W - 4, thumb_h, 0x999999);
    }

    /* Status bar */
    br_draw_statusbar(surf, bs);

    wm_invalidate(win->id);
}

/* ---- HTTP Fetch (THREAD ENTRY — kthread_create ile cagrilir) ------------- */
static fetch_args_t g_fetch_args; /* tek browser ayni anda fetch yapar */

static void br_fetch_thread(void) {
    fetch_args_t *fa = &g_fetch_args;
    window_t *win = fa->win;

    browser_state *bs = (browser_state *)win->app_data;
    if (!bs) {
        /* bs NULL — thread'i kapat */
        extern task_t *task_get_current(void);
        task_t *me = task_get_current();
        if (me) { me->exit_code = 0; me->state = TASK_STATE_ZOMBIE; }
        for (;;) __asm__ volatile("hlt");
    }

    bs->fetch_running = true;

    aurora_http_response_t resp;
    bool ok = aurora_http_get(fa->url, &resp);

    if (!ok) {
        if (!bs->window_closed) {
            bs->state = BR_STATE_DONE;
            br_strcpy(bs->status, "Baglanti hatasi!", BR_STATUS_MAX);
            bs->dom_root = NULL;
            bs->redraw_needed = 1;
        }
        bs->fetch_running = false;
        if (bs->window_closed) kfree(bs);
        /* Hata durumunda da zombie ol */
        extern task_t *task_get_current(void);
        task_t *me = task_get_current();
        if (me) { me->exit_code = 1; me->state = TASK_STATE_ZOMBIE; }
        for (;;) __asm__ volatile("hlt");
    }

    serial_printf("[Aurora] HTTP %d, %u bayt alindi\n",
                  resp.status_code, resp.body_len);

    /* DOM'u olustur */
    dom_reset();
    bs->dom_root = aurora_html_parse(resp.body, resp.body_len);
    aurora_http_free(&resp);

    if (bs->dom_root) {
        char stat[BR_STATUS_MAX];
        br_strcpy(stat, "Hazir | ", BR_STATUS_MAX);
        br_strcat_bounded(stat, fa->url, BR_STATUS_MAX);
        if (!bs->window_closed) br_strcpy(bs->status, stat, BR_STATUS_MAX);
    } else {
        if (!bs->window_closed) br_strcpy(bs->status, "HTML parse hatasi!", BR_STATUS_MAX);
    }

    if (!bs->window_closed) {
        bs->state = BR_STATE_DONE;
        bs->redraw_needed = 1; /* on_update akan mainloopda br_redraw() cagirir */
    }
    
    bs->fetch_running = false;
    if (bs->window_closed) kfree(bs);

    /* Thread'i zombie olarak isaretle; init/reaper temizler.
     * kthread_create donuş adresini ayarlamiyor — return yaparsak
     * heap magic (0x48454150) adresine atlayip kernel panic olur. */
    {
        extern task_t *task_get_current(void);
        task_t *me = task_get_current();
        if (me) {
            me->exit_code = 0;
            me->state     = TASK_STATE_ZOMBIE;
        }
    }
    /* Buraya hic ulasilmaz — scheduler bir sonraki cevrimde bizi atar */
    for (;;) __asm__ volatile("hlt");
}

/* ---- Gecmise ekle -------------------------------------------------------- */
static void br_push_history(browser_state *bs, const char *url) {
    if (!url || !url[0]) return;
    /* Eger su anki url ile ayniysa ekleme */
    if (bs->history_count > 0 && br_strcmp(bs->history[bs->history_idx], url) == 0) return;

    /* Eger geri gitmisken yeni linke tiklarsak, ileriyi sil */
    bs->history_count = bs->history_idx + 1;

    if (bs->history_count < BR_HISTORY_MAX) {
        br_strcpy(bs->history[bs->history_count], url, BR_URL_MAX);
        bs->history_idx = bs->history_count;
        bs->history_count++;
    } else {
        /* Kaydir */
        for (int i = 1; i < BR_HISTORY_MAX; i++) {
            br_strcpy(bs->history[i - 1], bs->history[i], BR_URL_MAX);
        }
        br_strcpy(bs->history[BR_HISTORY_MAX - 1], url, BR_URL_MAX);
        bs->history_idx = BR_HISTORY_MAX - 1;
    }
}

/* Ana thread'den cagrilir — fetch'i ayri bir kernel thread'e devreeder */
static void br_fetch_and_render(window_t *win, bool from_history) {
    browser_state *bs = (browser_state *)win->app_data;
    if (!bs || !bs->url[0]) return;
    if (bs->state == BR_STATE_LOADING) return; /* zaten fetch var */

    if (!from_history) {
        br_push_history(bs, bs->url);
    }

    bs->state = BR_STATE_LOADING;
    bs->scroll_y = 0;
    bs->link_count = 0;
    bs->dom_root = NULL;
    br_strcpy(bs->status, "Baglaniyor...", BR_STATUS_MAX);
    br_redraw(win);

    serial_printf("[Aurora] Fetch thread baslatiliyor: %s\n", bs->url);

    /* Argumanlari kopyala (stack uzerinde degil, global struct) */
    g_fetch_args.win = win;
    br_strcpy(g_fetch_args.url, bs->url, BR_URL_MAX);

    /* Yeni kernel thread olustur */
    extern task_t *kthread_create(void (*entry_point)(void), const char *name);
    kthread_create(br_fetch_thread, "aurora-fetch");
}

/* ---- Per-frame update (called by wm_update every frame) ------------------ */
static void br_on_update(window_t *win) {
    browser_state *bs = (browser_state *)win->app_data;
    if (!bs) return;

    /* Pencere yeniden boyutlandirildiginda (or. tam ekran) yeniden cizim tetikle */
    if (win->content && (bs->surf_w != (int32_t)win->content->width || bs->surf_h != (int32_t)win->content->height)) {
        bs->redraw_needed = 1;
    }

    if (bs->redraw_needed) {
        bs->redraw_needed = 0;
        br_redraw(win);
    }
}

/* ---- Klavye handler ------------------------------------------------------- */
static void br_on_key(window_t *win, char c) {
    browser_state *bs = (browser_state *)win->app_data;
    if (!bs) return;

    if (bs->state == BR_STATE_ADDR_INPUT) {
        if (c == '\n' || c == '\r') {
            /* Enter: URL'yi onayla ve fetch */
            bs->addr_input[bs->addr_len] = '\0';
            br_strcpy(bs->url, bs->addr_input, BR_URL_MAX);
            bs->state = BR_STATE_IDLE;
            bs->addr_len = 0;
            bs->addr_input[0] = '\0';
            if (!bs->fetch_running) br_fetch_and_render(win, false);
        } else if (c == '\b' || c == 127) {
            if (bs->addr_len > 0) {
                bs->addr_len--;
                bs->addr_input[bs->addr_len] = '\0';
            }
            br_redraw(win);
        } else if (c >= 32 && c < 127 && bs->addr_len < BR_URL_MAX - 1) {
            bs->addr_input[bs->addr_len++] = c;
            bs->addr_input[bs->addr_len] = '\0';
            br_redraw(win);
        }
    } else {
        /* Escape sequence parsing for arrow keys */
        if (c == '\x1b') { bs->escape_state = 1; return; }
        if (bs->escape_state == 1) {
            if (c == '[') bs->escape_state = 2; else bs->escape_state = 0;
            return;
        }
        if (bs->escape_state == 2) {
            bs->escape_state = 0;
            if (c == 'A') {
                /* Up Arrow */
                bs->scroll_y -= 30;
                if (bs->scroll_y < 0) bs->scroll_y = 0;
                br_redraw(win);
            } else if (c == 'B') {
                /* Down Arrow */
                bs->scroll_y += 30;
                if (bs->content_h > 0 && bs->scroll_y > bs->content_h)
                    bs->scroll_y = bs->content_h;
                br_redraw(win);
            }
            return;
        }

        /* Normal mod: klavye kisayollari */
        if (c == 'r' || c == 'R') {
            /* R: yenile */
            if (bs->url[0] && !bs->fetch_running) br_fetch_and_render(win, false);
        } else if (c == 'j') {
            /* J: asagi kaydir */
            bs->scroll_y += 30;
            if (bs->content_h > 0 && bs->scroll_y > bs->content_h)
                bs->scroll_y = bs->content_h;
            br_redraw(win);
        } else if (c == 'k') {
            /* K: yukari kaydir */
            bs->scroll_y -= 30;
            if (bs->scroll_y < 0) bs->scroll_y = 0;
            br_redraw(win);
        } else if (c == ' ') {
            /* Bosluk / PageDown: bir sayfa asagi */
            bs->scroll_y += bs->surf_h - BR_TOOLBAR_H - BR_STATUS_H - 30;
            if (bs->content_h > 0 && bs->scroll_y > bs->content_h)
                bs->scroll_y = bs->content_h;
            br_redraw(win);
        } else if (c == 'b') {
            /* B / PageUp: bir sayfa yukari */
            bs->scroll_y -= bs->surf_h - BR_TOOLBAR_H - BR_STATUS_H - 30;
            if (bs->scroll_y < 0) bs->scroll_y = 0;
            br_redraw(win);
        } else if (c == 'l' || c == 'L') {
            /* L: adres cubuguna odaklan */
            bs->state = BR_STATE_ADDR_INPUT;
            bs->addr_len = 0;
            bs->addr_input[0] = '\0';
            br_redraw(win);
        }
    }
}

/* ---- Tiklama handler ------------------------------------------------------ */
static void br_on_click(window_t *win, int32_t lx, int32_t ly) {
    browser_state *bs = (browser_state *)win->app_data;
    if (!bs) return;

    /* Toolbar tiklamalari */
    if (ly < BR_TOOLBAR_H) {
        if (lx >= 4 && lx <= 52) {
            /* Geri butonu: gecmiste geri git */
            if (bs->fetch_running) return; /* Yüklenirken islemi yoksay */
            if (bs->history_idx > 0) {
                bs->history_idx--;
                br_strcpy(bs->url, bs->history[bs->history_idx], BR_URL_MAX);
                br_fetch_and_render(win, true);
            }
        } else if (lx >= 56 && lx <= 104) {
            /* Yenile butonu */
            if (bs->url[0] && !bs->fetch_running) br_fetch_and_render(win, false);
        } else if (lx >= 108) {
            /* Adres cubugu */
            bs->state = BR_STATE_ADDR_INPUT;
            bs->addr_len = (uint32_t)br_strlen(bs->url);
            br_strcpy(bs->addr_input, bs->url, BR_URL_MAX);
            br_redraw(win);
        }
        return;
    }

    /* Status bar alani - tiklanamaz */
    if (ly >= bs->surf_h - BR_STATUS_H) return;

    /* Adres girisi modunda tek klik adresi onayla */
    if (bs->state == BR_STATE_ADDR_INPUT) {
        bs->state = BR_STATE_IDLE;
        br_redraw(win);
        return;
    }

    /* Icerik tiklamasi: link tablosunda ara veya scrollbar */
    if (lx >= bs->surf_w - BR_SCROLLBAR_W) {
        /* Scrollbar'a tiklandi */
        int32_t content_area_h = bs->surf_h - BR_TOOLBAR_H - BR_STATUS_H;
        if (bs->content_h > content_area_h) {
            /* ly = toolbar alti yerel y */
            int32_t local_y = ly - BR_TOOLBAR_H;
            if (local_y < 0) local_y = 0;
            if (local_y > content_area_h) local_y = content_area_h;

            bs->scroll_y = (local_y * bs->content_h) / content_area_h;
            /* Ortalamak icin biraz yukari kaydir */
            bs->scroll_y -= content_area_h / 2;
            
            if (bs->scroll_y < 0) bs->scroll_y = 0;
            if (bs->scroll_y > bs->content_h) bs->scroll_y = bs->content_h;
            br_redraw(win);
        }
        return;
    }

    int32_t content_y = ly + bs->scroll_y;  /* content koordinati */
    if (bs->fetch_running) return; /* Yüklenirken link tiklamayi yoksay */

    for (int32_t i = 0; i < bs->link_count; i++) {
        link_rect_t *lr = &bs->links[i];
        if (content_y >= lr->y && content_y <= lr->y + BR_LINE_H &&
            lx >= lr->x && lx <= lr->x + lr->w) {
            /* Link tiklanadi */
            serial_printf("[Aurora] Link: %s\n", lr->href);
            br_strcpy(bs->url, lr->href, BR_URL_MAX);
            br_fetch_and_render(win, false);
            return;
        }
    }
}

/* ---- Destroy handler ----------------------------------------------------- */
static void br_on_destroy(window_t *win) {
    browser_state *bs = (browser_state *)win->app_data;
    if (bs) {
        if (bs->fetch_running) {
            /* Thread calisiyor, sadece bayragi kaldir, thread kendini yok edecek */
            bs->window_closed = true;
        } else {
            /* Thread yok, guvenle temizle */
            kfree(bs);
        }
        win->app_data = NULL;
    }
}

/* ---- Public API ---------------------------------------------------------- */
int32_t browser_app_create(int32_t x, int32_t y, int32_t w, int32_t h,
                            const char *url) {
    int32_t id = wm_create_window("Aurora", x, y, w, h);
    if (id < 0) return -1;

    browser_state *bs = (browser_state *)kzalloc(sizeof(browser_state));
    if (!bs) { wm_destroy_window(id); return -1; }

    bs->win_id = id;
    bs->state  = BR_STATE_IDLE;
    bs->dom_root = NULL;
    bs->scroll_y = 0;
    bs->content_h = 0;
    bs->link_count = 0;
    bs->hover_link = -1;
    bs->history_count = 0;
    bs->history_idx = -1;
    bs->redraw_needed = 0;
    br_strcpy(bs->status, "Hazir", BR_STATUS_MAX);

    if (url && url[0]) {
        br_strcpy(bs->url, url, BR_URL_MAX);
        br_push_history(bs, url);
    }

    window_t *win = wm_get_window(id);
    if (!win) { kfree(bs); wm_destroy_window(id); return -1; }

    wm_set_app_data(id, bs);
    wm_set_key_handler(id, br_on_key);
    wm_set_click_handler(id, br_on_click);
    wm_set_destroy_handler(id, br_on_destroy);
    wm_set_resize_handler(id, br_redraw);
    wm_set_update_handler(id, br_on_update);

    bs->surf_w = (int32_t)win->content->width;
    bs->surf_h = (int32_t)win->content->height;

    /* Ilk cizim */
    br_redraw(win);

    /* URL verilmisse hemen fetch baslat */
    if (url && url[0])
        br_fetch_and_render(win, false);

    return id;
}
