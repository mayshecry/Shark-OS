/* CSS for the SharkOS browser: a UA stylesheet, <style> sheets, style=""
 * attributes, and the cascade (specificity + source order) producing one
 * computed br_style_t per element. */

#include "browser_internal.h"

static br_rule_t rules[BR_MAX_RULES];
static int rule_count = 0;
static int rule_order = 0;
static int fontface_count;
static int import_count;
static int parse_length(const char* s, int base_px, int* ok);

/* The UA stylesheet is expressed as CSS text so it goes through the same
 * parser as author styles. Parsed once per document (cheap). */
static const char ua_sheet[] =
    "html,body{display:block} head,script,style,title,meta,link,template{display:none} svg{display:inline-block}"
    "body{margin:8px;color:#000;background:#fff;font-size:16px;font-family:sans-serif;line-height:normal}"
    "div,p,h1,h2,h3,h4,h5,h6,ul,ol,li,pre,blockquote,form,section,article,header,footer,nav,main,"
    "aside,center,dl,dt,dd,fieldset,figure,figcaption,address,details,summary,hr,table,tr,td,th,"
    "thead,tbody,caption,textarea,select,option{display:block}"
    "table{display:table} tr{display:table-row} td,th{display:table-cell;padding:2px 4px}"
    "th{font-weight:bold;text-align:center}"
    "li{display:list-item}"
    "p{margin:1em 0} h1{font-size:2em;font-weight:bold;margin:0.67em 0}"
    "h2{font-size:1.5em;font-weight:bold;margin:0.83em 0} h3{font-weight:bold;font-size:1.17em;margin:1em 0}"
    "h4{font-weight:bold;margin:1.33em 0} h5{font-weight:bold;font-size:0.83em;margin:1.67em 0} h6{font-weight:bold;font-size:0.67em;margin:2.33em 0}"
    "ul,ol{margin:8px 0;padding-left:24px} ol{list-style:decimal} ul ul{margin:0}"
    "blockquote{margin:8px 24px;padding-left:6px;border-left:2px solid #808080;color:#303030}"
    "pre{font-family:monospace;font-size:13px;margin:1em 0;padding:4px;white-space:pre}"
    "code,kbd,samp,tt{font-family:monospace;font-size:0.9em}"
    "b,strong{font-weight:bold} i,em,cite,var,dfn{font-style:italic}"
    "u,ins{text-decoration:underline} s,strike,del{text-decoration:line-through}"
    "a{color:#0000ee;text-decoration:underline} a:hover{color:#ee0000}"
    "center{text-align:center} hr{margin:8px 0;border:1px solid #808080}"
    "small{font-size:smaller} big{font-size:larger} mark{background:#ffff00}"
    "button,input,select,textarea{display:inline-block;background:#c0c0c0;padding:2px 6px;font-size:13px;font-family:sans-serif}"
    "fieldset{border:1px solid #808080;padding:6px;margin:6px 0}"
    "dd{margin-left:24px} sup,sub{font-size:80%}"
    "figure{margin:8px 24px} summary{font-weight:bold}";

/* <noscript> is hidden while scripting is on; the browser flips this on
 * when a page ends up empty without JavaScript (see parse_and_show). */
static int noscript_visible = 0;
void br_css_set_noscript(int on) { noscript_visible = on; }
int br_css_noscript_visible(void) { return noscript_visible; }

void br_css_reset(void) {
    rule_count = 0;
    rule_order = 0;
    fontface_count = 0;
    import_count = 0;
    br_css_parse_sheet(ua_sheet, (int)sizeof(ua_sheet) - 1);
}

/* ------------------------------------------------------------ colours */

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static const struct { const char* name; uint32_t rgb; } named_colors[] = {
    {"black", 0x000000}, {"white", 0xFFFFFF}, {"red", 0xFF0000}, {"green", 0x008000},
    {"blue", 0x0000FF}, {"yellow", 0xFFFF00}, {"gray", 0x808080}, {"grey", 0x808080},
    {"silver", 0xC0C0C0}, {"maroon", 0x800000}, {"navy", 0x000080}, {"teal", 0x008080},
    {"purple", 0x800080}, {"olive", 0x808000}, {"lime", 0x00FF00}, {"aqua", 0x00FFFF},
    {"cyan", 0x00FFFF}, {"fuchsia", 0xFF00FF}, {"magenta", 0xFF00FF}, {"orange", 0xFFA500},
    {"pink", 0xFFC0CB}, {"brown", 0xA52A2A}, {"gold", 0xFFD700}, {"coral", 0xFF7F50},
    {"crimson", 0xDC143C}, {"darkblue", 0x00008B}, {"darkgreen", 0x006400}, {"darkred", 0x8B0000},
    {"darkgray", 0xA9A9A9}, {"darkgrey", 0xA9A9A9}, {"lightgray", 0xD3D3D3}, {"lightgrey", 0xD3D3D3},
    {"lightblue", 0xADD8E6}, {"lightgreen", 0x90EE90}, {"lightyellow", 0xFFFFE0}, {"skyblue", 0x87CEEB},
    {"steelblue", 0x4682B4}, {"royalblue", 0x4169E1}, {"dodgerblue", 0x1E90FF}, {"tomato", 0xFF6347},
    {"salmon", 0xFA8072}, {"khaki", 0xF0E68C}, {"beige", 0xF5F5DC}, {"ivory", 0xFFFFF0},
    {"tan", 0xD2B48C}, {"chocolate", 0xD2691E}, {"indigo", 0x4B0082}, {"violet", 0xEE82EE},
    {"orchid", 0xDA70D6}, {"turquoise", 0x40E0D0}, {"seagreen", 0x2E8B57}, {"forestgreen", 0x228B22},
    {"firebrick", 0xB22222}, {"slategray", 0x708090}, {"dimgray", 0x696969}, {"whitesmoke", 0xF5F5F5},
    {"gainsboro", 0xDCDCDC}, {"aliceblue", 0xF0F8FF}, {"lavender", 0xE6E6FA}, {"mintcream", 0xF5FFFA},
    {"honeydew", 0xF0FFF0}, {"linen", 0xFAF0E6}, {"wheat", 0xF5DEB3}, {"plum", 0xDDA0DD},
    {"rebeccapurple", 0x663399}, {"transparent", 0xFF000000}, {NULL, 0}
};

static int parse_num(const char** pp, int* out) {
    const char* p = *pp;
    while (*p == ' ') p++;
    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    if (!(*p >= '0' && *p <= '9') && *p != '.') return 0;
    int v = 0;
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
    if (*p == '.') { p++; while (*p >= '0' && *p <= '9') p++; }
    *out = neg ? -v : v;
    *pp = p;
    return 1;
}

uint32_t br_css_parse_color(const char* s, int* ok) {
    *ok = 1;
    while (*s == ' ') s++;
    if (*s == '#') {
        s++;
        int n = 0;
        while (hexval(s[n]) >= 0) n++;
        if (n == 3 || n == 4) {
            int r = hexval(s[0]), g = hexval(s[1]), b = hexval(s[2]);
            return 0xFF000000u | (uint32_t)((r * 17) << 16) | (uint32_t)((g * 17) << 8) | (uint32_t)(b * 17);
        }
        if (n == 6 || n == 8) {
            uint32_t v = 0;
            for (int i = 0; i < 6; i++) v = (v << 4) | (uint32_t)hexval(s[i]);
            return 0xFF000000u | v;
        }
        *ok = 0; return 0;
    }
    if ((s[0] == 'r' || s[0] == 'R') && (s[1] == 'g' || s[1] == 'G') && (s[2] == 'b' || s[2] == 'B')) {
        const char* p = s + 3;
        if (*p == 'a' || *p == 'A') p++;
        while (*p == ' ') p++;
        if (*p != '(') { *ok = 0; return 0; }
        p++;
        int c[3];
        for (int i = 0; i < 3; i++) {
            if (!parse_num(&p, &c[i])) { *ok = 0; return 0; }
            if (*p == '%') { c[i] = c[i] * 255 / 100; p++; }
            while (*p == ' ' || *p == ',' || *p == '/') p++;
            if (c[i] < 0) c[i] = 0;
            if (c[i] > 255) c[i] = 255;
        }
        /* alpha: treat 0 as transparent, anything else as opaque */
        int a = 1;
        if ((*p >= '0' && *p <= '9') || *p == '.') {
            if (*p == '0' && (p[1] == ')' || p[1] == ' ' || p[1] == 0)) a = 0;
            if (*p == '.' ) a = 1;
        }
        if (!a) return 0x00000000u;
        return 0xFF000000u | (uint32_t)(c[0] << 16) | (uint32_t)(c[1] << 8) | (uint32_t)c[2];
    }
    char name[24];
    int n = 0;
    while (s[n] && s[n] != ' ' && s[n] != ';' && n < 23) { name[n] = (char)((s[n] >= 'A' && s[n] <= 'Z') ? s[n] + 32 : s[n]); n++; }
    name[n] = 0;
    for (int i = 0; named_colors[i].name; i++) {
        if (br_streq(named_colors[i].name, name)) {
            if (named_colors[i].rgb == 0xFF000000u) return 0x00000000u;   /* transparent */
            return 0xFF000000u | named_colors[i].rgb;
        }
    }
    *ok = 0;
    return 0;
}

