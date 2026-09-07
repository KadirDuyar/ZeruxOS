/* =============================================================================
 * ZeruX OS — Aurora Browser: Basit HTML Parser
 * File: apps/aurora/html_parser.h
 * =============================================================================
 * Gerçek bir HTML5 parser değildir (hatalı iç içe geçmiş etiketleri kurtarma,
 * implicit tag oluşturma gibi tarayıcı-standardı davranışlar yok). Amaç,
 * v0.1 için desteklenen etiket kümesini (html/body/div/p/h1-h3/b/i/u/br/hr/
 * img/a/ul/li/title) hataya dayanıklı şekilde bir DOM ağacına çevirmek.
 *
 * <script> ve <style> içerikleri TAMAMEN ATLANIR (metin olarak bile DOM'a
 * eklenmez) — aksi halde JS/CSS kodu sayfada düz metin gibi görünürdü.
 * =============================================================================
 */
#ifndef AURORA_HTML_PARSER_H
#define AURORA_HTML_PARSER_H

#include "dom.h"

/* `html` (uzunluğu `len`) içeriğini parse eder. dom_reset() dahilen çağrılır
 * (önceki sayfanın node havuzu sıfırlanır), yeni ağacın senteti-kökünü
 * (tag=DOM_TAG_HTML) döner. Dönen kök asla NULL olmaz. */
dom_node_t *aurora_html_parse(const char *html, uint32_t len);

#endif /* AURORA_HTML_PARSER_H */
