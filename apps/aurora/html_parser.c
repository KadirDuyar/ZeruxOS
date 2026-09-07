/* =============================================================================
 * ZeruX OS — Aurora Browser: HTML Parser
 * File: apps/aurora/html_parser.c
 * =============================================================================
 * Tek geçişli (single-pass) karakter tabanlı tokenizer. Açık etiketleri bir
 * yığında (stack) tutar; bir kapanış etiketi geldiğinde yığında isim eşleşen
 * en yakın açık etiketi arar ve oraya kadar (dahil) hepsini kapatır — bozuk/
 * eksik kapatılmış HTML'de bile çökmeden makul bir ağaç üretir.
 *
 * Desteklenen özellikler:
 *  - Tüm dom.h'daki etiket türleri
 *  - HTML entity decode: named + sayısal (&#NNN; ve &#xHH;)
 *  - <script>/<style> içeriklerini tamamen atlar
 *  - <!-- --> yorumlarını atlar
 *  - <!DOCTYPE> ve diğer <!...> bildirimleri atlar
 *  - <ol> altındaki <li> elemanlarına otomatik sıra numarası atar
 *  - <img>: src, alt, width, height attr'larını parse eder
 *  - <a>: href attr'ını parse eder
 * =============================================================================
 */
#include "html_parser.h"

#define PARSE_STACK_MAX 48

/* ---- Yardımcı fonksiyonlar ------------------------------------------------ */

static bool is_ws(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool is_name_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_' || c == ':';
}

static bool matches_ci(const char *html, uint32_t i, uint32_t len, const char *needle) {
    uint32_t j = 0;
    while (needle[j]) {
        if (i + j >= len) return false;
        char a = html[i + j], b = needle[j];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return false;
        j++;
    }
    return true;
}

static void skip_until(const char *html, uint32_t *i, uint32_t len, const char *needle) {
    uint32_t nlen = 0;
    while (needle[nlen]) nlen++;
    while (*i < len) {
        if (matches_ci(html, *i, len, needle)) {
            *i += nlen;
            return;
        }
        (*i)++;
    }
}

/* Sayısal karakter değerini (10 tabanında) oku, geçerli ASCII döndür. */
static char decode_numeric_entity(const char *html, uint32_t i, uint32_t len,
                                   uint32_t *consumed) {
    /* i: '&' sonrasındaki '#' konumunu geçmiş ilk rakam */
    uint32_t start = i;
    uint32_t value = 0;
    bool is_hex = false;

    if (i < len && (html[i] == 'x' || html[i] == 'X')) {
        is_hex = true;
        i++;
    }

    while (i < len && html[i] != ';' && html[i] != '<' && html[i] != '&' && (i - start) < 8) {
        char c = html[i];
        if (is_hex) {
            if (c >= '0' && c <= '9') value = value * 16 + (uint32_t)(c - '0');
            else if (c >= 'a' && c <= 'f') value = value * 16 + (uint32_t)(c - 'a') + 10;
            else if (c >= 'A' && c <= 'F') value = value * 16 + (uint32_t)(c - 'A') + 10;
            else break;
        } else {
            if (c >= '0' && c <= '9') value = value * 10 + (uint32_t)(c - '0');
            else break;
        }
        i++;
    }
    if (i < len && html[i] == ';') i++;
    *consumed = i - start + 2; /* +2 için: '&' ve '#' karakterleri */

    /* Temel Latin (0x20-0x7E) ve bazı Latin-1 karakterleri */
    if (value >= 0x20 && value <= 0x7E) return (char)value;
    /* Bazı yararlı Latin-1 → ASCII benzerleri */
    if (value == 0xA0) return ' ';   /* nbsp */
    if (value == 0xAB) return '<';   /* laquo gibi */
    if (value == 0xBB) return '>';
    if (value == 0x2014) return '-'; /* mdash */
    if (value == 0x2013) return '-'; /* ndash */
    if (value == 0x201C || value == 0x201D) return '"';
    if (value == 0x2018 || value == 0x2019) return '\'';
    return '?';
}