/* Lengths -> px. em is relative to em_px (the element's font size, or the
 * parent's when computing font-size itself); % of base_px; rem of 16px. */
static int root_font_px = 16;
static int parse_length_em(const char* s, int base_px, int em_px, int* ok) {
    const char* p = s;
    int v;
    *ok = 1;
    while (*p == ' ') p++;
    if (br_streq_prefix(p, "calc(")) {
        /* calc(A op B ...): evaluate a single +/- chain of lengths (no nesting, no * /) */
        p += 5;
        int total = 0, sign = 1, any = 0;
        while (*p && *p != ')') {
            while (*p == ' ') p++;
            if (*p == '+') { sign = 1; p++; continue; }
            if (*p == '-' && (p[1] == ' ')) { sign = -1; p++; continue; }
            if (*p == '*' || *p == '/') { *ok = 0; return 0; }
            int o2; int l = parse_length_em(p, base_px, em_px, &o2);
            if (!o2) { *ok = 0; return 0; }
            total += sign * l; any = 1;
            while (*p && *p != ' ' && *p != ')' && *p != '+') p++;
        }
        if (!any) *ok = 0;
        return total;
    }
    if (br_streq(p, "auto") || br_streq(p, "inherit") || br_streq(p, "initial") || br_streq(p, "none") || br_streq(p, "unset")) { *ok = 0; return 0; }
    if (!parse_num(&p, &v)) {
        /* keywords */
        if (br_streq_prefix(p, "xx-small")) return 9;
        if (br_streq_prefix(p, "x-small")) return 10;
        if (br_streq_prefix(p, "smaller")) return em_px * 5 / 6;
        if (br_streq_prefix(p, "small")) return 13;
        if (br_streq_prefix(p, "medium")) return 16;
        if (br_streq_prefix(p, "larger")) return em_px * 6 / 5;
        if (br_streq_prefix(p, "large")) return 18;
        if (br_streq_prefix(p, "x-large")) return 24;
        if (br_streq_prefix(p, "xx-large")) return 32;
        if (br_streq_prefix(p, "thin")) return 1;
        if (br_streq_prefix(p, "thick")) return 3;
        *ok = 0; return 0;
    }
    /* fractional: re-read to get hundredths */
    const char* q = s;
    while (*q == ' ') q++;
    int neg = 0; if (*q == '-') { neg = 1; q++; } else if (*q == '+') q++;
    int whole = 0, frac = 0, fd = 0;
    while (*q >= '0' && *q <= '9') { whole = whole * 10 + (*q - '0'); q++; if (whole > 100000) whole = 100000; }
    if (*q == '.') { q++; while (*q >= '0' && *q <= '9') { if (fd < 2) { frac = frac * 10 + (*q - '0'); fd++; } q++; } }
    while (fd < 2) { frac *= 10; fd++; }
    int hundredths = whole * 100 + frac;
    if (neg) hundredths = -hundredths;
    if (p[0] == 'e' && p[1] == 'm') return (hundredths * em_px) / 100;
    if (p[0] == 'e' && p[1] == 'x') return (hundredths * em_px) / 200;
    if (p[0] == 'c' && p[1] == 'h') return (hundredths * em_px) / 200;
    if (p[0] == 'r' && p[1] == 'e' && p[2] == 'm') return (hundredths * root_font_px) / 100;
    if (p[0] == '%') return (hundredths * base_px) / 10000;
    if (p[0] == 'p' && p[1] == 't') return (hundredths * 4) / 300;
    if (p[0] == 'p' && p[1] == 'c') return (hundredths * 16) / 100;
    if (p[0] == 'i' && p[1] == 'n') return (hundredths * 96) / 100;
    if (p[0] == 'c' && p[1] == 'm') return (hundredths * 3780) / 10000;
    if (p[0] == 'm' && p[1] == 'm') return (hundredths * 378) / 10000;
    if (p[0] == 'v' && p[1] == 'w') return (hundredths * (brs.layout_width > 0 ? brs.layout_width : 1000)) / 10000;
    if (p[0] == 'v' && p[1] == 'h') return (hundredths * 600) / 10000;
    if (p[0] == 'v' && (p[1] == 'm')) return (hundredths * 600) / 10000;
    return hundredths / 100;   /* px or unitless */
}
static int parse_length(const char* s, int base_px, int* ok) { return parse_length_em(s, base_px, 16, ok); }

/* ---------------------------------------------------------- parsing */

static int is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

static char* trim_dup(const char* s, int n) {
    while (n > 0 && is_ws(*s)) { s++; n--; }
    while (n > 0 && is_ws(s[n - 1])) n--;
    return br_strdup_n(s, n);
}

static void lower_inplace(char* s) { for (; *s; s++) if (*s >= 'A' && *s <= 'Z') *s = (char)(*s + 32); }

/* Parse one compound selector like "div.note#x:hover[href^=http]" into a
 * part. Returns 0 when it contains something we cannot match (the rule is
 * then dropped, which is what a browser does with unknown selectors). */
static int parse_compound(const char* s, int n, br_selpart_t* part) {
    memset(part, 0, sizeof(*part));
    int i = 0;
    if (n == 0) return 0;
    if (s[0] == '*') i = 1;
    else if ((s[0] >= 'a' && s[0] <= 'z') || (s[0] >= 'A' && s[0] <= 'Z')) {
        int st = 0;
        while (i < n && s[i] != '.' && s[i] != '#' && s[i] != ':' && s[i] != '[') i++;
        char* t = br_strdup_n(&s[st], i - st);
        lower_inplace(t);
        part->tag = t;
    }
    char classes[128]; int cl = 0;
    while (i < n) {
        char k = s[i++];
        int st = i;
        if (k == '[') {
            while (i < n && s[i] != ']') i++;
            /* [name op "value"] */
            int e = i;
            int p = st;
            while (p < e && s[p] == ' ') p++;
            int ns = p;
            while (p < e && s[p] != '=' && s[p] != '^' && s[p] != '$' && s[p] != '*' && s[p] != '~' && s[p] != '|' && s[p] != ' ') p++;
            char* an = br_strdup_n(&s[ns], p - ns); lower_inplace(an);
            part->attr_name = an;
            while (p < e && s[p] == ' ') p++;
            if (p < e && s[p] != '=') { part->attr_op = s[p]; p++; if (part->attr_op == '|') part->attr_op = '^'; }
            if (p < e && s[p] == '=') {
                if (!part->attr_op) part->attr_op = '=';
                p++;
                while (p < e && s[p] == ' ') p++;
                char q = 0;
                if (p < e && (s[p] == '"' || s[p] == '\'')) { q = s[p]; p++; }
                int vs = p;
                while (p < e && (q ? s[p] != q : (s[p] != ' ' && s[p] != 'i' && s[p] != 's'))) p++;
                part->attr_value = br_strdup_n(&s[vs], p - vs);
            }
            if (i < n) i++;                                  /* ']' */
            continue;
        }
        if (k == ':' && i < n && s[i] == ':') return 0;      /* ::before etc.: never matches */
        while (i < n && s[i] != '.' && s[i] != '#' && s[i] != ':' && s[i] != '[') {
            if (s[i] == '(') { int d = 0; while (i < n) { if (s[i] == '(') d++; else if (s[i] == ')') { d--; if (d == 0) { i++; break; } } i++; } continue; }
            i++;
        }
        if (k == '.') {
            int l = i - st;
            if (cl + l + 1 < (int)sizeof(classes)) { if (cl) classes[cl++] = ' '; for (int z = 0; z < l; z++) classes[cl++] = s[st + z]; classes[cl] = 0; }
        }
        else if (k == '#') part->id = br_strdup_n(&s[st], i - st);
        else if (k == ':') {
            char ps[64]; int l = i - st; if (l > 63) l = 63;
            for (int z = 0; z < l; z++) ps[z] = s[st + z];
            ps[l] = 0;
            if (br_streq(ps, "hover") || br_streq(ps, "focus-within")) part->pseudo_hover = 1;
            else if (br_streq(ps, "link") || br_streq(ps, "visited") || br_streq(ps, "root") || br_streq(ps, "any-link") || br_streq(ps, "enabled")) { /* always true here */ }
            else if (br_streq(ps, "first-child") || br_streq(ps, "first-of-type")) part->pseudo = 1;
            else if (br_streq(ps, "last-child") || br_streq(ps, "last-of-type")) part->pseudo = 2;
            else if (br_streq(ps, "only-child")) part->pseudo = 6;
            else if (br_streq(ps, "empty")) part->pseudo = 5;
            else if (br_streq_prefix(ps, "nth-child(") || br_streq_prefix(ps, "nth-of-type(")) {
                const char* a = ps; while (*a && *a != '(') a++; a++;
                if (br_streq_prefix(a, "even") || br_streq_prefix(a, "2n)") || br_streq_prefix(a, "2n+0")) part->pseudo = 3;
                else if (br_streq_prefix(a, "odd") || br_streq_prefix(a, "2n+1")) part->pseudo = 4;
                else if (*a >= '0' && *a <= '9' && !my_strstr_ci(a, "n")) { part->pseudo = 7; part->attr_op = br_atoi(a); }
                else return 0;
            }
            else if (br_streq_prefix(ps, "not(")) {
                br_selpart_t inner;
                int il = l - 5; if (il < 0) il = 0;
                if (!parse_compound(ps + 4, il, &inner)) return 0;
                if (inner.attr_name || inner.pseudo || inner.pseudo_hover || inner.neg) return 0;
                part->neg = 1; part->not_tag = inner.tag; part->not_cls = inner.cls; part->not_id = inner.id;
            }
            else return 0;          /* unsupported pseudo: rule never matches */
        }
    }
    if (cl) part->cls = br_strdup_n(classes, cl);
    return 1;
}

