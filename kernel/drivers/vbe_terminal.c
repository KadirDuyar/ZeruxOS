/* =============================================================================
 * ZeruX OS — VBE Scrollable Text Terminal Implementation
 * File: kernel/drivers/vbe_terminal.c
 * =============================================================================
 *
 * Karakter hücresi modeli: her hücre 8×16 piksel.
 * Çözünürlük 1024×768 için terminal 127 sütun × 43 satır.
 *
 * Kaydırma (scroll): Son satır dolunca tüm framebuffer satırları yukarı
 * kopyalanır (pixel blit), son satır arka plan rengiyle silinir.
 * =============================================================================
 */

#include "vbe_terminal.h"
#include "vbe.h"
#include "font8x16.h"

/* =============================================================================
 * Dış Bağımlılıklar (vbe.c'den)
 * =============================================================================
 */
extern uint32_t *g_vbe_buffer;  /* VRAM sanal adresi             */
extern uint16_t           g_vbe_width;   /* Ekran genişliği piksel        */
extern uint16_t           g_vbe_height;  /* Ekran yüksekliği piksel       */
extern int                g_vbe_enabled; /* VBE aktif bayrak               */

/* =============================================================================
 * Terminal Durum Değişkenleri
 * =============================================================================
 */
int      g_vbe_term_enabled = 0;          /* API erişim kapısı            */

static uint32_t g_col = 0;               /* Geçerli sütun (0-based)       */
static uint32_t g_row = 0;               /* Geçerli satır (0-based)       */
static uint32_t g_fg  = VBE_TERM_FG_DEFAULT;
static uint32_t g_bg  = VBE_TERM_BG_DEFAULT;

/* =============================================================================
 * Yardımcı: Piksel bazında tek karakter çiz (arka planla birlikte)
 * =============================================================================
 */
static void draw_cell(uint32_t col, uint32_t row, char c, uint32_t fg, uint32_t bg) {
    if (!g_vbe_buffer || !g_vbe_width) return;

    uint32_t px = VBE_TERM_X + col * VBE_TERM_CHAR_W;
    uint32_t py = VBE_TERM_Y + row * VBE_TERM_CHAR_H;

    /* font8x16_data[c] = 16 bayt, her bayt bir piksel satırı */
    const uint8_t *glyph = font8x16[(uint8_t)c];
    
    /* Batch-blit Optimization: Prepare a row of pixels in memory first */
    uint32_t cell_row_buf[8]; /* VBE_TERM_CHAR_W is 8 */

    for (uint32_t gy = 0; gy < VBE_TERM_CHAR_H; gy++) {
        uint8_t bits = glyph[gy];
        for (uint32_t gx = 0; gx < VBE_TERM_CHAR_W; gx++) {
            cell_row_buf[gx] = (bits & (0x80u >> gx)) ? fg : bg;
        }
        
        uint32_t idx = (py + gy) * g_vbe_width + px;
        /* vbe_movsl uses 'rep movsd', moving 8 DWORDs efficiently via Write-Combining */
        vbe_movsl(&g_vbe_shadow[idx], cell_row_buf, VBE_TERM_CHAR_W);
        vbe_movsl(&g_vbe_buffer[idx], cell_row_buf, VBE_TERM_CHAR_W);
    }
}

/* =============================================================================
 * Kaydırma: tüm satırları bir yukarı taşı, son satırı temizle
 * =============================================================================
 */
static void term_scroll(void) {
    if (!g_vbe_buffer || !g_vbe_width) return;

    uint32_t w   = g_vbe_width;
    uint32_t src = (VBE_TERM_Y + VBE_TERM_CHAR_H) * w;
    uint32_t dst = VBE_TERM_Y * w;
    /* (ROWS-1) satır kopyala */
    uint32_t cnt = (VBE_TERM_ROWS - 1u) * VBE_TERM_CHAR_H * w;

    /* Yukarı blit (Shadow to Shadow) */
    vbe_movsl(&g_vbe_shadow[dst], &g_vbe_shadow[src], cnt);
    
    /* VRAM'e yaz (Shadow to VRAM) - Hızlı kopyalama */
    vbe_movsl(&g_vbe_buffer[dst], &g_vbe_shadow[dst], cnt);

    /* Son satırı arka plan rengiyle doldur */
    uint32_t last = (VBE_TERM_Y + (VBE_TERM_ROWS - 1u) * VBE_TERM_CHAR_H) * w;
    uint32_t clear_cnt = VBE_TERM_CHAR_H * w;
    vbe_stosl(&g_vbe_shadow[last], VBE_TERM_BG_DEFAULT, clear_cnt);
    vbe_stosl(&g_vbe_buffer[last], VBE_TERM_BG_DEFAULT, clear_cnt);
}