/* HTML entity çözümü — genişletilmiş tablo */
static uint32_t decode_entity(const char *html, uint32_t i, uint32_t len, char *out) {
    /* Sayısal entity: &#NNN; veya &#xHH; */
    if (i + 1 < len && html[i + 1] == '#') {
        uint32_t consumed = 0;
        *out = decode_numeric_entity(html, i + 2, len, &consumed);
        return consumed;
    }

    /* Named entities — alfabetik sıra */
    if (matches_ci(html, i, len, "&amp;"))    { *out = '&';   return 5; }
    if (matches_ci(html, i, len, "&apos;"))   { *out = '\'';  return 6; }
    if (matches_ci(html, i, len, "&bull;"))   { *out = '*';   return 6; }
    if (matches_ci(html, i, len, "&cent;"))   { *out = 'c';   return 6; }
    if (matches_ci(html, i, len, "&copy;"))   { *out = '(';   return 6; } /* (C) yerine ( */
    if (matches_ci(html, i, len, "&deg;"))    { *out = 'o';   return 5; }
    if (matches_ci(html, i, len, "&divide;")) { *out = '/';   return 8; }
    if (matches_ci(html, i, len, "&euro;"))   { *out = 'E';   return 6; }
    if (matches_ci(html, i, len, "&frac12;")) { *out = '?';   return 8; } /* 1/2 */
    if (matches_ci(html, i, len, "&frac14;")) { *out = '?';   return 8; } /* 1/4 */
    if (matches_ci(html, i, len, "&gt;"))     { *out = '>';   return 4; }
    if (matches_ci(html, i, len, "&laquo;"))  { *out = '<';   return 7; }
    if (matches_ci(html, i, len, "&lt;"))     { *out = '<';   return 4; }
    if (matches_ci(html, i, len, "&mdash;"))  { *out = '-';   return 7; }
    if (matches_ci(html, i, len, "&micro;"))  { *out = 'u';   return 7; }
    if (matches_ci(html, i, len, "&middot;")) { *out = '.';   return 8; }
    if (matches_ci(html, i, len, "&minus;"))  { *out = '-';   return 7; }
    if (matches_ci(html, i, len, "&nbsp;"))   { *out = ' ';   return 6; }
    if (matches_ci(html, i, len, "&ndash;"))  { *out = '-';   return 7; }
    if (matches_ci(html, i, len, "&not;"))    { *out = '!';   return 5; }
    if (matches_ci(html, i, len, "&para;"))   { *out = 'P';   return 6; }
    if (matches_ci(html, i, len, "&plusmn;")) { *out = '+';   return 8; }
    if (matches_ci(html, i, len, "&pound;"))  { *out = 'L';   return 7; }
    if (matches_ci(html, i, len, "&quot;"))   { *out = '"';   return 6; }
    if (matches_ci(html, i, len, "&raquo;"))  { *out = '>';   return 7; }
    if (matches_ci(html, i, len, "&reg;"))    { *out = 'R';   return 5; }
    if (matches_ci(html, i, len, "&sect;"))   { *out = 'S';   return 6; }
    if (matches_ci(html, i, len, "&shy;"))    { *out = '-';   return 5; }
    if (matches_ci(html, i, len, "&sup2;"))   { *out = '2';   return 6; }
    if (matches_ci(html, i, len, "&sup3;"))   { *out = '3';   return 6; }
    if (matches_ci(html, i, len, "&times;"))  { *out = 'x';   return 7; }
    if (matches_ci(html, i, len, "&trade;"))  { *out = 'T';   return 7; }
    if (matches_ci(html, i, len, "&uml;"))    { *out = '"';   return 5; }
    if (matches_ci(html, i, len, "&yen;"))    { *out = 'Y';   return 5; }
    if (matches_ci(html, i, len, "&#39;"))    { *out = '\'';  return 5; }

    /* Tanınmayan entity: olduğu gibi bırak */
    *out = html[i];
    return 1;
}

/* <tag ...> içindeki tek bir attr="value" çiftini arar (case-insensitive). */
static bool find_attr(const char *html, uint32_t tag_start, uint32_t tag_end,
                       const char *attr_name, char *out, uint32_t out_max) {
    uint32_t i = tag_start;
    uint32_t name_len = 0;
    while (attr_name[name_len]) name_len++;

    while (i < tag_end) {
        while (i < tag_end && is_ws(html[i])) i++;
        uint32_t name_start = i;
        while (i < tag_end && html[i] != '=' && !is_ws(html[i]) && html[i] != '/' && html[i] != '>') i++;
        uint32_t name_end = i;

        while (i < tag_end && is_ws(html[i])) i++;

        if (i < tag_end && html[i] == '=') {
            i++;
            while (i < tag_end && is_ws(html[i])) i++;
            char quote = 0;
            if (i < tag_end && (html[i] == '"' || html[i] == '\'')) {
                quote = html[i];
                i++;
            }
            uint32_t val_start = i;
            while (i < tag_end && ((quote && html[i] != quote) ||
                                   (!quote && !is_ws(html[i]) && html[i] != '>'))) {
                i++;
            }
            uint32_t val_end = i;
            if (quote && i < tag_end) i++; /* kapanış tırnağını atla */

            if ((name_end - name_start) == name_len &&
                matches_ci(html, name_start, name_end, attr_name)) {
                uint32_t vlen = val_end - val_start;
                if (vlen > out_max - 1) vlen = out_max - 1;
                for (uint32_t k = 0; k < vlen; k++) out[k] = html[val_start + k];
                out[vlen] = '\0';
                return true;
            }
        } else {
            /* Değersiz attribute (örn: <input disabled>) — atla */
            (void)name_start;
        }
        if (i < tag_end && html[i] == '/') break;
        if (i == name_start) i++; /* sonsuz döngü önlemi */
    }
    return false;
}