static void add_rule(const char* sel, int seln, const char* body, int bodyn) {
    if (rule_count >= BR_MAX_RULES) return;
    br_rule_t* r = &rules[rule_count];
    memset(r, 0, sizeof(*r));

    /* Split into compound parts right to left, remembering the combinator
     * that joins each part to the one on its left. */
    int parts_n = 0;
    int e = seln;
    while (e > 0 && parts_n < BR_MAX_SELPARTS) {
        int comb = 0;
        while (e > 0 && (is_ws(sel[e - 1]) || sel[e - 1] == '>' || sel[e - 1] == '+' || sel[e - 1] == '~')) {
            if (sel[e - 1] == '>' || sel[e - 1] == '+' || sel[e - 1] == '~') comb = sel[e - 1];
            e--;
        }
        int st = e;
        int depth = 0;
        while (st > 0) {
            char c = sel[st - 1];
            if (c == ']' || c == ')') depth++;
            else if (c == '[' || c == '(') depth--;
            else if (depth == 0 && (is_ws(c) || c == '>' || c == '+' || c == '~')) break;
            st--;
        }
        if (e > st) {
            if (!parse_compound(&sel[st], e - st, &r->parts[parts_n])) return;   /* unsupported */
            if (parts_n > 0) r->parts[parts_n - 1].combinator = comb == '~' ? '+' : comb;
            parts_n++;
        }
        e = st;
    }
    if (parts_n == 0) return;
    if (e > 0) return;                       /* selector deeper than BR_MAX_SELPARTS: drop */
    r->part_count = parts_n;

    /* specificity: ids*100 + classes/attrs/pseudo*10 + tags */
    int spec = 0;
    for (int i = 0; i < parts_n; i++) {
        if (r->parts[i].id) spec += 100;
        if (r->parts[i].cls) { spec += 10; for (const char* c = r->parts[i].cls; *c; c++) if (*c == ' ') spec += 10; }
        if (r->parts[i].attr_name) spec += 10;
        if (r->parts[i].pseudo_hover || r->parts[i].pseudo) spec += 10;
        if (r->parts[i].tag) spec += 1;
        if (r->parts[i].neg) spec += r->parts[i].not_id ? 100 : r->parts[i].not_cls ? 10 : 1;
    }
    r->specificity = spec;
    r->order = rule_order++;

    /* declarations */
    int i = 0;
    while (i < bodyn && r->decl_count < BR_MAX_DECLS) {
        int st = i;
        while (i < bodyn && body[i] != ':') i++;
        if (i >= bodyn) break;
        char* name = trim_dup(&body[st], i - st);
        lower_inplace(name);
        i++;
        int vs = i;
        int paren = 0;
        while (i < bodyn && (body[i] != ';' || paren)) { if (body[i] == '(') paren++; if (body[i] == ')') paren--; i++; }
        char* value = trim_dup(&body[vs], i - vs);
        int important = 0;
        int vl = (int)strlen(value);
        for (int k = 0; k + 1 < vl; k++) if (value[k] == '!' && (value[k + 1] == 'i' || value[k + 1] == 'I')) { important = 1; value[k] = 0; while (k > 0 && value[k - 1] == ' ') value[--k] = 0; break; }
        if (name[0]) { r->decls[r->decl_count].name = name; r->decls[r->decl_count].value = value; r->decls[r->decl_count].important = important; r->decl_count++; }
        i++;
    }
    rule_count++;
}

/* ----------------------------------------------------------- @font-face */

static br_fontface_t fontfaces[BR_MAX_FONTFACES];

int br_css_fontface_count(void) { return fontface_count; }
br_fontface_t* br_css_fontface(int i) { return (i >= 0 && i < fontface_count) ? &fontfaces[i] : NULL; }

static void unquote_copy(char* dst, int max, const char* s, int n) {
    while (n > 0 && is_ws(*s)) { s++; n--; }
    while (n > 0 && is_ws(s[n - 1])) n--;
    if (n >= 2 && (s[0] == '"' || s[0] == '\'') && s[n - 1] == s[0]) { s++; n -= 2; }
    if (n > max - 1) n = max - 1;
    for (int i = 0; i < n; i++) dst[i] = s[i];
    dst[n] = 0;
}

/* Parse the body of @font-face { ... }: family, weight, style and the first
 * src url() that looks like a TrueType/OpenType/WOFF file. */
static void parse_fontface(const char* body, int n) {
    if (fontface_count >= BR_MAX_FONTFACES) return;
    br_fontface_t* ff = &fontfaces[fontface_count];
    memset(ff, 0, sizeof(*ff));
    int i = 0;
    while (i < n) {
        while (i < n && (is_ws(body[i]) || body[i] == ';')) i++;
        int ns = i;
        while (i < n && body[i] != ':' && body[i] != ';') i++;
        if (i >= n || body[i] != ':') break;
        char name[32]; int nl = i - ns; if (nl > 31) nl = 31;
        for (int k = 0; k < nl; k++) name[k] = body[ns + k];
        name[nl] = 0;
        while (nl > 0 && is_ws(name[nl - 1])) name[--nl] = 0;
        lower_inplace(name);
        i++;
        int vs = i, paren = 0;
        while (i < n && (body[i] != ';' || paren)) { if (body[i] == '(') paren++; if (body[i] == ')') paren--; i++; }
        int vn = i - vs;
        if (br_streq(name, "font-family")) unquote_copy(ff->family, sizeof(ff->family), &body[vs], vn);
        else if (br_streq(name, "font-weight")) {
            char w[16]; unquote_copy(w, sizeof(w), &body[vs], vn);
            ff->bold = br_streq(w, "bold") || br_atoi(w) >= 600;
        }
        else if (br_streq(name, "font-style")) { char st[16]; unquote_copy(st, sizeof(st), &body[vs], vn); ff->italic = br_streq(st, "italic") || br_streq(st, "oblique"); }
        else if (br_streq(name, "src")) {
            /* choose: prefer truetype/opentype/woff formats; skip woff2/eot/svg */
            int p = vs, end = vs + vn;
            while (p < end && !ff->url[0]) {
                while (p < end && body[p] != 'u') p++;
                if (p + 4 >= end || body[p + 1] != 'r' || body[p + 2] != 'l' || body[p + 3] != '(') { p++; continue; }
                int us = p + 4;
                int ue = us; while (ue < end && body[ue] != ')') ue++;
                char url[BR_URL_MAX]; unquote_copy(url, sizeof(url), &body[us], ue - us);
                /* format hint */
                int q = ue; int bad = 0;
                while (q < end && body[q] != ',' ) q++;
                char seg[160]; int sl = q - ue; if (sl > 159) sl = 159;
                for (int k = 0; k < sl; k++) seg[k] = body[ue + k];
                seg[sl] = 0;
                lower_inplace(seg);
                if (my_strstr_ci(seg, "woff2") || my_strstr_ci(seg, "embedded-opentype") || my_strstr_ci(seg, "svg")) bad = 1;
                int ul = (int)strlen(url);
                if (ul > 6 && (br_streq(url + ul - 6, ".woff2") || br_streq(url + ul - 4, ".eot") || br_streq(url + ul - 4, ".svg"))) bad = 1;
                if (br_streq_prefix(url, "data:")) bad = 1;
                if (!bad) br_strlcpy(ff->url, url, sizeof(ff->url));
                p = q + 1;
            }
        }
        i++;
    }
    if (ff->family[0] && ff->url[0]) fontface_count++;
}

