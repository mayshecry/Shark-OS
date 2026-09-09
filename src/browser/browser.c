/* Shark Navigator: the browser window (chrome, navigation, painting, input).
 *
 * Everything else (HTML/CSS/layout/JS) lives in the sibling files; this one
 * owns the browser state, fetches documents and turns layout boxes into
 * pixels inside a Win98 window. */

#include "browser_internal.h"
#include "tls.h"

browser_state_t brs;
br_node_t* br_doc = NULL;
char br_page_src[BR_PAGE_MAX];
int br_page_len = 0;

/* Chrome geometry (client coordinates) */
#define BR_TOOLBAR_H   30
#define BR_ADDR_H      26
#define BR_STATUS_H    18
#define BR_SCROLL_W    16
#define BR_BTN_W       52
#define BR_BTN_H       22
#define BR_MENU_W      230
#define BR_LINE_STEP   24

/* Bookmarks shown in the drop-down menu */
static const struct { const char* title; const char* url; } bookmarks[] = {
    { "Home page",            "about:home" },
    { "Demo page (JS + CSS)", "about:demo" },
    { "example.com (https)",  "https://example.com/" },
    { "NPR text news (https)", "https://text.npr.org/" },
    { "Wiby search (https)",  "https://wiby.me/" },
    { "Legible News (https)", "https://legiblenews.com/" },
    { "info.cern.ch (http)",  "http://info.cern.ch/" },
    { "SharkOS readme",       "file:readme.txt" },
    { "JavaScript console",   "about:console" },
    { "View page source",     "about:source" },
};
#define BR_BOOKMARK_COUNT ((int)(sizeof(bookmarks) / sizeof(bookmarks[0])))

/* ------------------------------------------------------------ helpers */

void br_itoa(int v, char* out) {
    char tmp[12]; int t = 0, o = 0;
    unsigned int u = v < 0 ? (unsigned int)(-v) : (unsigned int)v;
    if (v < 0) out[o++] = '-';
    if (u == 0) tmp[t++] = '0';
    while (u > 0) { tmp[t++] = (char)('0' + u % 10); u /= 10; }
    while (t > 0) out[o++] = tmp[--t];
    out[o] = 0;
}

int br_atoi(const char* s) {
    if (!s) return 0;
    while (*s == ' ') s++;
    int neg = 0, v = 0;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; if (v > 100000000) break; }
    return neg ? -v : v;
}

void br_strlcpy(char* d, const char* s, int max) {
    if (max <= 0) return;
    int i = 0;
    if (s) for (; s[i] && i < max - 1; i++) d[i] = s[i];
    d[i] = 0;
}

void br_strlcat(char* d, const char* s, int max) {
    int l = (int)strlen(d);
    if (l >= max - 1) return;
    br_strlcpy(d + l, s, max - l);
}

void br_status(const char* s) {
    br_strlcpy(brs.status, s, sizeof(brs.status));
    br_request_repaint();
}

void br_request_repaint(void) {
    if (brs.win) { brs.win->needs_redraw = true; desktop.dirty = true; }
}

void br_js_console(const char* msg) {
    if (brs.js_console_lines < 8) {
        br_strlcpy(brs.js_console[brs.js_console_lines++], msg, 96);
    } else {
        for (int i = 1; i < 8; i++) memcpy(brs.js_console[i - 1], brs.js_console[i], 96);
        br_strlcpy(brs.js_console[7], msg, 96);
    }
}

void br_alert(const char* msg) {
    br_strlcpy(brs.alert_text, msg, sizeof(brs.alert_text));
    brs.alert_open = 1;
    br_request_repaint();
}

/* Scripts changed the tree: recompute style + layout before the next paint. */
void br_js_dom_changed(void) {
    if (brs.needs_layout < 1) brs.needs_layout = 1;
    br_request_repaint();
}

/* ---------------------------------------------------------------- URLs */

static int is_net_url(const char* u) { return br_streq_prefix(u, "http://") || br_streq_prefix(u, "https://"); }
static int scheme_len(const char* u) { return br_streq_prefix(u, "https://") ? 8 : 7; }

void br_resolve_url(const char* base, const char* rel, char* out, int max) {
    if (!rel) { br_strlcpy(out, base, max); return; }
    while (*rel == ' ') rel++;
    /* absolute? */
    const char* p = rel;
    while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '+' || *p == '-' || *p == '.') p++;
    if (p > rel && *p == ':' ) { br_strlcpy(out, rel, max); return; }
    if (rel[0] == '/' && rel[1] == '/') { br_strlcpy(out, br_streq_prefix(base, "https://") ? "https:" : "http:", max); br_strlcat(out, rel, max); return; }
    if (!is_net_url(base)) {
        /* about:/file: bases: relative names stay relative to the scheme */
        if (br_streq_prefix(base, "file:")) { br_strlcpy(out, "file:", max); br_strlcat(out, rel, max); return; }
        br_strlcpy(out, rel, max);
        return;
    }
    /* http(s)://host[:port]/path */
    const char* host_start = base + scheme_len(base);
    const char* path_start = host_start;
    while (*path_start && *path_start != '/') path_start++;
    char origin[BR_URL_MAX];
    int ol = (int)(path_start - base);
    if (ol > max - 2) ol = max - 2;
    for (int i = 0; i < ol; i++) origin[i] = base[i];
    origin[ol] = 0;
    if (rel[0] == '/') { br_strlcpy(out, origin, max); br_strlcat(out, rel, max); return; }
    if (rel[0] == '#' || rel[0] == '?') {
        /* keep base path, replace query/fragment */
        br_strlcpy(out, base, max);
        char* q = out; while (*q && *q != (rel[0] == '#' ? '#' : '?')) q++;
        if (rel[0] == '?') { char* h = out; while (*h && *h != '#') h++; if (h < q) q = h; }
        *q = 0;
        br_strlcat(out, rel, max);
        return;
    }
    /* relative path: directory of base path + rel */
    const char* path_end = path_start;
    while (*path_end && *path_end != '?' && *path_end != '#') path_end++;
    const char* last_slash = NULL;
    for (const char* s = path_start; s < path_end; s++) if (*s == '/') last_slash = s;
    br_strlcpy(out, origin, max);
    if (last_slash) {
        int l = (int)strlen(out);
        for (const char* s = path_start; s <= last_slash && l < max - 1; s++) out[l++] = *s;
        out[l] = 0;
    } else br_strlcat(out, "/", max);
    /* resolve ./ and ../ */
    while (br_streq_prefix(rel, "./")) rel += 2;
    while (br_streq_prefix(rel, "../")) {
        rel += 3;
        int l = (int)strlen(out);
        if (l > 0 && out[l - 1] == '/') l--;
        while (l > (int)strlen(origin) && out[l - 1] != '/') l--;
        out[l] = 0;
        if (l <= (int)strlen(origin)) br_strlcat(out, "/", max);
    }
    br_strlcat(out, rel, max);
}

/* ------------------------------------------------------------- fetch */

/* Fetches a resource into out; returns byte count or -1. Handles http:,
 * file: and about: URLs. */
int br_fetch_resource(const char* url, uint8_t* out, int max) {
    if (is_net_url(url)) {
        if (!net_configured) return -1;
        int n = net_http_get(url, out, (uint32_t)max);   /* http:// and https:// (TLS 1.3) */
        return n;
    }
    if (br_streq_prefix(url, "file:")) {
        const char* name = url + 5;
        while (*name == '/') name++;
        struct fs_node* f = search_path(name);
        if (!f) return -1;
        int n = f->content_len;
        if (n > max) n = max;
        for (int i = 0; i < n; i++) out[i] = (uint8_t)f->content[i];
        return n;
    }
    return -1;
}

/* ------------------------------------------------------- built-in pages */

static const char home_page[] =
    "<html><head><title>Shark Navigator</title>"
    "<style>"
    "body{background:#c0c0c0;margin:0}"
    ".hero{background:#000080;color:#ffffff;padding:14px 18px}"
    ".hero h1{margin:0 0 4px 0;font-size:200%}"
    ".hero p{margin:0;color:#c0c0ff}"
    ".card{background:#ffffff;border:1px solid #808080;margin:12px 18px;padding:10px 14px}"
    "h2{font-size:120%;margin:0 0 6px 0;color:#000080}"
    "ul{margin:4px 0}"
    "code{background:#e0e0e0}"
    ".small{font-size:80%;color:#404040}"
    "</style></head><body>"
    "<div class=hero><h1>Shark Navigator</h1><p>A small web browser built into SharkOS</p></div>"
    "<div class=card><h2>Search the web</h2>"
    "<form action=\"https://www.bing.com/search\"><input name=q size=40 placeholder=\"search terms\"> <input type=submit value=Search></form>"
    "<p class=small>Bing serves real results to browsers without JavaScript; Google only answers with an "
    "&quot;enable JavaScript&quot; page, so its front page loads here but searching there does not.</p></div>"
    "<div class=card><h2>Go somewhere</h2>"
    "<ul>"
    "<li><a href=\"https://example.com/\">https://example.com</a> - the classic test page, over TLS</li>"
    "<li><a href=\"https://text.npr.org/\">text.npr.org</a> - text-only news (https)</li>"
    "<li><a href=\"https://www.google.com/\">google.com</a> - the front page renders; results need JavaScript</li>"
    "<li><a href=\"https://wiby.me/\">wiby.me</a> - a search engine for simple pages (https)</li>"
    "<li><a href=\"https://legiblenews.com/\">Legible News</a> - plain-HTML news (https)</li>"
    "<li><a href=\"http://info.cern.ch/hypertext/WWW/TheProject.html\">info.cern.ch</a> - the first web page (plain http)</li>"
    "<li><a href=\"about:demo\">about:demo</a> - CSS + JavaScript feature demo (works offline)</li>"
    "<li><a href=\"file:readme.txt\">file:readme.txt</a> - a file from the SharkOS file system</li>"
    "</ul></div>"
    "<div class=card><h2>How to use it</h2>"
    "<p>Click the address bar (or press <b>Ctrl+L</b>), type a URL and press <b>Enter</b>. "
    "Scroll with the <b>arrow keys</b> or the scrollbar, press <b>Esc</b> to stop typing.</p>"
    "<p>Toolbar: <b>Back</b>, <b>Fwd</b>, <b>Reload</b>, <b>Home</b> and the <b>Go</b> button. "
    "The <b>Links</b> button opens the bookmark menu.</p>"
    "<p class=small>Supports HTML 4-ish markup, a useful subset of CSS, a JavaScript interpreter with DOM access, "
    "PNG/GIF/JPEG images, TrueType and @font-face fonts, forms (GET), <code>http://</code> and <code>https://</code> (TLS 1.3, AES-128-GCM, X25519) over the built-in TCP/IP stack (DHCP configured at boot). "
    "Note: certificates are <b>not</b> validated - the connection is encrypted but the server's identity is taken on trust.</p>"
    "</div>"
    "<div class=card><h2>Status</h2><p id=net>Network: <span id=netstate>checking...</span></p>"
    "<script>"
    "var el = document.getElementById('netstate');"
    "el.textContent = navigator.onLine ? 'online (DHCP bound)' : 'offline - about: and file: pages still work';"
    "el.style.color = navigator.onLine ? '#008000' : '#800000';"
    "</script></div>"
    "</body></html>";

