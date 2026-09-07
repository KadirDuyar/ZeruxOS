/* =============================================================================
 * ZeruX OS — VBE Scrollable Text Terminal
 * File: kernel/include/vbe_terminal.h
 * =============================================================================
 *
 * 1024x768x32bpp framebuffer üzerinde çalışan karakter bazlı terminal.
 * Font: 8x16 bit-mapped glyphs (font8x16.h)
 * Boyut: 127 sütun × 44 satır (VBE_TERM_COLS × VBE_TERM_ROWS)
 *
 * Özellikler:
 *   - Otomatik sözcük sarma (word-wrap)
 *   - Yukarı kaydırma (scroll-up) — pixel kopyalama
 *   - \n, \r, \b, \t desteği
 *   - Ön plan / arka plan rengi (32-bit ARGB)
 *   - serial_set_vbe_mirror() ile seri port çıktısını aynalama
 * =============================================================================
 */

#ifndef VBE_TERMINAL_H
#define VBE_TERMINAL_H

#include <stdint.h>

/* ── Terminal Geometri Sabitleri ─────────────────────────────────────── */
#define VBE_TERM_X       4u      /* Sol piksel kenar boşluğu          */
#define VBE_TERM_Y      72u      /* Üst piksel başlangıcı (title bar altı) */
#define VBE_TERM_CHAR_W  8u      /* Karakter genişliği (piksel)       */
#define VBE_TERM_CHAR_H 16u      /* Karakter yüksekliği (piksel)      */
#define VBE_TERM_COLS  127u      /* (1024 - 4) / 8 ≈ 127 sütun       */
#define VBE_TERM_ROWS   41u      /* (728 - 72) / 16 ≈ 41 satır  */

/* ── Renk Sabitleri ──────────────────────────────────────────────────── */
#define VBE_TERM_FG_DEFAULT  0xDDDDDDu  /* Açık gri — normal metin     */
#define VBE_TERM_BG_DEFAULT  0x0A0A16u  /* Çok koyu lacivert — zemin   */
#define VBE_TERM_FG_PROMPT   0x00FF88u  /* Parlak yeşil — prompt       */
#define VBE_TERM_FG_ERROR    0xFF5555u  /* Kırmızı — hata              */
#define VBE_TERM_FG_INFO     0x5BCEFA u /* Açık mavi — bilgi           */
#define VBE_TERM_FG_DIR      0xFFD700u  /* Altın sarısı — dizin        */

/* ── Global Bayrak: 1 ise terminal aktif ────────────────────────────── */
extern int g_vbe_term_enabled;

/* ── Public API ─────────────────────────────────────────────────────── */

/**
 * vbe_term_init() — Terminal alanını temizler ve terminali aktive eder.
 * Boot splash'ı siler; VBE framebuffer modunda çalıştığında çağrılmalı.
 */
void vbe_term_init(void);

/**
 * vbe_term_putchar(c) — Tek karakter yazar. \n, \r, \b, \t desteklenir.
 * g_vbe_term_enabled = 0 ise sessizce çıkar.
 */
void vbe_term_putchar(char c);

/**
 * vbe_term_puts(str) — Null-terminated string yazar.
 */
void vbe_term_puts(const char *str);

/**
 * vbe_term_set_color(fg, bg) — Sonraki karakterler için rengi ayarlar.
 * Renk değerleri 0xRRGGBB formatındadır.
 */
void vbe_term_set_color(uint32_t fg, uint32_t bg);

/**
 * vbe_term_reset_color() — Rengi varsayılana (açık gri / koyu lacivert) döndürür.
 */
void vbe_term_reset_color(void);

/**
 * vbe_term_clear() — Terminal alanını tamamen temizler, imleci (0,0)'a taşır.
 */
void vbe_term_clear(void);

#endif /* VBE_TERMINAL_H */
