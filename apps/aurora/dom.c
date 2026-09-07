/* =============================================================================
 * ZeruX OS — Aurora Browser: DOM (Document Object Model) Ağacı
 * File: apps/aurora/dom.c
 * =============================================================================
 */
#include "dom.h"
#include "serial.h"

/* ---- Sabit boyutlu node havuzu (BSS'te, kheap kullanmaz) ------------------ */
static dom_node_t g_node_pool[DOM_MAX_NODES];
static uint32_t   g_node_count = 0;

void dom_reset(void) {
    g_node_count = 0;
}

dom_node_t *dom_new_node(dom_tag_type_t tag) {
    if (g_node_count >= DOM_MAX_NODES) return NULL;

    dom_node_t *n = &g_node_pool[g_node_count++];
    n->tag         = tag;
    n->text[0]     = '\0';
    n->attr[0]     = '\0';
    n->attr2[0]    = '\0';
    n->attr_w      = 0;
    n->attr_h      = 0;
    n->list_index  = 0;
    n->first_child = NULL;
    n->next_sibling= NULL;
    n->parent      = NULL;
    return n;
}

void dom_append_child(dom_node_t *parent, dom_node_t *child) {
    if (!parent || !child) return;
    child->parent       = parent;
    child->next_sibling = NULL;

    if (!parent->first_child) {
        parent->first_child = child;
        return;
    }
    dom_node_t *last = parent->first_child;
    while (last->next_sibling) last = last->next_sibling;
    last->next_sibling = child;
}

/* ---- Etiket ismi -> tag türü eşlemesi ------------------------------------ */
typedef struct {
    const char     *name;
    dom_tag_type_t  tag;
} tag_entry_t;

static const tag_entry_t g_tag_table[] = {
    /* Temel yapı */
    { "html",       DOM_TAG_HTML },
    { "head",       DOM_TAG_HEAD },
    { "body",       DOM_TAG_BODY },
    { "title",      DOM_TAG_TITLE },
    { "div",        DOM_TAG_DIV },
    { "span",       DOM_TAG_SPAN },

    /* Başlıklar */
    { "h1",         DOM_TAG_H1 },
    { "h2",         DOM_TAG_H2 },
    { "h3",         DOM_TAG_H3 },
    { "h4",         DOM_TAG_H4 },
    { "h5",         DOM_TAG_H5 },
    { "h6",         DOM_TAG_H6 },

    /* Paragraf */
    { "p",          DOM_TAG_P },
    { "br",         DOM_TAG_BR },
    { "hr",         DOM_TAG_HR },

    /* Metin biçimlendirme */
    { "b",          DOM_TAG_B },
    { "strong",     DOM_TAG_B },    /* strong = b */
    { "i",          DOM_TAG_I },
    { "em",         DOM_TAG_I },    /* em = i */
    { "u",          DOM_TAG_U },
    { "s",          DOM_TAG_S },
    { "del",        DOM_TAG_S },    /* del = s */
    { "strike",     DOM_TAG_S },    /* eski HTML */
    { "code",       DOM_TAG_CODE },
    { "kbd",        DOM_TAG_CODE }, /* kbd = code */
    { "samp",       DOM_TAG_CODE }, /* samp = code */
    { "tt",         DOM_TAG_CODE }, /* eski HTML sabit genişlik */
    { "mark",       DOM_TAG_MARK },
    { "small",      DOM_TAG_SMALL },
    { "pre",        DOM_TAG_PRE },
    { "blockquote", DOM_TAG_BLOCKQUOTE },
    { "q",          DOM_TAG_BLOCKQUOTE }, /* q = satır içi alıntı, benzer render */

    /* Bağlantı & medya */
    { "a",          DOM_TAG_A },
    { "img",        DOM_TAG_IMG },

    /* Listeler */
    { "ul",         DOM_TAG_UL },
    { "ol",         DOM_TAG_OL },
    { "li",         DOM_TAG_LI },

    /* Tanım listesi */
    { "dl",         DOM_TAG_DL },
    { "dt",         DOM_TAG_DT },
    { "dd",         DOM_TAG_DD },

    /* Tablo */
    { "table",      DOM_TAG_TABLE },
    { "thead",      DOM_TAG_THEAD },
    { "tbody",      DOM_TAG_TBODY },
    { "tfoot",      DOM_TAG_TFOOT },
    { "caption",    DOM_TAG_CAPTION },
    { "tr",         DOM_TAG_TR },
    { "th",         DOM_TAG_TH },
    { "td",         DOM_TAG_TD },

    /* Semantik HTML5 — div gibi render edilir */
    { "header",     DOM_TAG_HEADER },
    { "nav",        DOM_TAG_NAV },
    { "main",       DOM_TAG_MAIN },
    { "section",    DOM_TAG_SECTION },
    { "article",    DOM_TAG_ARTICLE },
    { "aside",      DOM_TAG_ASIDE },
    { "footer",     DOM_TAG_FOOTER },

    /* Void etiketler (içerik yok, DOM'a ekleniyor ama stack'e girmiyor) */
    { "meta",       DOM_TAG_UNKNOWN },
    { "link",       DOM_TAG_UNKNOWN },
    { "input",      DOM_TAG_UNKNOWN },
    { "source",     DOM_TAG_UNKNOWN },
    { "wbr",        DOM_TAG_BR },
};
#define TAG_TABLE_LEN (sizeof(g_tag_table) / sizeof(g_tag_table[0]))

