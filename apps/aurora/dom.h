/* =============================================================================
 * ZeruX OS — Aurora Browser: DOM (Document Object Model) Ağacı
 * File: apps/aurora/dom.h
 * =============================================================================
 * Basit bir N-ary ağaç: her node'un ilk çocuğuna (first_child) ve bir sonraki
 * kardeşine (next_sibling) işaret ettiği klasik "first-child/next-sibling"
 * temsili — bu sayede her node değişken sayıda çocuğa sahip olabilir ama
 * struct içinde dinamik dizi/kmalloc gerekmez.
 *
 * BELLEK STRATEJİSİ: node'lar kmalloc/kfree ile TEK TEK değil, sabit boyutlu
 * bir HAVUZDAN (static array, BSS'te durur — kheap'i hiç kullanmaz) alınır.
 * Böylece:
 *   - kheap'in şu anki sabit 128KB sınırından etkilenmiyoruz,
 *   - sayfa değiştiğinde tek tek düğüm serbest bırakmak yerine dom_reset()
 *     ile tüm havuzu tek seferde "boşaltıyoruz" (kernel/gui.c'deki
 *     g_explorer_files statik önbelleğiyle aynı desen).
 * =============================================================================
 */
#ifndef AURORA_DOM_H
#define AURORA_DOM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define DOM_MAX_NODES     512   /* Bir sayfada en fazla bu kadar DOM node'u */
#define DOM_MAX_TEXT_LEN  256   /* Bir metin node'unun maksimum uzunluğu */
#define DOM_MAX_ATTR_LEN  256   /* <a href="..."> / <img src="..."> maksimum uzunluk */

typedef enum {
    /* Temel yapı */
    DOM_TAG_HTML = 0,
    DOM_TAG_HEAD,
    DOM_TAG_BODY,
    DOM_TAG_TITLE,
    DOM_TAG_DIV,
    DOM_TAG_SPAN,

    /* Başlıklar */
    DOM_TAG_H1,
    DOM_TAG_H2,
    DOM_TAG_H3,
    DOM_TAG_H4,
    DOM_TAG_H5,
    DOM_TAG_H6,

    /* Paragraf / satır içi yapı */
    DOM_TAG_P,
    DOM_TAG_BR,
    DOM_TAG_HR,

    /* Metin biçimlendirme */
    DOM_TAG_B,          /* b / strong */
    DOM_TAG_I,          /* i / em */
    DOM_TAG_U,
    DOM_TAG_S,          /* s / del / strike — üstü çizili */
    DOM_TAG_CODE,       /* code / kbd / samp — sabit genişlikli font */
    DOM_TAG_MARK,       /* vurgulu metin (sarı arka plan) */
    DOM_TAG_SMALL,      /* küçük metin */
    DOM_TAG_PRE,        /* ön-biçimlendirilmiş blok */
    DOM_TAG_BLOCKQUOTE, /* alıntı bloğu */

    /* Bağlantı & medya */
    DOM_TAG_A,
    DOM_TAG_IMG,

    /* Sırasız liste */
    DOM_TAG_UL,
    DOM_TAG_OL,         /* sıralı liste */
    DOM_TAG_LI,

    /* Tanım listesi */
    DOM_TAG_DL,
    DOM_TAG_DT,         /* tanım terimi (bold) */
    DOM_TAG_DD,         /* tanım açıklaması (girintili) */

    /* Tablo */
    DOM_TAG_TABLE,
    DOM_TAG_THEAD,
    DOM_TAG_TBODY,
    DOM_TAG_TFOOT,
    DOM_TAG_CAPTION,
    DOM_TAG_TR,
    DOM_TAG_TH,         /* başlık hücresi (bold) */
    DOM_TAG_TD,

    /* Semantik HTML5 */
    DOM_TAG_HEADER,
    DOM_TAG_NAV,
    DOM_TAG_MAIN,
    DOM_TAG_SECTION,
    DOM_TAG_ARTICLE,
    DOM_TAG_ASIDE,
    DOM_TAG_FOOTER,

    /* Özel node türleri */
    DOM_TAG_TEXT,     /* gerçek bir HTML etiketi değil: metin içeriği taşır */
    DOM_TAG_UNKNOWN   /* tanınmayan etiket — çocukları yine de işlenir */
} dom_tag_type_t;

typedef struct dom_node {
    dom_tag_type_t     tag;
    char                text[DOM_MAX_TEXT_LEN]; /* sadece DOM_TAG_TEXT için dolu */
    char                attr[DOM_MAX_ATTR_LEN]; /* A->href, IMG->src */
    char                attr2[DOM_MAX_ATTR_LEN];/* IMG->alt */
    int32_t             attr_w;                 /* IMG->width  (0 = belirsiz) */
    int32_t             attr_h;                 /* IMG->height (0 = belirsiz) */
    uint16_t            list_index;             /* OL->LI sıra numarası (1-tabanlı) */
    struct dom_node    *first_child;
    struct dom_node    *next_sibling;
    struct dom_node    *parent;
} dom_node_t;

/* Havuzu sıfırlar (yeni bir sayfa yüklenmeden önce çağrılmalı). */
void dom_reset(void);

/* Havuzdan boş bir node alır ve alanlarını ilklendirir. Havuz doluysa NULL döner. */
dom_node_t *dom_new_node(dom_tag_type_t tag);

/* `parent`'ın çocuk listesinin sonuna `child`'ı ekler. */
void dom_append_child(dom_node_t *parent, dom_node_t *child);

/* "h1", "P", "BR" gibi ham etiket ismini bilinen bir dom_tag_type_t değerine çevirir. */
dom_tag_type_t dom_tag_from_name(const char *name, uint32_t len);

/* br/hr/img/input/meta/link gibi kapanış etiketi beklenmeyen elemanlar için true. */
bool dom_is_void_tag(dom_tag_type_t tag);

/* Konsola DOM ağacını yazdırır (debug amaçlı) */
void dom_debug_print(dom_node_t *node, int depth);

#endif /* AURORA_DOM_H */