/* ------------------------------------------------------------- @import */

static char import_urls[4][BR_URL_MAX];
int br_css_import_count(void) { return import_count; }
const char* br_css_import_url(int i) { return (i >= 0 && i < import_count) ? import_urls[i] : NULL; }
void br_css_clear_imports(void) { import_count = 0; }

/* --------------------------------------------------------- @media eval */

/* Evaluate a media query list well enough for the common cases: screen,
 * all, (min-width: N), (max-width: N), prefers-color-scheme, print, not. */
static int media_matches(const char* q, int n) {
    int width = brs.layout_width > 0 ? brs.layout_width : 1000;
    char buf[200]; if (n > 199) n = 199;
    for (int i = 0; i < n; i++) buf[i] = q[i];
    buf[n] = 0;
    lower_inplace(buf);
    /* comma-separated alternatives: any true -> true */
    const char* p = buf;
    while (*p) {
        const char* e = p; while (*e && *e != ',') e++;
        int ok = 1, negate = 0;
        char part[200]; int pl = (int)(e - p);
        for (int i = 0; i < pl; i++) part[i] = p[i];
        part[pl] = 0;
        const char* s = part;
        while (*s == ' ') s++;
        if (br_streq_prefix(s, "not ")) { negate = 1; s += 4; }
        if (br_streq_prefix(s, "only ")) s += 5;
        if (my_strstr_ci(s, "print") || my_strstr_ci(s, "speech")) ok = 0;
        if (my_strstr_ci(s, "prefers-color-scheme: dark") || my_strstr_ci(s, "prefers-color-scheme:dark")) ok = 0;
        if (my_strstr_ci(s, "prefers-reduced-motion")) ok = 0;
        if (my_strstr_ci(s, "hover: none") || my_strstr_ci(s, "pointer: coarse")) ok = 0;
        if (my_strstr_ci(s, "orientation: portrait")) ok = 0;
        const char* mw;
        if ((mw = my_strstr_ci(s, "min-width"))) { const char* v = mw; while (*v && *v != ':') v++; if (*v) { v++; int okv; int px = parse_length(v, width, &okv); if (okv && width < px) ok = 0; } }
        if ((mw = my_strstr_ci(s, "max-width"))) { const char* v = mw; while (*v && *v != ':') v++; if (*v) { v++; int okv; int px = parse_length(v, width, &okv); if (okv && width > px) ok = 0; } }
        if ((mw = my_strstr_ci(s, "min-device-width"))) { const char* v = mw; while (*v && *v != ':') v++; if (*v) { v++; int okv; int px = parse_length(v, width, &okv); if (okv && (int)screen_width < px) ok = 0; } }
        if ((mw = my_strstr_ci(s, "max-device-width"))) { const char* v = mw; while (*v && *v != ':') v++; if (*v) { v++; int okv; int px = parse_length(v, width, &okv); if (okv && (int)screen_width > px) ok = 0; } }
        if ((mw = my_strstr_ci(s, "width >="))) { int okv; int px = parse_length(mw + 8, width, &okv); if (okv && width < px) ok = 0; }
        if ((mw = my_strstr_ci(s, "width <="))) { int okv; int px = parse_length(mw + 8, width, &okv); if (okv && width > px) ok = 0; }
        if (negate) ok = !ok;
        if (ok) return 1;
        p = *e ? e + 1 : e;
    }
    return 0;
}

void br_css_parse_sheet(const char* css, int len) {
    int i = 0;
    int media_depth = 0;                     /* nesting of entered @media/@supports blocks */
    while (i < len) {
        /* skip whitespace and comments */
        while (i < len && is_ws(css[i])) i++;
        if (i + 1 < len && css[i] == '/' && css[i + 1] == '*') {
            i += 2;
            while (i + 1 < len && !(css[i] == '*' && css[i + 1] == '/')) i++;
            i += 2;
            continue;
        }
        if (i >= len) break;
        if (i + 3 < len && css[i] == '<' && css[i + 1] == '!' && css[i + 2] == '-' && css[i + 3] == '-') { i += 4; continue; }
        if (i + 2 < len && css[i] == '-' && css[i + 1] == '-' && css[i + 2] == '>') { i += 3; continue; }
        if (css[i] == '@') {
            int st = i;
            while (i < len && css[i] != '{' && css[i] != ';') i++;
            int is_media = (len - st > 6 && css[st + 1] == 'm' && css[st + 2] == 'e' && css[st + 3] == 'd');
            int is_supports = (len - st > 9 && css[st + 1] == 's' && css[st + 2] == 'u' && css[st + 3] == 'p');
            int is_fontface = (len - st > 10 && css[st + 1] == 'f' && css[st + 2] == 'o' && css[st + 3] == 'n' && css[st + 6] == 'f');
            int is_import = (len - st > 7 && css[st + 1] == 'i' && css[st + 2] == 'm' && css[st + 3] == 'p');
            int is_layer = (len - st > 6 && css[st + 1] == 'l' && css[st + 2] == 'a' && css[st + 3] == 'y');
            if (i < len && css[i] == ';') {
                if (is_import && import_count < 4) {
                    /* @import url("x.css") screen; / @import "x.css"; */
                    const char* p = css + st + 7; const char* e = css + i;
                    while (p < e && (*p == ' ' || *p == 'u' || *p == 'r' || *p == 'l' || *p == '(')) p++;
                    const char* q = p; while (q < e && *q != ')' && *q != ';' && *q != ' ') q++;
                    unquote_copy(import_urls[import_count], BR_URL_MAX, p, (int)(q - p));
                    /* media list after the url */
                    const char* m = q; while (m < e && (*m == ')' || *m == ' ')) m++;
                    if (m >= e || media_matches(m, (int)(e - m))) import_count++;
                }
                i++; continue;
            }
            if (is_media && media_matches(css + st + 6, i - st - 6)) { media_depth++; i++; continue; }
            if (is_supports || is_layer) { media_depth++; i++; continue; }      /* assume supported */
            if (is_fontface) {
                int bs = i + 1, be = bs;
                while (be < len && css[be] != '}') be++;
                parse_fontface(css + bs, be - bs);
                i = be + 1;
                continue;
            }
            /* skip balanced block (unsupported at-rule or non-matching media) */
            int depth = 0;
            while (i < len) {
                if (css[i] == '{') depth++;
                else if (css[i] == '}') { depth--; if (depth == 0) { i++; break; } }
                i++;
            }
            continue;
        }
        if (css[i] == '}') { if (media_depth > 0) media_depth--; i++; continue; }
        int ss = i;
        while (i < len && css[i] != '{') { if (css[i] == '}') break; i++; }
        if (i >= len) break;
        if (css[i] == '}') { i++; continue; }
        int se = i;
        i++;
        int bs = i;
        int depth = 1;
        while (i < len) { if (css[i] == '{') depth++; else if (css[i] == '}') { depth--; if (depth == 0) break; } i++; }
        int be = i;
        i++;
        /* selector list */
        int p = ss;
        while (p < se) {
            int q = p; int d = 0;
            while (q < se && (css[q] != ',' || d)) { if (css[q] == '(' || css[q] == '[') d++; if (css[q] == ')' || css[q] == ']') d--; q++; }
            int a = p, b = q;
            while (a < b && is_ws(css[a])) a++;
            while (b > a && is_ws(css[b - 1])) b--;
            if (b > a) add_rule(&css[a], b - a, &css[bs], be - bs);
            p = q + 1;
        }
    }
}

/* ----------------------------------------------------------- matching */

static int attr_matches(const br_node_t* n, const br_selpart_t* p) {
    const char* v = br_attr(n, p->attr_name);
    if (!v) return 0;
    if (!p->attr_op || !p->attr_value) return 1;
    int vl = (int)strlen(v), wl = (int)strlen(p->attr_value);
    switch (p->attr_op) {
    case '=': return br_strieq(v, p->attr_value);
    case '^': return wl <= vl && br_streq_prefix(v, p->attr_value);
    case '$': return wl <= vl && br_streq(v + vl - wl, p->attr_value);
    case '*': return my_strstr_ci(v, p->attr_value) != NULL;
    case '~': {
        const char* q = v;
        while (*q) {
            while (*q == ' ') q++;
            const char* st = q; while (*q && *q != ' ') q++;
            if ((int)(q - st) == wl) { int eq = 1; for (int i = 0; i < wl; i++) if (st[i] != p->attr_value[i]) { eq = 0; break; } if (eq) return 1; }
        }
        return 0;
    }
    }
    return 0;
}