static const char demo_page[] =
    "<html><head><title>about:demo</title>"
    "<style>"
    "body{font-family:sans-serif;background:#f4f4f4;margin:10px}"
    "h1{color:#000080;border-bottom:2px solid #000080;margin:0 0 8px 0}"
    "h2{font-size:120%;margin:12px 0 4px 0}"
    ".box{background:#fff;border:1px solid #999;padding:8px;margin:6px 0}"
    ".red{color:red}.green{color:green}.blue{color:blue}"
    ".hl{background:yellow}"
    "#counter{font-weight:bold;font-size:150%}"
    "button{background:#c0c0c0;padding:2px 8px}"
    "table{border:1px solid #808080;width:100%}"
    "th{background:#000080;color:#fff}"
    "td{background:#fff}"
    ".done{text-decoration:line-through;color:#808080}"
    "pre{background:#eee;padding:4px;border:1px solid #ccc}"
    "</style></head><body>"
    "<h1>Shark Navigator demo</h1>"
    "<p>This page exercises the HTML parser, the CSS cascade and the JavaScript engine. Nothing here needs a network.</p>"
    "<h2>Text styling</h2>"
    "<div class=box><b>bold</b>, <i>italic</i>, <u>underlined</u>, <s>struck</s>, <code>code</code>, "
    "<span class=red>red</span>, <span class=green>green</span>, <span class=blue>blue</span>, "
    "<span class=hl>highlighted</span>, <a href=\"about:home\">a link</a>, <small>small</small> and <big>big</big> text. "
    "Entities: &amp; &lt; &gt; &quot; &copy; &euro; &mdash; &nbsp;done.</div>"
    "<h2>JavaScript</h2>"
    "<div class=box>"
    "<p>Counter: <span id=counter>0</span> "
    "<button onclick=\"inc(1)\">+1</button> <button onclick=\"inc(-1)\">-1</button> "
    "<button id=reset>reset</button></p>"
    "<p>Clock (setInterval): <span id=clock>--:--:--</span></p>"
    "<p>Type here: <input id=name value=\"Shark\" size=16> <button id=greet>Greet</button> <span id=greeting></span></p>"
    "<p>Todo list: <input id=todo size=20 placeholder=\"new item\"> <button id=add>Add</button></p>"
    "<ul id=todos><li>Boot SharkOS <span class=done>(done)</span></li><li>Open the browser</li></ul>"
    "<p id=out></p>"
    "</div>"
    "<h2>Table</h2>"
    "<table><tr><th>Feature</th><th>Status</th></tr>"
    "<tr><td>HTML parser</td><td class=green>ok</td></tr>"
    "<tr><td>CSS cascade</td><td class=green>ok</td></tr>"
    "<tr><td>JavaScript</td><td id=jsok class=red>not run</td></tr>"
    "<tr><td>Images (PNG)</td><td>needs http</td></tr></table>"
    "<h2>Lists</h2>"
    "<ol><li>First</li><li>Second<ul><li>nested a</li><li>nested b</li></ul></li><li>Third</li></ol>"
    "<h2>Preformatted</h2>"
    "<pre>int main(void) {\n    return 0;   /* spaces kept */\n}</pre>"
    "<hr><p><i>Generated by about:demo</i></p>"
    "<script>"
    "var count = 0;"
    "function inc(d) { count += d; document.getElementById('counter').textContent = count; }"
    "document.getElementById('reset').addEventListener('click', function() { count = 0; inc(0); });"
    "document.getElementById('jsok').textContent = 'ok'; document.getElementById('jsok').className = 'green';"
    "document.getElementById('greet').onclick = function() {"
    "  var n = document.getElementById('name').value;"
    "  document.getElementById('greeting').innerHTML = 'Hello, <b>' + n + '</b>! (' + n.length + ' chars)';"
    "};"
    "document.getElementById('add').onclick = function() {"
    "  var inp = document.getElementById('todo');"
    "  if (!inp.value) return;"
    "  var li = document.createElement('li'); li.textContent = inp.value;"
    "  li.onclick = function() { li.classList.toggle('done'); };"
    "  document.getElementById('todos').appendChild(li); inp.value = '';"
    "};"
    "setInterval(function() {"
    "  var d = new Date();"
    "  function p(n) { return (n < 10 ? '0' : '') + n; }"
    "  document.getElementById('clock').textContent = p(d.getHours()) + ':' + p(d.getMinutes()) + ':' + p(d.getSeconds());"
    "}, 1000);"
    "var arr = [3, 1, 2].sort(function(a, b) { return a - b; });"
    "var obj = { name: 'shark', legs: 0, tags: ['fish', 'fast'] };"
    "document.getElementById('out').innerHTML = 'Array: ' + arr.join(', ') + ' | JSON: ' + JSON.stringify(obj) + ' | Math: ' + Math.floor(Math.sqrt(144)) + ' | ' + [1,2,3].map(function(x) { return x * x; }).join('-');"
    "console.log('demo page script finished, count =', count);"
    "</script>"
    "</body></html>";

static void build_error_page(const char* url, const char* reason) {
    br_page_len = 0;
    br_page_src[0] = 0;
    br_strlcat(br_page_src, "<html><head><title>Cannot load page</title></head><body style=\"background:#fff;margin:16px\">"
        "<h2 style=\"color:#800000\">The page cannot be displayed</h2><p>Shark Navigator could not open <b>", BR_PAGE_MAX);
    br_strlcat(br_page_src, url, BR_PAGE_MAX);
    br_strlcat(br_page_src, "</b>.</p><p>", BR_PAGE_MAX);
    br_strlcat(br_page_src, reason, BR_PAGE_MAX);
    br_strlcat(br_page_src, "</p><hr><p><a href=\"about:home\">Home page</a></p></body></html>", BR_PAGE_MAX);
    br_page_len = (int)strlen(br_page_src);
}

static void build_console_page(void) {
    br_page_src[0] = 0;
    br_strlcat(br_page_src, "<html><head><title>JavaScript console</title></head><body style=\"background:#fff;margin:12px\">"
        "<h2>JavaScript console</h2><p>Last console.log() / error lines from the previous page:</p><pre style=\"background:#000;color:#0f0;padding:6px\">", BR_PAGE_MAX);
    if (brs.js_console_lines == 0) br_strlcat(br_page_src, "(empty)", BR_PAGE_MAX);
    for (int i = 0; i < brs.js_console_lines; i++) {
        /* escape < and & */
        const char* s = brs.js_console[i];
        char line[200]; int o = 0;
        for (int k = 0; s[k] && o < 190; k++) {
            if (s[k] == '<') { line[o++] = '&'; line[o++] = 'l'; line[o++] = 't'; line[o++] = ';'; }
            else if (s[k] == '&') { line[o++] = '&'; line[o++] = 'a'; line[o++] = 'm'; line[o++] = 'p'; line[o++] = ';'; }
            else line[o++] = s[k];
        }
        line[o] = 0;
        br_strlcat(br_page_src, line, BR_PAGE_MAX);
        br_strlcat(br_page_src, "\n", BR_PAGE_MAX);
    }
    br_strlcat(br_page_src, "</pre><p><a href=\"about:home\">Home</a></p></body></html>", BR_PAGE_MAX);
    br_page_len = (int)strlen(br_page_src);
}

static char source_copy[BR_PAGE_MAX];
static int source_copy_len = 0;

static void build_source_page(void) {
    /* HTML-escape the previous page source into a <pre> */
    br_page_src[0] = 0;
    br_strlcat(br_page_src, "<html><head><title>Page source</title></head><body style=\"background:#fff;margin:8px\"><pre style=\"font-size:80%\">", BR_PAGE_MAX);
    int o = (int)strlen(br_page_src);
    for (int i = 0; i < source_copy_len && o < BR_PAGE_MAX - 80; i++) {
        char c = source_copy[i];
        if (c == '<') { br_page_src[o++] = '&'; br_page_src[o++] = 'l'; br_page_src[o++] = 't'; br_page_src[o++] = ';'; }
        else if (c == '&') { br_page_src[o++] = '&'; br_page_src[o++] = 'a'; br_page_src[o++] = 'm'; br_page_src[o++] = 'p'; br_page_src[o++] = ';'; }
        else br_page_src[o++] = c;
    }
    br_page_src[o] = 0;
    br_strlcat(br_page_src, "</pre></body></html>", BR_PAGE_MAX);
    br_page_len = (int)strlen(br_page_src);
}

/* ---------------------------------------------------------- navigation */

static void set_window_title(void) {
    if (!brs.win) return;
    char t[WINDOW_TITLE_MAX];
    br_strlcpy(t, brs.title[0] ? brs.title : brs.url, sizeof(t));
    if (brs.title[0]) {
        int l = (int)strlen(t);
        if (l + 6 < WINDOW_TITLE_MAX) br_strlcat(t, " - SN", sizeof(t));
    }
    br_strlcpy(brs.win->title, t, WINDOW_TITLE_MAX);
}

/* External stylesheets: fetched into a static buffer (never the stack), then
 * parsed. @import inside a fetched sheet is followed one level deep. Only
 * the current page's sheets are cached (by URL) so a reload/relayout of the
 * same document does not refetch them. */
static uint8_t css_buf[BR_CSS_BUF];
#define CSS_CACHE_SLOTS 6
static struct { char url[BR_URL_MAX]; int off, len; } css_cache[CSS_CACHE_SLOTS];
static int css_cache_n = 0, css_cache_used = 0;
static int sheets_fetched = 0;

static void css_cache_reset(void) { css_cache_n = 0; css_cache_used = 0; sheets_fetched = 0; }

static void load_sheet(const char* abs, int depth) {
    if (depth > 1) return;
    for (int i = 0; i < css_cache_n; i++) {
        if (br_streq(css_cache[i].url, abs)) { br_css_parse_sheet((const char*)css_buf + css_cache[i].off, css_cache[i].len); return; }
    }
    if (css_cache_n >= CSS_CACHE_SLOTS || sheets_fetched >= 8) return;
    int room = (int)sizeof(css_buf) - css_cache_used - 1;
    if (room < 1024) return;
    br_status("Loading stylesheet...");
    sheets_fetched++;
    int len = br_fetch_resource(abs, css_buf + css_cache_used, room);
    if (len <= 0) return;
    /* a "stylesheet" that is really an HTML error page: skip */
    if (len > 14 && (my_strstr_ci((const char*)css_buf + css_cache_used, "<!doctype") == (const char*)css_buf + css_cache_used || my_strstr_ci((const char*)css_buf + css_cache_used, "<html") == (const char*)css_buf + css_cache_used)) return;
    css_buf[css_cache_used + len] = 0;
    br_strlcpy(css_cache[css_cache_n].url, abs, BR_URL_MAX);
    css_cache[css_cache_n].off = css_cache_used;
    css_cache[css_cache_n].len = len;
    css_cache_n++;
    int before = br_css_import_count();
    br_css_parse_sheet((const char*)css_buf + css_cache_used, len);
    css_cache_used += len + 1;
    /* nested imports (resolved against the sheet's own URL) */
    int after = br_css_import_count();
    for (int i = before; i < after && i < 4; i++) {
        char sub[BR_URL_MAX];
        br_resolve_url(abs, br_css_import_url(i), sub, sizeof(sub));
        load_sheet(sub, depth + 1);
    }
}

