/* HTML tokenizer + tree builder for the SharkOS browser.
 *
 * Deliberately simple but forgiving: unclosed tags, stray end tags, upper
 * case, unquoted attributes, entities and raw-text elements all work. The
 * node pool is a fixed arena so a malformed page can never exhaust the
 * kernel heap (the kernel allocator is a bump allocator without free()). */

#include "browser_internal.h"

/* ------------------------------------------------------------- arena */

static br_node_t dom_pool[BR_MAX_NODES];
static int node_count = 0;
static char text_pool[BR_TEXT_POOL];
static int text_used = 0;

void br_dom_reset(void) {
    node_count = 0;
    text_used = 0;
}

int br_dom_node_count(void) { return node_count; }
br_node_t* br_dom_node(int i) { return (i >= 0 && i < node_count) ? &dom_pool[i] : NULL; }

int br_text_in_pool(const char* s) { return s >= text_pool && s < text_pool + BR_TEXT_POOL; }

char* br_strdup_n(const char* s, int n) {
    if (n < 0) n = 0;
    if (text_used + n + 1 > BR_TEXT_POOL) {
        /* Out of text space: return an empty string rather than crashing. */
        text_pool[BR_TEXT_POOL - 1] = 0;
        return &text_pool[BR_TEXT_POOL - 1];
    }
    char* d = &text_pool[text_used];
    for (int i = 0; i < n; i++) d[i] = s[i];
    d[n] = 0;
    text_used += n + 1;
    return d;
}

char* br_strdup(const char* s) { return br_strdup_n(s, s ? (int)strlen(s) : 0); }

br_node_t* br_node_new(int type) {
    if (node_count >= BR_MAX_NODES) return NULL;
    br_node_t* n = &dom_pool[node_count++];
    memset(n, 0, sizeof(*n));
    n->type = type;
    n->id = node_count - 1;
    n->tag = "";
    n->text = "";
    return n;
}

void br_node_append(br_node_t* parent, br_node_t* child) {
    if (!parent || !child || parent == child) return;
    /* Refuse to create a cycle (appendChild of an ancestor) or a tree deeper
     * than the recursive walkers can handle. */
    int depth = 0;
    for (br_node_t* a = parent; a; a = a->parent, depth++) if (a == child) return;
    if (depth >= BR_MAX_DEPTH * 2) return;
    /* Detach first if it already has a parent (JS appendChild moves). */
    if (child->parent) {
        br_node_t* p = child->parent;
        if (p->first_child == child) p->first_child = child->next;
        else {
            br_node_t* c = p->first_child;
            while (c && c->next != child) c = c->next;
            if (c) c->next = child->next;
        }
        if (p->last_child == child) {
            br_node_t* c = p->first_child;
            br_node_t* last = NULL;
            while (c) { last = c; c = c->next; }
            p->last_child = last;
        }
    }
    child->parent = parent;
    child->next = NULL;
    if (parent->last_child) parent->last_child->next = child;
    else parent->first_child = child;
    parent->last_child = child;
}

void br_node_remove_children(br_node_t* n) {
    if (!n) return;
    n->first_child = n->last_child = NULL;
}

/* ------------------------------------------------------------ helpers */

