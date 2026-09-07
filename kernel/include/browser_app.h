/* =============================================================================
 * ZeruX OS - Aurora Browser Application Header
 * File: kernel/include/browser_app.h
 * =============================================================================
 * Aurora, ZeruX OS icin gelistirilmis ilk tam GUI web tarayicisidir.
 * DOM agacini (apps/aurora/dom.h) alip gfx2d.h ile bir window icine cizer.
 * Desteklenen etiketler: h1/h2/h3, p, a (link), ul/li, b/i, hr, br, div.
 * Kaydirma: yukari/asagi ok tuslari. URL girisi: adres cubugundan.
 * =============================================================================
 */

#ifndef BROWSER_APP_H
#define BROWSER_APP_H

#include <stdint.h>

/* Tarayici penceresini olusturur. Baslangi URL'si url ise hemen fetch
 * baslatir (NULL veya "" ise adres cubugunun bos gosterilir).
 * Pencere id'sini (window.h) doner, hata durumunda -1. */
int32_t browser_app_create(int32_t x, int32_t y, int32_t w, int32_t h,
                            const char *url);

#endif /* BROWSER_APP_H */