static void collect_styles(br_node_t* n) {
    if (!n || br_stack_headroom() < BR_STACK_MIN) return;
    if (n->type == BR_NODE_ELEMENT) {
        if (br_streq(n->tag, "noscript") && !br_css_noscript_visible()) return;   /* like a scripting browser */
        if (br_streq(n->tag, "style") && n->first_child && n->first_child->type == BR_NODE_TEXT) {
            const char* media = br_attr(n, "media");
            if (!media || !my_strstr_ci(media, "print")) {
                int before = br_css_import_count();
                br_css_parse_sheet(n->first_child->text, (int)strlen(n->first_child->text));
                int after = br_css_import_count();
                for (int i = before; i < after && i < 4; i++) {
                    char abs[BR_URL_MAX];
                    br_resolve_url(brs.url, br_css_import_url(i), abs, sizeof(abs));
                    if (is_net_url(abs)) load_sheet(abs, 1);
                }
            }
        } else if (br_streq(n->tag, "link")) {
            const char* rel = br_attr(n, "rel");
            const char* href = br_attr(n, "href");
            const char* media = br_attr(n, "media");
            if (rel && href && my_strstr_ci(rel, "stylesheet") && !my_strstr_ci(rel, "alternate") && is_net_url(brs.url) &&
                !(media && (my_strstr_ci(media, "print") || my_strstr_ci(media, "dark")))) {
                char abs[BR_URL_MAX];
                br_resolve_url(brs.url, href, abs, sizeof(abs));
                if (is_net_url(abs)) load_sheet(abs, 0);
            }
        }
    }
    for (br_node_t* c = n->first_child; c; c = c->next) collect_styles(c);
}

/* @font-face: fetch each declared TrueType/OpenType file (once per page)
 * straight into the font arena and register it under its family name. */
static void load_web_fonts(void) {
    if (!is_net_url(brs.url)) return;
    int n = br_css_fontface_count();
    for (int i = 0; i < n; i++) {
        br_fontface_t* ff = br_css_fontface(i);
        if (ff->loaded) continue;
        ff->loaded = -1;
        if (br_font_web_has(ff->family, ff->bold)) continue;              /* same weight already loaded (unicode-range duplicates) */
        if (ff->italic && br_font_find_web(ff->family, 0) >= 0) continue;  /* italic is synthesised from the upright face */
        char abs[BR_URL_MAX];
        br_resolve_url(brs.url, ff->url, abs, sizeof(abs));
        if (!is_net_url(abs)) continue;
        uint32_t avail = 0;
        uint8_t* dst = br_font_web_alloc(FONT_WEB_ARENA, &avail);
        if (avail < 8 * 1024) break;
        br_status("Loading font...");
        int len = br_fetch_resource(abs, dst, (int)avail);
        if (len <= 12) continue;
        if (len >= (int)avail) continue;                 /* truncated: unusable */
        int id = br_font_add_web(ff->family, ff->bold, ff->italic, dst, (uint32_t)len);
        if (id >= 0) { ff->loaded = 1; ff->family_id = id; }
    }
}

static void extract_title(void) {
    brs.title[0] = 0;
    br_node_t* t = br_find_first(br_doc, "title");
    if (t) {
        char buf[200];
        br_node_text_content(t, buf, sizeof(buf));
        /* collapse whitespace/newlines */
        int o = 0, sp = 1;
        for (int i = 0; buf[i] && o < (int)sizeof(brs.title) - 1; i++) {
            char c = buf[i];
            if (c == '\n' || c == '\t' || c == '\r') c = ' ';
            if (c == ' ') { if (sp) continue; sp = 1; } else sp = 0;
            brs.title[o++] = c;
        }
        while (o > 0 && brs.title[o - 1] == ' ') o--;
        brs.title[o] = 0;
    }
}

static void layout_now(void) {
    if (!br_doc) return;
    br_css_reset();
    collect_styles(br_doc);
    load_web_fonts();
    br_css_compute(br_doc);
    br_layout(br_doc, brs.layout_width);
    int max_scroll = brs.page_h - (brs.win ? brs.win->rect.client_h - BR_TOOLBAR_H - BR_ADDR_H - BR_STATUS_H : 300);
    if (max_scroll < 0) max_scroll = 0;
    if (brs.scroll_y > max_scroll) brs.scroll_y = max_scroll;
    if (brs.scroll_y < 0) brs.scroll_y = 0;
    brs.needs_layout = 0;
}

/* Re-run only the cascade + layout (DOM mutated by script, hover change). */
static void relayout_cheap(void) {
    if (!br_doc) return;
    br_css_compute(br_doc);
    br_layout(br_doc, brs.layout_width);
    int max_scroll = brs.page_h - (brs.win ? brs.win->rect.client_h - BR_TOOLBAR_H - BR_ADDR_H - BR_STATUS_H : 300);
    if (max_scroll < 0) max_scroll = 0;
    if (brs.scroll_y > max_scroll) brs.scroll_y = max_scroll;
    brs.needs_layout = 0;
}

/* No visible text, picture or form control at all? Then the page relies on
 * scripts we could not run (an app shell, Google's result page, ...). */
static int page_is_blank(void) {
    int n = br_box_count();
    for (int i = 0; i < n; i++) {
        br_box_t* b = br_box(i);
        if (b->kind == BR_BOX_TEXT) {
            for (int k = 0; k < b->text_len; k++) {
                uint8_t c = (uint8_t)b->text[k];
                if (c == 0xC2 && k + 1 < b->text_len && (uint8_t)b->text[k + 1] == 0xA0) { k++; continue; }   /* nbsp */
                if (c > ' ') return 0;
            }
        } else if (b->kind == BR_BOX_IMAGE) {
            if (b->w >= 8 && b->h >= 8) return 0;      /* tracking pixels do not count */
        } else if (b->kind == BR_BOX_INPUT || b->kind == BR_BOX_BUTTON || b->kind == BR_BOX_CHECKBOX) return 0;
    }
    return 1;
}

static int inside_hidden_noscript(br_node_t* n) {
    if (br_css_noscript_visible()) return 0;
    for (br_node_t* p = n->parent; p; p = p->parent)
        if (p->type == BR_NODE_ELEMENT && br_streq(p->tag, "noscript")) return 1;
    return 0;
}

static int auto_nav_chain = 0;    /* consecutive script/meta navigations without user input */
static int nav_is_auto = 0;       /* the next br_navigate() was not requested by the user */
static int page_needs_js = 0;     /* last page rendered empty and has scripts but no <noscript> */

/* <meta http-equiv="refresh" content="N;url=..."> - schedule the navigation. */
static void check_meta_refresh(void) {
    if (!br_doc) return;
    for (int i = 0; i < br_dom_node_count(); i++) {
        br_node_t* n = br_dom_node(i);
        if (n->type != BR_NODE_ELEMENT || !br_streq(n->tag, "meta")) continue;
        const char* eq = br_attr(n, "http-equiv");
        const char* content = br_attr(n, "content");
        if (!eq || !content || !br_strieq(eq, "refresh") || inside_hidden_noscript(n)) continue;
        const char* p = content;
        while (*p == ' ') p++;
        if (*p < '0' || *p > '9') continue;
        int delay = 0;
        while (*p >= '0' && *p <= '9') { if (delay < 100000) delay = delay * 10 + (*p - '0'); p++; }
        while (*p == '.' || (*p >= '0' && *p <= '9')) p++;
        while (*p == ' ' || *p == ';' || *p == ',') p++;
        char target[BR_URL_MAX];
        if (*p) {
            if ((p[0] == 'u' || p[0] == 'U') && (p[1] == 'r' || p[1] == 'R') && (p[2] == 'l' || p[2] == 'L')) {
                p += 3;
                while (*p == ' ') p++;
                if (*p == '=') p++;
                while (*p == ' ') p++;
            }
            char q = (*p == '"' || *p == '\'') ? *p++ : 0;
            int o = 0;
            while (*p && *p != q && !(q == 0 && *p == ' ') && o < (int)sizeof(target) - 1) target[o++] = *p++;
            target[o] = 0;
            if (!o) br_strlcpy(target, brs.url, sizeof(target));
        } else {
            br_strlcpy(target, brs.url, sizeof(target));
        }
        char abs[BR_URL_MAX];
        br_resolve_url(brs.url, target, abs, sizeof(abs));
        if (!is_net_url(abs) && !br_streq_prefix(abs, "file:") && !br_streq_prefix(abs, "about:")) continue;
        if (delay == 0 && br_streq(abs, brs.url)) continue;              /* would loop forever */
        br_strlcpy(brs.refresh_url, abs, sizeof(brs.refresh_url));
        brs.refresh_at = uptime_ticks + (uint32_t)delay * TICKS_PER_SEC;
        brs.refresh_auto = (delay == 0);
        brs.refresh_replace = (delay == 0) || br_streq(abs, brs.url);
        brs.refresh_pending = 1;
        return;
    }
}

static void parse_and_show(void) {
    br_css_set_noscript(0);
    br_dom_reset();
    br_layout_reset_images();
    br_font_reset_page();
    css_cache_reset();
    br_doc = br_html_parse(br_page_src, br_page_len);
    brs.hover_link = -1;
    brs.focus_input = -1;
    brs.scroll_y = 0;
    extract_title();
    set_window_title();
    layout_now();
    /* Scripts run after the first layout so offsetWidth etc. have values. */
    brs.js_console_lines = 0;
    br_js_reset();
    br_status("Running scripts...");
    br_js_run_document(br_doc);
    /* the title may have been changed by script */
    int pending_nav = brs.needs_layout >= 2 ? brs.needs_layout : 0;   /* location.replace()/history.back() while loading */
    if (pending_nav == 2 && br_streq(brs.address, brs.url)) { pending_nav = 0; brs.needs_layout = 1; }   /* reload() during load: ignore */
    if (brs.needs_layout) { extract_title(); set_window_title(); relayout_cheap(); }
    else { set_window_title(); }
    page_needs_js = 0;
    if (!pending_nav && page_is_blank()) {
        if (br_find_first(br_doc, "noscript")) {
            /* Nothing visible: fall back to the <noscript> content like a
             * browser with scripting turned off would show it. */
            br_css_set_noscript(1);
            layout_now();
            br_js_console("page is empty without JavaScript - showing its <noscript> content");
        } else if (br_find_first(br_doc, "script")) {
            page_needs_js = 1;
            br_js_console("page is empty: it is built entirely by scripts this browser cannot run");
        }
    }
    if (!pending_nav) check_meta_refresh();
    else brs.needs_layout = pending_nav;          /* executed by the next draw (run_pending_nav) */
    /* fragment */
    const char* hash = brs.url;
    while (*hash && *hash != '#') hash++;
    if (*hash == '#' && hash[1]) {
        br_node_t* t = br_find_by_id(br_doc, hash + 1);
        if (t) brs.scroll_y = t->ly;
    }
}

static int redirect_depth = 0;