/* =============================================================================
 * Satır sonu işlemi (newline logic)
 * =============================================================================
 */
static void newline(void) {
    g_col = 0;
    g_row++;
    if (g_row >= VBE_TERM_ROWS) {
        term_scroll();
        g_row = VBE_TERM_ROWS - 1u;
    }
}

/* =============================================================================
 * vbe_term_putchar() — Tek karakter yaz
 * =============================================================================
 */
void vbe_term_putchar(char c) {
    if (!g_vbe_enabled || !g_vbe_term_enabled) return;

    /* Atomicity: Disable interrupts to prevent IRQ12 (mouse) or thread switches 
       from drawing the mouse over partially drawn text or corrupting s_mouse_bg */
    uint32_t eflags;
    asm volatile("pushf; pop %0" : "=r"(eflags));
    asm volatile("cli");

    if (c == '\n') {
        newline();
    } else if (c == '\r') {
        g_col = 0;
    } else if (c == '\b') {
        if (g_col > 0) {
            g_col--;
            draw_cell(g_col, g_row, ' ', g_fg, g_bg);
        }
    } else if (c == '\t') {
        /* Tab = 8-sütun sınırına kadar boşluk */
        uint32_t next = (g_col + 8u) & ~7u;
        while (g_col < next && g_col < VBE_TERM_COLS) {
            draw_cell(g_col, g_row, ' ', g_fg, g_bg);
            g_col++;
        }
        if (g_col >= VBE_TERM_COLS) newline();
    } else if ((uint8_t)c >= 32u) {
        /* Yazdırılabilir karakter */
        draw_cell(g_col, g_row, c, g_fg, g_bg);
        g_col++;

        if (g_col >= VBE_TERM_COLS) {
            newline();
        }
    }

    /* Restore interrupts if they were enabled before */
    if (eflags & 0x200) {
        asm volatile("sti");
    }
}

/* =============================================================================
 * vbe_term_puts() — Null-terminated string yaz
 * =============================================================================
 */
void vbe_term_puts(const char *str) {
    if (!str) return;
    while (*str) vbe_term_putchar(*str++);
}

/* =============================================================================
 * vbe_term_set_color() / vbe_term_reset_color()
 * =============================================================================
 */
void vbe_term_set_color(uint32_t fg, uint32_t bg) {
    g_fg = fg;
    g_bg = bg;
}

void vbe_term_reset_color(void) {
    g_fg = VBE_TERM_FG_DEFAULT;
    g_bg = VBE_TERM_BG_DEFAULT;
}

/* =============================================================================
 * vbe_term_clear() — Terminal alanını sil, imleci (0,0)'a al
 * =============================================================================
 */
void vbe_term_clear(void) {
    if (!g_vbe_enabled || !g_vbe_buffer) return;

    vbe_fill_rect(0, VBE_TERM_Y, g_vbe_width, VBE_TERM_ROWS * VBE_TERM_CHAR_H, VBE_TERM_BG_DEFAULT);
    g_col = 0;
    g_row = 0;
}

/* =============================================================================
 * vbe_term_init() — Terminal UI'yi hazırla, aktive et
 * =============================================================================
 */
void vbe_term_init(void) {
    if (!g_vbe_enabled || !g_vbe_buffer) return;

    uint32_t W = g_vbe_width;
    uint32_t H = g_vbe_height;

    /* ── 1. Splash alanını terminal arka planıyla doldur ── */
    vbe_fill_rect(0, 48u, W, H - 48u, VBE_TERM_BG_DEFAULT);

    /* ── 2. Başlık çubuğu altına renkli ayraç çiz (2 px, parlak mor) ── */
    vbe_fill_rect(0, 48u, W, 2u, 0x5533FFu);

    /* ── 3. Alt durum çubuğu (son 16 piksel) ── */
    uint32_t bar_y = H - 16u;
    vbe_fill_rect(0, bar_y, W, 16u, 0x14143Cu);

    /* Alt durum çubuğu metni */
    vbe_draw_string(8u,  bar_y + 1u,
        "ZeruX OS v0.1  |  IA-32 Protected Mode  |  PCI + RTC + VBE  |  FAT32 VFS  |  PS/2 Kbd",
        0x7777AAu, 0x14143Cu);

    /* ── 4. Terminali aktive et ── */
    g_col = 0;
    g_row = 0;
    g_fg  = VBE_TERM_FG_DEFAULT;
    g_bg  = VBE_TERM_BG_DEFAULT;
    g_vbe_term_enabled = 1;
}