static bool ci_eq_n(const char *a, const char *b, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return false;
    }
    return true;
}

static uint32_t str_len(const char *s) {
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

dom_tag_type_t dom_tag_from_name(const char *name, uint32_t len) {
    for (uint32_t i = 0; i < TAG_TABLE_LEN; i++) {
        uint32_t tlen = str_len(g_tag_table[i].name);
        if (tlen == len && ci_eq_n(name, g_tag_table[i].name, len)) {
            return g_tag_table[i].tag;
        }
    }
    return DOM_TAG_UNKNOWN;
}

bool dom_is_void_tag(dom_tag_type_t tag) {
    return tag == DOM_TAG_BR  ||
           tag == DOM_TAG_HR  ||
           tag == DOM_TAG_IMG;
    /* meta/link/input/source DOM_TAG_UNKNOWN olarak parse edilir;
     * dom_tag_from_name() onları zaten UNKNOWN döndürüyor ve UNKNOWN
     * void olarak işaretlenmiyor — parser tarafında is_name_char()
     * ile void kontrolü yapılıyor */
}

/* ---- Debug print ---------------------------------------------------------- */
static const char *tag_name_str(dom_tag_type_t t) {
    switch (t) {
        case DOM_TAG_HTML:       return "html";
        case DOM_TAG_HEAD:       return "head";
        case DOM_TAG_BODY:       return "body";
        case DOM_TAG_TITLE:      return "title";
        case DOM_TAG_DIV:        return "div";
        case DOM_TAG_SPAN:       return "span";
        case DOM_TAG_H1:         return "h1";
        case DOM_TAG_H2:         return "h2";
        case DOM_TAG_H3:         return "h3";
        case DOM_TAG_H4:         return "h4";
        case DOM_TAG_H5:         return "h5";
        case DOM_TAG_H6:         return "h6";
        case DOM_TAG_P:          return "p";
        case DOM_TAG_BR:         return "br";
        case DOM_TAG_HR:         return "hr";
        case DOM_TAG_B:          return "b";
        case DOM_TAG_I:          return "i";
        case DOM_TAG_U:          return "u";
        case DOM_TAG_S:          return "s";
        case DOM_TAG_CODE:       return "code";
        case DOM_TAG_MARK:       return "mark";
        case DOM_TAG_SMALL:      return "small";
        case DOM_TAG_PRE:        return "pre";
        case DOM_TAG_BLOCKQUOTE: return "blockquote";
        case DOM_TAG_A:          return "a";
        case DOM_TAG_IMG:        return "img";
        case DOM_TAG_UL:         return "ul";
        case DOM_TAG_OL:         return "ol";
        case DOM_TAG_LI:         return "li";
        case DOM_TAG_DL:         return "dl";
        case DOM_TAG_DT:         return "dt";
        case DOM_TAG_DD:         return "dd";
        case DOM_TAG_TABLE:      return "table";
        case DOM_TAG_THEAD:      return "thead";
        case DOM_TAG_TBODY:      return "tbody";
        case DOM_TAG_TFOOT:      return "tfoot";
        case DOM_TAG_CAPTION:    return "caption";
        case DOM_TAG_TR:         return "tr";
        case DOM_TAG_TH:         return "th";
        case DOM_TAG_TD:         return "td";
        case DOM_TAG_HEADER:     return "header";
        case DOM_TAG_NAV:        return "nav";
        case DOM_TAG_MAIN:       return "main";
        case DOM_TAG_SECTION:    return "section";
        case DOM_TAG_ARTICLE:    return "article";
        case DOM_TAG_ASIDE:      return "aside";
        case DOM_TAG_FOOTER:     return "footer";
        case DOM_TAG_TEXT:       return "TEXT";
        default:                 return "?";
    }
}

void dom_debug_print(dom_node_t *node, int depth) {
    if (!node) return;
    for (int i = 0; i < depth; i++) serial_printf("  ");
    if (node->tag == DOM_TAG_TEXT) {
        serial_printf("TEXT: \"%s\"\n", node->text);
    } else {
        serial_printf("<%s%s%s>\n", tag_name_str(node->tag),
                       node->attr[0] ? " attr=" : "", node->attr);
    }
    for (dom_node_t *c = node->first_child; c; c = c->next_sibling)
        dom_debug_print(c, depth + 1);
}