void br_navigate(const char* url_in, int push_history) {
    char url[BR_URL_MAX];
    br_strlcpy(url, url_in, sizeof(url));
    /* trim */
    int l = (int)strlen(url);
    while (l > 0 && (url[l - 1] == ' ' || url[l - 1] == '\n')) url[--l] = 0;
    const char* u = url;
    while (*u == ' ') u++;
    if (!*u) return;
    char full[BR_URL_MAX];
    if (br_streq_prefix(u, "http://") || br_streq_prefix(u, "https://") || br_streq_prefix(u, "about:") || br_streq_prefix(u, "file:")) {
        br_strlcpy(full, u, sizeof(full));
    } else if (br_streq_prefix(u, "www.") || my_strstr_ci(u, ".com") || my_strstr_ci(u, ".org") || my_strstr_ci(u, ".net") || my_strstr_ci(u, ".ch") || my_strstr_ci(u, ".de") || my_strstr_ci(u, ".io") || (u[0] >= '0' && u[0] <= '9') || my_strstr_ci(u, "localhost")) {
        br_strlcpy(full, "http://", sizeof(full));
        br_strlcat(full, u, sizeof(full));
    } else if (search_path(u)) {
        br_strlcpy(full, "file:", sizeof(full));
        br_strlcat(full, u, sizeof(full));
    } else {
        br_strlcpy(full, "http://", sizeof(full));
        br_strlcat(full, u, sizeof(full));
    }

    /* push_history: 1 = new entry (user navigation), 0 = keep (back/forward,
     * reload), 2 = replace the current entry (script/meta redirects). */
    int automatic = nav_is_auto;
    nav_is_auto = 0;
    brs.refresh_pending = 0;
    if (automatic) {
        /* pages bouncing between each other via script or <meta refresh> */
        if (++auto_nav_chain > 8) { br_status("Redirect loop stopped"); br_js_console("automatic redirect chain too long, stopped"); return; }
    } else auto_nav_chain = 0;
    if (push_history == 2 && brs.history_len == 0) push_history = 1;

    brs.loading = 1;
    brs.error = 0;
    brs.alert_open = 0;
    brs.menu_open = 0;
    brs.address_focus = 0;
    br_status("Connecting...");
    if (brs.win) { brs.win->needs_redraw = true; desktop_render(); }   /* show status immediately */

    /* keep source for about:source before overwriting */
    if (!br_streq(full, "about:source")) { source_copy_len = br_page_len; memcpy(source_copy, br_page_src, (size_t)br_page_len); }

    int ok = 1;
    if (br_streq(full, "about:home") || br_streq(full, "about:") || br_streq(full, "about:blank")) {
        if (br_streq(full, "about:blank")) { br_strlcpy(br_page_src, "<html><head><title>about:blank</title></head><body></body></html>", BR_PAGE_MAX); }
        else { br_strlcpy(br_page_src, home_page, BR_PAGE_MAX); }
        br_page_len = (int)strlen(br_page_src);
    } else if (br_streq(full, "about:demo")) {
        br_strlcpy(br_page_src, demo_page, BR_PAGE_MAX);
        br_page_len = (int)strlen(br_page_src);
    } else if (br_streq(full, "about:console")) {
        build_console_page();
    } else if (br_streq(full, "about:source")) {
        build_source_page();
    } else if (br_streq_prefix(full, "about:")) {
        build_error_page(full, "Unknown about: page. Try about:home, about:demo, about:console or about:source.");
        ok = 0;
    } else if (br_streq_prefix(full, "file:")) {
        int n = br_fetch_resource(full, (uint8_t*)br_page_src, BR_PAGE_MAX - 1);
        if (n < 0) { build_error_page(full, "File not found in the SharkOS file system (looked in the current directory and /System/Bin)."); ok = 0; }
        else {
            br_page_src[n] = 0; br_page_len = n;
            /* plain text files: wrap in <pre> unless they look like HTML */
            int looks_html = my_strstr_ci(br_page_src, "<html") || my_strstr_ci(br_page_src, "<body") || my_strstr_ci(br_page_src, "<p") || my_strstr_ci(br_page_src, "<div") || my_strstr_ci(br_page_src, "<h1");
            if (!looks_html) {
                static char tmp[BR_PAGE_MAX];
                tmp[0] = 0;
                br_strlcat(tmp, "<html><head><title>", BR_PAGE_MAX);
                br_strlcat(tmp, full + 5, BR_PAGE_MAX);
                br_strlcat(tmp, "</title></head><body style=\"background:#fff\"><pre>", BR_PAGE_MAX);
                int o = (int)strlen(tmp);
                for (int i = 0; i < n && o < BR_PAGE_MAX - 40; i++) {
                    char c = br_page_src[i];
                    if (c == '<') { tmp[o++] = '&'; tmp[o++] = 'l'; tmp[o++] = 't'; tmp[o++] = ';'; }
                    else if (c == '&') { tmp[o++] = '&'; tmp[o++] = 'a'; tmp[o++] = 'm'; tmp[o++] = 'p'; tmp[o++] = ';'; }
                    else tmp[o++] = c;
                }
                tmp[o] = 0;
                br_strlcat(tmp, "</pre></body></html>", BR_PAGE_MAX);
                memcpy(br_page_src, tmp, BR_PAGE_MAX);
                br_page_len = (int)strlen(br_page_src);
            }
        }
    } else {
        if (!net_configured) {
            build_error_page(full, "The network is not configured yet (no DHCP lease). Wait a few seconds after boot, check the Network window in the tray, or run <b>dhcp</b> in a Terminal.");
            ok = 0;
        } else {
            int n = br_fetch_resource(full, (uint8_t*)br_page_src, BR_PAGE_MAX - 1);
            if (n < 0) n = 0;
            br_page_src[n] = 0; br_page_len = n;
            int is_http = is_net_url(full);
            int is_https = br_streq_prefix(full, "https://");
            int status = is_http ? net_http_last_status : 200;
            if (is_http && status >= 300 && status <= 399 && net_http_last_location[0]) {
                /* HTTP redirect (301/302/303/307/308): follow the Location header */
                char loc[BR_URL_MAX];
                br_resolve_url(full, net_http_last_location, loc, sizeof(loc));
                if (is_net_url(loc) && redirect_depth < 5) {
                    redirect_depth++;
                    br_strlcpy(brs.url, full, sizeof(brs.url));
                    if (automatic) auto_nav_chain--;      /* HTTP hops of one automatic navigation count once */
                    nav_is_auto = automatic;
                    br_navigate(loc, push_history);
                    redirect_depth--;
                    return;
                } else {
                    build_error_page(full, "Too many redirects, or a redirect to an unsupported address.");
                    ok = 0;
                }
            } else if (n <= 0 && status == 0 && is_https && net_tls_last_error) {
                char msg[256];
                br_strlcpy(msg, "TLS handshake failed: ", sizeof(msg));
                br_strlcat(msg, tls_error_string(net_tls_last_error), sizeof(msg));
                br_strlcat(msg, ". Shark Navigator only speaks TLS 1.3 with AES-128-GCM and X25519.", sizeof(msg));
                build_error_page(full, msg);
                ok = 0;
            } else if (n <= 0 && status == 0) {
                build_error_page(full, "The server could not be reached (DNS failure, connection refused or timed out).");
                ok = 0;
            } else if (is_http && status == 404 && n < 64) {
                build_error_page(full, "404 Not Found: the server has no such page.");
                ok = 0;
            } else if (n <= 0) {
                build_error_page(full, "The server sent an empty reply.");
                ok = 0;
            }
        }
    }

    br_strlcpy(brs.url, full, sizeof(brs.url));
    br_strlcpy(brs.address, full, sizeof(brs.address));
    brs.address_cursor = (int)strlen(brs.address);
    brs.error = !ok;
    if (push_history == 2) {
        br_strlcpy(brs.history[brs.history_pos], full, BR_URL_MAX);
    } else if (push_history) {
        /* drop forward history */
        if (brs.history_pos < brs.history_len - 1) brs.history_len = brs.history_pos + 1;
        if (brs.history_len >= BR_HISTORY_MAX) {
            for (int i = 1; i < BR_HISTORY_MAX; i++) memcpy(brs.history[i - 1], brs.history[i], BR_URL_MAX);
            brs.history_len--;
        }
        br_strlcpy(brs.history[brs.history_len], full, BR_URL_MAX);
        brs.history_pos = brs.history_len;
        brs.history_len++;
    }
    parse_and_show();
    brs.loading = 0;
    char st[160];
    br_strlcpy(st, ok ? "Done" : "Error", sizeof(st));
    if (ok) {
        char num[16];
        br_strlcat(st, " - ", sizeof(st));
        br_itoa(br_page_len, num); br_strlcat(st, num, sizeof(st));
        br_strlcat(st, " bytes, ", sizeof(st));
        br_itoa(br_dom_node_count(), num); br_strlcat(st, num, sizeof(st));
        br_strlcat(st, " nodes", sizeof(st));
        if (is_net_url(full) && net_http_last_status && net_http_last_status != 200) {
            br_strlcat(st, " (HTTP ", sizeof(st)); br_itoa(net_http_last_status, num); br_strlcat(st, num, sizeof(st)); br_strlcat(st, ")", sizeof(st));
        }
        if (br_streq_prefix(full, "https://")) {
            br_strlcat(st, " - TLS 1.3, cert: ", sizeof(st));
            br_strlcat(st, net_tls_peer_cn[0] ? net_tls_peer_cn : "?", sizeof(st));
            br_strlcat(st, " (unverified)", sizeof(st));
        }
        if (br_page_len >= BR_PAGE_MAX - 1) br_strlcat(st, " (truncated)", sizeof(st));
        if (page_needs_js) br_strlcat(st, " - page needs JavaScript", sizeof(st));
    }
    br_status(st);
}

static void go_back(void) {
    if (brs.history_pos > 0) { brs.history_pos--; br_navigate(brs.history[brs.history_pos], 0); }
}
static void go_forward(void) {
    if (brs.history_pos < brs.history_len - 1) { brs.history_pos++; br_navigate(brs.history[brs.history_pos], 0); }
}

/* Scripts/forms ask for navigation by setting needs_layout = 2 (3 = back);
 * it is executed here, outside the interpreter. */
static void run_pending_nav(int automatic) {
    if (brs.needs_layout == 2) {
        int mode = brs.nav_replace ? 2 : 1;
        brs.needs_layout = 0; brs.nav_replace = 0;
        nav_is_auto = automatic;
        br_navigate(brs.address, mode);
    } else if (brs.needs_layout == 3) {
        brs.needs_layout = 0;
        nav_is_auto = automatic;
        go_back();
    }
    nav_is_auto = 0;
}

/* --------------------------------------------------------------- state */

static void br_reset_state(window_t* w) {
    br_font_init();
    memset(&brs, 0, sizeof(brs));
    brs.win = w;
    brs.hover_link = -1;
    brs.focus_input = -1;
    brs.history_pos = -1;
    brs.layout_width = w->rect.client_w - BR_SCROLL_W - 2;
    br_strlcpy(brs.address, "about:home", sizeof(brs.address));
}

static int viewport_top(window_t* w) { return w->rect.client_y + BR_TOOLBAR_H + BR_ADDR_H; }
static int viewport_h(window_t* w) { return w->rect.client_h - BR_TOOLBAR_H - BR_ADDR_H - BR_STATUS_H; }
static int viewport_w(window_t* w) { return w->rect.client_w - BR_SCROLL_W; }

static int max_scroll(window_t* w) {
    int m = brs.page_h + 8 - viewport_h(w);
    return m < 0 ? 0 : m;
}

/* ---------------------------------------------------------------- paint */

/* Text runs are TrueType glyphs alpha-blended straight onto whatever is
 * already painted, so no background colour is needed. */
static void draw_text_run(const br_box_t* b, const char* s, int len, int x, int baseline_y, uint32_t fg, const w98_rect_t* clip) {
    int face = b->face, px = b->font_px > 0 ? b->font_px : 16;
    int flags = 0;
    if (b->italic) flags |= FONT_DRAW_ITALIC;
    if (b->bold && !br_font_face_is_bold(face)) flags |= FONT_DRAW_BOLD;
    br_font_draw_ex(face, px, s, len, x, baseline_y, fg, clip, flags);
}

static void fill_clipped(int x, int y, int w, int h, uint32_t c, const w98_rect_t* clip) {
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (x0 < clip->x) x0 = clip->x;
    if (y0 < clip->y) y0 = clip->y;
    if (x1 > clip->x + clip->w) x1 = clip->x + clip->w;
    if (y1 > clip->y + clip->h) y1 = clip->y + clip->h;
    if (x1 <= x0 || y1 <= y0) return;
    w98_fill(x0, y0, x1 - x0, y1 - y0, c);
}