/* Sayı stringini integer'a çevir (atoi yerine, kütüphane bağımlılığı yok) */
static int32_t parse_int(const char *s) {
    int32_t val = 0;
    bool neg = false;
    if (*s == '-') { neg = true; s++; }
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    return neg ? -val : val;
}

/* ---- Ana parse fonksiyonu ------------------------------------------------- */

/* ol_counter_stack: her OL açıldığında yeni sayaç, kapanınca çıkarılır */
typedef struct {
    dom_node_t *ol_node;
    uint16_t    counter;
} ol_entry_t;

dom_node_t *aurora_html_parse(const char *html, uint32_t len) {
    dom_reset();

    dom_node_t *root = dom_new_node(DOM_TAG_HTML);

    dom_node_t *stack[PARSE_STACK_MAX];
    int top = 0;
    stack[0] = root;

    /* OL sayaç yığını */
    ol_entry_t ol_stack[PARSE_STACK_MAX];
    int ol_top = -1;

    bool skipping_raw = false;
    char skip_close_tag[24] = {0};

    uint32_t i = 0;
    while (i < len) {
        /* --- raw bloğu atlama (script/style) --- */
        if (skipping_raw) {
            if (matches_ci(html, i, len, skip_close_tag)) {
                uint32_t clen = 0;
                while (skip_close_tag[clen]) clen++;
                i += clen;
                skipping_raw = false;
            } else {
                i++;
            }
            continue;
        }

        if (html[i] == '<') {
            /* HTML yorumu: <!-- --> */
            if (matches_ci(html, i, len, "<!--")) {
                i += 4;
                skip_until(html, &i, len, "-->");
                continue;
            }
            /* DOCTYPE ve diğer bildirimler */
            if (matches_ci(html, i, len, "<!")) {
                skip_until(html, &i, len, ">");
                continue;
            }
            /* Kapanış etiketi */
            if (i + 1 < len && html[i + 1] == '/') {
                i += 2;
                uint32_t name_start = i;
                while (i < len && is_name_char(html[i])) i++;
                uint32_t name_len_val = i - name_start;
                skip_until(html, &i, len, ">");

                dom_tag_type_t closing_tag = dom_tag_from_name(html + name_start, name_len_val);

                /* OL kapanıyor mu? */
                if (closing_tag == DOM_TAG_OL && ol_top >= 0) {
                    /* Bu OL'un stack kaydını bul ve çıkar */
                    for (int s = top; s >= 1; s--) {
                        if (stack[s]->tag == DOM_TAG_OL) {
                            /* ol_stack'ten bul */
                            for (int o = ol_top; o >= 0; o--) {
                                if (ol_stack[o].ol_node == stack[s]) {
                                    /* Bu ve üzerindeki ol girişlerini temizle */
                                    ol_top = o - 1;
                                    break;
                                }
                            }
                            break;
                        }
                    }
                }

                /* Yığında bu tag'e sahip en yakın açık node'u bul */
                int found = -1;
                for (int s = top; s >= 1; s--) {
                    if (stack[s]->tag == closing_tag) { found = s; break; }
                }
                if (found >= 1) {
                    top = found - 1;
                }
                continue;
            }

            /* ---- Açılış etiketi ---- */
            i++;
            uint32_t name_start = i;
            while (i < len && is_name_char(html[i])) i++;
            uint32_t name_len_val = i - name_start;

            if (name_len_val == 0) { i++; continue; }

            uint32_t tag_start = i;
            /* Etiketin sonunu bul */
            bool self_closing = false;
            uint32_t scan = i;
            while (scan < len && html[scan] != '>') scan++;
            if (scan > 0 && scan <= len && html[scan - 1] == '/') self_closing = true;
            uint32_t tag_end = scan;

            /* <script> / <style> içeriklerini tamamen atla */
            if ((name_len_val == 6 && matches_ci(html, name_start, len, "script")) ||
                (name_len_val == 5 && matches_ci(html, name_start, len, "style"))) {
                bool is_script = (name_len_val == 6);
                if (!self_closing) {
                    skipping_raw = true;
                    const char *ct = is_script ? "</script>" : "</style>";
                    uint32_t k = 0;
                    while (ct[k] && k < sizeof(skip_close_tag) - 1) {
                        skip_close_tag[k] = ct[k];
                        k++;
                    }
                    skip_close_tag[k] = '\0';
                }
                i = (tag_end < len) ? tag_end + 1 : len;
                continue;
            }

            dom_tag_type_t tag = dom_tag_from_name(html + name_start, name_len_val);
            dom_node_t *node = dom_new_node(tag);
            if (node) {
                /* Attribute parse */
                if (tag == DOM_TAG_A) {
                    find_attr(html, tag_start, tag_end, "href", node->attr, DOM_MAX_ATTR_LEN);
                } else if (tag == DOM_TAG_IMG) {
                    find_attr(html, tag_start, tag_end, "src",  node->attr,  DOM_MAX_ATTR_LEN);
                    find_attr(html, tag_start, tag_end, "alt",  node->attr2, DOM_MAX_ATTR_LEN);
                    /* width / height */
                    char num_buf[16] = {0};
                    if (find_attr(html, tag_start, tag_end, "width", num_buf, sizeof(num_buf)))
                        node->attr_w = parse_int(num_buf);
                    if (find_attr(html, tag_start, tag_end, "height", num_buf, sizeof(num_buf)))
                        node->attr_h = parse_int(num_buf);
                }

                /* OL içindeki LI ise sıra numarası ata */
                if (tag == DOM_TAG_LI && ol_top >= 0) {
                    ol_stack[ol_top].counter++;
                    node->list_index = ol_stack[ol_top].counter;
                }

                dom_append_child(stack[top], node);

                bool void_el = dom_is_void_tag(tag);
                /* meta/link/input/source gibi UNKNOWN void etiketlerin de stack'e girmemesi için:
                   void_el || (tag == DOM_TAG_UNKNOWN && self_closing) */
                if (!self_closing && !void_el && top < PARSE_STACK_MAX - 1) {
                    stack[++top] = node;

                    /* OL açıldı: yeni sayaç kaydet */
                    if (tag == DOM_TAG_OL && ol_top < PARSE_STACK_MAX - 1) {
                        ol_top++;
                        ol_stack[ol_top].ol_node = node;
                        ol_stack[ol_top].counter = 0;
                    }
                }
            }

            i = (tag_end < len) ? tag_end + 1 : len;
            continue;
        }

        /* ---- Metin içeriği (bir sonraki '<' işaretine kadar) ---- */
        {
            char text_buf[DOM_MAX_TEXT_LEN];
            uint32_t tlen = 0;
            bool any_non_ws = false;
            bool in_pre = false;

            /* <pre> içinde miyiz? Stack'e bak */
            for (int s = top; s >= 1; s--) {
                if (stack[s]->tag == DOM_TAG_PRE) { in_pre = true; break; }
            }

            while (i < len && html[i] != '<') {
                char decoded;
                uint32_t consumed;
                if (html[i] == '&') {
                    consumed = decode_entity(html, i, len, &decoded);
                } else {
                    decoded = html[i];
                    consumed = 1;
                }

                if (!is_ws(decoded)) any_non_ws = true;

                if (tlen < DOM_MAX_TEXT_LEN - 1) {
                    if (!in_pre && is_ws(decoded) && tlen > 0 && text_buf[tlen - 1] == ' ') {
                        /* ardışık boşlukları teke indir (pre dışında) */
                    } else {
                        text_buf[tlen++] = (!in_pre && is_ws(decoded)) ? ' ' : decoded;
                    }
                }
                
                /* Flush the buffer if it gets full to prevent truncation */
                if (tlen == DOM_MAX_TEXT_LEN - 1) {
                    text_buf[tlen] = '\0';
                    if (any_non_ws) {
                        dom_node_t *tnode = dom_new_node(DOM_TAG_TEXT);
                        if (tnode) {
                            for (uint32_t k = 0; k <= tlen; k++) tnode->text[k] = text_buf[k];
                            dom_append_child(stack[top], tnode);
                        }
                    }
                    tlen = 0;
                    any_non_ws = false;
                }

                i += consumed;
            }
            text_buf[tlen] = '\0';

            /* Başındaki/sonundaki boşlukları pre dışında temizle */
            if (!in_pre && tlen > 0 && text_buf[0] == ' ') {
                /* Sadece boşluktan oluşuyorsa ekleme */
                bool only_ws = true;
                for (uint32_t k = 0; k < tlen; k++) {
                    if (text_buf[k] != ' ') { only_ws = false; break; }
                }
                if (only_ws) { continue; }
            }

            if (any_non_ws && tlen > 0) {
                dom_node_t *tnode = dom_new_node(DOM_TAG_TEXT);
                if (tnode) {
                    uint32_t k = 0;
                    while (text_buf[k] && k < DOM_MAX_TEXT_LEN - 1) {
                        tnode->text[k] = text_buf[k];
                        k++;
                    }
                    tnode->text[k] = '\0';
                    dom_append_child(stack[top], tnode);
                }
            }
        }
    }

    return root;
}