static int has_all_classes(const br_node_t* n, const char* list) {
    /* list is space separated */
    char one[64];
    while (*list) {
        while (*list == ' ') list++;
        int l = 0;
        while (*list && *list != ' ' && l < 63) one[l++] = *list++;
        one[l] = 0;
        if (l && !br_has_class(n, one)) return 0;
    }
    return 1;
}

static int element_index(const br_node_t* n, int* count) {
    /* 1-based index among element siblings, and the number of element siblings */
    int idx = 0, cnt = 0;
    if (!n->parent) { *count = 1; return 1; }
    for (const br_node_t* c = n->parent->first_child; c; c = c->next) {
        if (c->type != BR_NODE_ELEMENT) continue;
        cnt++;
        if (c == n) idx = cnt;
    }
    *count = cnt;
    return idx;
}

static int part_matches(const br_node_t* n, const br_selpart_t* p, int hover_id) {
    if (n->type != BR_NODE_ELEMENT) return 0;
    if (p->tag && !br_streq(n->tag, p->tag)) return 0;
    if (p->id) { const char* v = br_attr(n, "id"); if (!v || !br_streq(v, p->id)) return 0; }
    if (p->cls && !has_all_classes(n, p->cls)) return 0;
    if (p->attr_name && !attr_matches(n, p)) return 0;
    if (p->neg) {
        int m = 1;
        if (p->not_tag && !br_streq(n->tag, p->not_tag)) m = 0;
        if (m && p->not_id) { const char* v = br_attr(n, "id"); if (!v || !br_streq(v, p->not_id)) m = 0; }
        if (m && p->not_cls && !has_all_classes(n, p->not_cls)) m = 0;
        if (m) return 0;
    }
    if (p->pseudo) {
        int cnt, idx = element_index(n, &cnt);
        switch (p->pseudo) {
        case 1: if (idx != 1) return 0; break;
        case 2: if (idx != cnt) return 0; break;
        case 3: if (idx % 2 != 0) return 0; break;
        case 4: if (idx % 2 != 1) return 0; break;
        case 5: if (n->first_child) return 0; break;
        case 6: if (cnt != 1) return 0; break;
        case 7: if (idx != p->attr_op) return 0; break;
        }
    }
    if (p->pseudo_hover) {
        /* hover applies to the hovered element and its ancestors */
        if (hover_id < 0) return 0;
        const br_node_t* h = br_dom_node(hover_id);
        while (h) { if (h == n) break; h = h->parent; }
        if (!h) return 0;
    }
    return 1;
}

static const br_node_t* prev_element(const br_node_t* n) {
    if (!n->parent) return NULL;
    const br_node_t* prev = NULL;
    for (const br_node_t* c = n->parent->first_child; c && c != n; c = c->next) if (c->type == BR_NODE_ELEMENT) prev = c;
    return prev;
}

static int rule_matches(const br_node_t* n, const br_rule_t* r, int hover_id) {
    if (!part_matches(n, &r->parts[0], hover_id)) return 0;
    const br_node_t* cur = n;
    for (int i = 1; i < r->part_count; i++) {
        int comb = r->parts[i - 1].combinator;
        if (comb == '>') {
            cur = cur->parent;
            if (!cur || !part_matches(cur, &r->parts[i], hover_id)) return 0;
        } else if (comb == '+') {
            cur = prev_element(cur);
            if (!cur || !part_matches(cur, &r->parts[i], hover_id)) return 0;
        } else {
            const br_node_t* a = cur->parent;
            while (a && !part_matches(a, &r->parts[i], hover_id)) a = a->parent;
            if (!a) return 0;
            cur = a;
        }
    }
    return 1;
}

int br_css_match_selector_string(br_node_t* n, const char* sel) {
    /* querySelector support: parse into a temporary rule */
    int saved = rule_count;
    add_rule(sel, (int)strlen(sel), "", 0);
    int ok = 0;
    if (rule_count > saved) {
        ok = rule_matches(n, &rules[saved], -1);
        rule_count = saved;
        rule_order--;
    }
    return ok;
}

/* ----------------------------------------------------- applying decls */

static void set_font_px(br_style_t* st, int px) {
    if (px < FONT_MIN_PX) px = FONT_MIN_PX;
    if (px > FONT_MAX_PX) px = FONT_MAX_PX;
    st->font_px = px;
    st->font_scale = px >= 22 ? 3 : px >= 14 ? 2 : 1;
}

/* font-family: walk the list, pick the first family we have (a loaded
 * @font-face, or a generic/known name mapped to sans/serif/mono). */
static void apply_font_family(br_style_t* st, const char* value) {
    const char* p = value;
    while (*p) {
        while (*p == ' ' || *p == ',') p++;
        if (!*p) break;
        char q = 0;
        if (*p == '"' || *p == '\'') { q = *p; p++; }
        char fam[40]; int l = 0;
        while (*p && (q ? *p != q : *p != ',') && l < 39) fam[l++] = *p++;
        if (q && *p == q) p++;
        while (l > 0 && fam[l - 1] == ' ') l--;
        fam[l] = 0;
        if (!l) continue;
        int web = br_font_find_web(fam, 0);
        if (web >= 0) { st->font_family = web; st->monospace = 0; return; }
        lower_inplace(fam);
        if (br_streq(fam, "monospace") || my_strstr_ci(fam, "mono") || my_strstr_ci(fam, "courier") || my_strstr_ci(fam, "consolas") || my_strstr_ci(fam, "menlo") || br_streq(fam, "fixed") || my_strstr_ci(fam, "code")) { st->font_family = FONT_FAMILY_MONO; st->monospace = 1; return; }
        if (br_streq(fam, "serif") || my_strstr_ci(fam, "times") || my_strstr_ci(fam, "georgia") || my_strstr_ci(fam, "garamond") || my_strstr_ci(fam, "book") || my_strstr_ci(fam, "palatino") || my_strstr_ci(fam, "cambria") || my_strstr_ci(fam, "lora") || my_strstr_ci(fam, "merriweather") || my_strstr_ci(fam, "charter") || my_strstr_ci(fam, "baskerville") || my_strstr_ci(fam, "roman")) { st->font_family = FONT_FAMILY_SERIF; st->monospace = 0; return; }
        if (br_streq(fam, "sans-serif") || br_streq(fam, "system-ui") || br_streq(fam, "-apple-system") || my_strstr_ci(fam, "arial") || my_strstr_ci(fam, "helvetica") || my_strstr_ci(fam, "verdana") || my_strstr_ci(fam, "roboto") || my_strstr_ci(fam, "segoe") || my_strstr_ci(fam, "ubuntu") || my_strstr_ci(fam, "inter") || my_strstr_ci(fam, "open sans") || my_strstr_ci(fam, "lato") || my_strstr_ci(fam, "noto") || my_strstr_ci(fam, "tahoma") || my_strstr_ci(fam, "dejavu") || my_strstr_ci(fam, "sans")) { st->font_family = FONT_FAMILY_SANS; st->monospace = 0; return; }
        if (br_streq(fam, "cursive") || br_streq(fam, "fantasy")) { st->font_family = FONT_FAMILY_SERIF; st->monospace = 0; return; }
        /* unknown named font: keep looking for a generic fallback */
    }
}

static void apply_box_sides(int* t, int* r, int* b, int* l, const char* value, int em_px, int base_px) {
    int v[4]; int n = 0; int ok;
    const char* p = value;
    while (*p && n < 4) {
        while (*p == ' ') p++;
        if (!*p) break;
        int len = parse_length_em(p, base_px, em_px, &ok);
        if (!ok) len = 0;                /* auto -> 0 */
        v[n++] = len;
        if (br_streq_prefix(p, "calc(")) { while (*p && *p != ')') p++; if (*p) p++; }
        else while (*p && *p != ' ') p++;
    }
    if (n == 0) return;
    *t = v[0]; *r = n > 1 ? v[1] : v[0]; *b = n > 2 ? v[2] : v[0]; *l = n > 3 ? v[3] : (n > 1 ? v[1] : v[0]);
}