static void draw_image_box(br_box_t* b, int sx, int sy, const w98_rect_t* clip) {
    br_image_t* im = br_image_get(b->img_index);
    if (im && im->pixels) {
        /* nearest-neighbour scale into the box */
        int x0 = sx, y0 = sy, x1 = sx + b->w, y1 = sy + b->h;
        if (x0 < clip->x) x0 = clip->x;
        if (y0 < clip->y) y0 = clip->y;
        if (x1 > clip->x + clip->w) x1 = clip->x + clip->w;
        if (y1 > clip->y + clip->h) y1 = clip->y + clip->h;
        if (x1 <= x0 || y1 <= y0) return;
        uint32_t stride = (uint32_t)(screen_pitch / 4);
        for (int py = y0; py < y1; py++) {
            int iy = (py - sy) * im->h / (b->h ? b->h : 1);
            if (iy >= im->h) iy = im->h - 1;
            uint32_t* row = &lfbptr[(uint32_t)py * stride];
            const uint32_t* src = &im->pixels[iy * im->w];
            for (int px = x0; px < x1; px++) {
                int ix = (px - sx) * im->w / (b->w ? b->w : 1);
                if (ix >= im->w) ix = im->w - 1;
                uint32_t p = src[ix];
                if ((p >> 24) < 0x80) continue;             /* transparent */
                row[px] = p | 0xFF000000u;
            }
        }
        if (b->border > 0) {
            uint32_t bc = b->border_color ? b->border_color : 0xFF808080u;
            int bw = b->border;
            fill_clipped(sx, sy, b->w, bw, bc, clip);
            fill_clipped(sx, sy + b->h - bw, b->w, bw, bc, clip);
            fill_clipped(sx, sy, bw, b->h, bc, clip);
            fill_clipped(sx + b->w - bw, sy, bw, b->h, bc, clip);
        }
        return;
    }
    /* placeholder: bordered box with alt text */
    fill_clipped(sx, sy, b->w, b->h, 0xFFF0F0F0u, clip);
    fill_clipped(sx, sy, b->w, 1, 0xFF808080u, clip);
    fill_clipped(sx, sy + b->h - 1, b->w, 1, 0xFF808080u, clip);
    fill_clipped(sx, sy, 1, b->h, 0xFF808080u, clip);
    fill_clipped(sx + b->w - 1, sy, 1, b->h, 0xFF808080u, clip);
    /* little "broken image" glyph */
    fill_clipped(sx + 3, sy + 3, 10, 10, 0xFF4080C0u, clip);
    fill_clipped(sx + 5, sy + 5, 3, 3, 0xFFFFFF00u, clip);
    if (b->text_len > 0 && b->w > 30 && b->h > 14) {
        w98_rect_t ic = { sx + 2, sy + 2, b->w - 4, b->h - 4 };
        if (ic.x < clip->x) { ic.w -= clip->x - ic.x; ic.x = clip->x; }
        if (ic.y < clip->y) { ic.h -= clip->y - ic.y; ic.y = clip->y; }
        if (ic.x + ic.w > clip->x + clip->w) ic.w = clip->x + clip->w - ic.x;
        if (ic.y + ic.h > clip->y + clip->h) ic.h = clip->y + clip->h - ic.y;
        if (ic.w > 0 && ic.h > 0) {
            int face = br_font_face(FONT_FAMILY_SANS, 0);
            /* alt text, clipped to the box */
            int n = b->text_len;
            while (n > 1 && br_font_text_width(face, 11, b->text, n) > b->w - 20) n--;
            br_font_draw(face, 11, b->text, n, sx + 16, sy + 4 + br_font_ascent(face, 11), 0xFF404040u, &ic);
        }
    }
}

static void draw_page(window_t* w) {
    int vx = w->rect.client_x, vy = viewport_top(w);
    int vw = viewport_w(w), vh = viewport_h(w);
    w98_rect_t clip = { vx, vy, vw, vh };
    uint32_t page_bg = 0xFFFFFFFFu;
    if (br_doc && br_doc->body && br_doc->body->style.background) page_bg = br_doc->body->style.background;
    w98_fill(vx, vy, vw, vh, page_bg);

    int n = br_box_count();
    int hover = brs.hover_link;
    for (int i = 0; i < n; i++) {
        br_box_t* b = br_box(i);
        int sx = vx + b->x, sy = vy + b->y - brs.scroll_y;
        if (sy + b->h < vy || sy > vy + vh) continue;
        if (sx > vx + vw) continue;
        switch (b->kind) {
        case BR_BOX_RECT:
            if (b->bg && (b->bg >> 24)) fill_clipped(sx, sy, b->w, b->h, b->bg, &clip);
            if (b->border > 0) {
                uint32_t bc = b->border_color ? b->border_color : 0xFF808080u;
                if (b->bt > 0) fill_clipped(sx, sy, b->w, b->bt, bc, &clip);
                if (b->bb > 0) fill_clipped(sx, sy + b->h - b->bb, b->w, b->bb, bc, &clip);
                if (b->bl > 0) fill_clipped(sx, sy, b->bl, b->h, bc, &clip);
                if (b->br_ > 0) fill_clipped(sx + b->w - b->br_, sy, b->br_, b->h, bc, &clip);
            }
            break;
        case BR_BOX_TEXT: {
            uint32_t fg = b->color ? b->color : 0xFF000000u;
            if (b->link && hover >= 0 && b->link->id == hover) fg = 0xFFEE0000u;
            uint32_t bg = (b->bg && (b->bg >> 24)) ? b->bg : 0;
            if (bg) fill_clipped(sx, sy - 1, b->w, b->h + 2, bg, &clip);
            int baseline = sy + b->baseline;
            draw_text_run(b, b->text, b->text_len, sx, baseline, fg, &clip);
            if (b->underline || (b->link && hover >= 0 && b->link->id == hover)) fill_clipped(sx, baseline + 2, b->w, 1, fg, &clip);
            if (b->strike) fill_clipped(sx, baseline - b->baseline / 3, b->w, 1, fg, &clip);
            break;
        }
        case BR_BOX_IMAGE:
            draw_image_box(b, sx, sy, &clip);
            if (b->link && hover >= 0 && b->link->id == hover) {
                fill_clipped(sx, sy, b->w, 1, 0xFFEE0000u, &clip); fill_clipped(sx, sy + b->h - 1, b->w, 1, 0xFFEE0000u, &clip);
                fill_clipped(sx, sy, 1, b->h, 0xFFEE0000u, &clip); fill_clipped(sx + b->w - 1, sy, 1, b->h, 0xFFEE0000u, &clip);
            }
            break;
        case BR_BOX_HR:
            fill_clipped(sx, sy, b->w, 1, 0xFF808080u, &clip);
            fill_clipped(sx, sy + 1, b->w, 1, 0xFFFFFFFFu, &clip);
            break;
        case BR_BOX_BULLET: {
            uint32_t fg = b->color ? b->color : 0xFF000000u;
            if (b->text) draw_text_run(b, b->text, b->text_len, sx, sy + b->baseline, fg, &clip);
            else {
                /* disc centred on the x-height middle */
                int px = b->font_px > 0 ? b->font_px : 16;
                int r = px >= 20 ? 4 : px >= 13 ? 3 : 2;
                int cyy = sy + b->baseline - px * 3 / 10;
                static const int rows3[6] = { 2, 4, 6, 6, 4, 2 };
                static const int rows4[8] = { 2, 6, 8, 8, 8, 8, 6, 2 };
                static const int rows2[4] = { 2, 4, 4, 2 };
                const int* rows = r == 4 ? rows4 : r == 3 ? rows3 : rows2;
                for (int k = 0; k < 2 * r; k++) fill_clipped(sx + 4 + r - rows[k] / 2, cyy - r + k, rows[k], 1, fg, &clip);
            }
            break;
        }
        case BR_BOX_INPUT: {
            int focused = b->node && brs.focus_input == b->node->id;
            fill_clipped(sx, sy, b->w, b->h, 0xFFFFFFFFu, &clip);
            /* sunken edge */
            fill_clipped(sx, sy, b->w, 1, 0xFF808080u, &clip); fill_clipped(sx, sy, 1, b->h, 0xFF808080u, &clip);
            fill_clipped(sx, sy + b->h - 1, b->w, 1, 0xFFFFFFFFu, &clip); fill_clipped(sx + b->w - 1, sy, 1, b->h, 0xFFFFFFFFu, &clip);
            fill_clipped(sx + 1, sy + 1, b->w - 2, 1, 0xFF000000u, &clip); fill_clipped(sx + 1, sy + 1, 1, b->h - 2, 0xFF000000u, &clip);
            const char* v = b->node && b->node->value ? b->node->value : "";
            const char* type = b->node ? br_attr(b->node, "type") : NULL;
            char masked[64];
            if (type && br_strieq(type, "password")) { int l = (int)strlen(v); if (l > 63) l = 63; for (int k = 0; k < l; k++) masked[k] = '*'; masked[l] = 0; v = masked; }
            int vl = (int)strlen(v);
            int px = b->font_px > 0 ? b->font_px : 13;
            /* show the tail of the value if it is wider than the field */
            int start = 0;
            while (start < vl && br_font_text_width(b->face, px, v + start, vl - start) > b->w - 8) start++;
            w98_rect_t ic = { sx + 2, sy + 2, b->w - 4, b->h - 4 };
            if (ic.x < clip.x) { ic.w -= clip.x - ic.x; ic.x = clip.x; }
            if (ic.y < clip.y) { ic.h -= clip.y - ic.y; ic.y = clip.y; }
            if (ic.x + ic.w > clip.x + clip.w) ic.w = clip.x + clip.w - ic.x;
            if (ic.y + ic.h > clip.y + clip.h) ic.h = clip.y + clip.h - ic.y;
            if (ic.w > 0 && ic.h > 0) {
                int baseline = sy + 3 + b->baseline;
                if (b->node && br_streq(b->node->tag, "textarea")) baseline = sy + 3 + b->baseline;
                else baseline = sy + (b->h - (b->baseline + br_font_descent(b->face, px))) / 2 + b->baseline;
                if (vl == 0 && !focused) {
                    const char* ph = b->node ? br_attr(b->node, "placeholder") : NULL;
                    if (ph) br_font_draw(b->face, px, ph, (int)strlen(ph), sx + 4, baseline, 0xFF808080u, &ic);
                } else {
                    br_font_draw(b->face, px, v + start, vl - start, sx + 4, baseline, 0xFF000000u, &ic);
                }
                if (focused && ((uptime_ticks / 500) & 1) == 0) fill_clipped(sx + 4 + br_font_text_width(b->face, px, v + start, vl - start), baseline - b->baseline, 1, b->baseline + br_font_descent(b->face, px), 0xFF000000u, &ic);
            }
            break;
        }
        case BR_BOX_BUTTON: {
            int pressed = brs.pressed_btn == 100 + i;
            uint32_t face = b->bg ? b->bg : W98_BTNFACE;
            fill_clipped(sx, sy, b->w, b->h, face, &clip);
            uint32_t tl = pressed ? 0xFF000000u : 0xFFFFFFFFu, br = pressed ? 0xFFFFFFFFu : 0xFF000000u;
            fill_clipped(sx, sy, b->w, 1, tl, &clip); fill_clipped(sx, sy, 1, b->h, tl, &clip);
            fill_clipped(sx, sy + b->h - 1, b->w, 1, br, &clip); fill_clipped(sx + b->w - 1, sy, 1, b->h, br, &clip);
            if (!pressed) { fill_clipped(sx + 1, sy + b->h - 2, b->w - 2, 1, 0xFF808080u, &clip); fill_clipped(sx + b->w - 2, sy + 1, 1, b->h - 2, 0xFF808080u, &clip); }
            int px = b->font_px > 0 ? b->font_px : 13;
            int tw = br_font_text_width(b->face, px, b->text, b->text_len);
            w98_rect_t ic = { sx + 2, sy + 2, b->w - 4, b->h - 4 };
            if (ic.x < clip.x) { ic.w -= clip.x - ic.x; ic.x = clip.x; }
            if (ic.y < clip.y) { ic.h -= clip.y - ic.y; ic.y = clip.y; }
            if (ic.x + ic.w > clip.x + clip.w) ic.w = clip.x + clip.w - ic.x;
            if (ic.y + ic.h > clip.y + clip.h) ic.h = clip.y + clip.h - ic.y;
            int baseline = sy + (b->h - (b->baseline + br_font_descent(b->face, px))) / 2 + b->baseline + pressed;
            if (ic.w > 0 && ic.h > 0) br_font_draw(b->face, px, b->text, b->text_len, sx + (b->w - tw) / 2 + pressed, baseline, b->color ? b->color : 0xFF000000u, &ic);
            break;
        }
        case BR_BOX_CHECKBOX: {
            fill_clipped(sx, sy, b->w, b->h, 0xFFFFFFFFu, &clip);
            fill_clipped(sx, sy, b->w, 1, 0xFF808080u, &clip); fill_clipped(sx, sy, 1, b->h, 0xFF808080u, &clip);
            fill_clipped(sx, sy + b->h - 1, b->w, 1, 0xFFFFFFFFu, &clip); fill_clipped(sx + b->w - 1, sy, 1, b->h, 0xFFFFFFFFu, &clip);
            fill_clipped(sx + 1, sy + 1, b->w - 2, 1, 0xFF000000u, &clip); fill_clipped(sx + 1, sy + 1, 1, b->h - 2, 0xFF000000u, &clip);
            int checked = b->node && (b->node->checked || (br_attr(b->node, "checked") && !b->node->value));
            const char* type = b->node ? br_attr(b->node, "type") : NULL;
            if (checked) {
                if (type && br_strieq(type, "radio")) fill_clipped(sx + 4, sy + 4, b->w - 8, b->h - 8, 0xFF000000u, &clip);
                else {
                    /* check mark */
                    for (int k = 0; k < 3; k++) fill_clipped(sx + 3 + k, sy + 6 + k, 1, 3, 0xFF000000u, &clip);
                    for (int k = 0; k < 5; k++) fill_clipped(sx + 6 + k, sy + 8 - k, 1, 3, 0xFF000000u, &clip);
                }
            }
            break;
        }
        }
    }
}