int br_strieq(const char* a, const char* b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

int br_streq(const char* a, const char* b) {
    if (!a || !b) return 0;
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == 0 && *b == 0;
}

int br_streq_n(const char* a, int n, const char* lit) {
    if (!a || !lit) return 0;
    for (int i = 0; i < n; i++) { if (!lit[i] || a[i] != lit[i]) return 0; }
    return lit[n] == 0;
}

int br_streq_prefix(const char* s, const char* prefix) {
    if (!s || !prefix) return 0;
    while (*prefix) { if (*s != *prefix) return 0; s++; prefix++; }
    return 1;
}

const char* my_strstr_ci(const char* hay, const char* needle) {
    if (!hay || !needle) return NULL;
    int nl = (int)strlen(needle);
    for (; *hay; hay++) {
        int i = 0;
        for (; i < nl; i++) {
            char a = hay[i], b = needle[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
        }
        if (i == nl) return hay;
    }
    return NULL;
}

const char* br_attr(const br_node_t* n, const char* name) {
    if (!n) return NULL;
    for (int i = 0; i < n->attr_count; i++) {
        if (br_strieq(n->attrs[i].name, name)) return n->attrs[i].value;
    }
    return NULL;
}

void br_set_attr(br_node_t* n, const char* name, const char* value) {
    if (!n) return;
    for (int i = 0; i < n->attr_count; i++) {
        if (br_strieq(n->attrs[i].name, name)) { n->attrs[i].value = br_strdup(value); return; }
    }
    if (n->attr_count < BR_MAX_ATTRS) {
        n->attrs[n->attr_count].name = br_strdup(name);
        n->attrs[n->attr_count].value = br_strdup(value);
        n->attr_count++;
    }
}

static int is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

static void to_lower(char* s) {
    for (; *s; s++) if (*s >= 'A' && *s <= 'Z') *s = (char)(*s + 32);
}

static int is_void_tag(const char* t) {
    static const char* v[] = {"br", "img", "hr", "input", "meta", "link", "area", "base",
                              "col", "embed", "param", "source", "track", "wbr", NULL};
    for (int i = 0; v[i]; i++) if (br_streq(t, v[i])) return 1;
    return 0;
}

int br_is_block_tag(const char* t) {
    static const char* b[] = {"html", "body", "div", "p", "h1", "h2", "h3", "h4", "h5", "h6", "dialog", "hgroup", "search", "menu",
                              "ul", "ol", "li", "table", "tr", "td", "th", "thead", "tbody",
                              "blockquote", "pre", "hr", "form", "section", "article",
                              "header", "footer", "nav", "main", "aside", "center", "dl",
                              "dt", "dd", "fieldset", "figure", "figcaption", "address",
                              "title", "head", "style", "script", "textarea", "select",
                              "option", "details", "summary", "caption", NULL};
    for (int i = 0; b[i]; i++) if (br_streq(t, b[i])) return 1;
    return 0;
}

/* Emit a code point as UTF-8 (the font engine renders UTF-8 directly). */
static int put_utf8(uint32_t cp, char* out) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) { out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) { out[0] = (char)(0xE0 | (cp >> 12)); out[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[2] = (char)(0x80 | (cp & 0x3F)); return 3; }
    if (cp < 0x110000) { out[0] = (char)(0xF0 | (cp >> 18)); out[1] = (char)(0x80 | ((cp >> 12) & 0x3F)); out[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[3] = (char)(0x80 | (cp & 0x3F)); return 4; }
    out[0] = '?'; return 1;
}

/* Entities: the common named ones plus numeric forms. Output is UTF-8. */
static int decode_entity(const char* s, int* consumed, char* out) {
    /* s points just after '&'. Returns bytes written to out. */
    static const struct { const char* name; uint32_t cp; } ents[] = {
        {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''},
        {"nbsp", 0xA0}, {"copy", 0xA9}, {"reg", 0xAE}, {"trade", 0x2122},
        {"mdash", 0x2014}, {"ndash", 0x2013}, {"hellip", 0x2026}, {"laquo", 0xAB},
        {"raquo", 0xBB}, {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"lsquo", 0x2018},
        {"rsquo", 0x2019}, {"bull", 0x2022}, {"middot", 0xB7}, {"times", 0xD7},
        {"euro", 0x20AC}, {"pound", 0xA3}, {"yen", 0xA5}, {"cent", 0xA2}, {"deg", 0xB0}, {"larr", 0x2190},
        {"rarr", 0x2192}, {"uarr", 0x2191}, {"darr", 0x2193}, {"hearts", 0x2665},
        {"check", 0x2713}, {"ensp", 0x2002}, {"emsp", 0x2003}, {"thinsp", 0x2009},
        {"sect", 0xA7}, {"para", 0xB6}, {"plusmn", 0xB1}, {"frac12", 0xBD}, {"frac14", 0xBC},
        {"micro", 0xB5}, {"divide", 0xF7}, {"ne", 0x2260}, {"le", 0x2264}, {"ge", 0x2265},
        {"minus", 0x2212}, {"shy", 0xAD}, {"zwj", 0x200D}, {"zwnj", 0x200C},
        {"eacute", 0xE9}, {"egrave", 0xE8}, {"ecirc", 0xEA}, {"agrave", 0xE0}, {"aacute", 0xE1}, {"acirc", 0xE2},
        {"auml", 0xE4}, {"ouml", 0xF6}, {"uuml", 0xFC}, {"Auml", 0xC4}, {"Ouml", 0xD6}, {"Uuml", 0xDC}, {"szlig", 0xDF},
        {"ccedil", 0xE7}, {"ntilde", 0xF1}, {"iacute", 0xED}, {"oacute", 0xF3}, {"uacute", 0xFA}, {"Eacute", 0xC9},
        {"aring", 0xE5}, {"oslash", 0xF8}, {"aelig", 0xE6}, {"iexcl", 0xA1}, {"iquest", 0xBF}, {"ordm", 0xBA}, {"ordf", 0xAA},
        {"dagger", 0x2020}, {"prime", 0x2032}, {"infin", 0x221E}, {"star", 0x2606}, {"starf", 0x2605},
        {"lrm", 0x200E}, {"rlm", 0x200F}, {"sbquo", 0x201A}, {"bdquo", 0x201E}, {"lsaquo", 0x2039}, {"rsaquo", 0x203A},
        {"permil", 0x2030}, {"loz", 0x25CA}, {"spades", 0x2660}, {"clubs", 0x2663}, {"diams", 0x2666}, {"harr", 0x2194},
        {"crarr", 0x21B5}, {"lArr", 0x21D0}, {"rArr", 0x21D2}, {"hArr", 0x21D4}, {"forall", 0x2200}, {"exist", 0x2203},
        {"empty", 0x2205}, {"nabla", 0x2207}, {"isin", 0x2208}, {"notin", 0x2209}, {"sum", 0x2211}, {"prod", 0x220F},
        {"radic", 0x221A}, {"prop", 0x221D}, {"and", 0x2227}, {"or", 0x2228}, {"cap", 0x2229}, {"cup", 0x222A}, {"int", 0x222B},
        {"there4", 0x2234}, {"sim", 0x223C}, {"cong", 0x2245}, {"asymp", 0x2248}, {"equiv", 0x2261}, {"sub", 0x2282}, {"sup", 0x2283},
        {"oplus", 0x2295}, {"otimes", 0x2297}, {"perp", 0x22A5}, {"sdot", 0x22C5}, {"lceil", 0x2308}, {"rceil", 0x2309},
        {"lfloor", 0x230A}, {"rfloor", 0x230B}, {"lang", 0x2329}, {"rang", 0x232A}, {"alpha", 0x3B1}, {"beta", 0x3B2}, {"gamma", 0x3B3},
        {"delta", 0x3B4}, {"epsilon", 0x3B5}, {"theta", 0x3B8}, {"lambda", 0x3BB}, {"mu", 0x3BC}, {"pi", 0x3C0}, {"sigma", 0x3C3},
        {"tau", 0x3C4}, {"phi", 0x3C6}, {"omega", 0x3C9}, {"Delta", 0x394}, {"Omega", 0x3A9}, {"Sigma", 0x3A3}, {"Pi", 0x3A0},
        {"uml", 0xA8}, {"macr", 0xAF}, {"acute", 0xB4}, {"cedil", 0xB8}, {"sup1", 0xB9}, {"sup2", 0xB2}, {"sup3", 0xB3}, {"frac34", 0xBE},
        {"not", 0xAC}, {"brvbar", 0xA6}, {"curren", 0xA4}, {"Agrave", 0xC0}, {"Aacute", 0xC1}, {"Acirc", 0xC2}, {"Atilde", 0xC3}, {"Aring", 0xC5},
        {"AElig", 0xC6}, {"Ccedil", 0xC7}, {"Egrave", 0xC8}, {"Ecirc", 0xCA}, {"Euml", 0xCB}, {"Igrave", 0xCC}, {"Iacute", 0xCD}, {"Icirc", 0xCE},
        {"Iuml", 0xCF}, {"ETH", 0xD0}, {"Ntilde", 0xD1}, {"Ograve", 0xD2}, {"Oacute", 0xD3}, {"Ocirc", 0xD4}, {"Otilde", 0xD5}, {"Oslash", 0xD8},
        {"Ugrave", 0xD9}, {"Uacute", 0xDA}, {"Ucirc", 0xDB}, {"Yacute", 0xDD}, {"THORN", 0xDE}, {"atilde", 0xE3}, {"euml", 0xEB}, {"igrave", 0xEC},
        {"icirc", 0xEE}, {"iuml", 0xEF}, {"eth", 0xF0}, {"ograve", 0xF2}, {"ocirc", 0xF4}, {"otilde", 0xF5}, {"ugrave", 0xF9}, {"ucirc", 0xFB},
        {"yacute", 0xFD}, {"thorn", 0xFE}, {"yuml", 0xFF}, {"OElig", 0x152}, {"oelig", 0x153}, {"Scaron", 0x160}, {"scaron", 0x161}, {"Yuml", 0x178},
        {"fnof", 0x192}, {"circ", 0x2C6}, {"tilde", 0x2DC}, {"zwsp", 0x200B}, {"NewLine", '\n'}, {"Tab", '\t'}, {"nbsp", 0xA0}, {"emsp13", 0x2004},
        {"numsp", 0x2007}, {"puncsp", 0x2008}, {"hairsp", 0x200A}, {"ZeroWidthSpace", 0x200B}, {"NoBreak", 0x2060}, {"colon", ':'}, {"comma", ','},
        {"period", '.'}, {"excl", '!'}, {"quest", '?'}, {"num", '#'}, {"dollar", '$'}, {"percnt", '%'}, {"lpar", '('}, {"rpar", ')'}, {"ast", '*'},
        {"plus", '+'}, {"sol", '/'}, {"semi", ';'}, {"equals", '='}, {"commat", '@'}, {"lsqb", '['}, {"bsol", '\\'}, {"rsqb", ']'}, {"Hat", '^'},
        {"lowbar", '_'}, {"grave", '`'}, {"lcub", '{'}, {"verbar", '|'}, {"rcub", '}'}, {"vert", '|'}, {"hyphen", 0x2010}, {"dash", 0x2010},
        {"horbar", 0x2015}, {"Vert", 0x2016}, {"caret", 0x2041}, {"tprime", 0x2034}, {"bprime", 0x2035}, {"oline", 0x203E}, {"frasl", 0x2044},
        {"weierp", 0x2118}, {"image", 0x2111}, {"real", 0x211C}, {"alefsym", 0x2135}, {"phone", 0x260E}, {"female", 0x2640}, {"male", 0x2642},
        {"sung", 0x266A}, {"flat", 0x266D}, {"natural", 0x266E}, {"sharp", 0x266F}, {"checkmark", 0x2713}, {"cross", 0x2717}, {"malt", 0x2720},
        {"sext", 0x2736}, {"VerticalSeparator", 0x2758}, {"lbbrk", 0x2772}, {"rbbrk", 0x2773}, {"bull", 0x2022}, {"bullet", 0x2022}, {"nbsp", 0xA0},
        {"quot", '"'}, {"QUOT", '"'}, {"AMP", '&'}, {"LT", '<'}, {"GT", '>'}, {"COPY", 0xA9}, {"REG", 0xAE}, {"TRADE", 0x2122},
        {NULL, 0}
    };
    if (s[0] == '#') {
        uint32_t cp = 0; int i = 1;
        if (s[1] == 'x' || s[1] == 'X') {
            i = 2;
            while ((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f') || (s[i] >= 'A' && s[i] <= 'F')) {
                char c = s[i];
                cp = cp * 16 + (uint32_t)((c <= '9') ? c - '0' : ((c | 32) - 'a' + 10));
                if (cp > 0x10FFFF) cp = 0xFFFD;
                i++;
            }
        } else {
            while (s[i] >= '0' && s[i] <= '9') { cp = cp * 10 + (uint32_t)(s[i] - '0'); if (cp > 0x10FFFF) cp = 0xFFFD; i++; }
        }
        if (s[i] == ';') i++;
        *consumed = i;
        if (cp == 0 || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
        if (cp < 32 && cp != '\t' && cp != '\n') cp = ' ';
        /* Windows-1252 leftovers: &#150; etc. */
        if (cp >= 0x80 && cp <= 0x9F) {
            static const uint16_t cp1252[32] = { 0x20AC, 0x81, 0x201A, 0x192, 0x201E, 0x2026, 0x2020, 0x2021, 0x2C6, 0x2030, 0x160, 0x2039, 0x152, 0x8D, 0x17D, 0x8F,
                                                 0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x2DC, 0x2122, 0x161, 0x203A, 0x153, 0x9D, 0x17E, 0x178 };
            cp = cp1252[cp - 0x80];
        }
        return put_utf8(cp, out);
    }
    for (int e = 0; ents[e].name; e++) {
        int n = (int)strlen(ents[e].name);
        int match = 1;
        for (int i = 0; i < n; i++) if (s[i] != ents[e].name[i]) { match = 0; break; }
        if (match && (s[n] == ';' || !((s[n] >= 'a' && s[n] <= 'z') || (s[n] >= 'A' && s[n] <= 'Z') || (s[n] >= '0' && s[n] <= '9')))) {
            *consumed = n + (s[n] == ';' ? 1 : 0);
            return put_utf8(ents[e].cp, out);
        }
    }
    /* Unknown: keep the ampersand literally. */
    *consumed = 0;
    out[0] = '&';
    return 1;
}

/* UTF-8 passes through untouched (validated); stray bytes from Latin-1
 * pages are re-encoded so the renderer never sees malformed sequences. */
static int utf8_fold(const unsigned char* s, int* consumed, char* out) {
    unsigned char c = s[0];
    if (c < 0x80) { *consumed = 1; out[0] = (char)c; return 1; }
    int len = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 0;
    if (len == 0) { *consumed = 1; return put_utf8(c, out); }          /* lone continuation byte: Latin-1 */
    for (int i = 1; i < len; i++) {
        if ((s[i] & 0xC0) != 0x80) { *consumed = 1; return put_utf8(c, out); }   /* invalid sequence: treat as Latin-1 */
    }
    *consumed = len;
    for (int i = 0; i < len; i++) out[i] = (char)s[i];
    return len;
}

/* Decode a raw text run (entities + UTF-8) into the text pool as UTF-8. When
 * `collapse` is set runs of whitespace fold to one space (normal HTML);
 * <pre> keeps them. */
char* br_decode_text(const char* s, int n, int collapse) {
    static char tmp[BR_MAX_TEXT_RUN];
    int o = 0;
    int prev_space = 0;
    for (int i = 0; i < n && o < BR_MAX_TEXT_RUN - 4;) {
        char c = s[i];
        if (collapse && is_space(c)) {
            if (!prev_space) { tmp[o++] = ' '; prev_space = 1; }
            i++;
            continue;
        }
        prev_space = 0;
        if (c == '&') {
            int used = 0;
            int w = decode_entity(&s[i + 1], &used, &tmp[o]);
            o += w;
            i += 1 + used;
            continue;
        }
        if ((unsigned char)c >= 0x80) {
            int used = 1;
            int w = utf8_fold((const unsigned char*)&s[i], &used, &tmp[o]);
            o += w; i += used;
            continue;
        }
        if (c == '\r') { i++; continue; }
        if (c == '\t' && !collapse) { tmp[o++] = ' '; tmp[o++] = ' '; tmp[o++] = ' '; tmp[o++] = ' '; i++; continue; }
        tmp[o++] = c;
        i++;
    }
    tmp[o] = 0;
    return br_strdup_n(tmp, o);
}

/* ------------------------------------------------------------- parser */

typedef struct {
    const char* src;
    int len;
    int pos;
    br_node_t* stack[BR_MAX_DEPTH];
    int depth;
    br_node_t* doc;
    int pre_depth;
} parser_t;

static br_node_t* cur(parser_t* p) { return p->stack[p->depth - 1]; }

static void push(parser_t* p, br_node_t* n) {
    if (p->depth < BR_MAX_DEPTH) p->stack[p->depth++] = n;
}

static void pop_to(parser_t* p, const char* tag) {
    /* Close the innermost open element with this tag (and everything above). */
    for (int i = p->depth - 1; i >= 1; i--) {
        if (br_streq(p->stack[i]->tag, tag)) { p->depth = i; return; }
    }
}

static int has_open(parser_t* p, const char* tag) {
    for (int i = p->depth - 1; i >= 1; i--) if (br_streq(p->stack[i]->tag, tag)) return 1;
    return 0;
}
/* like has_open, but the search stops at an enclosing table scope so a
 * <tr> inside a nested table never closes the outer table's row */
static int has_open_in_table(parser_t* p, const char* tag) {
    for (int i = p->depth - 1; i >= 1; i--) {
        const char* t = p->stack[i]->tag;
        if (br_streq(t, tag)) return 1;
        if (br_streq(t, "table")) return 0;
        if (!br_streq(tag, "tr") && br_streq(t, "tr")) return 0;      /* td/th: stop at the row */
    }
    return 0;
}

static void flush_text(parser_t* p, int start, int end) {
    if (end <= start) return;
    int collapse = p->pre_depth == 0;
    /* Skip whitespace-only runs between block elements: they would otherwise
     * become empty lines. Inline whitespace is kept by the layout engine. */
    int all_space = 1;
    for (int i = start; i < end; i++) if (!is_space(p->src[i])) { all_space = 0; break; }
    br_node_t* parent = cur(p);
    if (all_space && collapse) {
        /* Keep a single space only after an inline element (it separates
         * "<b>bold</b> word"); whitespace between blocks is dropped. */
        if (!parent->last_child || parent->last_child->type != BR_NODE_ELEMENT ||
            br_is_block_tag(parent->last_child->tag)) return;
        if (br_is_block_tag(parent->tag) && !parent->last_child) return;
        /* if the next thing is a closing tag of a block we'd add a trailing
         * space: harmless (dropped at line end by layout). */
    }
    char* text = br_decode_text(&p->src[start], end - start, collapse);
    if (!text[0]) return;
    br_node_t* t = br_node_new(BR_NODE_TEXT);
    if (!t) return;
    t->text = text;
    br_node_append(parent, t);
}

static void implied_end_tags(parser_t* p, const char* tag) {
    /* <p> closes an open <p>; <li> closes an open <li>; <tr>/<td> likewise;
     * <option> closes <option>; block-level tags close an open <p>. */
    if (br_streq(tag, "li")) { if (has_open(p, "li")) { pop_to(p, "li"); } return; }
    if (br_streq(tag, "dt") || br_streq(tag, "dd")) {
        if (has_open(p, "dd")) pop_to(p, "dd"); else if (has_open(p, "dt")) pop_to(p, "dt");
        return;
    }
    if (br_streq(tag, "tr")) { if (has_open_in_table(p, "tr")) pop_to(p, "tr"); return; }
    if (br_streq(tag, "td") || br_streq(tag, "th")) {
        if (has_open_in_table(p, "td")) pop_to(p, "td"); else if (has_open_in_table(p, "th")) pop_to(p, "th");
        return;
    }
    if (br_streq(tag, "option")) { if (has_open(p, "option")) pop_to(p, "option"); return; }
    if (br_is_block_tag(tag) && !br_streq(tag, "html") && !br_streq(tag, "body") && !br_streq(tag, "head")) {
        /* Only close a <p> if it is the innermost block context. */
        for (int i = p->depth - 1; i >= 1; i--) {
            if (br_streq(p->stack[i]->tag, "p")) { p->depth = i; break; }
            if (br_is_block_tag(p->stack[i]->tag)) break;
        }
    }
}

static void parse_attrs(parser_t* p, br_node_t* n) {
    const char* s = p->src;
    while (p->pos < p->len) {
        while (p->pos < p->len && is_space(s[p->pos])) p->pos++;
        if (p->pos >= p->len) return;
        if (s[p->pos] == '>' ) return;
        if (s[p->pos] == '/') { p->pos++; continue; }
        int ns = p->pos;
        while (p->pos < p->len && !is_space(s[p->pos]) && s[p->pos] != '=' && s[p->pos] != '>' && s[p->pos] != '/') p->pos++;
        int ne = p->pos;
        while (p->pos < p->len && is_space(s[p->pos])) p->pos++;
        char* name = br_strdup_n(&s[ns], ne - ns);
        to_lower(name);
        char* value = "";
        if (p->pos < p->len && s[p->pos] == '=') {
            p->pos++;
            while (p->pos < p->len && is_space(s[p->pos])) p->pos++;
            if (p->pos < p->len && (s[p->pos] == '"' || s[p->pos] == '\'')) {
                char q = s[p->pos++];
                int vs = p->pos;
                while (p->pos < p->len && s[p->pos] != q) p->pos++;
                value = br_decode_text(&s[vs], p->pos - vs, 0);
                if (p->pos < p->len) p->pos++;
            } else {
                int vs = p->pos;
                while (p->pos < p->len && !is_space(s[p->pos]) && s[p->pos] != '>') p->pos++;
                value = br_decode_text(&s[vs], p->pos - vs, 0);
            }
        }
        if (ne > ns && n->attr_count < BR_MAX_ATTRS) {
            n->attrs[n->attr_count].name = name;
            n->attrs[n->attr_count].value = value;
            n->attr_count++;
        }
    }
}

static int match_ci(const char* s, int len, const char* lit) {
    int n = (int)strlen(lit);
    if (len < n) return 0;
    for (int i = 0; i < n; i++) {
        char a = s[i], b = lit[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (a != b) return 0;
    }
    return 1;
}

br_node_t* br_html_parse(const char* html, int len) {
    parser_t P;
    parser_t* p = &P;
    memset(p, 0, sizeof(*p));
    p->src = html; p->len = len;

    br_node_t* doc = br_node_new(BR_NODE_DOCUMENT);
    if (!doc) return NULL;
    doc->tag = "#document";
    p->doc = doc;
    push(p, doc);

    /* Implied <html><body>: created lazily so <head> content ends up in the
     * right place but pages without any of it still work. */
    br_node_t* html_el = br_node_new(BR_NODE_ELEMENT); html_el->tag = "html"; br_node_append(doc, html_el);
    br_node_t* head_el = br_node_new(BR_NODE_ELEMENT); head_el->tag = "head"; br_node_append(html_el, head_el);
    br_node_t* body_el = br_node_new(BR_NODE_ELEMENT); body_el->tag = "body"; br_node_append(html_el, body_el);
    push(p, html_el);
    push(p, body_el);
    doc->body = body_el;
    doc->head = head_el;

    int text_start = 0;
    const char* s = html;
    while (p->pos < len) {
        if (s[p->pos] != '<') { p->pos++; continue; }
        /* Comment / doctype / CDATA */
        if (p->pos + 3 < len && s[p->pos + 1] == '!') {
            flush_text(p, text_start, p->pos);
            if (s[p->pos + 2] == '-' && s[p->pos + 3] == '-') {
                int e = p->pos + 4;
                while (e + 2 < len && !(s[e] == '-' && s[e + 1] == '-' && s[e + 2] == '>')) e++;
                p->pos = e + 3;
            } else {
                while (p->pos < len && s[p->pos] != '>') p->pos++;
                p->pos++;
            }
            text_start = p->pos;
            continue;
        }
        /* End tag */
        if (p->pos + 1 < len && s[p->pos + 1] == '/') {
            flush_text(p, text_start, p->pos);
            p->pos += 2;
            int ts = p->pos;
            while (p->pos < len && s[p->pos] != '>' && !is_space(s[p->pos])) p->pos++;
            char* tag = br_strdup_n(&s[ts], p->pos - ts);
            to_lower(tag);
            while (p->pos < len && s[p->pos] != '>') p->pos++;
            p->pos++;
            text_start = p->pos;
            if (br_streq(tag, "pre") && p->pre_depth > 0) p->pre_depth--;
            if (br_streq(tag, "body") || br_streq(tag, "html")) continue;
            if (br_streq(tag, "p") && !has_open(p, "p")) {
                /* Stray </p> creates an empty paragraph in browsers; ignore. */
                continue;
            }
            pop_to(p, tag);
            /* Text after </head> belongs to body. */
            if (br_streq(tag, "head")) { p->depth = 2; push(p, body_el); }
            continue;
        }
        /* Start tag: must be followed by a letter, otherwise it is text. */
        char c1 = (p->pos + 1 < len) ? s[p->pos + 1] : 0;
        if (!((c1 >= 'a' && c1 <= 'z') || (c1 >= 'A' && c1 <= 'Z'))) { p->pos++; continue; }
        flush_text(p, text_start, p->pos);
        p->pos++;
        int ts = p->pos;
        while (p->pos < len && !is_space(s[p->pos]) && s[p->pos] != '>' && s[p->pos] != '/') p->pos++;
        char* tag = br_strdup_n(&s[ts], p->pos - ts);
        to_lower(tag);

        br_node_t* n = br_node_new(BR_NODE_ELEMENT);
        if (!n) break;
        n->tag = tag;
        parse_attrs(p, n);
        int self_closing = 0;
        if (p->pos > 0 && s[p->pos - 1] == '/') self_closing = 1;
        if (self_closing && !is_void_tag(tag)) {
            /* HTML5 ignores the slash on ordinary elements (<tr/> opens a
             * row); it is only honoured for unknown/custom tags, where it
             * is usually meant */
            static const char* const plain[] = { "tr", "td", "th", "div", "span", "p", "a", "li", "ul", "ol", "table", "tbody", "thead", "tfoot",
                "section", "article", "nav", "header", "footer", "main", "aside", "b", "i", "em", "strong", "small", "label", "form",
                "h1", "h2", "h3", "h4", "h5", "h6", "button", "select", "option", "iframe", "textarea", "script", "style", "title", NULL };
            for (int k = 0; plain[k]; k++) if (br_streq(tag, plain[k])) { self_closing = 0; break; }
        }
        /* parse_attrs stops at '>' */
        if (p->pos < len && s[p->pos] == '>') p->pos++;
        text_start = p->pos;

        /* Where does it go? */
        if (br_streq(tag, "html")) {
            /* merge attributes (lang etc.) and drop */
            for (int i = 0; i < n->attr_count; i++) br_set_attr(html_el, n->attrs[i].name, n->attrs[i].value);
            continue;
        }
        if (br_streq(tag, "head")) { p->depth = 2; push(p, head_el); continue; }
        if (br_streq(tag, "body")) {
            for (int i = 0; i < n->attr_count; i++) br_set_attr(body_el, n->attrs[i].name, n->attrs[i].value);
            p->depth = 2; push(p, body_el);
            continue;
        }
        int head_only = br_streq(tag, "title") || br_streq(tag, "meta") || br_streq(tag, "link") ||
                        br_streq(tag, "style") || br_streq(tag, "base");
        if (head_only && cur(p) == body_el && !body_el->first_child) {
            /* A <title>/<style> before any content, without <head>: file it
             * under head like a browser would. */
            br_node_append(head_el, n);
        } else {
            implied_end_tags(p, tag);
            br_node_append(cur(p), n);
        }

        /* <template> content is inert, inline <svg> is not rendered (it only
         * keeps its box) and <math>: skip their subtrees entirely so their
         * text never leaks into the page and they cost no nodes. */
        if (br_streq(tag, "template") || br_streq(tag, "svg") || br_streq(tag, "math")) {
            if (self_closing) continue;
            int depth = 1, e = p->pos;
            int tl = (int)strlen(tag);
            while (e < len && depth > 0) {
                if (s[e] == '<') {
                    if (s[e + 1] == '/' && match_ci(&s[e + 2], len - e - 2, tag) && (s[e + 2 + tl] == '>' || is_space(s[e + 2 + tl]))) depth--;
                    else if (match_ci(&s[e + 1], len - e - 1, tag) && (s[e + 1 + tl] == '>' || is_space(s[e + 1 + tl]) || s[e + 1 + tl] == '/')) {
                        /* nested same tag (unless self-closing) */
                        int q = e + 1; while (q < len && s[q] != '>') q++;
                        if (!(q > 0 && s[q - 1] == '/')) depth++;
                    }
                }
                e++;
            }
            while (e < len && s[e] != '>') e++;
            p->pos = e + 1;
            text_start = p->pos;
            continue;
        }
        /* Raw text elements swallow everything up to their end tag. */
        if (br_streq(tag, "script") || br_streq(tag, "style") || br_streq(tag, "textarea") || br_streq(tag, "title")) {
            int e = p->pos;
            char endtag[16];
            endtag[0] = '<'; endtag[1] = '/';
            int tl = (int)strlen(tag);
            for (int i = 0; i < tl; i++) endtag[2 + i] = tag[i];
            endtag[2 + tl] = 0;
            while (e < len && !match_ci(&s[e], len - e, endtag)) e++;
            if (e > p->pos) {
                br_node_t* t = br_node_new(BR_NODE_TEXT);
                if (t) {
                    if (br_streq(tag, "script") || br_streq(tag, "style")) t->text = br_strdup_n(&s[p->pos], e - p->pos);
                    else t->text = br_decode_text(&s[p->pos], e - p->pos, 0);
                    br_node_append(n, t);
                }
            }
            p->pos = e;
            while (p->pos < len && s[p->pos] != '>') p->pos++;
            p->pos++;
            text_start = p->pos;
            continue;
        }

        if (is_void_tag(tag) || self_closing) continue;
        if (br_streq(tag, "pre")) p->pre_depth++;
        push(p, n);
    }
    flush_text(p, text_start, len);
    return doc;
}

/* Parse a fragment (innerHTML) into an existing element. */
void br_html_parse_fragment(br_node_t* parent, const char* html, int len) {
    br_node_remove_children(parent);
    br_node_t* tmp = br_html_parse(html, len);
    if (!tmp || !tmp->body) return;
    /* Move body children (and any head children: <style> in innerHTML) */
    br_node_t* c = tmp->body->first_child;
    while (c) { br_node_t* nx = c->next; br_node_append(parent, c); c = nx; }
    c = tmp->head->first_child;
    while (c) { br_node_t* nx = c->next; br_node_append(parent, c); c = nx; }
}

/* Collect textContent. */
int br_node_text_content(const br_node_t* n, char* out, int max) {
    int o = 0;
    if (!n || br_stack_headroom() < BR_STACK_MIN) { if (max) out[0] = 0; return 0; }
    if (n->type == BR_NODE_TEXT) {
        for (int i = 0; n->text[i] && o < max - 1; i++) out[o++] = n->text[i];
        out[o] = 0;
        return o;
    }
    for (br_node_t* c = n->first_child; c && o < max - 1; c = c->next) {
        o += br_node_text_content(c, out + o, max - o);
    }
    out[o] = 0;
    return o;
}

/* Serialize back to HTML (innerHTML getter). */
static int ser(const br_node_t* n, char* out, int max, int o) {
    if (!n || o >= max - 1 || br_stack_headroom() < BR_STACK_MIN) return o;
    if (n->type == BR_NODE_TEXT) {
        for (int i = 0; n->text[i] && o < max - 1; i++) out[o++] = n->text[i];
        return o;
    }
    if (n->type == BR_NODE_ELEMENT) {
        if (o < max - 1) out[o++] = '<';
        for (int i = 0; n->tag[i] && o < max - 1; i++) out[o++] = n->tag[i];
        for (int a = 0; a < n->attr_count && o < max - 4; a++) {
            out[o++] = ' ';
            for (int i = 0; n->attrs[a].name[i] && o < max - 1; i++) out[o++] = n->attrs[a].name[i];
            out[o++] = '='; out[o++] = '"';
            for (int i = 0; n->attrs[a].value[i] && o < max - 1; i++) out[o++] = n->attrs[a].value[i];
            if (o < max - 1) out[o++] = '"';
        }
        if (o < max - 1) out[o++] = '>';
        if (is_void_tag(n->tag)) return o;
    }
    for (const br_node_t* c = n->first_child; c; c = c->next) o = ser(c, out, max, o);
    if (n->type == BR_NODE_ELEMENT && o < max - 3) {
        out[o++] = '<'; out[o++] = '/';
        for (int i = 0; n->tag[i] && o < max - 1; i++) out[o++] = n->tag[i];
        if (o < max - 1) out[o++] = '>';
    }
    return o;
}

int br_node_inner_html(const br_node_t* n, char* out, int max) {
    int o = 0;
    for (const br_node_t* c = n ? n->first_child : NULL; c; c = c->next) o = ser(c, out, max, o);
    out[o < max ? o : max - 1] = 0;
    return o;
}

br_node_t* br_find_by_id(br_node_t* n, const char* id) {
    if (!n || br_stack_headroom() < BR_STACK_MIN) return NULL;
    if (n->type == BR_NODE_ELEMENT) {
        const char* v = br_attr(n, "id");
        if (v && br_streq(v, id)) return n;
    }
    for (br_node_t* c = n->first_child; c; c = c->next) {
        br_node_t* r = br_find_by_id(c, id);
        if (r) return r;
    }
    return NULL;
}

int br_has_class(const br_node_t* n, const char* cls) {
    const char* v = br_attr(n, "class");
    if (!v) return 0;
    int cl = (int)strlen(cls);
    while (*v) {
        while (*v == ' ') v++;
        const char* st = v;
        while (*v && *v != ' ') v++;
        if ((int)(v - st) == cl) {
            int m = 1;
            for (int i = 0; i < cl; i++) if (st[i] != cls[i]) { m = 0; break; }
            if (m) return 1;
        }
    }
    return 0;
}

br_node_t* br_find_first(br_node_t* n, const char* tag) {
    if (br_stack_headroom() < BR_STACK_MIN) return NULL;
    if (!n) return NULL;
    if (n->type == BR_NODE_ELEMENT && br_streq(n->tag, tag)) return n;
    for (br_node_t* c = n->first_child; c; c = c->next) {
        br_node_t* r = br_find_first(c, tag);
        if (r) return r;
    }
    return NULL;
}