static void apply_decl(br_style_t* st, const char* name, const char* value, int parent_font_px) {
    int ok;
    int em = st->font_px > 0 ? st->font_px : parent_font_px;
    if (br_streq(value, "inherit") || br_streq(value, "unset") || br_streq(value, "revert")) return;   /* keep inherited value */
    if (br_streq(name, "color")) { uint32_t c = br_css_parse_color(value, &ok); if (ok && c) st->color = c; }
    else if (br_streq(name, "background") || br_streq(name, "background-color")) {
        /* background: <color> [image...] -> first token that parses as colour */
        const char* p = value;
        int found = 0;
        while (*p && !found) {
            uint32_t c = br_css_parse_color(p, &ok);
            if (ok) { st->background = c; found = 1; break; }
            if (br_streq_prefix(p, "url(") || br_streq_prefix(p, "linear-gradient(") || br_streq_prefix(p, "radial-gradient(") || br_streq_prefix(p, "rgb")) {
                /* gradient: approximate with its first colour stop */
                if (p[0] == 'l' || p[0] == 'r') {
                    const char* q = p; while (*q && *q != '(') q++; if (*q) q++;
                    int guard = 0;
                    while (*q && guard++ < 8) {
                        while (*q == ' ' || *q == ',') q++;
                        uint32_t c2 = br_css_parse_color(q, &ok);
                        if (ok) { st->background = c2; found = 1; break; }
                        int d = 0; while (*q && (d || (*q != ',' && *q != ')'))) { if (*q == '(') d++; if (*q == ')') d--; q++; }
                    }
                }
                int d = 0; while (*p && (d || *p != ' ')) { if (*p == '(') d++; if (*p == ')') d--; p++; }
            } else while (*p && *p != ' ') p++;
            while (*p == ' ') p++;
        }
        if (!found && br_streq(name, "background") && (br_streq_prefix(value, "none") || br_streq_prefix(value, "transparent"))) st->background = 0;
    }
    else if (br_streq(name, "font-size")) {
        int px = parse_length_em(value, parent_font_px, parent_font_px, &ok);
        if (ok && px > 0) set_font_px(st, px);
    }
    else if (br_streq(name, "font-weight")) {
        if (br_streq(value, "bold") || br_streq(value, "bolder") || br_atoi(value) >= 600) st->bold = 1;
        else if (br_streq(value, "normal") || br_streq(value, "lighter") || (br_atoi(value) > 0 && br_atoi(value) < 600)) st->bold = 0;
    }
    else if (br_streq(name, "font-style")) st->italic = br_streq(value, "italic") || br_streq(value, "oblique");
    else if (br_streq(name, "font-family")) apply_font_family(st, value);
    else if (br_streq(name, "font")) {
        /* shorthand: [style] [weight] size[/line-height] family */
        if (br_streq(value, "inherit") || br_streq(value, "menu") || br_streq(value, "caption") || br_streq(value, "message-box")) return;
        if (my_strstr_ci(value, "bold") || my_strstr_ci(value, " 700") || my_strstr_ci(value, " 600")) st->bold = 1;
        if (my_strstr_ci(value, "italic") || my_strstr_ci(value, "oblique")) st->italic = 1;
        const char* p = value;
        const char* size_tok = NULL;
        while (*p) {
            while (*p == ' ') p++;
            if ((*p >= '0' && *p <= '9') || *p == '.') {
                /* a weight number (100..900 without unit) or the size */
                const char* q = p; while (*q >= '0' && *q <= '9') q++;
                if (*q == ' ' && (q - p) == 3) { p = q; continue; }
                size_tok = p; break;
            }
            if (br_streq_prefix(p, "xx-") || br_streq_prefix(p, "x-") || br_streq_prefix(p, "small") || br_streq_prefix(p, "medium") || br_streq_prefix(p, "large")) { size_tok = p; break; }
            while (*p && *p != ' ') p++;
        }
        if (size_tok) {
            int px = parse_length_em(size_tok, parent_font_px, parent_font_px, &ok);
            if (ok && px > 0) set_font_px(st, px);
            const char* q = size_tok; while (*q && *q != ' ' && *q != '/') q++;
            if (*q == '/') { q++; int lh = parse_length_em(q, st->font_px, st->font_px, &ok); if (ok) st->line_height = lh; else if (*q >= '0' && *q <= '9') { int num = br_atoi(q); if (num > 0 && num < 4) st->line_height = st->font_px * num; } while (*q && *q != ' ') q++; }
            while (*q == ' ') q++;
            if (*q) apply_font_family(st, q);
        }
    }
    else if (br_streq(name, "line-height")) {
        if (br_streq(value, "normal")) { st->line_height = 0; return; }
        /* unitless multiplier vs length */
        const char* p = value; while (*p == ' ') p++;
        const char* q = p; while ((*q >= '0' && *q <= '9') || *q == '.') q++;
        if (*q == 0 || *q == ' ') {
            int whole = 0, frac = 0, fd = 0; const char* r = p;
            while (*r >= '0' && *r <= '9') { whole = whole * 10 + (*r - '0'); r++; }
            if (*r == '.') { r++; while (*r >= '0' && *r <= '9') { if (fd < 2) { frac = frac * 10 + (*r - '0'); fd++; } r++; } }
            while (fd < 2) { frac *= 10; fd++; }
            st->line_height = -(whole * 100 + frac);          /* negative = multiplier in hundredths */
        } else {
            int lh = parse_length_em(value, em, em, &ok);
            if (ok) st->line_height = lh;
        }
    }
    else if (br_streq(name, "text-decoration") || br_streq(name, "text-decoration-line")) {
        if (my_strstr_ci(value, "underline")) st->underline = 1;
        else if (my_strstr_ci(value, "line-through")) st->strike = 1;
        else if (my_strstr_ci(value, "none")) { st->underline = 0; st->strike = 0; }
    }
    else if (br_streq(name, "text-transform")) {
        if (br_streq(value, "uppercase")) st->text_transform = 1;
        else if (br_streq(value, "lowercase")) st->text_transform = 2;
        else if (br_streq(value, "capitalize")) st->text_transform = 3;
        else st->text_transform = 0;
    }
    else if (br_streq(name, "letter-spacing")) { int l = parse_length_em(value, em, em, &ok); st->letter_spacing = ok ? l : 0; }
    else if (br_streq(name, "text-align")) {
        if (br_streq(value, "center") || br_streq(value, "-webkit-center")) st->text_align = 1;
        else if (br_streq(value, "right") || br_streq(value, "end")) st->text_align = 2;
        else if (br_streq(value, "justify")) st->text_align = 0;
        else st->text_align = 0;
    }
    else if (br_streq(name, "display")) {
        if (br_streq(value, "none")) st->display = BR_DISPLAY_NONE;
        else if (br_streq(value, "inline")) st->display = BR_DISPLAY_INLINE;
        else if (br_streq(value, "list-item")) st->display = BR_DISPLAY_LIST_ITEM;
        else if (br_streq(value, "table")) st->display = BR_DISPLAY_TABLE;
        else if (br_streq(value, "table-row")) st->display = BR_DISPLAY_TABLE_ROW;
        else if (br_streq(value, "table-cell")) st->display = BR_DISPLAY_TABLE_CELL;
        else if (br_streq(value, "inline-block") || br_streq(value, "inline-flex") || br_streq(value, "inline-grid") || br_streq(value, "inline-table")) st->display = BR_DISPLAY_INLINE_BLOCK;
        else if (br_streq(value, "contents")) st->display = BR_DISPLAY_INLINE;
        else if (br_streq(value, "flex") || br_streq(value, "grid")) st->display = BR_DISPLAY_FLEX;
        else st->display = BR_DISPLAY_BLOCK;   /* block, flow-root, ... */
    }
    else if (br_streq(name, "flex-direction") || br_streq(name, "flex-flow")) st->flex_col = br_streq_prefix(value, "column");
    else if (br_streq(name, "gap") || br_streq(name, "column-gap") || br_streq(name, "grid-gap")) { int l = parse_length_em(value, 400, em, &ok); if (ok) st->gap = l; }
    else if (br_streq(name, "float")) { st->float_dir = br_streq(value, "left") ? 1 : br_streq(value, "right") ? 2 : 0; }
    else if (br_streq(name, "visibility")) st->visible = !br_streq(value, "hidden") && !br_streq(value, "collapse");
    else if (br_streq(name, "margin")) {
        apply_box_sides(&st->margin_t, &st->margin_r, &st->margin_b, &st->margin_l, value, em, 400);
        /* margin: A auto / margin: 0 auto 0 auto / margin: auto */
        st->margin_auto = 0;
        int tok = 0; const char* p = value;
        while (*p && tok < 4) {
            while (*p == ' ') p++;
            if (!*p) break;
            int is_auto = br_streq_prefix(p, "auto");
            if (is_auto) {
                if (tok == 0) st->margin_auto |= 3;               /* applies to all four */
                else if (tok == 1) st->margin_auto |= 3;          /* horizontal */
                else if (tok == 3) st->margin_auto |= 1;          /* left only */
            } else if (tok == 1 || tok == 3) {
                if (tok == 1) st->margin_auto &= ~3;
                if (tok == 3) st->margin_auto &= ~1;
            }
            if (br_streq_prefix(p, "calc(")) { while (*p && *p != ')') p++; if (*p) p++; }
            else while (*p && *p != ' ') p++;
            tok++;
        }
    }
    else if (br_streq(name, "padding")) apply_box_sides(&st->padding_t, &st->padding_r, &st->padding_b, &st->padding_l, value, em, 400);
    else if (br_streq(name, "margin-top")) { int l = parse_length_em(value, 400, em, &ok); if (ok) st->margin_t = l; else st->margin_t = 0; }
    else if (br_streq(name, "margin-bottom")) { int l = parse_length_em(value, 400, em, &ok); if (ok) st->margin_b = l; else st->margin_b = 0; }
    else if (br_streq(name, "margin-left") || br_streq(name, "margin-inline-start")) { int l = parse_length_em(value, 400, em, &ok); if (ok) { st->margin_l = l; st->margin_auto &= ~1; } else { st->margin_l = 0; if (br_streq(value, "auto")) st->margin_auto |= 1; } }
    else if (br_streq(name, "margin-right") || br_streq(name, "margin-inline-end")) { int l = parse_length_em(value, 400, em, &ok); if (ok) { st->margin_r = l; st->margin_auto &= ~2; } else { st->margin_r = 0; if (br_streq(value, "auto")) st->margin_auto |= 2; } }
    else if (br_streq(name, "margin-inline")) { int l = parse_length_em(value, 400, em, &ok); st->margin_l = st->margin_r = ok ? l : 0; if (!ok && br_streq(value, "auto")) st->margin_auto = 3; else st->margin_auto = 0; }
    else if (br_streq(name, "margin-block")) { int l = parse_length_em(value, 400, em, &ok); st->margin_t = st->margin_b = ok ? l : 0; }
    else if (br_streq(name, "padding-top")) { int l = parse_length_em(value, 400, em, &ok); if (ok) st->padding_t = l; }
    else if (br_streq(name, "padding-bottom")) { int l = parse_length_em(value, 400, em, &ok); if (ok) st->padding_b = l; }
    else if (br_streq(name, "padding-left") || br_streq(name, "padding-inline-start")) { int l = parse_length_em(value, 400, em, &ok); if (ok) st->padding_l = l; }
    else if (br_streq(name, "padding-right") || br_streq(name, "padding-inline-end")) { int l = parse_length_em(value, 400, em, &ok); if (ok) st->padding_r = l; }
    else if (br_streq(name, "padding-inline")) { int l = parse_length_em(value, 400, em, &ok); if (ok) st->padding_l = st->padding_r = l; }
    else if (br_streq(name, "padding-block")) { int l = parse_length_em(value, 400, em, &ok); if (ok) st->padding_t = st->padding_b = l; }
    else if (br_streq(name, "border") || br_streq(name, "border-top") || br_streq(name, "border-bottom") ||
             br_streq(name, "border-left") || br_streq(name, "border-right") || br_streq(name, "outline") ||
             br_streq(name, "border-block-end") || br_streq(name, "border-block-start") ||
             br_streq(name, "border-inline-start") || br_streq(name, "border-inline-end")) {
        if (br_streq(name, "outline")) return;
        int side = br_streq(name, "border") ? 15 :
                   (br_streq(name, "border-top") || br_streq(name, "border-block-start")) ? 1 :
                   (br_streq(name, "border-right") || br_streq(name, "border-inline-end")) ? 2 :
                   (br_streq(name, "border-bottom") || br_streq(name, "border-block-end")) ? 4 : 8;
        const char* p = value;
        int w = 1;
        int got_w = 0, got_style = 0, hidden = 0;
        if (br_streq(value, "none") || br_streq(value, "0") || br_streq(value, "0px") || br_streq_prefix(value, "0 ")) hidden = 1;
        while (*p && !hidden) {
            while (*p == ' ') p++;
            if (!*p) break;
            if ((*p >= '0' && *p <= '9') || *p == '.') {
                w = parse_length_em(p, 400, em, &ok); got_w = 1;
                if (w == 0 && !(p[0] == '0' && (p[1] == 0 || p[1] == ' ' || p[1] == 'p' || p[1] == 'e' || p[1] == 'r'))) w = 1;   /* .5px -> 1px */
            }
            else if (br_streq_prefix(p, "thin")) { w = 1; got_w = 1; }
            else if (br_streq_prefix(p, "medium")) { w = 3; got_w = 1; }
            else if (br_streq_prefix(p, "thick")) { w = 5; got_w = 1; }
            else if (br_streq_prefix(p, "none") || br_streq_prefix(p, "hidden")) { hidden = 1; }
            else if (br_streq_prefix(p, "solid") || br_streq_prefix(p, "dashed") || br_streq_prefix(p, "dotted") ||
                     br_streq_prefix(p, "double") || br_streq_prefix(p, "inset") || br_streq_prefix(p, "outset") ||
                     br_streq_prefix(p, "ridge") || br_streq_prefix(p, "groove")) { got_style = 1; }
            else {
                uint32_t c = br_css_parse_color(p, &ok);
                if (ok) st->border_color = c;
            }
            int d = 0; while (*p && (d || *p != ' ')) { if (*p == '(') d++; if (*p == ')') d--; p++; }
        }
        int bw;
        if (hidden) bw = 0;
        else if (!got_style && !got_w) return;
        else { bw = got_w ? (w > 0 ? w : 0) : 1; if (bw > 8) bw = 8; }
        if (side & 1) st->border_t = bw;
        if (side & 2) st->border_r = bw;
        if (side & 4) st->border_b = bw;
        if (side & 8) st->border_l = bw;
        int m = st->border_t;
        if (st->border_r > m) m = st->border_r;
        if (st->border_b > m) m = st->border_b;
        if (st->border_l > m) m = st->border_l;
        st->border = m;
    }
    else if (br_streq(name, "border-width")) { int l = parse_length_em(value, 400, em, &ok); if (ok) { if (l > 8) l = 8; st->border = st->border_t = st->border_r = st->border_b = st->border_l = l; } }
    else if (br_streq(name, "border-top-width") || br_streq(name, "border-bottom-width") || br_streq(name, "border-left-width") || br_streq(name, "border-right-width")) {
        int l = parse_length_em(value, 400, em, &ok);
        if (ok) {
            if (l > 8) l = 8;
            if (name[7] == 't') st->border_t = l; else if (name[7] == 'b') st->border_b = l; else if (name[7] == 'l') st->border_l = l; else st->border_r = l;
            int m = st->border_t;
            if (st->border_r > m) m = st->border_r;
            if (st->border_b > m) m = st->border_b;
            if (st->border_l > m) m = st->border_l;
            st->border = m;
        }
    }
    else if (br_streq(name, "border-color")) { uint32_t c = br_css_parse_color(value, &ok); if (ok) st->border_color = c; }
    else if (br_streq(name, "border-style")) {
        if (br_streq(value, "none") || br_streq(value, "hidden")) st->border = st->border_t = st->border_r = st->border_b = st->border_l = 0;
        else if (!st->border) st->border = st->border_t = st->border_r = st->border_b = st->border_l = 1;
    }
    else if (br_streq(name, "width")) {
        int l = parse_length_em(value, 0, em, &ok);
        if (ok) {
            if (value[strlen(value) - 1] == '%') st->width = -(2 + br_atoi(value));   /* encode percent */
            else st->width = l;
        } else if (br_streq(value, "auto")) st->width = -1;
    }
    else if (br_streq(name, "min-width")) {
        int l = parse_length_em(value, 0, em, &ok);
        st->min_width = (ok && value[strlen(value) - 1] != '%' && l > 0) ? l : 0;
    }
    else if (br_streq(name, "box-sizing")) st->border_box = br_streq(value, "border-box");
    else if (br_streq(name, "max-width")) {
        int l = parse_length_em(value, 0, em, &ok);
        if (ok && value[strlen(value) - 1] != '%') st->max_width = l;
        else if (br_streq(value, "none")) st->max_width = -1;
    }
    else if (br_streq(name, "height") || br_streq(name, "min-height")) { int l = parse_length_em(value, 0, em, &ok); if (ok && value[strlen(value) - 1] != '%') st->height = l; }
    else if (br_streq(name, "list-style") || br_streq(name, "list-style-type")) {
        if (my_strstr_ci(value, "none")) st->list_style = 2;
        else if (my_strstr_ci(value, "decimal")) st->list_style = 1;
        else st->list_style = 0;
    }
    else if (br_streq(name, "white-space")) { /* pre handled by tag */ }
    else if (br_streq(name, "opacity")) { if (value[0] == '0' && (value[1] == 0 || value[1] == '.')) { if (value[1] == 0) st->visible = 0; } }
    else if (br_streq(name, "position")) { if (br_streq(value, "fixed")) st->display = BR_DISPLAY_NONE; /* fixed overlays (cookie bars, sticky headers) would cover content */ }
    else if (br_streq(name, "clip") || br_streq(name, "clip-path")) { if (br_streq_prefix(value, "rect(0") || br_streq_prefix(value, "rect(1px") || br_streq(value, "inset(50%)")) st->display = BR_DISPLAY_NONE; /* visually-hidden idiom */ }
    else if (br_streq(name, "transform")) { if (br_streq_prefix(value, "scale(0")) st->display = BR_DISPLAY_NONE; }
}