static void draw_scrollbar(window_t* w) {
    int x = w->rect.client_x + w->rect.client_w - BR_SCROLL_W;
    int y = viewport_top(w), h = viewport_h(w);
    /* track */
    for (int py = y; py < y + h; py++) for (int px = x; px < x + BR_SCROLL_W; px++) draw_pixel(px, py, ((px + py) & 1) ? W98_BTNFACE : W98_BTNHILITE);
    /* arrows */
    w98_button(x, y, BR_SCROLL_W, 16, "", brs.pressed_btn == 1, 1, 0);
    w98_button(x, y + h - 16, BR_SCROLL_W, 16, "", brs.pressed_btn == 2, 1, 0);
    for (int k = 0; k < 4; k++) { w98_fill(x + 7 - k, y + 5 + k, 1 + 2 * k, 1, W98_BTNTEXT); w98_fill(x + 7 - k, y + h - 6 - k, 1 + 2 * k, 1, W98_BTNTEXT); }
    /* thumb */
    int track = h - 32;
    int ms = max_scroll(w);
    int total = brs.page_h + 8 > h ? brs.page_h + 8 : h;
    int th = total > 0 ? track * h / total : track;
    if (th < 12) th = 12;
    if (th > track) th = track;
    int ty = y + 16 + (ms > 0 ? (track - th) * brs.scroll_y / ms : 0);
    w98_fill(x, ty, BR_SCROLL_W, th, W98_BTNFACE);
    w98_bevel(x, ty, BR_SCROLL_W, th, W98_BEVEL_RAISED);
}

static void draw_toolbar(window_t* w) {
    int x = w->rect.client_x, y = w->rect.client_y, cw = w->rect.client_w;
    w98_fill(x, y, cw, BR_TOOLBAR_H + BR_ADDR_H, W98_BTNFACE);
    int bx = x + 4, by = y + 4;
    int can_back = brs.history_pos > 0, can_fwd = brs.history_pos < brs.history_len - 1;
    w98_button(bx, by, BR_BTN_W, BR_BTN_H, "< Back", brs.pressed_btn == 10, can_back, 0); bx += BR_BTN_W + 2;
    w98_button(bx, by, BR_BTN_W, BR_BTN_H, "Fwd >", brs.pressed_btn == 11, can_fwd, 0); bx += BR_BTN_W + 2;
    w98_button(bx, by, BR_BTN_W, BR_BTN_H, "Reload", brs.pressed_btn == 12, 1, 0); bx += BR_BTN_W + 2;
    w98_button(bx, by, BR_BTN_W, BR_BTN_H, "Home", brs.pressed_btn == 13, 1, 0); bx += BR_BTN_W + 2;
    w98_button(bx, by, BR_BTN_W, BR_BTN_H, "Links v", brs.pressed_btn == 14, 1, 0); bx += BR_BTN_W + 2;
    /* throbber / logo */
    int lx = x + cw - 26, ly = y + 4;
    w98_fill(lx, ly, 22, 22, W98_ACTIVE_TITLE);
    if (brs.loading) w98_fill(lx + 6, ly + 6, 10, 10, 0xFFFFFF00u);
    else { w98_fill(lx + 4, ly + 9, 14, 5, 0xFF60A0E0u); w98_fill(lx + 8, ly + 5, 4, 4, 0xFF60A0E0u); w98_fill(lx + 15, ly + 6, 3, 3, 0xFF60A0E0u); }
    w98_bevel(lx, ly, 22, 22, W98_BEVEL_SUNKEN);
    /* separator */
    w98_fill(x, y + BR_TOOLBAR_H - 1, cw, 1, W98_BTNSHADOW);
    w98_fill(x, y + BR_TOOLBAR_H, cw, 1, W98_BTNHILITE);

    /* address bar */
    int ay = y + BR_TOOLBAR_H + 3;
    w98_text("Address", x + 6, ay + 5, W98_BTNTEXT, W98_BTNFACE, 1, NULL);
    int ax = x + 56, aw = cw - 56 - 44 - 6, ah = 20;
    w98_fill(ax, ay, aw, ah, W98_WINDOW);
    w98_bevel(ax, ay, aw, ah, W98_BEVEL_SUNKEN);
    w98_rect_t aclip = { ax + 3, ay + 2, aw - 6, ah - 4 };
    int maxc = (aw - 8) / 6;
    int al = (int)strlen(brs.address);
    int start = 0;
    if (brs.address_focus && brs.address_cursor > maxc - 1) start = brs.address_cursor - (maxc - 1);
    else if (!brs.address_focus && al > maxc) start = 0;
    char shown[BR_URL_MAX];
    int o = 0;
    for (int i = start; brs.address[i] && o < maxc && o < BR_URL_MAX - 1; i++) shown[o++] = brs.address[i];
    shown[o] = 0;
    if (brs.address_focus && brs.address_select_all) {
        w98_fill(ax + 3, ay + 3, o * 6 + 1, 14, W98_HIGHLIGHT);
        w98_text(shown, ax + 4, ay + 6, W98_HIGHLIGHTTEXT, W98_HIGHLIGHT, 1, &aclip);
    } else {
        w98_text(shown, ax + 4, ay + 6, W98_WINDOWTEXT, W98_WINDOW, 1, &aclip);
    }
    if (brs.address_focus && ((uptime_ticks / 500) & 1) == 0) {
        int cx = ax + 4 + (brs.address_cursor - start) * 6;
        if (cx < ax + aw - 3) w98_fill(cx, ay + 4, 1, 12, W98_WINDOWTEXT);
    }
    w98_button(ax + aw + 4, ay - 1, 40, 22, "Go", brs.pressed_btn == 15, 1, 0);
}

static void draw_status(window_t* w) {
    int x = w->rect.client_x, y = w->rect.client_y + w->rect.client_h - BR_STATUS_H, cw = w->rect.client_w;
    w98_fill(x, y, cw, BR_STATUS_H, W98_BTNFACE);
    w98_bevel(x + 1, y + 1, cw - 2, BR_STATUS_H - 2, W98_BEVEL_SUNKEN);
    w98_rect_t clip = { x + 4, y + 2, cw - 8, BR_STATUS_H - 4 };
    char s[160];
    if (brs.hover_link >= 0) {
        br_node_t* l = br_dom_node(brs.hover_link);
        const char* href = l ? br_attr(l, "href") : NULL;
        if (href) { char abs[BR_URL_MAX]; br_resolve_url(brs.url, href, abs, sizeof(abs)); br_strlcpy(s, abs, sizeof(s)); }
        else br_strlcpy(s, brs.status, sizeof(s));
    } else br_strlcpy(s, brs.status, sizeof(s));
    int maxc = (cw - 100) / 6; if (maxc < 10) maxc = 10;
    if ((int)strlen(s) > maxc) s[maxc] = 0;
    w98_text(s, x + 6, y + 5, W98_BTNTEXT, W98_BTNFACE, 1, &clip);
    /* right: zone indicator */
    const char* zone = br_streq_prefix(brs.url, "https://") ? "Internet (TLS)" : br_streq_prefix(brs.url, "http://") ? "Internet" : br_streq_prefix(brs.url, "file:") ? "Local file" : "Built-in";
    int zw = w98_text_width(zone, 1) + 10;
    w98_fill(x + cw - zw - 4, y + 1, 1, BR_STATUS_H - 2, W98_BTNSHADOW);
    w98_fill(x + cw - zw - 3, y + 1, 1, BR_STATUS_H - 2, W98_BTNHILITE);
    w98_text(zone, x + cw - zw + 2, y + 5, W98_BTNTEXT, W98_BTNFACE, 1, NULL);
}

static void draw_menu(window_t* w) {
    int x = w->rect.client_x + 4 + (BR_BTN_W + 2) * 4, y = w->rect.client_y + 4 + BR_BTN_H;
    int h = BR_BOOKMARK_COUNT * 20 + 6;
    if (x + BR_MENU_W > (int)screen_width) x = (int)screen_width - BR_MENU_W - 2;
    w98_fill(x, y, BR_MENU_W, h, W98_MENU_BG);
    w98_bevel(x, y, BR_MENU_W, h, W98_BEVEL_RAISED);
    for (int i = 0; i < BR_BOOKMARK_COUNT; i++) {
        int iy = y + 3 + i * 20;
        int hot = (desktop_mouse_x >= x && desktop_mouse_x < x + BR_MENU_W && desktop_mouse_y >= iy && desktop_mouse_y < iy + 20);
        if (hot) w98_fill(x + 2, iy, BR_MENU_W - 4, 20, W98_HIGHLIGHT);
        w98_text(bookmarks[i].title, x + 10, iy + 6, hot ? W98_HIGHLIGHTTEXT : W98_BTNTEXT, hot ? W98_HIGHLIGHT : W98_MENU_BG, 1, NULL);
    }
}

static void draw_alert(window_t* w) {
    int aw = 300, ah = 110;
    int x = w->rect.client_x + (w->rect.client_w - aw) / 2, y = w->rect.client_y + (w->rect.client_h - ah) / 2;
    w98_fill(x, y, aw, ah, W98_BTNFACE);
    w98_bevel(x, y, aw, ah, W98_BEVEL_RAISED);
    w98_fill(x + 3, y + 3, aw - 6, 18, W98_ACTIVE_TITLE);
    w98_text_bold("Shark Navigator", x + 8, y + 8, W98_TITLETEXT, W98_ACTIVE_TITLE, 1, NULL);
    /* wrap alert text into up to 3 lines of 46 chars */
    const char* s = brs.alert_text;
    int line = 0, i = 0, len = (int)strlen(s);
    while (i < len && line < 3) {
        int take = len - i; if (take > 46) take = 46;
        if (take == 46) { int k = take; while (k > 20 && s[i + k] != ' ' && s[i + k - 1] != ' ') k--; if (k > 20) take = k; }
        char buf[48]; int o = 0;
        for (int k = 0; k < take; k++) { char c = s[i + k]; buf[o++] = (c == '\n') ? ' ' : c; }
        buf[o] = 0;
        w98_text(buf, x + 14, y + 30 + line * 12, W98_BTNTEXT, W98_BTNFACE, 1, NULL);
        i += take; line++;
        while (i < len && s[i] == ' ') i++;
    }
    w98_button(x + aw / 2 - 30, y + ah - 32, 60, 22, "OK", 0, 1, 1);
}

void app_window_draw_browser(window_t* w) {
    if (brs.win != w) {
        /* first draw for this window (or window struct moved by close): (re)bind */
        if (!brs.win || brs.win->type != WINDOW_TYPE_BROWSER || brs.history_len == 0) {
            br_reset_state(w);
            br_navigate(brs.address, 1);
        } else {
            brs.win = w;
        }
    }
    /* resize: layout width follows the window */
    int lw = w->rect.client_w - BR_SCROLL_W - 2;
    if (lw != brs.layout_width) { brs.layout_width = lw; brs.needs_layout = 1; }
    if (brs.needs_layout >= 2) run_pending_nav(1);
    else if (brs.needs_layout) relayout_cheap();

    draw_toolbar(w);
    draw_page(w);
    draw_scrollbar(w);
    draw_status(w);
    if (brs.menu_open) draw_menu(w);
    if (brs.alert_open) draw_alert(w);
}

/* ---------------------------------------------------------------- input */

static br_box_t* box_at(window_t* w, int mx, int my) {
    int px = mx - w->rect.client_x, py = my - viewport_top(w) + brs.scroll_y;
    /* topmost = last painted */
    for (int i = br_box_count() - 1; i >= 0; i--) {
        br_box_t* b = br_box(i);
        if (px >= b->x && px < b->x + b->w && py >= b->y && py < b->y + b->h) {
            if (b->kind == BR_BOX_RECT && !(b->node && b->node->type == BR_NODE_ELEMENT && (br_streq(b->node->tag, "a") || br_attr(b->node, "onclick")))) {
                /* plain background rects don't swallow clicks unless the node is interactive */
                br_node_t* n = b->node; int interactive = 0;
                for (br_node_t* p = n; p; p = p->parent) if (p->type == BR_NODE_ELEMENT && (br_streq(p->tag, "a") || br_attr(p, "onclick") || (p->js_obj >= 0))) { interactive = 1; break; }
                if (!interactive) continue;
            }
            return b;
        }
    }
    return NULL;
}

static br_node_t* link_at(window_t* w, int mx, int my) {
    br_box_t* b = box_at(w, mx, my);
    if (!b) return NULL;
    if (b->link) return b->link;
    for (br_node_t* p = b->node; p; p = p->parent) if (p->type == BR_NODE_ELEMENT && br_streq(p->tag, "a") && br_attr(p, "href")) return p;
    return NULL;
}

/* Called on every pointer move over the window (from browser_tick). */
static void update_hover(window_t* w, int mx, int my) {
    int vy = viewport_top(w);
    int hover = -1;
    if (mx >= w->rect.client_x && mx < w->rect.client_x + viewport_w(w) && my >= vy && my < vy + viewport_h(w) && !brs.menu_open && !brs.alert_open) {
        br_node_t* l = link_at(w, mx, my);
        if (l) hover = l->id;
    }
    if (hover != brs.hover_link) {
        brs.hover_link = hover;
        br_request_repaint();
    }
    if (brs.menu_open) br_request_repaint();
}

static void scroll_by(window_t* w, int dy) {
    int ms = max_scroll(w);
    brs.scroll_y += dy;
    if (brs.scroll_y > ms) brs.scroll_y = ms;
    if (brs.scroll_y < 0) brs.scroll_y = 0;
    br_request_repaint();
}

static int toolbar_button_at(window_t* w, int mx, int my) {
    int x = w->rect.client_x, y = w->rect.client_y;
    if (my < y + 4 || my >= y + 4 + BR_BTN_H) {
        int ay = y + BR_TOOLBAR_H + 3;
        int ax = x + 56, aw = w->rect.client_w - 56 - 44 - 6;
        if (my >= ay - 1 && my < ay + 21 && mx >= ax + aw + 4 && mx < ax + aw + 44) return 15;
        return -1;
    }
    for (int i = 0; i < 5; i++) {
        int bx = x + 4 + i * (BR_BTN_W + 2);
        if (mx >= bx && mx < bx + BR_BTN_W) return 10 + i;
    }
    return -1;
}

static void activate_button(int id) {
    switch (id) {
    case 10: go_back(); break;
    case 11: go_forward(); break;
    case 12: br_navigate(brs.url, 0); break;
    case 13: br_navigate("about:home", 1); break;
    case 14: brs.menu_open = !brs.menu_open; br_request_repaint(); break;
    case 15: br_navigate(brs.address, 1); break;
    }
}

void app_window_mouse_browser(window_t* w, int mx, int my, int buttons) {
    if (brs.win != w) return;
    if (buttons) auto_nav_chain = 0;         /* a click ends any redirect chain */
    if (!(buttons & 1)) return;              /* presses only; releases handled in tick */
    int x = w->rect.client_x, y = w->rect.client_y;

    if (brs.alert_open) {
        int aw = 300, ah = 110;
        int ax = x + (w->rect.client_w - aw) / 2, ay = y + (w->rect.client_h - ah) / 2;
        if (mx >= ax + aw / 2 - 30 && mx < ax + aw / 2 + 30 && my >= ay + ah - 32 && my < ay + ah - 10) { brs.alert_open = 0; br_request_repaint(); }
        return;
    }
    if (brs.menu_open) {
        int menu_x = x + 4 + (BR_BTN_W + 2) * 4, menu_y = y + 4 + BR_BTN_H;
        if (menu_x + BR_MENU_W > (int)screen_width) menu_x = (int)screen_width - BR_MENU_W - 2;
        if (mx >= menu_x && mx < menu_x + BR_MENU_W && my >= menu_y + 3 && my < menu_y + 3 + BR_BOOKMARK_COUNT * 20) {
            int i = (my - menu_y - 3) / 20;
            brs.menu_open = 0;
            br_navigate(bookmarks[i].url, 1);
            return;
        }
        brs.menu_open = 0;
        br_request_repaint();
        /* fall through: the click also acts normally, except on the Links button */
        if (toolbar_button_at(w, mx, my) == 14) return;
    }

    /* toolbar buttons: press now, act on release (see browser_tick) */
    int tb = toolbar_button_at(w, mx, my);
    if (tb >= 0) {
        if ((tb == 10 && brs.history_pos <= 0) || (tb == 11 && brs.history_pos >= brs.history_len - 1)) return;
        brs.pressed_btn = tb;
        brs.address_focus = 0;
        br_request_repaint();
        return;
    }
    /* address bar */
    int ay = y + BR_TOOLBAR_H + 3;
    int ax = x + 56, aw = w->rect.client_w - 56 - 44 - 6;
    if (my >= ay && my < ay + 20 && mx >= ax && mx < ax + aw) {
        if (!brs.address_focus) { brs.address_focus = 1; brs.address_select_all = 1; brs.address_cursor = (int)strlen(brs.address); }
        else {
            brs.address_select_all = 0;
            int c = (mx - ax - 4) / 6;
            int al = (int)strlen(brs.address);
            int maxc = (aw - 8) / 6;
            int start = brs.address_cursor > maxc - 1 ? brs.address_cursor - (maxc - 1) : 0;
            c += start;
            brs.address_cursor = c < 0 ? 0 : c > al ? al : c;
        }
        brs.focus_input = -1;
        br_request_repaint();
        return;
    }
    if (my >= y && my < y + BR_TOOLBAR_H + BR_ADDR_H) { brs.address_focus = 0; br_request_repaint(); return; }

    /* scrollbar */
    int sx = x + w->rect.client_w - BR_SCROLL_W;
    int vy = viewport_top(w), vh = viewport_h(w);
    if (mx >= sx && my >= vy && my < vy + vh) {
        brs.address_focus = 0;
        if (my < vy + 16) { brs.pressed_btn = 1; scroll_by(w, -BR_LINE_STEP); }
        else if (my >= vy + vh - 16) { brs.pressed_btn = 2; scroll_by(w, BR_LINE_STEP); }
        else {
            /* page up/down or thumb drag start */
            int track = vh - 32;
            int ms = max_scroll(w);
            int total = brs.page_h + 8 > vh ? brs.page_h + 8 : vh;
            int th = total > 0 ? track * vh / total : track; if (th < 12) th = 12; if (th > track) th = track;
            int ty = vy + 16 + (ms > 0 ? (track - th) * brs.scroll_y / ms : 0);
            if (my < ty) scroll_by(w, -(vh - BR_LINE_STEP));
            else if (my >= ty + th) scroll_by(w, vh - BR_LINE_STEP);
            else { brs.pressed_btn = 3; brs.js_console_lines = brs.js_console_lines; /* thumb drag */ }
        }
        return;
    }

    /* page click */
    if (my >= vy && my < vy + vh && mx >= x && mx < sx) {
        brs.address_focus = 0;
        br_box_t* b = box_at(w, mx, my);
        if (!b) { brs.focus_input = -1; br_request_repaint(); return; }
        br_node_t* n = b->node;
        if (b->kind == BR_BOX_INPUT) {
            brs.focus_input = n ? n->id : -1;
            /* clicking a select cycles its options */
            if (n && br_streq(n->tag, "select")) { br_js_dispatch_click(n); }
            br_request_repaint();
            return;
        }
        brs.focus_input = -1;
        if (b->kind == BR_BOX_BUTTON) {
            int idx = -1;
            for (int i = 0; i < br_box_count(); i++) if (br_box(i) == b) { idx = i; break; }
            brs.pressed_btn = 100 + idx;
            br_request_repaint();
            return;
        }
        if (b->kind == BR_BOX_CHECKBOX) { br_js_dispatch_click(n); br_request_repaint(); return; }
        /* any other box: click the innermost element (text -> parent element) */
        br_node_t* target = n;
        if (target && target->type == BR_NODE_TEXT) target = target->parent;
        if (b->link && !target) target = b->link;
        if (target) {
            br_status("");
            br_js_dispatch_click(target);
            run_pending_nav(0);
        }
        br_request_repaint();
    }
}