void br_css_apply_inline(br_node_t* n, const char* css) {
    /* "a:b;c:d" -> decls */
    const char* p = css;
    int parent_px = n->parent ? (n->parent->style.font_px > 0 ? n->parent->style.font_px : 16) : 16;
    while (*p) {
        while (is_ws(*p) || *p == ';') p++;
        if (!*p) break;
        const char* ns = p;
        while (*p && *p != ':' && *p != ';') p++;
        if (*p != ':') break;
        char* name = trim_dup(ns, (int)(p - ns));
        lower_inplace(name);
        p++;
        const char* vs = p;
        int paren = 0;
        while (*p && (*p != ';' || paren)) { if (*p == '(') paren++; if (*p == ')') paren--; p++; }
        char* value = trim_dup(vs, (int)(p - vs));
        apply_decl(&n->style, name, value, parent_px);
    }
}

/* -------------------------------------------------------- the cascade */

static int hover_node_id = -1;

static void compute_node(br_node_t* n, const br_style_t* parent) {
    br_style_t* st = &n->style;
    /* inherited */
    st->color = parent->color;
    st->font_scale = parent->font_scale;
    st->font_px = parent->font_px > 0 ? parent->font_px : 16;
    st->font_family = parent->font_family;
    st->line_height = parent->line_height;
    st->letter_spacing = parent->letter_spacing;
    st->text_transform = parent->text_transform;
    st->bold = parent->bold;
    st->italic = parent->italic;
    st->underline = parent->underline;
    st->strike = parent->strike;
    st->text_align = parent->text_align;
    st->list_style = parent->list_style;
    st->monospace = parent->monospace;
    st->visible = parent->visible;
    /* non-inherited */
    st->background = 0;
    st->display = BR_DISPLAY_INLINE;
    st->margin_t = st->margin_b = st->margin_l = st->margin_r = 0;
    st->padding_t = st->padding_b = st->padding_l = st->padding_r = 0;
    st->border = 0;
    st->border_t = st->border_r = st->border_b = st->border_l = 0;
    st->border_color = 0;                   /* 0 = currentColor, resolved below */
    st->width = -1;
    st->height = -1;
    st->max_width = -1;
    st->min_width = 0;
    st->border_box = 0;
    st->float_dir = 0;
    st->margin_auto = 0;
    st->flex_col = 0;
    st->gap = 0;

    if (n->type != BR_NODE_ELEMENT) return;

    /* Presentational attributes (old-school HTML) come first, lowest priority. */
    const char* a;
    if ((a = br_attr(n, "bgcolor"))) { int ok; uint32_t c = br_css_parse_color(a, &ok); if (ok) st->background = c; }
    if ((a = br_attr(n, "text")) && br_streq(n->tag, "body")) { int ok; uint32_t c = br_css_parse_color(a, &ok); if (ok) st->color = c; }
    if ((a = br_attr(n, "color")) && br_streq(n->tag, "font")) { int ok; uint32_t c = br_css_parse_color(a, &ok); if (ok) st->color = c; }
    if ((a = br_attr(n, "size")) && br_streq(n->tag, "font")) {
        static const int font_sizes[8] = { 10, 10, 13, 16, 18, 24, 32, 48 };
        int sz = (a[0] == '+' || a[0] == '-') ? 3 + br_atoi(a) : br_atoi(a);
        if (sz < 1) sz = 1;
        if (sz > 7) sz = 7;
        set_font_px(st, font_sizes[sz]);
    }
    if ((a = br_attr(n, "face")) && br_streq(n->tag, "font")) apply_font_family(st, a);
    if ((a = br_attr(n, "align"))) { if (br_streq(a, "center")) st->text_align = 1; else if (br_streq(a, "right")) st->text_align = 2; else st->text_align = 0; }
    if ((a = br_attr(n, "hidden"))) st->display = BR_DISPLAY_NONE;
    if ((a = br_attr(n, "border")) && br_streq(n->tag, "table")) { st->border = br_atoi(a) > 0 ? 1 : 0; st->border_t = st->border_r = st->border_b = st->border_l = st->border; }
    if ((a = br_attr(n, "width"))) { int ok; int l = parse_length(a, 0, &ok); if (ok) st->width = (a[strlen(a) - 1] == '%') ? -(2 + br_atoi(a)) : l; }

    /* Matching rules sorted by (specificity, order). rule_count is small so
     * an insertion-ordered scan per specificity bucket is fine. */
    int parent_px = parent->font_px > 0 ? parent->font_px : 16;
    /* Gather matches then apply in ascending (spec, order). */
    int idx[96]; int cnt = 0;
    for (int i = 0; i < rule_count && cnt < 96; i++) {
        if (rule_matches(n, &rules[i], hover_node_id)) idx[cnt++] = i;
    }
    /* insertion sort */
    for (int i = 1; i < cnt; i++) {
        int k = idx[i]; int j = i - 1;
        while (j >= 0 && (rules[idx[j]].specificity > rules[k].specificity ||
               (rules[idx[j]].specificity == rules[k].specificity && rules[idx[j]].order > rules[k].order))) {
            idx[j + 1] = idx[j]; j--;
        }
        idx[j + 1] = k;
    }
    int any_important = 0;
    for (int i = 0; i < cnt; i++) {
        br_rule_t* r = &rules[idx[i]];
        for (int d = 0; d < r->decl_count; d++) {
            if (r->decls[d].important) { any_important = 1; continue; }
            apply_decl(st, r->decls[d].name, r->decls[d].value, parent_px);
        }
    }
    /* style="" wins over normal declarations */
    const char* inl = br_attr(n, "style");
    if (inl) br_css_apply_inline(n, inl);
    /* ... but !important wins over everything */
    if (any_important) {
        for (int i = 0; i < cnt; i++) {
            br_rule_t* r = &rules[idx[i]];
            for (int d = 0; d < r->decl_count; d++)
                if (r->decls[d].important) apply_decl(st, r->decls[d].name, r->decls[d].value, parent_px);
        }
    }

    /* <noscript>: author CSS cannot override this either way */
    if (br_streq(n->tag, "noscript")) st->display = noscript_visible ? BR_DISPLAY_BLOCK : BR_DISPLAY_NONE;
    /* <a> without href is not a link */
    if (br_streq(n->tag, "a") && !br_attr(n, "href")) { st->underline = parent->underline; st->color = parent->color; }
    if (br_streq(n->tag, "pre")) st->monospace = 1;
    /* Inputs/buttons: never inherit huge fonts */
    if (br_streq(n->tag, "input") || br_streq(n->tag, "button") || br_streq(n->tag, "select") || br_streq(n->tag, "textarea")) {
        if (st->font_px > 18) set_font_px(st, 16);
        if (st->display == BR_DISPLAY_INLINE) st->display = BR_DISPLAY_INLINE_BLOCK;
    }
    if (st->monospace && st->font_family < FONT_FAMILY_WEB) st->font_family = FONT_FAMILY_MONO;
    if (!st->border_color) st->border_color = (br_streq(n->tag, "table") || br_streq(n->tag, "td") || br_streq(n->tag, "th") || br_streq(n->tag, "hr") || br_streq(n->tag, "fieldset")) ? 0xFF808080u : st->color;
    if (st->font_px >= 22) st->font_scale = 3; else if (st->font_px >= 14) st->font_scale = 2; else st->font_scale = 1;
    if (br_streq(n->tag, "img") && st->display == BR_DISPLAY_INLINE) st->display = BR_DISPLAY_INLINE_BLOCK;
}

static void compute_tree(br_node_t* n, const br_style_t* parent) {
    if (br_stack_headroom() < BR_STACK_MIN) { n->style = *parent; n->style.display = BR_DISPLAY_NONE; return; }
    compute_node(n, parent);
    if (n->style.display == BR_DISPLAY_NONE) return;
    for (br_node_t* c = n->first_child; c; c = c->next) compute_tree(c, &n->style);
}

void br_css_compute(br_node_t* doc) {
    br_style_t root;
    memset(&root, 0, sizeof(root));
    root.color = 0xFF000000u;
    root.font_scale = 2;
    root.font_px = 16;
    root.font_family = FONT_FAMILY_SANS;
    root.max_width = -1;
    root.margin_auto = 0;
    root.display = BR_DISPLAY_BLOCK;
    root.width = -1;
    root.height = -1;
    root.visible = 1;
    hover_node_id = brs.hover_link;
    compute_tree(doc, &root);
}