static void address_insert(char c) {
    int al = (int)strlen(brs.address);
    if (brs.address_select_all) { brs.address[0] = 0; brs.address_cursor = 0; al = 0; brs.address_select_all = 0; }
    if (al >= BR_URL_MAX - 1) return;
    for (int i = al; i >= brs.address_cursor; i--) brs.address[i + 1] = brs.address[i];
    brs.address[brs.address_cursor++] = c;
}

void app_window_keyboard_browser(window_t* w, char c) {
    auto_nav_chain = 0;                     /* user input ends any redirect chain */
    if (brs.win != w) return;
    if (brs.alert_open) { if (c == '\n' || c == 27 || c == ' ') { brs.alert_open = 0; br_request_repaint(); } return; }
    if (brs.menu_open && c == 27) { brs.menu_open = 0; br_request_repaint(); return; }
    /* Ctrl+L focuses the address bar, Ctrl+R reloads, Ctrl+H home */
    if (ctrl_pressed) {
        if (c == 'l' || c == 'L' || c == 12) { brs.address_focus = 1; brs.address_select_all = 1; brs.address_cursor = (int)strlen(brs.address); brs.focus_input = -1; br_request_repaint(); return; }
        if (c == 'r' || c == 'R' || c == 18) { br_navigate(brs.url, 0); return; }
        if (c == 'h' || c == 'H' || c == 8) { br_navigate("about:home", 1); return; }
        if (c == 'u' || c == 'U' || c == 21) { br_navigate("about:source", 1); return; }
        if (c == 'j' || c == 'J' || c == 10) { br_navigate("about:console", 1); return; }
    }
    if (brs.address_focus) {
        if (c == '\n') { brs.address_focus = 0; br_navigate(brs.address, 1); return; }
        if (c == 27) { brs.address_focus = 0; br_strlcpy(brs.address, brs.url, sizeof(brs.address)); br_request_repaint(); return; }
        if (c == '\b') {
            if (brs.address_select_all) { brs.address[0] = 0; brs.address_cursor = 0; brs.address_select_all = 0; }
            else if (brs.address_cursor > 0) {
                int al = (int)strlen(brs.address);
                for (int i = brs.address_cursor - 1; i < al; i++) brs.address[i] = brs.address[i + 1];
                brs.address_cursor--;
            }
            br_request_repaint();
            return;
        }
        if (c == 0x10) { brs.address_cursor = 0; brs.address_select_all = 0; br_request_repaint(); return; }       /* Up: home */
        if (c == 0x11) { brs.address_cursor = (int)strlen(brs.address); brs.address_select_all = 0; br_request_repaint(); return; }
        if (c == '\t') { brs.address_focus = 0; br_request_repaint(); return; }
        if (c >= 32 && c < 127) { address_insert(c); br_request_repaint(); }
        return;
    }
    if (brs.focus_input >= 0) {
        br_node_t* n = br_dom_node(brs.focus_input);
        if (!n) { brs.focus_input = -1; return; }
        if (c == 27) { brs.focus_input = -1; br_request_repaint(); return; }
        /* arrow keys always scroll the page (there is no caret movement) */
        if (c == 0x10) { scroll_by(w, -BR_LINE_STEP); return; }
        if (c == 0x11) { scroll_by(w, BR_LINE_STEP); return; }
        if (c == '\t') {
            /* next input */
            int found = 0;
            for (int i = n->id + 1; i < br_dom_node_count(); i++) { br_node_t* k = br_dom_node(i); if (k->type == BR_NODE_ELEMENT && br_streq(k->tag, "input") && k->lw > 0) { const char* t = br_attr(k, "type"); if (!t || br_strieq(t, "text") || br_strieq(t, "password") || br_strieq(t, "search") || br_strieq(t, "email") || br_strieq(t, "number") || br_strieq(t, "url")) { brs.focus_input = k->id; found = 1; break; } } }
            if (!found) brs.focus_input = -1;
            br_request_repaint();
            return;
        }
        if (c == '\n') { br_js_dispatch_key(n, c); br_js_dispatch_change(n); run_pending_nav(0); br_request_repaint(); return; }
        if (!n->value) { const char* v = br_attr(n, "value"); n->value = br_strdup(v ? v : ""); }
        int vl = (int)strlen(n->value);
        if (c == '\b') {
            if (vl > 0) { char* nv = br_strdup_n(n->value, vl - 1); n->value = nv; br_js_dispatch_input(n); }
            br_request_repaint();
            return;
        }
        if (c >= 32 && c < 127 && vl < 200) {
            const char* ml = br_attr(n, "maxlength");
            if (ml && vl >= br_atoi(ml)) return;
            char* nv = br_strdup_n(n->value, vl + 1);
            nv[vl] = c; nv[vl + 1] = 0;
            n->value = nv;
            br_js_dispatch_key(n, c);
            br_js_dispatch_input(n);
            br_request_repaint();
        }
        return;
    }
    /* page navigation keys */
    if (c == 0x10) { scroll_by(w, -BR_LINE_STEP); return; }
    if (c == 0x11) { scroll_by(w, BR_LINE_STEP); return; }
    if (c == ' ') { scroll_by(w, shift_pressed ? -(viewport_h(w) - BR_LINE_STEP) : viewport_h(w) - BR_LINE_STEP); return; }
    if (c == '\b') { go_back(); return; }
    if (c == 27) { brs.menu_open = 0; brs.refresh_pending = 0; br_status("Stopped"); return; }   /* Esc also cancels a pending <meta refresh> */
    if (c == '/' ) { brs.address_focus = 1; brs.address_select_all = 1; brs.address_cursor = (int)strlen(brs.address); br_request_repaint(); return; }
    if (c == '\t') {
        /* focus first text input */
        for (int i = 0; i < br_dom_node_count(); i++) { br_node_t* k = br_dom_node(i); if (k->type == BR_NODE_ELEMENT && br_streq(k->tag, "input") && k->lw > 0) { const char* t = br_attr(k, "type"); if (!t || br_strieq(t, "text") || br_strieq(t, "password") || br_strieq(t, "search")) { brs.focus_input = k->id; break; } } }
        br_request_repaint();
        return;
    }
    /* typed text with nothing focused starts editing the address bar (like typing in IE) */
    if (c >= 32 && c < 127) {
        brs.address_focus = 1; brs.address_select_all = 1; brs.address_cursor = (int)strlen(brs.address);
        address_insert(c);
        br_request_repaint();
    }
}

void browser_close(void) {
    brs.win = NULL;
    brs.history_len = 0;
    brs.alert_open = 0;
    brs.menu_open = 0;
}

/* ---------------------------------------------------------------- tick */

static int last_mouse_buttons = 0;
static int last_tick_mx = -1, last_tick_my = -1;
static uint32_t last_blink = 0;
static uint32_t last_timer_run = 0;
static int drag_start_y = 0, drag_start_scroll = 0;

static window_t* find_browser_window(void) {
    for (int i = 0; i < desktop.window_count; i++) {
        if (desktop.windows[i].type == WINDOW_TYPE_BROWSER && desktop.windows[i].visible) return &desktop.windows[i];
    }
    return NULL;
}

void browser_tick(void) {
    window_t* w = find_browser_window();
    if (!w) { if (brs.win) browser_close(); return; }
    if (brs.win != w) {
        /* window array shifted (another window closed): follow it */
        if (brs.win && brs.history_len > 0) brs.win = w;
        else return;                      /* draw_func will initialise */
    }
    if (w->state == WINDOW_STATE_MINIMIZED) return;

    int mx = mouse_cursor_x, my = mouse_cursor_y, buttons = mouse_state.buttons;

    /* releases: toolbar & page buttons act on release, scrollbar drag ends */
    if (last_mouse_buttons & 1 && !(buttons & 1)) {
        int pb = brs.pressed_btn;
        brs.pressed_btn = 0;
        if (pb >= 10 && pb <= 15) {
            if (toolbar_button_at(w, mx, my) == pb) activate_button(pb);
            br_request_repaint();
        } else if (pb >= 100) {
            br_box_t* b = br_box(pb - 100);
            if (b && b->kind == BR_BOX_BUTTON && b->node && box_at(w, mx, my) == b) {
                br_js_dispatch_click(b->node);
                run_pending_nav(0);
            }
            br_request_repaint();
        } else if (pb) br_request_repaint();
    }
    if (!(last_mouse_buttons & 1) && (buttons & 1) && brs.pressed_btn == 3) {
        drag_start_y = my; drag_start_scroll = brs.scroll_y;
    }
    if (brs.pressed_btn == 3 && (buttons & 1)) {
        if (last_mouse_buttons & 1 && drag_start_y == 0 && drag_start_scroll == 0) { drag_start_y = my; drag_start_scroll = brs.scroll_y; }
        int vh = viewport_h(w), track = vh - 32;
        int total = brs.page_h + 8 > vh ? brs.page_h + 8 : vh;
        int th = total > 0 ? track * vh / total : track; if (th < 12) th = 12; if (th > track) th = track;
        int ms = max_scroll(w);
        if (track - th > 0 && drag_start_y) {
            int ny = drag_start_scroll + (my - drag_start_y) * ms / (track - th);
            if (ny < 0) { ny = 0; }
            if (ny > ms) { ny = ms; }
            if (ny != brs.scroll_y) { brs.scroll_y = ny; br_request_repaint(); }
        }
    } else if (brs.pressed_btn != 3) { drag_start_y = 0; drag_start_scroll = 0; }
    /* auto-repeat scroll arrows while held */
    if ((buttons & 1) && (brs.pressed_btn == 1 || brs.pressed_btn == 2) && (uptime_ticks % 80) == 0) scroll_by(w, brs.pressed_btn == 1 ? -BR_LINE_STEP : BR_LINE_STEP);
    last_mouse_buttons = buttons;

    /* hover */
    if (mx != last_tick_mx || my != last_tick_my) {
        last_tick_mx = mx; last_tick_my = my;
        if (w->has_focus) update_hover(w, mx, my);
    }

    /* JS timers (setTimeout/setInterval), throttled to 50 Hz */
    if (br_js_has_timers() && uptime_ticks - last_timer_run >= 20) {
        last_timer_run = uptime_ticks;
        br_js_tick();
        run_pending_nav(1);
    }

    /* <meta http-equiv=refresh> */
    if (brs.refresh_pending && (int32_t)(uptime_ticks - brs.refresh_at) >= 0) {
        brs.refresh_pending = 0;
        nav_is_auto = brs.refresh_auto;
        br_navigate(brs.refresh_url, brs.refresh_replace ? 2 : 1);
    }

    /* caret blink */
    if (w->has_focus && (brs.address_focus || brs.focus_input >= 0) && uptime_ticks - last_blink >= 500) {
        last_blink = uptime_ticks;
        br_request_repaint();
    }
}

void browser_open_url(const char* url) {
    window_t* w = find_browser_window();
    if (!w) {
        desktop_icon_launch_offset(WINDOW_TYPE_BROWSER, "Shark Navigator");
        w = find_browser_window();
        if (!w) return;
        br_reset_state(w);
        brs.history_len = 0;
        if (url && url[0]) { br_strlcpy(brs.address, url, sizeof(brs.address)); }
        br_navigate(brs.address, 1);
        return;
    }
    for (int i = 0; i < desktop.window_count; i++) if (&desktop.windows[i] == w) { window_focus(i); break; }
    if (url && url[0]) br_navigate(url, 1);
}
