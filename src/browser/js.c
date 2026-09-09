/* A small JavaScript interpreter for the SharkOS browser.
 *
 * Design:
 *   - Tokenizer -> recursive-descent parser -> AST in a fixed arena
 *   - Tree-walking evaluator with lexical scopes (closures work)
 *   - Values: undefined, null, boolean, number, string, object, array,
 *     function (script or native). Numbers are 32.16 fixed point stored in a
 *     64-bit integer because the kernel is compiled without FPU/soft-float
 *     support; this keeps integer arithmetic exact up to 2^47 and gives two
 *     decimals for the common `0.5 * x` style code.
 *   - Property storage: small open arrays per object (pages are small)
 *   - No GC: everything lives in arenas that are reset per page load. A
 *     runaway script is stopped by a step budget so the desktop never hangs.
 *
 * Supported syntax: var/let/const, function declarations + expressions,
 * arrow functions, if/else, for, for-in, for-of, while, do-while, break,
 * continue, return, switch, try/catch/finally (catch everything), throw,
 * ternary, all arithmetic/comparison/logical/bitwise operators, ++/--,
 * compound assignment, template literals (no nesting), object/array
 * literals, member access, calls, new (for Object/Array/Date/Error),
 * typeof, instanceof (by constructor name), delete, comma, spread in calls
 * (arrays), destructuring is NOT supported, classes are NOT supported.
 */

#include "browser_internal.h"

/* ------------------------------------------------------------ limits */

#define JS_MAX_TOKENS   24000
#define JS_MAX_NODES    24000
#define JS_MAX_OBJECTS  4000
#define JS_MAX_PROPS    24000
#define JS_STR_POOL     (256 * 1024)
#define JS_MAX_SCOPES   512
#define JS_MAX_TIMERS   16
#define JS_STEP_BUDGET  400000
#define JS_MAX_DEPTH    64

#define FX_SHIFT 16
#define FX_ONE   (1LL << FX_SHIFT)

typedef int64_t fx_t;

/* ------------------------------------------------------------ values */

enum { V_UNDEF = 0, V_NULL, V_BOOL, V_NUM, V_STR, V_OBJ, V_FUNC, V_NATIVE };

typedef struct value {
    uint8_t type;
    union {
        int b;
        fx_t n;
        const char* s;
        int obj;                    /* object index (also arrays, functions) */
    } u;
} value_t;

typedef struct { const char* key; value_t val; int next; uint32_t hash; } prop_t;

enum { O_PLAIN = 0, O_ARRAY, O_FUNC, O_NATIVE, O_DOM, O_DATE, O_ERROR, O_REGEXP };

typedef struct {
    uint8_t kind;
    int first_prop;             /* linked list head into props[] */
    int last_prop;              /* tail, for O(1) append */
    int prop_count;
    int proto;                  /* prototype object or -1 */
    /* arrays */
    value_t* items; int len; int cap;
    /* functions */
    int fn_node;                /* AST node of the function */
    int fn_scope;               /* closure scope */
    int fn_this;                /* bound this (arrow) or -1 */
    value_t (*native)(int this_obj, value_t* args, int argc);
    /* DOM */
    int dom_id;                 /* br_node id */
    int date_ms;
    const char* name;
} object_t;

typedef struct { const char* names[24]; value_t vals[24]; int n; int parent; int this_obj; } scope_t;

/* ------------------------------------------------------------ tokens */

enum {
    T_EOF = 0, T_NUM, T_STR, T_TEMPLATE, T_IDENT, T_KW, T_PUNCT
};

typedef struct { uint8_t type; const char* s; int len; fx_t num; int line; int nl_before; } token_t;

/* -------------------------------------------------------------- AST */

enum {
    N_NUM = 1, N_STR, N_IDENT, N_TEMPLATE, N_TRUE, N_FALSE, N_NULL, N_UNDEF, N_THIS,
    N_ARRAY, N_OBJECT, N_FUNC, N_ARROW,
    N_MEMBER, N_INDEX, N_CALL, N_NEW,
    N_UNARY, N_POSTFIX, N_PREFIX, N_BINARY, N_LOGICAL, N_ASSIGN, N_COND, N_COMMA, N_TYPEOF, N_DELETE, N_SPREAD,
    N_VAR, N_BLOCK, N_IF, N_FOR, N_FORIN, N_FOROF, N_WHILE, N_DOWHILE, N_RETURN, N_BREAK, N_CONTINUE,
    N_EXPR, N_EMPTY, N_FUNCDECL, N_SWITCH, N_CASE, N_TRY, N_THROW, N_PROGRAM
};

typedef struct {
    uint8_t type;
    uint8_t op;                 /* punct id / var kind */
    int a, b, c, d;             /* children */
    int list;                   /* index into lists[] (first), -1 */
    int count;
    const char* s;
    fx_t num;
    int line;
} node_t;

/* ------------------------------------------------------------ arenas */

static token_t tokens[JS_MAX_TOKENS];
static int tok_count = 0;
static node_t nodes[JS_MAX_NODES];
static int node_count = 0;
static int lists[JS_MAX_NODES];
static int list_count = 0;
static object_t objects[JS_MAX_OBJECTS];
static int obj_count = 0;
static prop_t props[JS_MAX_PROPS];
static int prop_count = 0;
static char strpool[JS_STR_POOL];
static int str_used = 0;
static scope_t scopes[JS_MAX_SCOPES];
static int scope_count = 0;
static value_t arr_pool[16384];
static int arr_used = 0;

static int steps = 0;
static int js_error = 0;
static int js_aborted = 0;      /* step budget exhausted: uncatchable, ends the script */
static char js_error_msg[128];
static int global_scope = 0;
static int depth = 0;
static int eval_nest = 0;        /* combined exec/eval recursion depth */
#define JS_MAX_EVAL_NEST 200     /* backstop; the real limit is br_stack_headroom() */
static int parse_nest = 0;       /* parser recursion guard for pathological nesting */
#define JS_MAX_PARSE_NEST 96

/* control flow */
enum { F_NONE = 0, F_BREAK, F_CONTINUE, F_RETURN, F_THROW };
static int flow = F_NONE;
static value_t flow_val;
static const char* flow_label = NULL;

/* well-known objects */
static int obj_global = -1, proto_object = -1, proto_array = -1, proto_string = -1, proto_function = -1, proto_number = -1;
static int obj_document = -1, obj_window = -1, obj_console = -1, obj_math = -1, obj_json = -1;
static int obj_location = -1;
static void navigate_to(const char* url, int replace);   /* location.href = ... */
static int proto_element = -1;

static value_t UNDEF = { V_UNDEF, { 0 } };

typedef struct { int fn; int due; int interval; int active; int id; } timer_t;
static timer_t timers[JS_MAX_TIMERS];
static int timer_next_id = 1;

/* ------------------------------------------------------------ helpers */

static void throw_error(const char* msg, const char* detail);

/* Scratch area handed out when the string pool is exhausted. Callers write
 * up to the length they asked for, so the fallback must really be that big:
 * anything larger than the scratch is refused (truncated to fit) and an
 * exception is raised. Nothing is ever written past the pool. */
#define JS_STR_MAX (16 * 1024)           /* longest single string */
static char str_overflow[JS_STR_MAX + 1];

static char* js_alloc_str(int n) {
    if (n < 0) n = 0;
    if (n > JS_STR_MAX) n = JS_STR_MAX;
    if (str_used + n + 1 > JS_STR_POOL) {
        throw_error("RangeError: out of string memory", NULL);
        str_overflow[0] = 0;
        return str_overflow;                       /* JS_STR_MAX+1 bytes: safe for any caller */
    }
    char* d = &strpool[str_used];
    str_used += n + 1;
    d[0] = 0;
    return d;
}

static const char* js_strdup_n(const char* s, int n) {
    if (n < 0) n = 0;
    if (n > JS_STR_MAX) n = JS_STR_MAX;
    char* d = js_alloc_str(n);
    for (int i = 0; i < n; i++) d[i] = s[i];
    d[n] = 0;
    return d;
}
static const char* js_strdup(const char* s) { return js_strdup_n(s, (int)strlen(s)); }

static void throw_error(const char* msg, const char* detail) {
    if (flow == F_THROW) return;
    int o = 0;
    for (int i = 0; msg[i] && o < 120; i++) js_error_msg[o++] = msg[i];
    if (detail) { if (o < 120) js_error_msg[o++] = ' '; for (int i = 0; detail[i] && o < 126; i++) js_error_msg[o++] = detail[i]; }
    js_error_msg[o] = 0;
    flow = F_THROW;
    flow_val.type = V_STR; flow_val.u.s = js_strdup(js_error_msg);
}

static value_t mk_num(fx_t n) { value_t v; v.type = V_NUM; v.u.n = n; return v; }
static value_t mk_int(int n) { return mk_num((fx_t)n << FX_SHIFT); }
static value_t mk_bool(int b) { value_t v; v.type = V_BOOL; v.u.b = b ? 1 : 0; return v; }
static value_t mk_str(const char* s) { value_t v; v.type = V_STR; v.u.s = s; return v; }
static value_t mk_obj(int o) { value_t v; v.type = (o >= 0 && (objects[o].kind == O_FUNC || objects[o].kind == O_NATIVE)) ? V_FUNC : V_OBJ; v.u.obj = o; return v; }
static value_t mk_null(void) { value_t v; v.type = V_NULL; v.u.obj = -1; return v; }

static int new_object(int kind) {
    if (obj_count >= JS_MAX_OBJECTS) { throw_error("out of memory (objects)", NULL); return -1; }
    object_t* o = &objects[obj_count];
    memset(o, 0, sizeof(*o));
    o->kind = (uint8_t)kind;
    o->first_prop = -1;
    o->last_prop = -1;
    o->proto = kind == O_ARRAY ? proto_array : (kind == O_FUNC || kind == O_NATIVE) ? proto_function : proto_object;
    o->fn_this = -1;
    o->dom_id = -1;
    return obj_count++;
}

static int new_array(void) {
    int o = new_object(O_ARRAY);
    if (o < 0) return -1;
    objects[o].items = NULL; objects[o].len = 0; objects[o].cap = 0;
    return o;
}

static void array_push(int o, value_t v) {
    object_t* a = &objects[o];
    if (a->len >= a->cap) {
        int ncap = a->cap ? a->cap * 2 : 8;
        if (arr_used + ncap > (int)(sizeof(arr_pool) / sizeof(arr_pool[0]))) { throw_error("out of memory (arrays)", NULL); return; }
        value_t* ni = &arr_pool[arr_used];
        arr_used += ncap;
        for (int i = 0; i < a->len; i++) ni[i] = a->items[i];
        a->items = ni; a->cap = ncap;
    }
    a->items[a->len++] = v;
}

static uint32_t key_hash(const char* key) {
    uint32_t h = 2166136261u;
    while (*key) { h ^= (uint8_t)*key++; h *= 16777619u; }
    return h;
}
static prop_t* find_own_h(int o, const char* key, uint32_t h) {
    if (o < 0) return NULL;
    for (int p = objects[o].first_prop; p >= 0; p = props[p].next) {
        if (props[p].hash == h && br_streq(props[p].key, key)) return &props[p];
    }
    return NULL;
}
static prop_t* find_own(int o, const char* key) { return find_own_h(o, key, key_hash(key)); }

static void set_prop(int o, const char* key, value_t v) {
    if (o < 0) return;
    uint32_t h = key_hash(key);
    prop_t* p = find_own_h(o, key, h);
    if (p) { p->val = v; return; }
    if (prop_count >= JS_MAX_PROPS) { throw_error("out of memory (props)", NULL); return; }
    props[prop_count].key = key;
    props[prop_count].val = v;
    props[prop_count].next = -1;
    props[prop_count].hash = h;
    /* append at tail to preserve insertion order for for-in / JSON */
    if (objects[o].first_prop < 0) objects[o].first_prop = prop_count;
    else props[objects[o].last_prop].next = prop_count;
    objects[o].last_prop = prop_count;
    objects[o].prop_count++;
    prop_count++;
}


static int del_prop(int o, const char* key) {
    if (o < 0) return 0;
    int prev = -1;
    for (int p = objects[o].first_prop; p >= 0; prev = p, p = props[p].next) {
        if (br_streq(props[p].key, key)) {
            if (prev < 0) objects[o].first_prop = props[p].next; else props[prev].next = props[p].next;
            if (objects[o].last_prop == p) objects[o].last_prop = prev;
            objects[o].prop_count--;
            return 1;
        }
    }
    return 0;
}

static value_t get_prop(int o, const char* key) {
    int guard = 0;
    while (o >= 0 && guard++ < 16) {
        prop_t* p = find_own(o, key);
        if (p) return p->val;
        o = objects[o].proto;
    }
    return UNDEF;
}

static void def_native(int o, const char* name, value_t (*fn)(int, value_t*, int)) {
    int f = new_object(O_NATIVE);
    if (f < 0) return;
    objects[f].native = fn;
    objects[f].name = name;
    set_prop(o, name, mk_obj(f));
}

/* ------------------------------------------------------ conversions */

static void fx_to_str(fx_t n, char* out) {
    /* integers print as integers; otherwise up to 4 decimals trimmed */
    int neg = n < 0;
    if (neg) n = -n;
    int64_t ip = n >> FX_SHIFT;
    int64_t frac = n & (FX_ONE - 1);
    char tmp[32]; int t = 0;
    if (ip == 0) tmp[t++] = '0';
    while (ip > 0 && t < 24) { tmp[t++] = (char)('0' + ip % 10); ip /= 10; }
    int o = 0;
    if (neg && (n != 0)) out[o++] = '-';
    while (t > 0) out[o++] = tmp[--t];
    if (frac) {
        /* 4 decimals, rounded */
        int64_t d = (frac * 10000 + FX_ONE / 2) >> FX_SHIFT;
        if (d >= 10000) { d = 9999; }
        if (d > 0) {
            out[o++] = '.';
            char ds[5];
            ds[0] = (char)('0' + d / 1000); ds[1] = (char)('0' + (d / 100) % 10); ds[2] = (char)('0' + (d / 10) % 10); ds[3] = (char)('0' + d % 10);
            int nd = 4;
            while (nd > 1 && ds[nd - 1] == '0') nd--;
            for (int i = 0; i < nd; i++) out[o++] = ds[i];
        }
    }
    out[o] = 0;
}

static fx_t str_to_fx(const char* s, int* ok) {
    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2; int64_t v = 0; int any = 0;
        while (1) {
            int d = (*s >= '0' && *s <= '9') ? *s - '0' : (*s >= 'a' && *s <= 'f') ? *s - 'a' + 10 : (*s >= 'A' && *s <= 'F') ? *s - 'A' + 10 : -1;
            if (d < 0) break;
            v = v * 16 + d; s++; any = 1;
        }
        *ok = any;
        return (neg ? -v : v) << FX_SHIFT;
    }
    if (!(*s >= '0' && *s <= '9') && *s != '.') { *ok = 0; return 0; }
    int64_t ip = 0;
    while (*s >= '0' && *s <= '9') { ip = ip * 10 + (*s - '0'); s++; }
    fx_t v = ip << FX_SHIFT;
    if (*s == '.') {
        s++;
        int64_t scale = 1; int64_t frac = 0;
        while (*s >= '0' && *s <= '9' && scale < 100000) { frac = frac * 10 + (*s - '0'); scale *= 10; s++; }
        while (*s >= '0' && *s <= '9') s++;
        v += (frac << FX_SHIFT) / scale;
    }
    if (*s == 'e' || *s == 'E') {
        s++; int eneg = 0; if (*s == '-') { eneg = 1; s++; } else if (*s == '+') s++;
        int e = 0; while (*s >= '0' && *s <= '9') { e = e * 10 + (*s - '0'); s++; }
        for (int i = 0; i < e && i < 12; i++) v = eneg ? v / 10 : v * 10;
    }
    *ok = 1;
    while (*s == ' ') s++;
    return neg ? -v : v;
}

static const char* to_string(value_t v);

static const char* obj_to_string(int o) {
    object_t* ob = &objects[o];
    if (ob->kind == O_ARRAY) {
        char* out = js_alloc_str(1);
        /* build "a,b,c" */
        int total = 0;
        static char tmp[2048];
        tmp[0] = 0;
        for (int i = 0; i < ob->len; i++) {
            const char* s = (ob->items[i].type == V_UNDEF || ob->items[i].type == V_NULL) ? "" : to_string(ob->items[i]);
            int l = (int)strlen(s);
            if (total + l + 2 >= (int)sizeof(tmp)) break;
            if (i) tmp[total++] = ',';
            for (int k = 0; k < l; k++) tmp[total++] = s[k];
            tmp[total] = 0;
        }
        (void)out;
        return js_strdup_n(tmp, total);
    }
    if (ob->kind == O_FUNC || ob->kind == O_NATIVE) return "function() { [code] }";
    if (ob->kind == O_ERROR) { value_t m = get_prop(o, "message"); return m.type == V_STR ? m.u.s : "Error"; }
    if (ob->kind == O_DOM) return "[object HTMLElement]";
    if (ob->kind == O_DATE) return "[object Date]";
    /* toString method? */
    value_t ts = get_prop(o, "toString");
    (void)ts;
    return "[object Object]";
}

static const char* to_string(value_t v) {
    static char buf[40];
    switch (v.type) {
    case V_UNDEF: return "undefined";
    case V_NULL: return "null";
    case V_BOOL: return v.u.b ? "true" : "false";
    case V_NUM: fx_to_str(v.u.n, buf); return js_strdup(buf);
    case V_STR: return v.u.s;
    case V_OBJ: case V_FUNC: return obj_to_string(v.u.obj);
    }
    return "";
}

static fx_t to_num(value_t v) {
    int ok;
    switch (v.type) {
    case V_NUM: return v.u.n;
    case V_BOOL: return v.u.b ? FX_ONE : 0;
    case V_NULL: return 0;
    case V_STR: { if (!v.u.s[0]) return 0; fx_t n = str_to_fx(v.u.s, &ok); return ok ? n : (fx_t)0x7FFFFFFFFFFFLL; }
    case V_OBJ: if (objects[v.u.obj].kind == O_DATE) return (fx_t)objects[v.u.obj].date_ms << FX_SHIFT;
                if (objects[v.u.obj].kind == O_ARRAY && objects[v.u.obj].len == 1) return to_num(objects[v.u.obj].items[0]);
                if (objects[v.u.obj].kind == O_ARRAY && objects[v.u.obj].len == 0) return 0;
                return (fx_t)0x7FFFFFFFFFFFLL;   /* NaN marker */
    default: return (fx_t)0x7FFFFFFFFFFFLL;
    }
}

#define NAN_FX ((fx_t)0x7FFFFFFFFFFFLL)
static int is_nan(fx_t n) { return n == NAN_FX; }

static int truthy(value_t v) {
    switch (v.type) {
    case V_UNDEF: case V_NULL: return 0;
    case V_BOOL: return v.u.b;
    case V_NUM: return v.u.n != 0 && !is_nan(v.u.n);
    case V_STR: return v.u.s[0] != 0;
    default: return 1;
    }
}

static const char* type_name(value_t v) {
    switch (v.type) {
    case V_UNDEF: return "undefined";
    case V_NULL: return "object";
    case V_BOOL: return "boolean";
    case V_NUM: return "number";
    case V_STR: return "string";
    case V_FUNC: return "function";
    default: return "object";
    }
}

static int strict_eq(value_t a, value_t b) {
    if (a.type != b.type) return 0;
    switch (a.type) {
    case V_UNDEF: case V_NULL: return 1;
    case V_BOOL: return a.u.b == b.u.b;
    case V_NUM: return a.u.n == b.u.n && !is_nan(a.u.n);
    case V_STR: return br_streq(a.u.s, b.u.s);
    default: return a.u.obj == b.u.obj;
    }
}

static int loose_eq(value_t a, value_t b) {
    if (a.type == b.type) return strict_eq(a, b);
    if ((a.type == V_NULL || a.type == V_UNDEF) && (b.type == V_NULL || b.type == V_UNDEF)) return 1;
    if (a.type == V_NULL || a.type == V_UNDEF || b.type == V_NULL || b.type == V_UNDEF) return 0;
    if (a.type == V_STR && b.type == V_NUM) return to_num(a) == b.u.n;
    if (a.type == V_NUM && b.type == V_STR) return to_num(b) == a.u.n;
    if (a.type == V_BOOL) return loose_eq(mk_num(to_num(a)), b);
    if (b.type == V_BOOL) return loose_eq(a, mk_num(to_num(b)));
    if ((a.type == V_OBJ || a.type == V_FUNC) && b.type == V_STR) return br_streq(to_string(a), b.u.s);
    if (a.type == V_STR && (b.type == V_OBJ || b.type == V_FUNC)) return br_streq(a.u.s, to_string(b));
    if ((a.type == V_OBJ) && b.type == V_NUM) return to_num(a) == b.u.n;
    if (a.type == V_NUM && (b.type == V_OBJ)) return to_num(b) == a.u.n;
    return 0;
}

/* --------------------------------------------------------- tokenizer */

static const char* kw_list[] = {"var", "let", "const", "function", "return", "if", "else", "for", "while", "do",
    "break", "continue", "new", "this", "true", "false", "null", "undefined", "typeof", "instanceof", "in", "of",
    "delete", "switch", "case", "default", "try", "catch", "finally", "throw", "void", "class", "async", "await", NULL};

static int is_ident_start(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$'; }
static int is_ident_char(char c) { return is_ident_start(c) || (c >= '0' && c <= '9'); }

static int tokenize_into(const char* src, int len, int base);

static int tokenize(const char* src, int len) {
    int n = tokenize_into(src, len, 0);
    if (n < 0) { tok_count = 0; return 0; }
    tok_count = n;
    return 1;
}

/* Tokenizes `src` into tokens[base..], returns the number of tokens written
 * (including the trailing EOF) or -1 on overflow. */
static int tokenize_into(const char* src, int len, int base) {
    int tc = base;
    int i = 0, line = 1, nl = 0;
    while (i < len) {
        char c = src[i];
        if (c == '\n') { line++; nl = 1; i++; continue; }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || (unsigned char)c == 0xA0) { i++; continue; }
        if (c == '/' && i + 1 < len && src[i + 1] == '/') { while (i < len && src[i] != '\n') i++; continue; }
        if (c == '/' && i + 1 < len && src[i + 1] == '*') { i += 2; while (i + 1 < len && !(src[i] == '*' && src[i + 1] == '/')) { if (src[i] == '\n') line++; i++; } i += 2; continue; }
        if (c == '<' && i + 3 < len && src[i + 1] == '!' && src[i + 2] == '-' && src[i + 3] == '-') { while (i < len && src[i] != '\n') i++; continue; }
        if (c == '-' && i + 2 < len && src[i + 1] == '-' && src[i + 2] == '>' && nl) { while (i < len && src[i] != '\n') i++; continue; }
        if (tc >= JS_MAX_TOKENS - 1) { throw_error("script too large", NULL); return -1; }
        token_t* t = &tokens[tc];
        t->line = line; t->nl_before = nl; nl = 0;
        if ((c >= '0' && c <= '9') || (c == '.' && i + 1 < len && src[i + 1] >= '0' && src[i + 1] <= '9')) {
            int st = i;
            if (c == '0' && i + 1 < len && (src[i + 1] == 'x' || src[i + 1] == 'X')) { i += 2; while (i < len && is_ident_char(src[i])) i++; }
            else {
                while (i < len && ((src[i] >= '0' && src[i] <= '9') || src[i] == '.')) i++;
                if (i < len && (src[i] == 'e' || src[i] == 'E')) { i++; if (src[i] == '-' || src[i] == '+') i++; while (i < len && src[i] >= '0' && src[i] <= '9') i++; }
            }
            int ok;
            char tmp[48]; int l = i - st; if (l > 47) l = 47;
            for (int k = 0; k < l; k++) tmp[k] = src[st + k];
            tmp[l] = 0;
            t->type = T_NUM; t->num = str_to_fx(tmp, &ok); t->s = &src[st]; t->len = i - st;
            tc++;
            continue;
        }
        if (is_ident_start(c)) {
            int st = i;
            while (i < len && is_ident_char(src[i])) i++;
            t->type = T_IDENT; t->s = &src[st]; t->len = i - st;
            for (int k = 0; kw_list[k]; k++) {
                int kl = (int)strlen(kw_list[k]);
                if (kl == t->len) { int m = 1; for (int q = 0; q < kl; q++) if (src[st + q] != kw_list[k][q]) { m = 0; break; } if (m) { t->type = T_KW; break; } }
            }
            tc++;
            continue;
        }
        if (c == '"' || c == '\'' || c == '`') {
            char q = c; i++;
            if (str_used + 8 >= JS_STR_POOL) { throw_error("RangeError: out of string memory", NULL); return -1; }
            char* out = &strpool[str_used];
            int o = 0;
            while (i < len && src[i] != q) {
                if (str_used + o + 4 >= JS_STR_POOL) break;
                if (src[i] == '\\' && i + 1 < len) {
                    i++;
                    char e = src[i];
                    switch (e) {
                    case 'n': out[o++] = '\n'; break;
                    case 't': out[o++] = '\t'; break;
                    case 'r': out[o++] = '\r'; break;
                    case '0': out[o++] = 0; break;
                    case 'b': out[o++] = '\b'; break;
                    case '\n': line++; break;
                    case 'x': { int v = 0; for (int k = 0; k < 2 && i + 1 < len; k++) { i++; char h = src[i]; v = v * 16 + ((h >= '0' && h <= '9') ? h - '0' : ((h | 32) - 'a' + 10)); } out[o++] = (char)(v < 128 ? v : '?'); break; }
                    case 'u': {
                        int v = 0;
                        for (int k = 0; k < 4 && i + 1 < len; k++) { i++; char h = src[i]; v = v * 16 + ((h >= '0' && h <= '9') ? h - '0' : ((h | 32) - 'a' + 10)); }
                        if (v < 0x80) out[o++] = (char)v;
                        else if (v < 0x800) { out[o++] = (char)(0xC0 | (v >> 6)); out[o++] = (char)(0x80 | (v & 0x3F)); }
                        else { out[o++] = (char)(0xE0 | (v >> 12)); out[o++] = (char)(0x80 | ((v >> 6) & 0x3F)); out[o++] = (char)(0x80 | (v & 0x3F)); }
                        break;
                    }
                    default: out[o++] = e; break;
                    }
                    i++;
                    continue;
                }
                if (src[i] == '\n') line++;
                if ((unsigned char)src[i] >= 0x80) {
                    /* UTF-8 passes through: the renderer draws it (string
                     * length/indexing are byte based, as in the HTML path) */
                    out[o++] = src[i++];
                    continue;
                }
                out[o++] = src[i++];
            }
            i++;
            out[o] = 0;
            str_used += o + 1;
            t->type = (q == '`') ? T_TEMPLATE : T_STR; t->s = out; t->len = o;
            tc++;
            continue;
        }
        /* regex literal: only when a value cannot precede it */
        if (c == '/') {
            int prev_is_value = 0;
            if (tc > base) {
                token_t* p = &tokens[tc - 1];
                if (p->type == T_NUM || p->type == T_STR || p->type == T_TEMPLATE || p->type == T_IDENT) prev_is_value = 1;
                if (p->type == T_KW && (br_streq_n(p->s, p->len, "this") || br_streq_n(p->s, p->len, "true") || br_streq_n(p->s, p->len, "false") || br_streq_n(p->s, p->len, "null"))) prev_is_value = 1;
                if (p->type == T_PUNCT && (p->s[0] == ')' || p->s[0] == ']' || p->s[0] == '}') && p->len == 1) prev_is_value = 1;
            }
            if (!prev_is_value) {
                int st = i; i++;
                int in_class = 0;
                while (i < len && (src[i] != '/' || in_class) && src[i] != '\n') {
                    if (src[i] == '\\') i++;
                    else if (src[i] == '[') in_class = 1;
                    else if (src[i] == ']') in_class = 0;
                    i++;
                }
                i++;
                while (i < len && is_ident_char(src[i])) i++;
                /* store as a string token with a marker: regexes are matched by a tiny engine */
                t->type = T_STR; t->s = js_strdup_n(&src[st], i - st); t->len = i - st;
                t->num = 1;   /* regex marker */
                tc++;
                continue;
            }
        }
        /* punctuators, longest first */
        static const char* puncts[] = {">>>=", "===", "!==", "**=", "<<=", ">>=", ">>>", "...", "=>", "==", "!=", "<=", ">=", "&&", "||", "??",
            "++", "--", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<", ">>", "**", "?.",
            "{", "}", "(", ")", "[", "]", ";", ",", "<", ">", "+", "-", "*", "/", "%", "&", "|", "^", "!", "~", "?", ":", "=", ".", NULL};
        int matched = 0;
        for (int k = 0; puncts[k]; k++) {
            int pl = (int)strlen(puncts[k]);
            if (i + pl <= len) {
                int m = 1;
                for (int q = 0; q < pl; q++) if (src[i + q] != puncts[k][q]) { m = 0; break; }
                if (m) { t->type = T_PUNCT; t->s = &src[i]; t->len = pl; i += pl; matched = 1; break; }
            }
        }
        if (!matched) { i++; continue; }     /* skip unknown char */
        tc++;
    }
    tokens[tc].type = T_EOF; tokens[tc].s = ""; tokens[tc].len = 0; tokens[tc].line = line; tokens[tc].nl_before = 1;
    tc++;
    return tc - base;
}

/* ------------------------------------------------------------ parser */

static int pos = 0;

static token_t* cur(void) { return &tokens[pos]; }
static int is_punct(const char* p) { token_t* t = cur(); if (t->type != T_PUNCT) return 0; int l = (int)strlen(p); return t->len == l && br_streq_n(t->s, l, p); }
static int is_kw(const char* k) { token_t* t = cur(); if (t->type != T_KW) return 0; return br_streq_n(t->s, t->len, k); }
static int accept_punct(const char* p) { if (is_punct(p)) { pos++; return 1; } return 0; }
static int accept_kw(const char* k) { if (is_kw(k)) { pos++; return 1; } return 0; }
static void expect_punct(const char* p) {
    if (!accept_punct(p)) {
        if (!js_error) { char msg[64]; int o = 0; const char* m = "SyntaxError: expected '"; while (*m) msg[o++] = *m++; while (*p) msg[o++] = *p++; msg[o++] = '\''; msg[o] = 0; throw_error(msg, NULL); js_error = 1; }
    }
}

static int new_node(int type) {
    if (node_count >= JS_MAX_NODES) { if (!js_error) { throw_error("script too large (ast)", NULL); js_error = 1; } return 0; }
    node_t* n = &nodes[node_count];
    memset(n, 0, sizeof(*n));
    n->type = (uint8_t)type; n->a = n->b = n->c = n->d = -1; n->list = -1; n->line = cur()->line;
    return node_count++;
}

static int parse_expr(void);
static int parse_assign(void);
static int parse_statement(void);
static int parse_block(void);
static int parse_function(int is_expr);

/* list building: collect children in a temporary stack then copy */
static int tmp_stack[JS_MAX_NODES];
static int tmp_top = 0;

static int commit_list(int start) {
    int cnt = tmp_top - start;
    if (list_count + cnt >= JS_MAX_NODES) { throw_error("script too large (lists)", NULL); js_error = 1; tmp_top = start; return -1; }
    int first = list_count;
    for (int i = 0; i < cnt; i++) lists[list_count++] = tmp_stack[start + i];
    tmp_top = start;
    return first;
}

static const char* tok_str(token_t* t) { return js_strdup_n(t->s, t->len); }

static int parse_template(token_t* t) {
    /* `text ${expr} text` -> N_TEMPLATE with a list of string/expr nodes.
     * Each ${...} fragment is tokenized into the spare tail of tokens[] and
     * parsed with a temporarily redirected cursor. */
    int n = new_node(N_TEMPLATE);
    int start = tmp_top;
    const char* s = t->s;
    int i = 0, len = t->len;
    while (i < len) {
        int st = i;
        while (i < len && !(s[i] == '$' && i + 1 < len && s[i + 1] == '{')) i++;
        if (i > st) { int sn = new_node(N_STR); nodes[sn].s = js_strdup_n(&s[st], i - st); tmp_stack[tmp_top++] = sn; }
        if (i >= len) break;
        i += 2;
        int es = i; int d = 1;
        while (i < len && d > 0) { if (s[i] == '{') d++; else if (s[i] == '}') d--; if (d > 0) i++; }
        const char* frag = js_strdup_n(&s[es], i - es);
        int base = tok_count;
        int cnt = tokenize_into(frag, i - es, base);
        if (cnt > 0 && !js_error) {
            int save_pos = pos, save_count = tok_count;
            tok_count = base + cnt;
            pos = base;
            int e = parse_expr();
            pos = save_pos; tok_count = save_count;
            tmp_stack[tmp_top++] = e;
        }
        i++;                                             /* skip '}' */
    }
    nodes[n].list = commit_list(start);
    nodes[n].count = list_count - nodes[n].list;
    return n;
}

static int parse_primary_inner(void);
static int parse_primary(void) {
    if (parse_nest >= JS_MAX_PARSE_NEST) {
        if (!js_error) { throw_error("SyntaxError: expression nested too deeply", NULL); js_error = 1; }
        pos++;
        return new_node(N_UNDEF);
    }
    parse_nest++;
    int n = parse_primary_inner();
    parse_nest--;
    return n;
}

static int parse_primary_inner(void) {
    token_t* t = cur();
    if (t->type == T_NUM) { int n = new_node(N_NUM); nodes[n].num = t->num; pos++; return n; }
    if (t->type == T_STR) { int n = new_node(N_STR); nodes[n].s = (t->num == 1 && t->s[0] == '/') ? t->s : js_strdup_n(t->s, t->len); nodes[n].op = (t->num == 1 && t->s[0] == '/') ? 1 : 0; pos++; return n; }
    if (t->type == T_TEMPLATE) { pos++; return parse_template(t); }
    if (t->type == T_IDENT) {
        /* arrow function: ident => expr */
        if (tokens[pos + 1].type == T_PUNCT && tokens[pos + 1].len == 2 && tokens[pos + 1].s[0] == '=' && tokens[pos + 1].s[1] == '>') {
            int n = new_node(N_ARROW);
            int start = tmp_top;
            int pn = new_node(N_IDENT); nodes[pn].s = tok_str(t); tmp_stack[tmp_top++] = pn;
            nodes[n].list = commit_list(start); nodes[n].count = 1;
            pos += 2;
            if (is_punct("{")) { nodes[n].a = parse_block(); nodes[n].op = 0; }
            else { nodes[n].a = parse_assign(); nodes[n].op = 1; }
            return n;
        }
        int n = new_node(N_IDENT); nodes[n].s = tok_str(t); pos++; return n;
    }
    if (t->type == T_KW) {
        if (accept_kw("true")) return new_node(N_TRUE);
        if (accept_kw("false")) return new_node(N_FALSE);
        if (accept_kw("null")) return new_node(N_NULL);
        if (accept_kw("undefined")) return new_node(N_UNDEF);
        if (accept_kw("this")) return new_node(N_THIS);
        if (is_kw("function")) { pos++; return parse_function(1); }
        if (accept_kw("async")) { if (is_kw("function")) { pos++; return parse_function(1); } }
        if (accept_kw("new")) {
            int n = new_node(N_NEW);
            /* callee: member chain without call */
            int callee;
            token_t* ct = cur();
            if (ct->type == T_IDENT) { callee = new_node(N_IDENT); nodes[callee].s = tok_str(ct); pos++; }
            else { callee = parse_primary(); }
            while (accept_punct(".")) { int m = new_node(N_MEMBER); nodes[m].a = callee; nodes[m].s = tok_str(cur()); pos++; callee = m; }
            nodes[n].a = callee;
            int start = tmp_top;
            if (accept_punct("(")) {
                while (!is_punct(")") && cur()->type != T_EOF && !js_error) { tmp_stack[tmp_top++] = parse_assign(); if (!accept_punct(",")) break; }
                expect_punct(")");
            }
            nodes[n].list = commit_list(start); nodes[n].count = list_count - nodes[n].list;
            return n;
        }
        if (accept_kw("typeof")) { int n = new_node(N_TYPEOF); nodes[n].a = parse_primary(); /* unary binds tighter below */ return n; }
    }
    if (accept_punct("(")) {
        /* arrow with parenthesised params? scan ahead for ") =>" */
        int save = pos, d = 1, k = pos;
        while (k < tok_count && d > 0) { if (tokens[k].type == T_PUNCT && tokens[k].len == 1) { if (tokens[k].s[0] == '(') d++; else if (tokens[k].s[0] == ')') d--; } if (d > 0) k++; }
        if (k + 1 < tok_count && tokens[k + 1].type == T_PUNCT && tokens[k + 1].len == 2 && tokens[k + 1].s[0] == '=' && tokens[k + 1].s[1] == '>') {
            int n = new_node(N_ARROW);
            int start = tmp_top;
            while (!is_punct(")") && cur()->type != T_EOF) {
                accept_punct("...");
                int pn = new_node(N_IDENT); nodes[pn].s = tok_str(cur()); pos++;
                if (accept_punct("=")) { nodes[pn].a = parse_assign(); }   /* default value */
                tmp_stack[tmp_top++] = pn;
                if (!accept_punct(",")) break;
            }
            expect_punct(")");
            nodes[n].list = commit_list(start); nodes[n].count = list_count - nodes[n].list;
            expect_punct("=>");
            if (is_punct("{")) { nodes[n].a = parse_block(); nodes[n].op = 0; }
            else { nodes[n].a = parse_assign(); nodes[n].op = 1; }
            return n;
        }
        pos = save;
        int e = parse_expr();
        expect_punct(")");
        return e;
    }
    if (accept_punct("[")) {
        int n = new_node(N_ARRAY);
        int start = tmp_top;
        while (!is_punct("]") && cur()->type != T_EOF && !js_error) {
            if (is_punct(",")) { pos++; tmp_stack[tmp_top++] = new_node(N_UNDEF); continue; }
            if (accept_punct("...")) { int sp = new_node(N_SPREAD); nodes[sp].a = parse_assign(); tmp_stack[tmp_top++] = sp; }
            else tmp_stack[tmp_top++] = parse_assign();
            if (!accept_punct(",")) break;
        }
        expect_punct("]");
        nodes[n].list = commit_list(start); nodes[n].count = list_count - nodes[n].list;
        return n;
    }
    if (accept_punct("{")) {
        int n = new_node(N_OBJECT);
        int start = tmp_top;
        while (!is_punct("}") && cur()->type != T_EOF && !js_error) {
            token_t* kt = cur();
            int key = new_node(N_STR);
            if (accept_punct("...")) { nodes[key].type = N_SPREAD; nodes[key].a = parse_assign(); tmp_stack[tmp_top++] = key; tmp_stack[tmp_top++] = key; if (!accept_punct(",")) break; continue; }
            if (accept_punct("[")) { nodes[key].type = N_EXPR; nodes[key].a = parse_assign(); expect_punct("]"); }
            else if (kt->type == T_STR || kt->type == T_IDENT || kt->type == T_KW) { nodes[key].s = tok_str(kt); pos++; }
            else if (kt->type == T_NUM) { char b[32]; fx_to_str(kt->num, b); nodes[key].s = js_strdup(b); pos++; }
            else { pos++; continue; }
            int val;
            if (is_punct("(")) { val = parse_function(1); }              /* method shorthand */
            else if (accept_punct(":")) val = parse_assign();
            else { val = new_node(N_IDENT); nodes[val].s = nodes[key].s; }   /* shorthand {a} */
            /* get/set accessors are treated as plain props named after them */
            tmp_stack[tmp_top++] = key; tmp_stack[tmp_top++] = val;
            if (!accept_punct(",")) break;
        }
        expect_punct("}");
        nodes[n].list = commit_list(start); nodes[n].count = list_count - nodes[n].list;
        return n;
    }
    if (!js_error) { char m[48]; const char* p = "SyntaxError: unexpected token "; int o = 0; while (*p) m[o++] = *p++; for (int k = 0; k < t->len && k < 12; k++) m[o++] = t->s[k]; m[o] = 0; throw_error(m, NULL); js_error = 1; }
    pos++;
    return new_node(N_UNDEF);
}

static int parse_call_args(int callee) {
    int n = new_node(N_CALL);
    nodes[n].a = callee;
    int start = tmp_top;
    while (!is_punct(")") && cur()->type != T_EOF && !js_error) {
        if (accept_punct("...")) { int sp = new_node(N_SPREAD); nodes[sp].a = parse_assign(); tmp_stack[tmp_top++] = sp; }
        else tmp_stack[tmp_top++] = parse_assign();
        if (!accept_punct(",")) break;
    }
    expect_punct(")");
    nodes[n].list = commit_list(start); nodes[n].count = list_count - nodes[n].list;
    return n;
}

static int parse_postfix(void) {
    int e = parse_primary();
    while (!js_error) {
        if (accept_punct(".") || accept_punct("?.")) {
            if (is_punct("(")) { pos++; e = parse_call_args(e); continue; }   /* a?.() */
            int m = new_node(N_MEMBER); nodes[m].a = e; nodes[m].s = tok_str(cur()); pos++; e = m;
        } else if (accept_punct("[")) {
            int m = new_node(N_INDEX); nodes[m].a = e; nodes[m].b = parse_expr(); expect_punct("]"); e = m;
        } else if (accept_punct("(")) {
            e = parse_call_args(e);
        } else if (cur()->type == T_TEMPLATE) {
            /* tagged template: treat as call with the string */
            token_t* t = cur(); pos++;
            int n = new_node(N_CALL); nodes[n].a = e;
            int start = tmp_top; tmp_stack[tmp_top++] = parse_template(t);
            nodes[n].list = commit_list(start); nodes[n].count = 1; e = n;
        } else if ((is_punct("++") || is_punct("--")) && !cur()->nl_before) {
            int n = new_node(N_POSTFIX); nodes[n].a = e; nodes[n].op = (uint8_t)(cur()->s[0] == '+' ? 1 : 2); pos++; e = n;
        } else break;
    }
    return e;
}

static int parse_unary(void) {
    if (is_punct("!") || is_punct("-") || is_punct("+") || is_punct("~")) {
        int n = new_node(N_UNARY); nodes[n].op = (uint8_t)cur()->s[0]; pos++; nodes[n].a = parse_unary(); return n;
    }
    if (is_punct("++") || is_punct("--")) { int n = new_node(N_PREFIX); nodes[n].op = (uint8_t)(cur()->s[0] == '+' ? 1 : 2); pos++; nodes[n].a = parse_unary(); return n; }
    if (accept_kw("typeof")) { int n = new_node(N_TYPEOF); nodes[n].a = parse_unary(); return n; }
    if (accept_kw("void")) { int n = new_node(N_COMMA); int a = parse_unary(); int u = new_node(N_UNDEF); nodes[n].a = a; nodes[n].b = u; return n; }
    if (accept_kw("delete")) { int n = new_node(N_DELETE); nodes[n].a = parse_unary(); return n; }
    if (accept_kw("await")) return parse_unary();
    return parse_postfix();
}

/* binary precedence climbing */
static int binop_prec(token_t* t, char* opbuf) {
    if (t->type == T_KW) {
        if (br_streq_n(t->s, t->len, "instanceof")) { opbuf[0] = 'I'; opbuf[1] = 0; return 7; }
        if (br_streq_n(t->s, t->len, "in")) { opbuf[0] = 'i'; opbuf[1] = 'n'; opbuf[2] = 0; return 7; }
        return -1;
    }
    if (t->type != T_PUNCT) return -1;
    for (int i = 0; i < t->len && i < 3; i++) opbuf[i] = t->s[i];
    opbuf[t->len < 3 ? t->len : 3] = 0;
    const char* o = opbuf;
    if (br_streq(o, "??")) return 1;
    if (br_streq(o, "||")) return 1;
    if (br_streq(o, "&&")) return 2;
    if (br_streq(o, "|")) return 3;
    if (br_streq(o, "^")) return 4;
    if (br_streq(o, "&")) return 5;
    if (br_streq(o, "==") || br_streq(o, "!=") || br_streq(o, "===") || br_streq(o, "!==")) return 6;
    if (br_streq(o, "<") || br_streq(o, ">") || br_streq(o, "<=") || br_streq(o, ">=")) return 7;
    if (br_streq(o, "<<") || br_streq(o, ">>") || br_streq(o, ">>>")) return 8;
    if (br_streq(o, "+") || br_streq(o, "-")) return 9;
    if (br_streq(o, "*") || br_streq(o, "/") || br_streq(o, "%")) return 10;
    if (br_streq(o, "**")) return 11;
    return -1;
}

static int parse_binary(int min_prec) {
    int left = parse_unary();
    while (!js_error) {
        char op[4];
        int prec = binop_prec(cur(), op);
        if (prec < 0 || prec < min_prec) break;
        pos++;
        int right = parse_binary(prec + 1);
        int n = new_node((br_streq(op, "&&") || br_streq(op, "||") || br_streq(op, "??")) ? N_LOGICAL : N_BINARY);
        nodes[n].a = left; nodes[n].b = right; nodes[n].s = js_strdup(op);
        left = n;
    }
    return left;
}

static int parse_cond(void) {
    int c = parse_binary(1);
    if (accept_punct("?")) {
        int n = new_node(N_COND);
        nodes[n].a = c; nodes[n].b = parse_assign(); expect_punct(":"); nodes[n].c = parse_assign();
        return n;
    }
    return c;
}

static int parse_assign(void) {
    int left = parse_cond();
    token_t* t = cur();
    if (t->type == T_PUNCT && t->len >= 1 && t->s[t->len - 1] == '=' && !(t->len == 2 && (t->s[0] == '=' || t->s[0] == '!' || t->s[0] == '<' || t->s[0] == '>')) && !(t->len == 3)) {
        int n = new_node(N_ASSIGN);
        nodes[n].a = left; nodes[n].s = js_strdup_n(t->s, t->len - 1);   /* "" for plain = */
        pos++;
        nodes[n].b = parse_assign();
        return n;
    }
    if (t->type == T_PUNCT && t->len == 3 && (br_streq_n(t->s, 3, "**=") || br_streq_n(t->s, 3, "<<=") || br_streq_n(t->s, 3, ">>="))) {
        int n = new_node(N_ASSIGN); nodes[n].a = left; nodes[n].s = js_strdup_n(t->s, 2); pos++; nodes[n].b = parse_assign(); return n;
    }
    return left;
}

static int parse_expr(void) {
    int e = parse_assign();
    while (is_punct(",")) { pos++; int n = new_node(N_COMMA); nodes[n].a = e; nodes[n].b = parse_assign(); e = n; }
    return e;
}

static int parse_function(int is_expr) {
    int n = new_node(is_expr ? N_FUNC : N_FUNCDECL);
    accept_punct("*");
    if (cur()->type == T_IDENT) { nodes[n].s = tok_str(cur()); pos++; }
    expect_punct("(");
    int start = tmp_top;
    while (!is_punct(")") && cur()->type != T_EOF && !js_error) {
        int rest = accept_punct("...");
        int pn = new_node(N_IDENT); nodes[pn].s = tok_str(cur()); nodes[pn].op = (uint8_t)rest; pos++;
        if (accept_punct("=")) nodes[pn].a = parse_assign();
        tmp_stack[tmp_top++] = pn;
        if (!accept_punct(",")) break;
    }
    expect_punct(")");
    nodes[n].list = commit_list(start); nodes[n].count = list_count - nodes[n].list;
    nodes[n].a = parse_block();
    return n;
}

static int parse_block(void) {
    int n = new_node(N_BLOCK);
    expect_punct("{");
    int start = tmp_top;
    while (!is_punct("}") && cur()->type != T_EOF && !js_error) tmp_stack[tmp_top++] = parse_statement();
    expect_punct("}");
    nodes[n].list = commit_list(start); nodes[n].count = list_count - nodes[n].list;
    return n;
}

static void skip_semi(void) { accept_punct(";"); }

static int parse_var(void) {
    int n = new_node(N_VAR);
    token_t* t = cur();
    nodes[n].op = (uint8_t)(t->s[0] == 'c' ? 2 : t->s[0] == 'l' ? 1 : 0);
    pos++;
    int start = tmp_top;
    while (!js_error) {
        int d = new_node(N_IDENT);
        if (is_punct("{") || is_punct("[")) {
            /* destructuring: minimal support for `const {a, b} = obj` / `const [a, b] = arr` */
            int is_obj = is_punct("{");
            pos++;
            int names_start = tmp_top;
            while (!is_punct(is_obj ? "}" : "]") && cur()->type != T_EOF) {
                int nn = new_node(N_IDENT); nodes[nn].s = tok_str(cur()); pos++;
                if (accept_punct(":")) { nodes[nn].op = 1; nodes[nn].b = new_node(N_IDENT); nodes[nodes[nn].b].s = tok_str(cur()); pos++; }
                if (accept_punct("=")) nodes[nn].a = parse_assign();
                tmp_stack[tmp_top++] = nn;
                if (!accept_punct(",")) break;
            }
            pos++;
            nodes[d].type = is_obj ? N_OBJECT : N_ARRAY;
            nodes[d].list = commit_list(names_start); nodes[d].count = list_count - nodes[d].list;
        } else {
            nodes[d].s = tok_str(cur()); pos++;
        }
        if (accept_punct("=")) nodes[d].a = parse_assign();
        tmp_stack[tmp_top++] = d;
        if (!accept_punct(",")) break;
    }
    nodes[n].list = commit_list(start); nodes[n].count = list_count - nodes[n].list;
    return n;
}

static int parse_statement_inner(void);
static int parse_statement(void) {
    if (parse_nest >= JS_MAX_PARSE_NEST) {
        if (!js_error) { throw_error("SyntaxError: statements nested too deeply", NULL); js_error = 1; }
        pos++;
        return new_node(N_EMPTY);
    }
    parse_nest++;
    int n = parse_statement_inner();
    parse_nest--;
    return n;
}

static int parse_statement_inner(void) {
    steps++;
    if (is_punct("{")) return parse_block();
    if (accept_punct(";")) return new_node(N_EMPTY);
    if (is_kw("var") || is_kw("let") || is_kw("const")) { int n = parse_var(); skip_semi(); return n; }
    if (is_kw("function")) { pos++; return parse_function(0); }
    if (is_kw("async") && tokens[pos + 1].type == T_KW) { pos += 2; return parse_function(0); }
    if (accept_kw("if")) {
        int n = new_node(N_IF);
        expect_punct("("); nodes[n].a = parse_expr(); expect_punct(")");
        nodes[n].b = parse_statement();
        if (accept_kw("else")) nodes[n].c = parse_statement();
        return n;
    }
    if (accept_kw("while")) {
        int n = new_node(N_WHILE);
        expect_punct("("); nodes[n].a = parse_expr(); expect_punct(")");
        nodes[n].b = parse_statement();
        return n;
    }
    if (accept_kw("do")) {
        int n = new_node(N_DOWHILE);
        nodes[n].b = parse_statement();
        accept_kw("while"); expect_punct("("); nodes[n].a = parse_expr(); expect_punct(")"); skip_semi();
        return n;
    }
    if (accept_kw("for")) {
        expect_punct("(");
        int init = -1;
        if (is_kw("var") || is_kw("let") || is_kw("const")) {
            /* for-in / for-of ? */
            int save = pos;
            pos++;
            if (cur()->type == T_IDENT && tokens[pos + 1].type == T_KW && (br_streq_n(tokens[pos + 1].s, tokens[pos + 1].len, "in") || br_streq_n(tokens[pos + 1].s, tokens[pos + 1].len, "of"))) {
                int n = new_node(br_streq_n(tokens[pos + 1].s, tokens[pos + 1].len, "in") ? N_FORIN : N_FOROF);
                nodes[n].s = tok_str(cur()); pos += 2;
                nodes[n].a = parse_assign(); expect_punct(")");
                nodes[n].b = parse_statement();
                return n;
            }
            pos = save;
            init = parse_var();
        } else if (!is_punct(";")) {
            /* for (x in obj) without declaration */
            if (cur()->type == T_IDENT && tokens[pos + 1].type == T_KW && (br_streq_n(tokens[pos + 1].s, tokens[pos + 1].len, "in") || br_streq_n(tokens[pos + 1].s, tokens[pos + 1].len, "of"))) {
                int n = new_node(br_streq_n(tokens[pos + 1].s, tokens[pos + 1].len, "in") ? N_FORIN : N_FOROF);
                nodes[n].s = tok_str(cur()); nodes[n].op = 1; pos += 2;
                nodes[n].a = parse_assign(); expect_punct(")");
                nodes[n].b = parse_statement();
                return n;
            }
            init = new_node(N_EXPR); nodes[init].a = parse_expr();
        }
        expect_punct(";");
        int n = new_node(N_FOR);
        nodes[n].a = init;
        nodes[n].b = is_punct(";") ? -1 : parse_expr();
        expect_punct(";");
        nodes[n].c = is_punct(")") ? -1 : parse_expr();
        expect_punct(")");
        nodes[n].d = parse_statement();
        return n;
    }
    if (accept_kw("return")) {
        int n = new_node(N_RETURN);
        if (!is_punct(";") && !is_punct("}") && cur()->type != T_EOF && !cur()->nl_before) nodes[n].a = parse_expr();
        skip_semi();
        return n;
    }
    if (accept_kw("break")) { int n = new_node(N_BREAK); if (cur()->type == T_IDENT && !cur()->nl_before) { nodes[n].s = tok_str(cur()); pos++; } skip_semi(); return n; }
    if (accept_kw("continue")) { int n = new_node(N_CONTINUE); if (cur()->type == T_IDENT && !cur()->nl_before) { nodes[n].s = tok_str(cur()); pos++; } skip_semi(); return n; }
    if (accept_kw("throw")) { int n = new_node(N_THROW); nodes[n].a = parse_expr(); skip_semi(); return n; }
    if (accept_kw("switch")) {
        int n = new_node(N_SWITCH);
        expect_punct("("); nodes[n].a = parse_expr(); expect_punct(")"); expect_punct("{");
        int start = tmp_top;
        while (!is_punct("}") && cur()->type != T_EOF && !js_error) {
            int c = new_node(N_CASE);
            if (accept_kw("default")) { nodes[c].a = -1; }
            else { accept_kw("case"); nodes[c].a = parse_expr(); }
            expect_punct(":");
            int bs = tmp_top;
            while (!is_kw("case") && !is_kw("default") && !is_punct("}") && cur()->type != T_EOF && !js_error) tmp_stack[tmp_top++] = parse_statement();
            nodes[c].list = commit_list(bs); nodes[c].count = list_count - nodes[c].list;
            tmp_stack[tmp_top++] = c;
        }
        expect_punct("}");
        nodes[n].list = commit_list(start); nodes[n].count = list_count - nodes[n].list;
        return n;
    }
    if (accept_kw("try")) {
        int n = new_node(N_TRY);
        nodes[n].a = parse_block();
        if (accept_kw("catch")) {
            if (accept_punct("(")) { nodes[n].s = tok_str(cur()); pos++; expect_punct(")"); }
            nodes[n].b = parse_block();
        }
        if (accept_kw("finally")) nodes[n].c = parse_block();
        return n;
    }
    if (accept_kw("class")) {
        /* Not supported: skip the whole class body so the rest of the script still runs. */
        while (!is_punct("{") && cur()->type != T_EOF) pos++;
        int d = 0;
        while (cur()->type != T_EOF) { if (is_punct("{")) d++; else if (is_punct("}")) { d--; if (d == 0) { pos++; break; } } pos++; }
        return new_node(N_EMPTY);
    }
    /* labelled statement */
    if (cur()->type == T_IDENT && tokens[pos + 1].type == T_PUNCT && tokens[pos + 1].len == 1 && tokens[pos + 1].s[0] == ':') {
        const char* label = tok_str(cur()); pos += 2;
        int st = parse_statement();
        nodes[st].s = nodes[st].s ? nodes[st].s : label;
        return st;
    }
    int n = new_node(N_EXPR);
    nodes[n].a = parse_expr();
    skip_semi();
    return n;
}

static int parse_program(void) {
    parse_nest = 0;
    int n = new_node(N_PROGRAM);
    int start = tmp_top;
    while (cur()->type != T_EOF && !js_error) {
        int before = pos;
        tmp_stack[tmp_top++] = parse_statement();
        if (pos == before) pos++;                          /* never stall */
    }
    nodes[n].list = commit_list(start); nodes[n].count = list_count - nodes[n].list;
    return n;
}

/* ------------------------------------------------------------ scopes */

/* Scopes live in a stack-like arena. Loops and blocks allocate one per
 * iteration, which used to exhaust the 512 slots after a few hundred
 * iterations ("out of memory (scopes)"). A scope is popped again when it was
 * the most recently allocated one and nothing captured it: make_function()
 * bumps scope_pinned to the current top, so any scope at or below a closure's
 * birth stays alive for the rest of the page. */
static int scope_pinned = 0;

static int new_scope(int parent, int this_obj) {
    if (scope_count >= JS_MAX_SCOPES) { throw_error("out of memory (scopes)", NULL); return parent; }
    scope_t* s = &scopes[scope_count];
    s->n = 0; s->parent = parent; s->this_obj = this_obj;
    return scope_count++;
}

static void release_scope(int sc) {
    if (sc == scope_count - 1 && sc >= scope_pinned && sc > global_scope) scope_count--;
}

static void scope_declare(int sc, const char* name, value_t v) {
    scope_t* s = &scopes[sc];
    for (int i = 0; i < s->n; i++) if (br_streq(s->names[i], name)) { s->vals[i] = v; return; }
    if (s->n >= 24) {
        /* overflow: spill to the global object */
        set_prop(obj_global, name, v);
        return;
    }
    s->names[s->n] = name; s->vals[s->n] = v; s->n++;
}

static value_t* scope_lookup(int sc, const char* name) {
    int guard = 0;
    while (sc >= 0 && guard++ < JS_MAX_SCOPES) {
        scope_t* s = &scopes[sc];
        for (int i = 0; i < s->n; i++) if (br_streq(s->names[i], name)) return &s->vals[i];
        sc = s->parent;
    }
    prop_t* p = find_own(obj_global, name);
    return p ? &p->val : NULL;
}

static int scope_this(int sc) {
    int guard = 0;
    while (sc >= 0 && guard++ < JS_MAX_SCOPES) { if (scopes[sc].this_obj >= 0) return scopes[sc].this_obj; sc = scopes[sc].parent; }
    return obj_window;
}

/* --------------------------------------------------------- evaluator */

static value_t eval(int n, int sc);
static void exec(int n, int sc);
static value_t call_function(value_t fn, int this_obj, value_t* args, int argc);
static value_t get_member(value_t obj, const char* key);
static void set_member(value_t obj, const char* key, value_t v);

/* DOM hooks implemented further down */
static value_t dom_get(int dom_obj, const char* key, int* handled);
static int dom_set(int dom_obj, const char* key, value_t v);
static int wrap_node(br_node_t* n);

static value_t string_method(const char* s, const char* key, int* handled);
static value_t array_method(int arr, const char* key, int* handled);

static const char* concat(const char* a, const char* b) {
    int la = (int)strlen(a), lb = (int)strlen(b);
    if (la > JS_STR_MAX) la = JS_STR_MAX;
    if (lb > JS_STR_MAX - la) { lb = JS_STR_MAX - la; throw_error("RangeError: string too long", NULL); }
    char* d = js_alloc_str(la + lb);
    for (int i = 0; i < la; i++) d[i] = a[i];
    for (int i = 0; i < lb; i++) d[la + i] = b[i];
    d[la + lb] = 0;
    return d;
}

static value_t get_member(value_t obj, const char* key) {
    if (obj.type == V_STR) {
        if (br_streq(key, "length")) return mk_int((int)strlen(obj.u.s));
        int idx = br_atoi(key);
        if ((key[0] >= '0' && key[0] <= '9') && idx >= 0 && idx < (int)strlen(obj.u.s)) return mk_str(js_strdup_n(&obj.u.s[idx], 1));
        int handled = 0;
        value_t v = string_method(obj.u.s, key, &handled);
        if (handled) return v;
        return get_prop(proto_string, key);
    }
    if (obj.type == V_NUM) {
        int handled = 0; (void)handled;
        return get_prop(proto_number, key);
    }
    if (obj.type == V_BOOL) return UNDEF;
    if (obj.type == V_UNDEF || obj.type == V_NULL) {
        throw_error("TypeError: cannot read property of", obj.type == V_NULL ? "null" : "undefined");
        return UNDEF;
    }
    int o = obj.u.obj;
    object_t* ob = &objects[o];
    if (ob->kind == O_ARRAY) {
        if (br_streq(key, "length")) return mk_int(ob->len);
        if (key[0] >= '0' && key[0] <= '9') { int idx = br_atoi(key); return (idx >= 0 && idx < ob->len) ? ob->items[idx] : UNDEF; }
        int handled = 0;
        value_t v = array_method(o, key, &handled);
        if (handled) return v;
    }
    if (ob->kind == O_DOM) {
        int handled = 0;
        value_t v = dom_get(o, key, &handled);
        if (handled) return v;
    }
    if ((ob->kind == O_FUNC || ob->kind == O_NATIVE) && br_streq(key, "prototype")) {
        prop_t* p = find_own(o, "prototype");
        if (!p) { int pr = new_object(O_PLAIN); set_prop(o, "prototype", mk_obj(pr)); return mk_obj(pr); }
        return p->val;
    }
    return get_prop(o, key);
}

static void set_member(value_t obj, const char* key, value_t v) {
    if (obj.type != V_OBJ && obj.type != V_FUNC) {
        if (obj.type == V_UNDEF || obj.type == V_NULL) throw_error("TypeError: cannot set property of", obj.type == V_NULL ? "null" : "undefined");
        return;
    }
    int o = obj.u.obj;
    object_t* ob = &objects[o];
    if (ob->kind == O_ARRAY) {
        if (key[0] >= '0' && key[0] <= '9') {
            int idx = br_atoi(key);
            if (idx >= 0 && idx < 4096) {
                while (ob->len <= idx) { array_push(o, UNDEF); ob = &objects[o]; if (flow == F_THROW) return; }
                ob->items[idx] = v;
                return;
            }
        }
        if (br_streq(key, "length")) {
            int nl = (int)(to_num(v) >> FX_SHIFT);
            if (nl < ob->len) ob->len = nl < 0 ? 0 : nl;
            else while (ob->len < nl && ob->len < 4096) { array_push(o, UNDEF); ob = &objects[o]; }
            return;
        }
    }
    if (ob->kind == O_DOM && dom_set(o, key, v)) return;
    /* location.href = url / window.location = url navigate */
    if (o == obj_location && br_streq(key, "href")) { navigate_to(to_string(v), 0); return; }
    if (o == obj_window && br_streq(key, "location") && v.type == V_STR) { navigate_to(v.u.s, 0); return; }
    set_prop(o, js_strdup(key), v);
}

static value_t binary_op(const char* op, value_t a, value_t b) {
    if (op[0] == '+' && !op[1]) {
        if (a.type == V_STR || b.type == V_STR || ((a.type == V_OBJ) && objects[a.u.obj].kind != O_DATE) || (b.type == V_OBJ && objects[b.u.obj].kind != O_DATE)) {
            return mk_str(concat(to_string(a), to_string(b)));
        }
        fx_t x = to_num(a), y = to_num(b);
        if (is_nan(x) || is_nan(y)) return mk_num(NAN_FX);
        return mk_num(x + y);
    }
    if (br_streq(op, "===")) return mk_bool(strict_eq(a, b));
    if (br_streq(op, "!==")) return mk_bool(!strict_eq(a, b));
    if (br_streq(op, "==")) return mk_bool(loose_eq(a, b));
    if (br_streq(op, "!=")) return mk_bool(!loose_eq(a, b));
    if (br_streq(op, "in")) {
        if (b.type != V_OBJ && b.type != V_FUNC) return mk_bool(0);
        const char* k = to_string(a);
        if (objects[b.u.obj].kind == O_ARRAY && k[0] >= '0' && k[0] <= '9') return mk_bool(br_atoi(k) < objects[b.u.obj].len);
        return mk_bool(get_prop(b.u.obj, k).type != V_UNDEF);
    }
    if (op[0] == 'I') {   /* instanceof: compare constructor name / prototype chain */
        if (a.type != V_OBJ && a.type != V_FUNC) return mk_bool(0);
        if (b.type != V_FUNC) return mk_bool(0);
        const char* cname = objects[b.u.obj].name;
        object_t* ao = &objects[a.u.obj];
        if (cname) {
            if (br_streq(cname, "Array")) return mk_bool(ao->kind == O_ARRAY);
            if (br_streq(cname, "Object")) return mk_bool(1);
            if (br_streq(cname, "Function")) return mk_bool(ao->kind == O_FUNC || ao->kind == O_NATIVE);
            if (br_streq(cname, "Date")) return mk_bool(ao->kind == O_DATE);
            if (br_streq(cname, "Error")) return mk_bool(ao->kind == O_ERROR);
            if (br_streq(cname, "HTMLElement") || br_streq(cname, "Element") || br_streq(cname, "Node")) return mk_bool(ao->kind == O_DOM);
        }
        value_t proto = get_prop(b.u.obj, "prototype");
        int p = ao->proto; int guard = 0;
        while (p >= 0 && guard++ < 16) { if (proto.type == V_OBJ && proto.u.obj == p) return mk_bool(1); p = objects[p].proto; }
        return mk_bool(0);
    }
    /* string comparison */
    if ((op[0] == '<' || op[0] == '>') && a.type == V_STR && b.type == V_STR) {
        int c = strcmp(a.u.s, b.u.s);
        if (br_streq(op, "<")) return mk_bool(c < 0);
        if (br_streq(op, ">")) return mk_bool(c > 0);
        if (br_streq(op, "<=")) return mk_bool(c <= 0);
        if (br_streq(op, ">=")) return mk_bool(c >= 0);
    }
    fx_t x = to_num(a), y = to_num(b);
    if (is_nan(x) || is_nan(y)) {
        if (op[0] == '<' || op[0] == '>') return mk_bool(0);
        if (op[0] == '&' || op[0] == '|' || op[0] == '^' || op[0] == '<' || op[0] == '>') { x = is_nan(x) ? 0 : x; y = is_nan(y) ? 0 : y; }
        else return mk_num(NAN_FX);
    }
    switch (op[0]) {
    case '-': return mk_num(x - y);
    case '*': {
        if (op[1] == '*') {
            /* integer exponent only */
            int e = (int)(y >> FX_SHIFT);
            fx_t r = FX_ONE;
            if (e >= 0) { for (int i = 0; i < e && i < 64; i++) r = (r * x) >> FX_SHIFT; }
            else { for (int i = 0; i < -e && i < 64; i++) r = (r << FX_SHIFT) / (x ? x : 1); }
            return mk_num(r);
        }
        return mk_num((x * y) >> FX_SHIFT);
    }
    case '/': if (y == 0) return mk_num(x == 0 ? NAN_FX : (x > 0 ? (fx_t)0x7FFFFFFFFFFELL : -(fx_t)0x7FFFFFFFFFFELL)); return mk_num((x << FX_SHIFT) / y);
    case '%': { fx_t yi = y; if (yi == 0) return mk_num(NAN_FX); fx_t r = x % yi; return mk_num(r); }
    case '<':
        if (op[1] == '=') return mk_bool(x <= y);
        if (op[1] == '<') return mk_int((int)((int32_t)(x >> FX_SHIFT) << ((y >> FX_SHIFT) & 31)));
        return mk_bool(x < y);
    case '>':
        if (op[1] == '=') return mk_bool(x >= y);
        if (op[1] == '>' && op[2] == '>') return mk_num((fx_t)((uint32_t)(x >> FX_SHIFT) >> ((y >> FX_SHIFT) & 31)) << FX_SHIFT);
        if (op[1] == '>') return mk_int((int32_t)(x >> FX_SHIFT) >> ((y >> FX_SHIFT) & 31));
        return mk_bool(x > y);
    case '&': return mk_int((int32_t)(x >> FX_SHIFT) & (int32_t)(y >> FX_SHIFT));
    case '|': return mk_int((int32_t)(x >> FX_SHIFT) | (int32_t)(y >> FX_SHIFT));
    case '^': return mk_int((int32_t)(x >> FX_SHIFT) ^ (int32_t)(y >> FX_SHIFT));
    }
    return UNDEF;
}

/* Assign to an lvalue node */
static void assign_to(int target, value_t v, int sc) {
    node_t* t = &nodes[target];
    if (t->type == N_IDENT) {
        value_t* slot = scope_lookup(sc, t->s);
        if (slot && v.type == V_STR && br_streq(t->s, "location") && obj_location >= 0) { prop_t* lp = find_own(obj_global, "location"); if (lp && slot == &lp->val) { navigate_to(v.u.s, 0); return; } }   /* location = url */
        if (slot) *slot = v; else set_prop(obj_global, t->s, v);
    } else if (t->type == N_MEMBER) {
        value_t o = eval(t->a, sc);
        if (flow) return;
        set_member(o, t->s, v);
    } else if (t->type == N_INDEX) {
        value_t o = eval(t->a, sc);
        if (flow) return;
        value_t k = eval(t->b, sc);
        if (flow) return;
        set_member(o, to_string(k), v);
    } else if (t->type == N_ARRAY) {
        /* [a, b] = arr */
        for (int i = 0; i < t->count; i++) {
            value_t item = (v.type == V_OBJ && objects[v.u.obj].kind == O_ARRAY && i < objects[v.u.obj].len) ? objects[v.u.obj].items[i] : UNDEF;
            assign_to(lists[t->list + i], item, sc);
        }
    } else if (t->type == N_OBJECT) {
        for (int i = 0; i + 1 < t->count; i += 2) {
            const char* key = nodes[lists[t->list + i]].s;
            assign_to(lists[t->list + i + 1], get_member(v, key), sc);
        }
    }
}

static value_t make_function(int n, int sc, int this_obj) {
    int o = new_object(O_FUNC);
    if (o < 0) return UNDEF;
    if (scope_count > scope_pinned) scope_pinned = scope_count;   /* closure: keep every live scope */
    objects[o].fn_node = n;
    objects[o].fn_scope = sc;
    objects[o].fn_this = this_obj;
    objects[o].name = nodes[n].s ? nodes[n].s : "anonymous";
    return mk_obj(o);
}

static int collect_args(int list, int count, int sc, value_t* out, int max) {
    int argc = 0;
    for (int i = 0; i < count; i++) {
        int an = lists[list + i];
        if (nodes[an].type == N_SPREAD) {
            value_t arr = eval(nodes[an].a, sc);
            if (flow) return argc;
            if (arr.type == V_OBJ && objects[arr.u.obj].kind == O_ARRAY) {
                for (int k = 0; k < objects[arr.u.obj].len && argc < max; k++) out[argc++] = objects[arr.u.obj].items[k];
            } else if (arr.type == V_STR) {
                for (int k = 0; arr.u.s[k] && argc < max; k++) out[argc++] = mk_str(js_strdup_n(&arr.u.s[k], 1));
            }
            continue;
        }
        value_t v = eval(an, sc);
        if (flow) return argc;
        if (argc < max) out[argc++] = v;
    }
    return argc;
}

static value_t eval_inner(int n, int sc);
static value_t eval(int n, int sc) {
    if (eval_nest >= JS_MAX_EVAL_NEST || br_stack_headroom() < BR_STACK_MIN) {
        if (flow != F_THROW) throw_error("RangeError: Maximum call stack size exceeded", NULL);
        return UNDEF;
    }
    eval_nest++;
    value_t v = eval_inner(n, sc);
    eval_nest--;
    return v;
}

static value_t eval_inner(int n, int sc) {
    if (n < 0) return UNDEF;
    if (++steps > JS_STEP_BUDGET) { if (flow != F_THROW) throw_error("Script terminated: too many steps", NULL); js_aborted = 1; return UNDEF; }
    if (flow) return UNDEF;
    node_t* nd = &nodes[n];
    switch (nd->type) {
    case N_NUM: return mk_num(nd->num);
    case N_STR: return mk_str(nd->s);
    case N_TRUE: return mk_bool(1);
    case N_FALSE: return mk_bool(0);
    case N_NULL: return mk_null();
    case N_UNDEF: return UNDEF;
    case N_THIS: { int t = scope_this(sc); return t >= 0 ? mk_obj(t) : UNDEF; }
    case N_TEMPLATE: {
        const char* acc = "";
        for (int i = 0; i < nd->count; i++) { value_t v = eval(lists[nd->list + i], sc); if (flow) return UNDEF; acc = concat(acc, to_string(v)); }
        return mk_str(acc);
    }
    case N_IDENT: {
        value_t* slot = scope_lookup(sc, nd->s);
        if (slot) return *slot;
        value_t g = get_prop(obj_global, nd->s);
        if (g.type == V_UNDEF && !find_own(obj_global, nd->s)) {
            if (br_streq(nd->s, "window") || br_streq(nd->s, "globalThis") || br_streq(nd->s, "self")) return mk_obj(obj_window);
            throw_error("ReferenceError:", concat(nd->s, " is not defined"));
        }
        return g;
    }
    case N_ARRAY: {
        int a = new_array();
        if (a < 0) return UNDEF;
        for (int i = 0; i < nd->count; i++) {
            int en = lists[nd->list + i];
            if (nodes[en].type == N_SPREAD) {
                value_t src = eval(nodes[en].a, sc); if (flow) return UNDEF;
                if (src.type == V_OBJ && objects[src.u.obj].kind == O_ARRAY) for (int k = 0; k < objects[src.u.obj].len; k++) array_push(a, objects[src.u.obj].items[k]);
                else if (src.type == V_STR) for (int k = 0; src.u.s[k]; k++) array_push(a, mk_str(js_strdup_n(&src.u.s[k], 1)));
                continue;
            }
            value_t v = eval(en, sc); if (flow) return UNDEF;
            array_push(a, v);
        }
        return mk_obj(a);
    }
    case N_OBJECT: {
        int o = new_object(O_PLAIN);
        if (o < 0) return UNDEF;
        for (int i = 0; i + 1 < nd->count; i += 2) {
            int kn = lists[nd->list + i];
            int vn = lists[nd->list + i + 1];
            if (nodes[kn].type == N_SPREAD) {
                value_t src = eval(nodes[kn].a, sc); if (flow) return UNDEF;
                if (src.type == V_OBJ) for (int p = objects[src.u.obj].first_prop; p >= 0; p = props[p].next) set_prop(o, props[p].key, props[p].val);
                continue;
            }
            const char* key;
            if (nodes[kn].type == N_EXPR) { value_t kv = eval(nodes[kn].a, sc); if (flow) return UNDEF; key = js_strdup(to_string(kv)); }
            else key = nodes[kn].s;
            value_t v;
            if (nodes[vn].type == N_FUNC) v = make_function(vn, sc, -1);
            else v = eval(vn, sc);
            if (flow) return UNDEF;
            set_prop(o, key, v);
        }
        return mk_obj(o);
    }
    case N_FUNC: return make_function(n, sc, -1);
    case N_ARROW: return make_function(n, sc, scope_this(sc));
    case N_MEMBER: {
        value_t o = eval(nd->a, sc);
        if (flow) return UNDEF;
        if ((o.type == V_UNDEF || o.type == V_NULL)) {
            /* optional chaining was folded into MEMBER; be lenient for `a?.b` */
            token_t* t = NULL; (void)t;
        }
        return get_member(o, nd->s);
    }
    case N_INDEX: {
        value_t o = eval(nd->a, sc); if (flow) return UNDEF;
        value_t k = eval(nd->b, sc); if (flow) return UNDEF;
        if (o.type == V_OBJ && objects[o.u.obj].kind == O_ARRAY && k.type == V_NUM) {
            int idx = (int)(k.u.n >> FX_SHIFT);
            return (idx >= 0 && idx < objects[o.u.obj].len) ? objects[o.u.obj].items[idx] : UNDEF;
        }
        if (o.type == V_STR && k.type == V_NUM) {
            int idx = (int)(k.u.n >> FX_SHIFT);
            return (idx >= 0 && idx < (int)strlen(o.u.s)) ? mk_str(js_strdup_n(&o.u.s[idx], 1)) : UNDEF;
        }
        return get_member(o, to_string(k));
    }
    case N_CALL: {
        int callee_n = nd->a;
        value_t fn, thisv = UNDEF;
        int this_obj = -1;
        if (nodes[callee_n].type == N_MEMBER || nodes[callee_n].type == N_INDEX) {
            thisv = eval(nodes[callee_n].a, sc); if (flow) return UNDEF;
            const char* key;
            if (nodes[callee_n].type == N_MEMBER) key = nodes[callee_n].s;
            else { value_t k = eval(nodes[callee_n].b, sc); if (flow) return UNDEF; key = to_string(k); }
            /* primitives get a temporary receiver via string/array methods */
            if (thisv.type == V_STR) {
                value_t args[16]; int argc = collect_args(nd->list, nd->count, sc, args, 16); if (flow) return UNDEF;
                extern value_t js_string_call(const char* s, const char* key, value_t* args, int argc, int* handled);
                int handled = 0;
                value_t r = js_string_call(thisv.u.s, key, args, argc, &handled);
                if (handled) return r;
                fn = get_prop(proto_string, key);
                if (fn.type == V_FUNC) return call_function(fn, -1, args, argc);
                throw_error("TypeError: not a function:", key);
                return UNDEF;
            }
            if (thisv.type == V_NUM) {
                value_t args[4]; int argc = collect_args(nd->list, nd->count, sc, args, 4); if (flow) return UNDEF;
                if (br_streq(key, "toFixed")) {
                    int digits = argc > 0 ? (int)(to_num(args[0]) >> FX_SHIFT) : 0;
                    char buf[40]; fx_t v = thisv.u.n; int neg = v < 0; if (neg) v = -v;
                    int64_t scale = 1; for (int i = 0; i < digits && i < 6; i++) scale *= 10;
                    int64_t scaled = (v * scale + FX_ONE / 2) >> FX_SHIFT;
                    int64_t ip = scaled / scale, fp = scaled % scale;
                    char tmp[24]; int t = 0; if (ip == 0) tmp[t++] = '0'; while (ip > 0) { tmp[t++] = (char)('0' + ip % 10); ip /= 10; }
                    int o = 0; if (neg && scaled) buf[o++] = '-'; while (t > 0) buf[o++] = tmp[--t];
                    if (digits > 0) { buf[o++] = '.'; for (int i = digits - 1; i >= 0; i--) { int64_t d = fp; for (int k = 0; k < i; k++) d /= 10; buf[o++] = (char)('0' + d % 10); } }
                    buf[o] = 0;
                    return mk_str(js_strdup(buf));
                }
                if (br_streq(key, "toString")) {
                    int radix = argc > 0 ? (int)(to_num(args[0]) >> FX_SHIFT) : 10;
                    if (radix == 10) return mk_str(to_string(thisv));
                    char buf[40]; int64_t v = thisv.u.n >> FX_SHIFT; int neg = v < 0; if (neg) v = -v;
                    char tmp[40]; int t = 0; if (v == 0) tmp[t++] = '0'; while (v > 0) { int d = (int)(v % radix); tmp[t++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); v /= radix; }
                    int o = 0; if (neg) buf[o++] = '-'; while (t > 0) buf[o++] = tmp[--t]; buf[o] = 0;
                    return mk_str(js_strdup(buf));
                }
                if (br_streq(key, "toLocaleString") || br_streq(key, "valueOf")) return mk_str(to_string(thisv));
                throw_error("TypeError: not a function:", key);
                return UNDEF;
            }
            if (thisv.type == V_BOOL) { if (br_streq(key, "toString")) return mk_str(to_string(thisv)); }
            fn = get_member(thisv, key);
            if (flow) return UNDEF;
            if (thisv.type == V_OBJ || thisv.type == V_FUNC) this_obj = thisv.u.obj;
            if (fn.type != V_FUNC) {
                if (thisv.type == V_OBJ && objects[thisv.u.obj].kind == O_ARRAY) {
                    extern value_t js_array_call(int arr, const char* key, value_t* args, int argc, int* handled);
                    value_t args[16]; int argc = collect_args(nd->list, nd->count, sc, args, 16); if (flow) return UNDEF;
                    int handled = 0;
                    value_t r = js_array_call(thisv.u.obj, key, args, argc, &handled);
                    if (handled) return r;
                }
                if (thisv.type == V_OBJ && objects[thisv.u.obj].kind == O_DOM) {
                    extern value_t js_dom_call(int obj, const char* key, value_t* args, int argc, int* handled);
                    value_t args[16]; int argc = collect_args(nd->list, nd->count, sc, args, 16); if (flow) return UNDEF;
                    int handled = 0;
                    value_t r = js_dom_call(thisv.u.obj, key, args, argc, &handled);
                    if (handled) return r;
                }
                if (thisv.type == V_OBJ && objects[thisv.u.obj].kind == O_DATE) {
                    extern value_t js_date_call(int obj, const char* key, value_t* args, int argc, int* handled);
                    value_t args[8]; int argc = collect_args(nd->list, nd->count, sc, args, 8); if (flow) return UNDEF;
                    int handled = 0;
                    value_t r = js_date_call(thisv.u.obj, key, args, argc, &handled);
                    if (handled) return r;
                }
                if (thisv.type == V_OBJ || thisv.type == V_FUNC) {
                    /* generic object methods */
                    if (br_streq(key, "hasOwnProperty")) { value_t args[2]; int argc = collect_args(nd->list, nd->count, sc, args, 2); return mk_bool(argc > 0 && find_own(thisv.u.obj, to_string(args[0])) != NULL); }
                    if (br_streq(key, "toString")) return mk_str(to_string(thisv));
                    if (br_streq(key, "valueOf")) return thisv;
                    if (thisv.type == V_FUNC) {
                        extern value_t js_function_call(int fnobj, const char* key, value_t* args, int argc, int* handled);
                        value_t args[16]; int argc = collect_args(nd->list, nd->count, sc, args, 16); if (flow) return UNDEF;
                        int handled = 0;
                        value_t r = js_function_call(thisv.u.obj, key, args, argc, &handled);
                        if (handled) return r;
                    }
                }
                throw_error("TypeError: not a function:", key);
                return UNDEF;
            }
        } else {
            fn = eval(callee_n, sc);
            if (flow) return UNDEF;
        }
        if (fn.type != V_FUNC) {
            throw_error("TypeError: not a function:", nodes[callee_n].type == N_IDENT ? nodes[callee_n].s : "(expression)");
            return UNDEF;
        }
        value_t args[16];
        int argc = collect_args(nd->list, nd->count, sc, args, 16);
        if (flow) return UNDEF;
        return call_function(fn, this_obj, args, argc);
    }
    case N_NEW: {
        value_t ctor = eval(nd->a, sc);
        if (flow) return UNDEF;
        value_t args[16];
        int argc = collect_args(nd->list, nd->count, sc, args, 16);
        if (flow) return UNDEF;
        if (ctor.type != V_FUNC) { throw_error("TypeError: not a constructor", NULL); return UNDEF; }
        object_t* co = &objects[ctor.u.obj];
        if (co->kind == O_NATIVE) {
            /* natives that act as constructors get this_obj = -2 marker */
            return co->native(-2, args, argc);
        }
        int o = new_object(O_PLAIN);
        if (o < 0) return UNDEF;
        value_t proto = get_prop(ctor.u.obj, "prototype");
        if (proto.type == V_OBJ) objects[o].proto = proto.u.obj;
        set_prop(o, "constructor", ctor);
        value_t r = call_function(ctor, o, args, argc);
        if (r.type == V_OBJ) return r;
        return mk_obj(o);
    }
    case N_UNARY: {
        value_t v = eval(nd->a, sc); if (flow) return UNDEF;
        switch (nd->op) {
        case '!': return mk_bool(!truthy(v));
        case '-': { fx_t x = to_num(v); return mk_num(is_nan(x) ? x : -x); }
        case '+': return mk_num(to_num(v));
        case '~': return mk_int(~(int32_t)(to_num(v) >> FX_SHIFT));
        }
        return UNDEF;
    }
    case N_TYPEOF: {
        /* typeof undeclared -> "undefined" without throwing */
        if (nodes[nd->a].type == N_IDENT) {
            value_t* slot = scope_lookup(sc, nodes[nd->a].s);
            if (!slot) {
                const char* s = nodes[nd->a].s;
                if (br_streq(s, "window") || br_streq(s, "document") || br_streq(s, "globalThis")) return mk_str("object");
                return mk_str("undefined");
            }
            return mk_str(type_name(*slot));
        }
        value_t v = eval(nd->a, sc); if (flow) return UNDEF;
        return mk_str(type_name(v));
    }
    case N_DELETE: {
        node_t* t = &nodes[nd->a];
        if (t->type == N_MEMBER) { value_t o = eval(t->a, sc); if (flow) return UNDEF; if (o.type == V_OBJ) del_prop(o.u.obj, t->s); }
        else if (t->type == N_INDEX) { value_t o = eval(t->a, sc); if (flow) return UNDEF; value_t k = eval(t->b, sc); if (flow) return UNDEF; if (o.type == V_OBJ) { if (objects[o.u.obj].kind == O_ARRAY && k.type == V_NUM) { int i = (int)(k.u.n >> FX_SHIFT); if (i >= 0 && i < objects[o.u.obj].len) objects[o.u.obj].items[i] = UNDEF; } else del_prop(o.u.obj, to_string(k)); } }
        return mk_bool(1);
    }
    case N_PREFIX: case N_POSTFIX: {
        value_t old = eval(nd->a, sc); if (flow) return UNDEF;
        fx_t x = to_num(old);
        fx_t nx = nd->op == 1 ? x + FX_ONE : x - FX_ONE;
        assign_to(nd->a, mk_num(nx), sc);
        return nd->type == N_PREFIX ? mk_num(nx) : mk_num(x);
    }
    case N_BINARY: {
        value_t a = eval(nd->a, sc); if (flow) return UNDEF;
        value_t b = eval(nd->b, sc); if (flow) return UNDEF;
        return binary_op(nd->s, a, b);
    }
    case N_LOGICAL: {
        value_t a = eval(nd->a, sc); if (flow) return UNDEF;
        if (nd->s[0] == '&') return truthy(a) ? eval(nd->b, sc) : a;
        if (nd->s[0] == '|') return truthy(a) ? a : eval(nd->b, sc);
        return (a.type == V_UNDEF || a.type == V_NULL) ? eval(nd->b, sc) : a;   /* ?? */
    }
    case N_COND: {
        value_t c = eval(nd->a, sc); if (flow) return UNDEF;
        return truthy(c) ? eval(nd->b, sc) : eval(nd->c, sc);
    }
    case N_COMMA: { eval(nd->a, sc); if (flow) return UNDEF; return eval(nd->b, sc); }
    case N_ASSIGN: {
        value_t v;
        if (nd->s[0] == 0) {
            v = eval(nd->b, sc); if (flow) return UNDEF;
            /* name anonymous functions after the variable */
            if (v.type == V_FUNC && nodes[nd->a].type == N_IDENT && objects[v.u.obj].kind == O_FUNC && br_streq(objects[v.u.obj].name, "anonymous")) objects[v.u.obj].name = nodes[nd->a].s;
        } else {
            value_t old = eval(nd->a, sc); if (flow) return UNDEF;
            if (br_streq(nd->s, "||")) { if (truthy(old)) return old; v = eval(nd->b, sc); }
            else if (br_streq(nd->s, "&&")) { if (!truthy(old)) return old; v = eval(nd->b, sc); }
            else if (br_streq(nd->s, "??")) { if (old.type != V_UNDEF && old.type != V_NULL) return old; v = eval(nd->b, sc); }
            else { value_t r = eval(nd->b, sc); if (flow) return UNDEF; v = binary_op(nd->s, old, r); }
            if (flow) return UNDEF;
        }
        assign_to(nd->a, v, sc);
        return v;
    }
    case N_SPREAD: return eval(nd->a, sc);
    default: break;
    }
    return UNDEF;
}

/* hoisting: declare function declarations and `var`s of a body up front.
 * `var` is function-scoped, so nested blocks / if / loops / try / switch
 * are searched too (`if (x) { var a = 1; } use(a)` is a common idiom in
 * minified scripts); only function bodies stop the walk. */
static void hoist_vars(int s, int sc, int nest) {
    if (s < 0 || nest > 12) return;
    node_t* nd = &nodes[s];
    switch (nd->type) {
    case N_VAR:
        if (nd->op == 0) for (int k = 0; k < nd->count; k++) { int d = lists[nd->list + k]; if (nodes[d].type == N_IDENT && !scope_lookup(sc, nodes[d].s)) scope_declare(sc, nodes[d].s, UNDEF); }
        return;
    case N_BLOCK:
        for (int i = 0; i < nd->count; i++) hoist_vars(lists[nd->list + i], sc, nest + 1);
        return;
    case N_IF: hoist_vars(nd->b, sc, nest + 1); hoist_vars(nd->c, sc, nest + 1); return;
    case N_WHILE: case N_DOWHILE: hoist_vars(nd->b, sc, nest + 1); return;
    case N_FOR: hoist_vars(nd->a, sc, nest + 1); hoist_vars(nd->d, sc, nest + 1); return;
    case N_FORIN: case N_FOROF: if (nd->op == 0 && nd->s && !scope_lookup(sc, nd->s)) scope_declare(sc, nd->s, UNDEF); hoist_vars(nd->b, sc, nest + 1); return;
    case N_TRY: hoist_vars(nd->a, sc, nest + 1); hoist_vars(nd->b, sc, nest + 1); hoist_vars(nd->c, sc, nest + 1); return;
    case N_SWITCH:
        for (int i = 0; i < nd->count; i++) { int c = lists[nd->list + i]; for (int k = 0; k < nodes[c].count; k++) hoist_vars(lists[nodes[c].list + k], sc, nest + 1); }
        return;
    default: return;
    }
}
static void hoist(int list, int count, int sc) {
    for (int i = 0; i < count; i++) {
        int s = lists[list + i];
        if (nodes[s].type == N_FUNCDECL) scope_declare(sc, nodes[s].s, make_function(s, sc, -1));
        else hoist_vars(s, sc, 0);
    }
}

static void exec_list(int list, int count, int sc) {
    for (int i = 0; i < count && !flow; i++) exec(lists[list + i], sc);
}

static value_t call_function(value_t fn, int this_obj, value_t* args, int argc) {
    if (fn.type != V_FUNC) { throw_error("TypeError: not a function", NULL); return UNDEF; }
    object_t* fo = &objects[fn.u.obj];
    if (fo->kind == O_NATIVE) return fo->native(this_obj, args, argc);
    if (depth >= JS_MAX_DEPTH || br_stack_headroom() < BR_STACK_MIN) { throw_error("RangeError: Maximum call stack size exceeded", NULL); return UNDEF; }
    int fnode = fo->fn_node;
    node_t* f = &nodes[fnode];
    int use_this = (fo->fn_this >= 0) ? fo->fn_this : this_obj;
    if (use_this < 0 && f->type != N_ARROW) use_this = obj_window;
    int sc = new_scope(fo->fn_scope, use_this);
    if (flow) return UNDEF;
    /* params */
    for (int i = 0; i < f->count; i++) {
        int pn = lists[f->list + i];
        value_t v = i < argc ? args[i] : UNDEF;
        if (nodes[pn].op == 1 && f->type != N_ARROW) {          /* rest param */
            int a = new_array();
            for (int k = i; k < argc; k++) array_push(a, args[k]);
            v = mk_obj(a);
        }
        if (v.type == V_UNDEF && nodes[pn].a >= 0) { v = eval(nodes[pn].a, sc); if (flow) return UNDEF; }
        scope_declare(sc, nodes[pn].s, v);
    }
    if (f->type != N_ARROW) {
        int a = new_array();
        if (a >= 0) { for (int k = 0; k < argc; k++) array_push(a, args[k]); scope_declare(sc, "arguments", mk_obj(a)); }
    }
    depth++;
    value_t result = UNDEF;
    if (f->type == N_ARROW && f->op == 1) {
        result = eval(f->a, sc);
    } else {
        int body = f->a;
        hoist(nodes[body].list, nodes[body].count, sc);
        exec_list(nodes[body].list, nodes[body].count, sc);
        if (flow == F_RETURN) { result = flow_val; flow = F_NONE; }
        else if (flow == F_BREAK || flow == F_CONTINUE) flow = F_NONE;
    }
    depth--;
    release_scope(sc);
    return result;
}

static int label_matches(node_t* loop) {
    if (flow_label == NULL) return 1;
    if (loop->s && br_streq(loop->s, flow_label)) { flow_label = NULL; return 1; }
    return 0;
}

static void exec_inner(int n, int sc);
static void exec(int n, int sc) {
    if (eval_nest >= JS_MAX_EVAL_NEST || br_stack_headroom() < BR_STACK_MIN) {
        if (flow != F_THROW) throw_error("RangeError: Maximum call stack size exceeded", NULL);
        return;
    }
    eval_nest++;
    exec_inner(n, sc);
    eval_nest--;
}

static void exec_inner(int n, int sc) {
    if (n < 0 || flow) return;
    if (++steps > JS_STEP_BUDGET) { if (flow != F_THROW) throw_error("Script terminated: too many steps", NULL); js_aborted = 1; return; }
    node_t* nd = &nodes[n];
    switch (nd->type) {
    case N_EMPTY: return;
    case N_EXPR: eval(nd->a, sc); return;
    case N_VAR:
        for (int i = 0; i < nd->count; i++) {
            int d = lists[nd->list + i];
            if (nodes[d].type == N_IDENT) {
                value_t v = UNDEF;
                if (nodes[d].a >= 0) {
                    v = eval(nodes[d].a, sc); if (flow) return;
                    if (v.type == V_FUNC && objects[v.u.obj].kind == O_FUNC && br_streq(objects[v.u.obj].name, "anonymous")) objects[v.u.obj].name = nodes[d].s;
                } else if (nd->op == 0) {
                    value_t* existing = scope_lookup(sc, nodes[d].s);
                    if (existing) continue;                    /* var re-declaration keeps value */
                }
                if (nd->op == 0) {
                    /* `var` is function-scoped: hoisting already declared it
                     * in the enclosing function scope, assign there rather
                     * than shadowing it in this block */
                    value_t* slot = scope_lookup(sc, nodes[d].s);
                    if (slot) { *slot = v; continue; }
                }
                scope_declare(sc, nodes[d].s, v);
            } else {
                /* destructuring declaration */
                value_t src = eval(nodes[d].a, sc); if (flow) return;
                for (int k = 0; k < nodes[d].count; k++) {
                    int nn = lists[nodes[d].list + k];
                    value_t v;
                    if (nodes[d].type == N_ARRAY) v = (src.type == V_OBJ && objects[src.u.obj].kind == O_ARRAY && k < objects[src.u.obj].len) ? objects[src.u.obj].items[k] : UNDEF;
                    else v = (src.type == V_OBJ || src.type == V_STR) ? get_member(src, nodes[nn].s) : UNDEF;
                    if (v.type == V_UNDEF && nodes[nn].a >= 0) { v = eval(nodes[nn].a, sc); if (flow) return; }
                    scope_declare(sc, nodes[nn].op == 1 ? nodes[nodes[nn].b].s : nodes[nn].s, v);
                }
            }
        }
        return;
    case N_FUNCDECL: {
        value_t* slot = scope_lookup(sc, nd->s);
        if (!slot) scope_declare(sc, nd->s, make_function(n, sc, -1));
        return;
    }
    case N_BLOCK: {
        int bs = new_scope(sc, -1);
        if (flow) return;
        hoist(nd->list, nd->count, bs);
        exec_list(nd->list, nd->count, bs);
        release_scope(bs);
        return;
    }
    case N_IF: {
        value_t c = eval(nd->a, sc); if (flow) return;
        if (truthy(c)) exec(nd->b, sc); else if (nd->c >= 0) exec(nd->c, sc);
        return;
    }
    case N_WHILE: case N_DOWHILE: {
        int first = nd->type == N_DOWHILE;
        int guard = 0;
        while (guard++ < 1000000) {
            if (!first) { value_t c = eval(nd->a, sc); if (flow) return; if (!truthy(c)) break; }
            first = 0;
            exec(nd->b, sc);
            if (flow == F_BREAK) { if (label_matches(nd)) flow = F_NONE; return; }
            if (flow == F_CONTINUE) { if (label_matches(nd)) flow = F_NONE; else return; }
            if (flow) return;
            if (nd->type == N_DOWHILE) { value_t c = eval(nd->a, sc); if (flow) return; if (!truthy(c)) break; }
        }
        return;
    }
    case N_FOR: {
        int ls = new_scope(sc, -1);
        if (flow) return;
        if (nd->a >= 0) exec(nd->a, ls);
        int guard = 0;
        while (guard++ < 1000000 && !flow) {
            if (nd->b >= 0) { value_t c = eval(nd->b, ls); if (flow) return; if (!truthy(c)) break; }
            /* per-iteration scope so closures capture the current value (let semantics) */
            int is = new_scope(ls, -1);
            if (flow) return;
            for (int i = 0; i < scopes[ls].n; i++) scope_declare(is, scopes[ls].names[i], scopes[ls].vals[i]);
            exec(nd->d, is);
            /* copy back */
            for (int i = 0; i < scopes[ls].n; i++) { value_t* v = scope_lookup(is, scopes[ls].names[i]); if (v) scopes[ls].vals[i] = *v; }
            release_scope(is);
            if (flow == F_BREAK) { if (label_matches(nd)) flow = F_NONE; break; }
            if (flow == F_CONTINUE) { if (label_matches(nd)) flow = F_NONE; else break; }
            if (flow) break;
            if (nd->c >= 0) { eval(nd->c, ls); if (flow) break; }
        }
        release_scope(ls);
        return;
    }
    case N_FORIN: case N_FOROF: {
        value_t src = eval(nd->a, sc); if (flow) return;
        int ls = new_scope(sc, -1);
        if (flow) return;
        if (src.type == V_OBJ && objects[src.u.obj].kind == O_ARRAY) {
            for (int i = 0; i < objects[src.u.obj].len && !flow; i++) {
                int is = new_scope(ls, -1); if (flow) return;
                if (nd->type == N_FORIN) { char b[16]; br_itoa(i, b); scope_declare(is, nd->s, mk_str(js_strdup(b))); }
                else scope_declare(is, nd->s, objects[src.u.obj].items[i]);
                if (nd->op == 1) { value_t* slot = scope_lookup(sc, nd->s); if (slot) *slot = scopes[is].vals[0]; }
                exec(nd->b, is);
                release_scope(is);
                if (flow == F_BREAK) { if (label_matches(nd)) flow = F_NONE; break; }
                if (flow == F_CONTINUE) { if (label_matches(nd)) flow = F_NONE; else break; }
            }
        } else if (src.type == V_STR) {
            for (int i = 0; src.u.s[i] && !flow; i++) {
                int is = new_scope(ls, -1); if (flow) return;
                if (nd->type == N_FORIN) { char b[16]; br_itoa(i, b); scope_declare(is, nd->s, mk_str(js_strdup(b))); }
                else scope_declare(is, nd->s, mk_str(js_strdup_n(&src.u.s[i], 1)));
                exec(nd->b, is);
                release_scope(is);
                if (flow == F_BREAK) { if (label_matches(nd)) flow = F_NONE; break; }
                if (flow == F_CONTINUE) { if (label_matches(nd)) flow = F_NONE; else break; }
            }
        } else if (src.type == V_OBJ || src.type == V_FUNC) {
            /* snapshot keys first: the body may mutate (static: keeps the
             * exec frame small; nested for-in over objects shares it, which
             * only costs correctness for a mutated outer object) */
            static const char* keys[256]; int nk = 0;
            int o = src.u.obj;
            if (objects[o].kind == O_DOM && nd->type == N_FOROF) {
                /* for (const child of element.children) handled via array getter */
                value_t arr = get_member(src, "children");
                if (arr.type == V_OBJ) { value_t tmp = arr; src = tmp; o = src.u.obj;
                    for (int i = 0; i < objects[o].len && !flow; i++) { int is = new_scope(ls, -1); if (flow) return; scope_declare(is, nd->s, objects[o].items[i]); exec(nd->b, is); release_scope(is); if (flow == F_BREAK) { if (label_matches(nd)) flow = F_NONE; break; } if (flow == F_CONTINUE) { if (label_matches(nd)) flow = F_NONE; else break; } }
                }
                release_scope(ls);
                return;
            }
            for (int p = objects[o].first_prop; p >= 0 && nk < 256; p = props[p].next) keys[nk++] = props[p].key;
            for (int i = 0; i < nk && !flow; i++) {
                int is = new_scope(ls, -1); if (flow) return;
                if (nd->type == N_FORIN) scope_declare(is, nd->s, mk_str(keys[i]));
                else { int pair = new_array(); array_push(pair, mk_str(keys[i])); array_push(pair, get_prop(o, keys[i])); scope_declare(is, nd->s, mk_obj(pair)); }
                if (nd->op == 1) { value_t* slot = scope_lookup(sc, nd->s); if (slot) *slot = scopes[is].vals[0]; }
                exec(nd->b, is);
                release_scope(is);
                if (flow == F_BREAK) { if (label_matches(nd)) flow = F_NONE; break; }
                if (flow == F_CONTINUE) { if (label_matches(nd)) flow = F_NONE; else break; }
            }
        }
        release_scope(ls);
        return;
    }
    case N_RETURN: {
        value_t v = nd->a >= 0 ? eval(nd->a, sc) : UNDEF;
        if (flow) return;
        flow = F_RETURN; flow_val = v;
        return;
    }
    case N_BREAK: flow = F_BREAK; flow_label = nd->s; return;
    case N_CONTINUE: flow = F_CONTINUE; flow_label = nd->s; return;
    case N_THROW: {
        value_t v = eval(nd->a, sc); if (flow) return;
        flow = F_THROW; flow_val = v;
        br_strlcpy(js_error_msg, to_string(v), sizeof(js_error_msg));
        return;
    }
    case N_SWITCH: {
        value_t d = eval(nd->a, sc); if (flow) return;
        int ss = new_scope(sc, -1); if (flow) return;
        int matched = -1;
        for (int i = 0; i < nd->count; i++) {
            int c = lists[nd->list + i];
            if (nodes[c].a < 0) continue;
            value_t cv = eval(nodes[c].a, ss); if (flow) return;
            if (strict_eq(d, cv)) { matched = i; break; }
        }
        if (matched < 0) for (int i = 0; i < nd->count; i++) if (nodes[lists[nd->list + i]].a < 0) { matched = i; break; }
        if (matched < 0) return;
        for (int i = matched; i < nd->count && !flow; i++) {
            int c = lists[nd->list + i];
            hoist(nodes[c].list, nodes[c].count, ss);
            exec_list(nodes[c].list, nodes[c].count, ss);
        }
        if (flow == F_BREAK && flow_label == NULL) flow = F_NONE;
        release_scope(ss);
        return;
    }
    case N_TRY: {
        exec(nd->a, sc);
        if (js_aborted) return;                 /* budget exhausted: not catchable */
        if (flow == F_THROW) {
            value_t err = flow_val;
            flow = F_NONE;
            if (nd->b >= 0) {
                int cs = new_scope(sc, -1); if (flow) return;
                if (nd->s) {
                    /* wrap string errors into an Error-ish object with .message */
                    if (err.type == V_STR) { int eo = new_object(O_ERROR); if (eo >= 0) { set_prop(eo, "message", err); set_prop(eo, "name", mk_str("Error")); err = mk_obj(eo); } }
                    scope_declare(cs, nd->s, err);
                }
                exec(nd->b, cs);
                release_scope(cs);
            }
        }
        if (nd->c >= 0 && !js_aborted) { int saved = flow; value_t sv = flow_val; flow = F_NONE; exec(nd->c, sc); if (!flow) { flow = saved; flow_val = sv; } }
        return;
    }
    case N_PROGRAM: hoist(nd->list, nd->count, sc); exec_list(nd->list, nd->count, sc); return;
    default:
        eval(n, sc);
        return;
    }
}

/* ------------------------------------------------------- string API */

static int str_index_of(const char* hay, const char* needle, int from) {
    int hl = (int)strlen(hay), nl = (int)strlen(needle);
    if (from < 0) from = 0;
    for (int i = from; i + nl <= hl; i++) {
        int m = 1;
        for (int k = 0; k < nl; k++) if (hay[i + k] != needle[k]) { m = 0; break; }
        if (m) return i;
    }
    return -1;
}

static const char* str_slice(const char* s, int a, int b) {
    int l = (int)strlen(s);
    if (a < 0) { a = l + a; }
    if (a < 0) { a = 0; }
    if (a > l) { a = l; }
    if (b < 0) { b = l + b; }
    if (b < 0) { b = 0; }
    if (b > l) { b = l; }
    if (b < a) b = a;
    return js_strdup_n(&s[a], b - a);
}

/* Tiny regex engine: supports literals, ., character classes [a-z], \d \w \s
 * \b, anchors ^ $, quantifiers * + ? {n,m}, groups (...) (non-capturing
 * semantics), alternation |, flags g i. Enough for the split/replace/test
 * calls typical demo pages make. */
typedef struct { const char* pat; int plen; int icase; int global; } regex_t;

static int rx_parse(const char* lit, regex_t* rx) {
    /* lit = "/pattern/flags" */
    if (lit[0] != '/') return 0;
    int e = (int)strlen(lit) - 1;
    while (e > 0 && lit[e] != '/') e--;
    if (e <= 0) return 0;
    rx->pat = lit + 1; rx->plen = e - 1; rx->icase = 0; rx->global = 0;
    for (int i = e + 1; lit[i]; i++) { if (lit[i] == 'i') rx->icase = 1; if (lit[i] == 'g') rx->global = 1; }
    return 1;
}

static int rx_lower(int c, int icase) { return (icase && c >= 'A' && c <= 'Z') ? c + 32 : c; }

static int rx_class_match(const char* p, int plen, int c, int icase, int* adv) {
    /* p points at '[' */
    int i = 1, neg = 0, matched = 0;
    if (i < plen && p[i] == '^') { neg = 1; i++; }
    while (i < plen && p[i] != ']') {
        int lo, hi;
        if (p[i] == '\\' && i + 1 < plen) {
            char e = p[i + 1]; i += 2;
            if (e == 'd') { if (c >= '0' && c <= '9') matched = 1; continue; }
            if (e == 'w') { if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') matched = 1; continue; }
            if (e == 's') { if (c == ' ' || c == '\t' || c == '\n' || c == '\r') matched = 1; continue; }
            lo = hi = (e == 'n') ? '\n' : (e == 't') ? '\t' : e;
        } else { lo = hi = p[i]; i++; }
        if (i + 1 < plen && p[i] == '-' && p[i + 1] != ']') { hi = p[i + 1]; if (hi == '\\' && i + 2 < plen) hi = p[i + 2], i++; i += 2; }
        int cc = rx_lower(c, icase);
        if ((cc >= rx_lower(lo, icase) && cc <= rx_lower(hi, icase)) || (c >= lo && c <= hi)) matched = 1;
    }
    *adv = i + 1;
    return neg ? !matched : matched;
}

/* match a single atom at p against char c; returns atom length in *alen, 1 if match */
static int rx_atom(const char* p, int plen, const char* s, int si, int slen, int icase, int* alen) {
    if (plen <= 0) return 0;
    if (p[0] == '[') { int adv; int c = si < slen ? s[si] : -1; int m = c >= 0 && rx_class_match(p, plen, c, icase, &adv); *alen = adv; if (c < 0) { int a2; rx_class_match(p, plen, 'a', icase, &a2); *alen = a2; } return m; }
    if (p[0] == '\\' && plen > 1) {
        *alen = 2;
        if (si >= slen) return 0;
        int c = s[si];
        switch (p[1]) {
        case 'd': return c >= '0' && c <= '9';
        case 'D': return !(c >= '0' && c <= '9');
        case 'w': return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        case 'W': return !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_');
        case 's': return c == ' ' || c == '\t' || c == '\n' || c == '\r';
        case 'S': return !(c == ' ' || c == '\t' || c == '\n' || c == '\r');
        case 'n': return c == '\n';
        case 't': return c == '\t';
        default: return rx_lower(c, icase) == rx_lower(p[1], icase);
        }
    }
    *alen = 1;
    if (si >= slen) return 0;
    if (p[0] == '.') return s[si] != '\n';
    return rx_lower(s[si], icase) == rx_lower(p[0], icase);
}

static int rx_match_here(const char* p, int plen, const char* s, int si, int slen, int icase, int* end);

static int rx_nest = 0, rx_budget = 0;
#define RX_MAX_NEST 40           /* 40 * ~1.2 KB frames */
#define RX_BUDGET   200000       /* backtracking steps per search */

static int rx_match_seq_inner(const char* p, int plen, const char* s, int si, int slen, int icase, int* end);
static int rx_match_seq(const char* p, int plen, const char* s, int si, int slen, int icase, int* end) {
    if (rx_nest >= RX_MAX_NEST || --rx_budget < 0 || br_stack_headroom() < BR_STACK_MIN) return 0;   /* give up: no match */
    rx_nest++;
    int r = rx_match_seq_inner(p, plen, s, si, slen, icase, end);
    rx_nest--;
    return r;
}

static int rx_match_seq_inner(const char* p, int plen, const char* s, int si, int slen, int icase, int* end) {
    if (plen == 0) { *end = si; return 1; }
    if (p[0] == '$' && plen == 1) { if (si == slen) { *end = si; return 1; } return 0; }
    if (p[0] == '^') return rx_match_seq(p + 1, plen - 1, s, si, slen, icase, end);
    if (p[0] == '\\' && plen > 1 && p[1] == 'b') {
        int before = si > 0 && ((s[si - 1] >= 'a' && s[si - 1] <= 'z') || (s[si - 1] >= 'A' && s[si - 1] <= 'Z') || (s[si - 1] >= '0' && s[si - 1] <= '9') || s[si - 1] == '_');
        int after = si < slen && ((s[si] >= 'a' && s[si] <= 'z') || (s[si] >= 'A' && s[si] <= 'Z') || (s[si] >= '0' && s[si] <= '9') || s[si] == '_');
        if (before == after) return 0;
        return rx_match_seq(p + 2, plen - 2, s, si, slen, icase, end);
    }
    /* group */
    int alen;
    int group_len = 0;
    const char* gp = NULL; int gplen = 0;
    if (p[0] == '(') {
        int d = 0, i = 0;
        for (; i < plen; i++) { if (p[i] == '\\') { i++; continue; } if (p[i] == '(') d++; else if (p[i] == ')') { d--; if (d == 0) break; } }
        group_len = i + 1;
        gp = p + 1; gplen = i - 1;
        if (gplen >= 2 && gp[0] == '?' && gp[1] == ':') { gp += 2; gplen -= 2; }
        alen = group_len;
    } else {
        if (!rx_atom(p, plen, s, si, slen, icase, &alen) && !(si >= slen)) { /* fallthrough: still need alen for quantifier '?' or '*' */ }
        if (p[0] == '[') { int adv; rx_class_match(p, plen, 'a', icase, &adv); alen = adv; }
        else if (p[0] == '\\' && plen > 1) alen = 2; else alen = 1;
    }
    /* quantifier */
    int min = 1, max = 1, qlen = 0;
    if (alen < plen) {
        char q = p[alen];
        if (q == '*') { min = 0; max = 100000; qlen = 1; }
        else if (q == '+') { min = 1; max = 100000; qlen = 1; }
        else if (q == '?') { min = 0; max = 1; qlen = 1; }
        else if (q == '{') {
            int i = alen + 1; min = 0; while (i < plen && p[i] >= '0' && p[i] <= '9') min = min * 10 + (p[i++] - '0');
            max = min;
            if (i < plen && p[i] == ',') { i++; if (i < plen && p[i] == '}') max = 100000; else { max = 0; while (i < plen && p[i] >= '0' && p[i] <= '9') max = max * 10 + (p[i++] - '0'); } }
            if (i < plen && p[i] == '}') i++;
            qlen = i - alen;
        }
        if (qlen && alen + qlen < plen && p[alen + qlen] == '?') qlen++;   /* lazy: treated greedy */
    }
    const char* rest = p + alen + qlen; int restlen = plen - alen - qlen;
    /* greedy: collect match positions */
    int positions[256]; int np = 0;
    positions[np++] = si;
    int cur_i = si;
    for (int count = 0; count < max && np < 256; count++) {
        int ne;
        if (gp) {
            if (!rx_match_here(gp, gplen, s, cur_i, slen, icase, &ne)) break;
            if (ne == cur_i) { break; }
        } else {
            int al;
            if (!rx_atom(p, alen, s, cur_i, slen, icase, &al)) break;
            ne = cur_i + 1;
        }
        cur_i = ne;
        positions[np++] = cur_i;
    }
    for (int k = np - 1; k >= 0; k--) {
        if (k < min) break;
        if (rx_match_seq(rest, restlen, s, positions[k], slen, icase, end)) return 1;
    }
    return 0;
}

static int rx_match_here(const char* p, int plen, const char* s, int si, int slen, int icase, int* end) {
    /* alternation at top level of this sequence */
    int d = 0;
    for (int i = 0; i < plen; i++) {
        if (p[i] == '\\') { i++; continue; }
        if (p[i] == '[') { while (i < plen && p[i] != ']') i++; continue; }
        if (p[i] == '(') d++; else if (p[i] == ')') d--;
        else if (p[i] == '|' && d == 0) {
            if (rx_match_seq(p, i, s, si, slen, icase, end)) return 1;
            return rx_match_here(p + i + 1, plen - i - 1, s, si, slen, icase, end);
        }
    }
    return rx_match_seq(p, plen, s, si, slen, icase, end);
}

/* find first match at or after `from`; returns start or -1, *end = end */
static int rx_search(regex_t* rx, const char* s, int from, int* end) {
    int slen = (int)strlen(s);
    rx_nest = 0; rx_budget = RX_BUDGET;
    int anchored = rx->plen > 0 && rx->pat[0] == '^';
    for (int i = from; i <= slen; i++) {
        if (rx_match_here(rx->pat, rx->plen, s, i, slen, rx->icase, end)) return i;
        if (anchored) break;
    }
    return -1;
}

value_t js_string_call(const char* s, const char* key, value_t* args, int argc, int* handled) {
    *handled = 1;
    int len = (int)strlen(s);
    if (br_streq(key, "toUpperCase") || br_streq(key, "toLocaleUpperCase")) { int l2 = len > JS_STR_MAX ? JS_STR_MAX : len; char* d = js_alloc_str(l2); for (int i = 0; i < l2; i++) d[i] = (s[i] >= 'a' && s[i] <= 'z') ? (char)(s[i] - 32) : s[i]; d[l2] = 0; return mk_str(d); }
    if (br_streq(key, "toLowerCase") || br_streq(key, "toLocaleLowerCase")) { int l2 = len > JS_STR_MAX ? JS_STR_MAX : len; char* d = js_alloc_str(l2); for (int i = 0; i < l2; i++) d[i] = (s[i] >= 'A' && s[i] <= 'Z') ? (char)(s[i] + 32) : s[i]; d[l2] = 0; return mk_str(d); }
    if (br_streq(key, "trim") || br_streq(key, "trimStart") || br_streq(key, "trimEnd")) {
        int a = 0, b = len;
        if (!br_streq(key, "trimEnd")) while (a < b && (s[a] == ' ' || s[a] == '\n' || s[a] == '\t' || s[a] == '\r')) a++;
        if (!br_streq(key, "trimStart")) while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\n' || s[b - 1] == '\t' || s[b - 1] == '\r')) b--;
        return mk_str(js_strdup_n(&s[a], b - a));
    }
    if (br_streq(key, "charAt")) { int i = argc ? (int)(to_num(args[0]) >> FX_SHIFT) : 0; return mk_str(i >= 0 && i < len ? js_strdup_n(&s[i], 1) : ""); }
    if (br_streq(key, "charCodeAt") || br_streq(key, "codePointAt")) { int i = argc ? (int)(to_num(args[0]) >> FX_SHIFT) : 0; return i >= 0 && i < len ? mk_int((unsigned char)s[i]) : mk_num(NAN_FX); }
    if (br_streq(key, "indexOf")) return mk_int(argc ? str_index_of(s, to_string(args[0]), argc > 1 ? (int)(to_num(args[1]) >> FX_SHIFT) : 0) : -1);
    if (br_streq(key, "lastIndexOf")) { int r = -1; const char* n = argc ? to_string(args[0]) : ""; int from = 0; while (1) { int f = str_index_of(s, n, from); if (f < 0) break; r = f; from = f + 1; } return mk_int(r); }
    if (br_streq(key, "includes") || br_streq(key, "contains")) return mk_bool(argc && str_index_of(s, to_string(args[0]), 0) >= 0);
    if (br_streq(key, "startsWith")) { const char* n = argc ? to_string(args[0]) : ""; int nl = (int)strlen(n); int from = argc > 1 ? (int)(to_num(args[1]) >> FX_SHIFT) : 0; return mk_bool(from + nl <= len && br_streq_n(&s[from], nl, n)); }
    if (br_streq(key, "endsWith")) { const char* n = argc ? to_string(args[0]) : ""; int nl = (int)strlen(n); return mk_bool(nl <= len && br_streq_n(&s[len - nl], nl, n)); }
    if (br_streq(key, "slice") || br_streq(key, "substring") || br_streq(key, "substr")) {
        int a = argc ? (int)(to_num(args[0]) >> FX_SHIFT) : 0;
        int b = argc > 1 && args[1].type != V_UNDEF ? (int)(to_num(args[1]) >> FX_SHIFT) : len;
        if (br_streq(key, "substr")) { if (a < 0) a = len + a; b = a + b; }
        if (br_streq(key, "substring")) { if (a < 0) a = 0; if (b < 0) b = 0; if (a > b) { int t = a; a = b; b = t; } }
        return mk_str(str_slice(s, a, b));
    }
    if (br_streq(key, "split")) {
        int arr = new_array();
        if (arr < 0) return UNDEF;
        if (!argc || args[0].type == V_UNDEF) { array_push(arr, mk_str(s)); return mk_obj(arr); }
        int limit = argc > 1 ? (int)(to_num(args[1]) >> FX_SHIFT) : 100000;
        if (args[0].type == V_OBJ && objects[args[0].u.obj].kind == O_REGEXP) {
            regex_t rx; rx_parse(objects[args[0].u.obj].name, &rx);
            int from = 0, seg = 0;
            while (from <= len && objects[arr].len < limit) {
                int e; int m = rx_search(&rx, s, from, &e);
                if (m < 0 || e == m) { array_push(arr, mk_str(js_strdup_n(&s[seg], len - seg))); break; }
                array_push(arr, mk_str(js_strdup_n(&s[seg], m - seg)));
                seg = e; from = e;
            }
            return mk_obj(arr);
        }
        const char* sep = to_string(args[0]);
        int sl = (int)strlen(sep);
        if (sl == 0) { for (int i = 0; i < len && objects[arr].len < limit; i++) array_push(arr, mk_str(js_strdup_n(&s[i], 1))); return mk_obj(arr); }
        int from = 0;
        while (objects[arr].len < limit) {
            int f = str_index_of(s, sep, from);
            if (f < 0) { array_push(arr, mk_str(js_strdup_n(&s[from], len - from))); break; }
            array_push(arr, mk_str(js_strdup_n(&s[from], f - from)));
            from = f + sl;
        }
        return mk_obj(arr);
    }
    if (br_streq(key, "replace") || br_streq(key, "replaceAll")) {
        if (argc < 2) return mk_str(s);
        int all = br_streq(key, "replaceAll");
        char* out = js_alloc_str(0);
        /* we build into a temp buffer then dup */
        static char tmp[8192];
        int o = 0;
        int from = 0;
        int is_rx = args[0].type == V_OBJ && objects[args[0].u.obj].kind == O_REGEXP;
        regex_t rx;
        const char* needle = NULL; int nl = 0;
        if (is_rx) { rx_parse(objects[args[0].u.obj].name, &rx); if (rx.global) all = 1; }
        else { needle = to_string(args[0]); nl = (int)strlen(needle); }
        int guard = 0;
        while (from <= len && guard++ < 10000) {
            int m, e;
            if (is_rx) { m = rx_search(&rx, s, from, &e); }
            else { m = nl ? str_index_of(s, needle, from) : -1; e = m + nl; }
            if (m < 0) break;
            for (int i = from; i < m && o < (int)sizeof(tmp) - 1; i++) tmp[o++] = s[i];
            const char* rep;
            if (args[1].type == V_FUNC) { value_t a0 = mk_str(js_strdup_n(&s[m], e - m)); value_t r = call_function(args[1], -1, &a0, 1); rep = to_string(r); }
            else rep = to_string(args[1]);
            for (int i = 0; rep[i] && o < (int)sizeof(tmp) - 1; i++) {
                if (rep[i] == '$' && rep[i + 1] == '&') { for (int k = m; k < e && o < (int)sizeof(tmp) - 1; k++) tmp[o++] = s[k]; i++; continue; }
                tmp[o++] = rep[i];
            }
            if (e == m) { if (m < len && o < (int)sizeof(tmp) - 1) tmp[o++] = s[m]; from = m + 1; } else from = e;
            if (!all) break;
        }
        for (int i = from; i < len && o < (int)sizeof(tmp) - 1; i++) tmp[o++] = s[i];
        tmp[o] = 0;
        (void)out;
        return mk_str(js_strdup_n(tmp, o));
    }
    if (br_streq(key, "match") || br_streq(key, "search") || br_streq(key, "matchAll")) {
        if (!argc) return mk_null();
        regex_t rx;
        if (args[0].type == V_OBJ && objects[args[0].u.obj].kind == O_REGEXP) rx_parse(objects[args[0].u.obj].name, &rx);
        else { const char* lit = concat("/", concat(to_string(args[0]), "/")); rx_parse(lit, &rx); }
        if (br_streq(key, "search")) { int e; return mk_int(rx_search(&rx, s, 0, &e)); }
        int arr = new_array();
        int from = 0;
        while (from <= len) {
            int e; int m = rx_search(&rx, s, from, &e);
            if (m < 0) break;
            array_push(arr, mk_str(js_strdup_n(&s[m], e - m)));
            if (!rx.global) break;
            from = e > m ? e : m + 1;
        }
        if (objects[arr].len == 0) return mk_null();
        return mk_obj(arr);
    }
    if (br_streq(key, "repeat")) { int n = argc ? (int)(to_num(args[0]) >> FX_SHIFT) : 0; if (n < 0) n = 0; if (n * len > 4096) n = 4096 / (len ? len : 1); char* d = js_alloc_str(n * len); for (int i = 0; i < n; i++) for (int k = 0; k < len; k++) d[i * len + k] = s[k]; d[n * len] = 0; return mk_str(d); }
    if (br_streq(key, "padStart") || br_streq(key, "padEnd")) {
        int target = argc ? (int)(to_num(args[0]) >> FX_SHIFT) : 0;
        const char* pad = argc > 1 ? to_string(args[1]) : " ";
        if (target <= len || !pad[0]) return mk_str(s);
        if (target > 1024) target = 1024;
        char* d = js_alloc_str(target);
        int pl = (int)strlen(pad);
        if (br_streq(key, "padStart")) { for (int i = 0; i < target - len; i++) d[i] = pad[i % pl]; for (int i = 0; i < len; i++) d[target - len + i] = s[i]; }
        else { for (int i = 0; i < len; i++) d[i] = s[i]; for (int i = 0; i < target - len; i++) d[len + i] = pad[i % pl]; }
        d[target] = 0;
        return mk_str(d);
    }
    if (br_streq(key, "concat")) { const char* r = s; for (int i = 0; i < argc; i++) r = concat(r, to_string(args[i])); return mk_str(r); }
    if (br_streq(key, "at")) { int i = argc ? (int)(to_num(args[0]) >> FX_SHIFT) : 0; if (i < 0) i += len; return mk_str(i >= 0 && i < len ? js_strdup_n(&s[i], 1) : ""); }
    if (br_streq(key, "toString") || br_streq(key, "valueOf") || br_streq(key, "normalize")) return mk_str(s);
    if (br_streq(key, "localeCompare")) return mk_int(argc ? strcmp(s, to_string(args[0])) : 0);
    *handled = 0;
    return UNDEF;
}

static value_t string_method(const char* s, const char* key, int* handled) {
    (void)s; (void)key;
    *handled = 0;
    return UNDEF;
}

/* -------------------------------------------------------- array API */

static void sort_values(value_t* items, int n, value_t cmp) {
    /* insertion sort: arrays are small */
    for (int i = 1; i < n; i++) {
        value_t key = items[i];
        int j = i - 1;
        while (j >= 0) {
            int gt;
            if (cmp.type == V_FUNC) { value_t a[2]; a[0] = items[j]; a[1] = key; value_t r = call_function(cmp, -1, a, 2); if (flow) return; gt = to_num(r) > 0; }
            else gt = strcmp(to_string(items[j]), to_string(key)) > 0;
            if (!gt) break;
            items[j + 1] = items[j];
            j--;
        }
        items[j + 1] = key;
    }
}

value_t js_array_call(int arr, const char* key, value_t* args, int argc, int* handled) {
    *handled = 1;
    object_t* a = &objects[arr];
    if (br_streq(key, "push")) { for (int i = 0; i < argc; i++) array_push(arr, args[i]); return mk_int(objects[arr].len); }
    if (br_streq(key, "pop")) { if (a->len == 0) return UNDEF; return a->items[--a->len]; }
    if (br_streq(key, "shift")) { if (a->len == 0) return UNDEF; value_t v = a->items[0]; for (int i = 1; i < a->len; i++) a->items[i - 1] = a->items[i]; a->len--; return v; }
    if (br_streq(key, "unshift")) { for (int k = argc - 1; k >= 0; k--) { array_push(arr, UNDEF); a = &objects[arr]; for (int i = a->len - 1; i > 0; i--) a->items[i] = a->items[i - 1]; a->items[0] = args[k]; } return mk_int(a->len); }
    if (br_streq(key, "join")) {
        const char* sep = argc && args[0].type != V_UNDEF ? to_string(args[0]) : ",";
        static char tmp[4096]; int o = 0;
        for (int i = 0; i < a->len; i++) {
            if (i) for (int k = 0; sep[k] && o < (int)sizeof(tmp) - 1; k++) tmp[o++] = sep[k];
            const char* s = (a->items[i].type == V_UNDEF || a->items[i].type == V_NULL) ? "" : to_string(a->items[i]);
            for (int k = 0; s[k] && o < (int)sizeof(tmp) - 1; k++) tmp[o++] = s[k];
        }
        tmp[o] = 0;
        return mk_str(js_strdup_n(tmp, o));
    }
    if (br_streq(key, "indexOf")) { for (int i = 0; i < a->len; i++) if (argc && strict_eq(a->items[i], args[0])) return mk_int(i); return mk_int(-1); }
    if (br_streq(key, "lastIndexOf")) { for (int i = a->len - 1; i >= 0; i--) if (argc && strict_eq(a->items[i], args[0])) return mk_int(i); return mk_int(-1); }
    if (br_streq(key, "includes")) { for (int i = 0; i < a->len; i++) if (argc && strict_eq(a->items[i], args[0])) return mk_bool(1); return mk_bool(0); }
    if (br_streq(key, "slice")) {
        int s = argc ? (int)(to_num(args[0]) >> FX_SHIFT) : 0, e = argc > 1 && args[1].type != V_UNDEF ? (int)(to_num(args[1]) >> FX_SHIFT) : a->len;
        if (s < 0) { s += a->len; }
        if (e < 0) { e += a->len; }
        if (s < 0) { s = 0; }
        if (e > a->len) { e = a->len; }
        int r = new_array(); for (int i = s; i < e; i++) array_push(r, objects[arr].items[i]); return mk_obj(r);
    }
    if (br_streq(key, "splice")) {
        int s = argc ? (int)(to_num(args[0]) >> FX_SHIFT) : 0;
        if (s < 0) { s += a->len; }
        if (s < 0) { s = 0; }
        if (s > a->len) { s = a->len; }
        int del = argc > 1 ? (int)(to_num(args[1]) >> FX_SHIFT) : a->len - s;
        if (del < 0) { del = 0; }
        if (s + del > a->len) { del = a->len - s; }
        int removed = new_array();
        for (int i = 0; i < del; i++) array_push(removed, a->items[s + i]);
        int ins = argc > 2 ? argc - 2 : 0;
        int newlen = a->len - del + ins;
        /* grow if needed */
        while (objects[arr].cap < newlen) { array_push(arr, UNDEF); objects[arr].len--; }
        a = &objects[arr];
        if (ins > del) for (int i = a->len - 1; i >= s + del; i--) a->items[i + ins - del] = a->items[i];
        else for (int i = s + del; i < a->len; i++) a->items[i - del + ins] = a->items[i];
        for (int i = 0; i < ins; i++) a->items[s + i] = args[2 + i];
        a->len = newlen;
        return mk_obj(removed);
    }
    if (br_streq(key, "concat")) {
        int r = new_array();
        for (int i = 0; i < a->len; i++) array_push(r, objects[arr].items[i]);
        for (int k = 0; k < argc; k++) {
            if (args[k].type == V_OBJ && objects[args[k].u.obj].kind == O_ARRAY) for (int i = 0; i < objects[args[k].u.obj].len; i++) array_push(r, objects[args[k].u.obj].items[i]);
            else array_push(r, args[k]);
        }
        return mk_obj(r);
    }
    if (br_streq(key, "reverse")) { for (int i = 0; i < a->len / 2; i++) { value_t t = a->items[i]; a->items[i] = a->items[a->len - 1 - i]; a->items[a->len - 1 - i] = t; } return mk_obj(arr); }
    if (br_streq(key, "sort")) { sort_values(a->items, a->len, argc ? args[0] : UNDEF); return mk_obj(arr); }
    if (br_streq(key, "fill")) { for (int i = 0; i < a->len; i++) a->items[i] = argc ? args[0] : UNDEF; return mk_obj(arr); }
    if (br_streq(key, "at")) { int i = argc ? (int)(to_num(args[0]) >> FX_SHIFT) : 0; if (i < 0) i += a->len; return i >= 0 && i < a->len ? a->items[i] : UNDEF; }
    if (br_streq(key, "flat")) { int r = new_array(); for (int i = 0; i < a->len; i++) { value_t v = objects[arr].items[i]; if (v.type == V_OBJ && objects[v.u.obj].kind == O_ARRAY) for (int k = 0; k < objects[v.u.obj].len; k++) array_push(r, objects[v.u.obj].items[k]); else array_push(r, v); } return mk_obj(r); }
    if (br_streq(key, "toString")) return mk_str(obj_to_string(arr));
    if (br_streq(key, "keys") || br_streq(key, "entries") || br_streq(key, "values")) {
        int r = new_array();
        for (int i = 0; i < a->len; i++) {
            if (br_streq(key, "keys")) array_push(r, mk_int(i));
            else if (br_streq(key, "values")) array_push(r, objects[arr].items[i]);
            else { int p = new_array(); array_push(p, mk_int(i)); array_push(p, objects[arr].items[i]); array_push(r, mk_obj(p)); }
        }
        return mk_obj(r);
    }
    /* callbacks */
    int is_foreach = br_streq(key, "forEach"), is_map = br_streq(key, "map"), is_filter = br_streq(key, "filter"),
        is_find = br_streq(key, "find"), is_findidx = br_streq(key, "findIndex"), is_some = br_streq(key, "some"),
        is_every = br_streq(key, "every"), is_reduce = br_streq(key, "reduce"), is_flatmap = br_streq(key, "flatMap"),
        is_findlast = br_streq(key, "findLast");
    if (is_foreach || is_map || is_filter || is_find || is_findidx || is_some || is_every || is_reduce || is_flatmap || is_findlast) {
        if (!argc || args[0].type != V_FUNC) { throw_error("TypeError: callback is not a function", NULL); return UNDEF; }
        value_t fn = args[0];
        int r = (is_map || is_filter || is_flatmap) ? new_array() : -1;
        value_t acc = UNDEF;
        int start = 0;
        if (is_reduce) { if (argc > 1) acc = args[1]; else { if (a->len == 0) { throw_error("TypeError: reduce of empty array", NULL); return UNDEF; } acc = a->items[0]; start = 1; } }
        int n = a->len;
        if (is_findlast) { for (int i = n - 1; i >= 0; i--) { value_t cb[3]; cb[0] = objects[arr].items[i]; cb[1] = mk_int(i); cb[2] = mk_obj(arr); value_t res = call_function(fn, -1, cb, 3); if (flow) return UNDEF; if (truthy(res)) return objects[arr].items[i]; } return UNDEF; }
        for (int i = start; i < n && i < objects[arr].len; i++) {
            value_t cb[4]; int cn;
            value_t item = objects[arr].items[i];
            if (is_reduce) { cb[0] = acc; cb[1] = item; cb[2] = mk_int(i); cb[3] = mk_obj(arr); cn = 4; }
            else { cb[0] = item; cb[1] = mk_int(i); cb[2] = mk_obj(arr); cn = 3; }
            value_t res = call_function(fn, -1, cb, cn);
            if (flow) return UNDEF;
            if (is_map) array_push(r, res);
            else if (is_flatmap) { if (res.type == V_OBJ && objects[res.u.obj].kind == O_ARRAY) for (int k = 0; k < objects[res.u.obj].len; k++) array_push(r, objects[res.u.obj].items[k]); else array_push(r, res); }
            else if (is_filter) { if (truthy(res)) array_push(r, item); }
            else if (is_find) { if (truthy(res)) return item; }
            else if (is_findidx) { if (truthy(res)) return mk_int(i); }
            else if (is_some) { if (truthy(res)) return mk_bool(1); }
            else if (is_every) { if (!truthy(res)) return mk_bool(0); }
            else if (is_reduce) acc = res;
        }
        if (is_map || is_filter || is_flatmap) return mk_obj(r);
        if (is_find) return UNDEF;
        if (is_findidx) return mk_int(-1);
        if (is_some) return mk_bool(0);
        if (is_every) return mk_bool(1);
        if (is_reduce) return acc;
        return UNDEF;
    }
    *handled = 0;
    return UNDEF;
}

static value_t array_method(int arr, const char* key, int* handled) {
    (void)arr; (void)key;
    *handled = 0;
    return UNDEF;
}

value_t js_function_call(int fnobj, const char* key, value_t* args, int argc, int* handled) {
    *handled = 1;
    value_t fn = mk_obj(fnobj);
    if (br_streq(key, "call")) { int t = argc && (args[0].type == V_OBJ || args[0].type == V_FUNC) ? args[0].u.obj : -1; return call_function(fn, t, args + 1, argc > 0 ? argc - 1 : 0); }
    if (br_streq(key, "apply")) {
        int t = argc && (args[0].type == V_OBJ || args[0].type == V_FUNC) ? args[0].u.obj : -1;
        value_t a2[16]; int n = 0;
        if (argc > 1 && args[1].type == V_OBJ && objects[args[1].u.obj].kind == O_ARRAY) for (; n < objects[args[1].u.obj].len && n < 16; n++) a2[n] = objects[args[1].u.obj].items[n];
        return call_function(fn, t, a2, n);
    }
    if (br_streq(key, "bind")) {
        /* bound function: a native trampoline stored as a FUNC with fn_this set and bound args in a prop */
        int b = new_object(O_FUNC);
        if (b < 0) return UNDEF;
        objects[b].fn_node = objects[fnobj].fn_node;
        objects[b].fn_scope = objects[fnobj].fn_scope;
        objects[b].fn_this = argc && (args[0].type == V_OBJ || args[0].type == V_FUNC) ? args[0].u.obj : obj_window;
        objects[b].name = objects[fnobj].name;
        objects[b].native = objects[fnobj].native;
        if (objects[fnobj].kind == O_NATIVE) objects[b].kind = O_NATIVE;
        return mk_obj(b);
    }
    if (br_streq(key, "toString")) return mk_str("function() { [code] }");
    *handled = 0;
    return UNDEF;
}

/* ---------------------------------------------------------- Date API */

/* writes v (zero padded to 2 digits when pad) into out, returns chars written */
static int date_field(char* out, int v, int pad) {
    char n[12]; int o = 0;
    if (pad && v < 10) out[o++] = '0';
    br_itoa(v, n);
    for (int i = 0; n[i]; i++) out[o++] = n[i];
    return o;
}

value_t js_date_call(int obj, const char* key, value_t* args, int argc, int* handled) {
    (void)args; (void)argc;
    *handled = 1;
    int ms = objects[obj].date_ms;
    rtc_read_time();
    if (br_streq(key, "getTime") || br_streq(key, "valueOf")) return mk_int(ms);
    if (br_streq(key, "getFullYear")) return mk_int((int)rtc_year);
    if (br_streq(key, "getMonth")) return mk_int((int)rtc_month - 1);
    if (br_streq(key, "getDate")) return mk_int((int)rtc_day);
    if (br_streq(key, "getDay")) { int y = (int)rtc_year, m = (int)rtc_month, d = (int)rtc_day; static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4}; if (m < 3) y--; return mk_int((y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7); }
    if (br_streq(key, "getHours")) return mk_int((int)rtc_hours);
    if (br_streq(key, "getMinutes")) return mk_int((int)rtc_minutes);
    if (br_streq(key, "getSeconds")) return mk_int((int)rtc_seconds);
    if (br_streq(key, "getMilliseconds")) return mk_int(ms % 1000);
    if (br_streq(key, "toLocaleTimeString") || br_streq(key, "toTimeString") || br_streq(key, "toLocaleString") || br_streq(key, "toString") || br_streq(key, "toLocaleDateString") || br_streq(key, "toDateString") || br_streq(key, "toISOString")) {
        char b[48]; int o = 0;
        int date_part = !br_streq(key, "toLocaleTimeString") && !br_streq(key, "toTimeString");
        int time_part = !br_streq(key, "toLocaleDateString") && !br_streq(key, "toDateString");
        if (date_part) {
            o += date_field(b + o, (int)rtc_year, 0); b[o++] = '-';
            o += date_field(b + o, (int)rtc_month, 1); b[o++] = '-';
            o += date_field(b + o, (int)rtc_day, 1);
            if (time_part) b[o++] = br_streq(key, "toISOString") ? 'T' : ' ';
        }
        if (time_part) {
            o += date_field(b + o, (int)rtc_hours, 1); b[o++] = ':';
            o += date_field(b + o, (int)rtc_minutes, 1); b[o++] = ':';
            o += date_field(b + o, (int)rtc_seconds, 1);
        }
        b[o] = 0;
        return mk_str(js_strdup(b));
    }
    *handled = 0;
    return UNDEF;
}

/* ------------------------------------------------------------- JSON */

static void json_out(value_t v, char* out, int max, int* o, int depth_) {
    if (*o >= max - 8 || depth_ > 32 || br_stack_headroom() < BR_STACK_MIN) return;
    switch (v.type) {
    case V_NUM: { char b[40]; fx_to_str(v.u.n, b); for (int i = 0; b[i] && *o < max - 1; i++) out[(*o)++] = b[i]; return; }
    case V_BOOL: { const char* s = v.u.b ? "true" : "false"; for (int i = 0; s[i]; i++) out[(*o)++] = s[i]; return; }
    case V_NULL: case V_UNDEF: { const char* s = "null"; for (int i = 0; s[i]; i++) out[(*o)++] = s[i]; return; }
    case V_STR: {
        out[(*o)++] = '"';
        for (int i = 0; v.u.s[i] && *o < max - 3; i++) {
            char c = v.u.s[i];
            if (c == '"' || c == '\\') { out[(*o)++] = '\\'; out[(*o)++] = c; }
            else if (c == '\n') { out[(*o)++] = '\\'; out[(*o)++] = 'n'; }
            else out[(*o)++] = c;
        }
        out[(*o)++] = '"';
        return;
    }
    case V_FUNC: { const char* s = "null"; for (int i = 0; s[i]; i++) out[(*o)++] = s[i]; return; }
    case V_OBJ: {
        if (depth_ > 12) return;
        object_t* ob = &objects[v.u.obj];
        if (ob->kind == O_ARRAY) {
            out[(*o)++] = '[';
            for (int i = 0; i < ob->len && *o < max - 4; i++) { if (i) out[(*o)++] = ','; json_out(ob->items[i], out, max, o, depth_ + 1); }
            out[(*o)++] = ']';
            return;
        }
        out[(*o)++] = '{';
        int first = 1;
        for (int p = ob->first_prop; p >= 0 && *o < max - 4; p = props[p].next) {
            if (props[p].val.type == V_FUNC || props[p].val.type == V_UNDEF) continue;
            if (!first) out[(*o)++] = ',';
            first = 0;
            json_out(mk_str(props[p].key), out, max, o, depth_ + 1);
            out[(*o)++] = ':';
            json_out(props[p].val, out, max, o, depth_ + 1);
        }
        out[(*o)++] = '}';
        return;
    }
    }
}

static value_t json_parse_value(const char** pp) {
    const char* p = *pp;
    while (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r') p++;
    value_t r = UNDEF;
    if (*p == '{') {
        int o = new_object(O_PLAIN); p++;
        while (*p) {
            while (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r' || *p == ',') p++;
            if (*p == '}') { p++; break; }
            if (*p != '"') break;
            *pp = p; value_t k = json_parse_value(pp); p = *pp;
            while (*p == ' ' || *p == ':') p++;
            *pp = p; value_t v = json_parse_value(pp); p = *pp;
            if (k.type == V_STR) set_prop(o, k.u.s, v);
        }
        r = mk_obj(o);
    } else if (*p == '[') {
        int a = new_array(); p++;
        while (*p) {
            while (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r' || *p == ',') p++;
            if (*p == ']') { p++; break; }
            *pp = p; value_t v = json_parse_value(pp); if (*pp == p) break; p = *pp;
            array_push(a, v);
        }
        r = mk_obj(a);
    } else if (*p == '"') {
        p++;
        static char tmp[4096]; int o = 0;
        while (*p && *p != '"' && o < (int)sizeof(tmp) - 1) {
            if (*p == '\\' && p[1]) { p++; tmp[o++] = *p == 'n' ? '\n' : *p == 't' ? '\t' : *p == 'u' ? '?' : *p; if (*p == 'u') p += 4; p++; continue; }
            tmp[o++] = *p++;
        }
        if (*p == '"') p++;
        tmp[o] = 0;
        r = mk_str(js_strdup_n(tmp, o));
    } else if (br_streq_prefix(p, "true")) { p += 4; r = mk_bool(1); }
    else if (br_streq_prefix(p, "false")) { p += 5; r = mk_bool(0); }
    else if (br_streq_prefix(p, "null")) { p += 4; r = mk_null(); }
    else {
        int ok; fx_t n = str_to_fx(p, &ok);
        if (!ok) { *pp = p; return UNDEF; }
        if (*p == '-') p++;
        while ((*p >= '0' && *p <= '9') || *p == '.' || *p == 'e' || *p == 'E' || *p == '+' || *p == '-') p++;
        r = mk_num(n);
    }
    *pp = p;
    return r;
}

/* -------------------------------------------------------- natives */

static char console_buf[512];

static value_t n_console_log(int t, value_t* a, int n) {
    (void)t;
    int o = 0;
    for (int i = 0; i < n; i++) {
        const char* s;
        if (a[i].type == V_OBJ && objects[a[i].u.obj].kind != O_DOM) { static char js[512]; int jo = 0; json_out(a[i], js, sizeof(js), &jo, 0); js[jo] = 0; s = js; }
        else s = to_string(a[i]);
        if (i && o < (int)sizeof(console_buf) - 1) console_buf[o++] = ' ';
        for (int k = 0; s[k] && o < (int)sizeof(console_buf) - 1; k++) console_buf[o++] = s[k];
    }
    console_buf[o] = 0;
    br_js_console(console_buf);
    return UNDEF;
}

static value_t n_alert(int t, value_t* a, int n) { (void)t; br_alert(n ? to_string(a[0]) : ""); return UNDEF; }
static value_t n_confirm(int t, value_t* a, int n) { (void)t; br_alert(n ? to_string(a[0]) : ""); return mk_bool(1); }
static value_t n_prompt(int t, value_t* a, int n) { (void)t; (void)a; return n > 1 ? mk_str(to_string(a[1])) : mk_str(""); }

static value_t n_parseInt(int t, value_t* a, int n) {
    (void)t;
    if (!n) return mk_num(NAN_FX);
    const char* s = to_string(a[0]);
    int radix = n > 1 ? (int)(to_num(a[1]) >> FX_SHIFT) : 10;
    while (*s == ' ') s++;
    int neg = 0; if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    if (radix == 16 || (radix == 10 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))) { if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2; radix = 16; }
    if (radix == 0) radix = 10;
    int64_t v = 0; int any = 0;
    while (*s) {
        int d = (*s >= '0' && *s <= '9') ? *s - '0' : (*s >= 'a' && *s <= 'z') ? *s - 'a' + 10 : (*s >= 'A' && *s <= 'Z') ? *s - 'A' + 10 : 99;
        if (d >= radix) break;
        v = v * radix + d; s++; any = 1;
    }
    if (!any) return mk_num(NAN_FX);
    return mk_num((neg ? -v : v) << FX_SHIFT);
}

static value_t n_parseFloat(int t, value_t* a, int n) { (void)t; if (!n) return mk_num(NAN_FX); int ok; fx_t v = str_to_fx(to_string(a[0]), &ok); return ok ? mk_num(v) : mk_num(NAN_FX); }
static value_t n_isNaN(int t, value_t* a, int n) { (void)t; return mk_bool(!n || is_nan(to_num(a[0]))); }
static value_t n_isFinite(int t, value_t* a, int n) { (void)t; return mk_bool(n && !is_nan(to_num(a[0]))); }
static value_t n_String(int t, value_t* a, int n) { (void)t; return mk_str(n ? to_string(a[0]) : ""); }
static value_t n_Number(int t, value_t* a, int n) { (void)t; return mk_num(n ? to_num(a[0]) : 0); }
static value_t n_Boolean(int t, value_t* a, int n) { (void)t; return mk_bool(n && truthy(a[0])); }
static value_t n_Array(int t, value_t* a, int n) { (void)t; int r = new_array(); if (n == 1 && a[0].type == V_NUM) { int len = (int)(a[0].u.n >> FX_SHIFT); for (int i = 0; i < len && i < 4096; i++) array_push(r, UNDEF); } else for (int i = 0; i < n; i++) array_push(r, a[i]); return mk_obj(r); }
static value_t n_Object(int t, value_t* a, int n) { (void)t; (void)a; (void)n; return mk_obj(new_object(O_PLAIN)); }
static value_t n_Error(int t, value_t* a, int n) { (void)t; int e = new_object(O_ERROR); if (e < 0) return UNDEF; set_prop(e, "message", mk_str(n ? to_string(a[0]) : "")); set_prop(e, "name", mk_str("Error")); set_prop(e, "stack", mk_str("")); return mk_obj(e); }
static value_t n_Date(int t, value_t* a, int n) { (void)t; (void)a; (void)n; int d = new_object(O_DATE); if (d < 0) return UNDEF; objects[d].date_ms = (int)(uptime_ticks * (1000 / TICKS_PER_SEC)); return mk_obj(d); }
static value_t n_Date_now(int t, value_t* a, int n) { (void)t; (void)a; (void)n; return mk_int((int)(uptime_ticks * (1000 / TICKS_PER_SEC))); }
static value_t n_RegExp(int t, value_t* a, int n) { (void)t; int r = new_object(O_REGEXP); if (r < 0) return UNDEF; const char* pat = n ? to_string(a[0]) : ""; const char* flags = n > 1 ? to_string(a[1]) : ""; objects[r].name = concat(concat("/", pat), concat("/", flags)); return mk_obj(r); }
static value_t n_Promise(int t, value_t* a, int n) { (void)t; (void)a; (void)n; return mk_obj(new_object(O_PLAIN)); }
/* IntersectionObserver: everything is "intersecting" immediately, so
 * reveal-on-scroll sites (opacity:0 until .active is added) show their
 * content. observe(el) queues one callback with a single entry. */
static value_t n_setTimeout(int t, value_t* a, int n);
static value_t n_noop(int t, value_t* a, int n);
static value_t n_io_observe(int t, value_t* a, int n) {
    if (t < 0 || !n || a[0].type != V_OBJ) return UNDEF;
    value_t cb = get_prop(t, "__cb");
    if (cb.type != V_FUNC) return UNDEF;
    int entry = new_object(O_PLAIN); if (entry < 0) return UNDEF;
    set_prop(entry, "target", a[0]); set_prop(entry, "isIntersecting", mk_bool(1)); set_prop(entry, "intersectionRatio", mk_int(1));
    int arr = new_array(); if (arr < 0) return UNDEF;
    array_push(arr, mk_obj(entry));
    /* bind: () => cb([entry], observer) via a native trampoline stored on the entry list */
    int call = new_object(O_PLAIN); if (call < 0) return UNDEF;
    set_prop(call, "__cb", cb); set_prop(call, "__arg", mk_obj(arr)); set_prop(call, "__obs", mk_obj(t));
    value_t targs[2]; targs[0] = get_prop(obj_window, "__ioFire"); targs[1] = mk_int(0);
    /* the trampoline reads the pending list */
    value_t pend = get_prop(obj_window, "__ioPending");
    if (pend.type != V_OBJ) { int p = new_array(); if (p < 0) return UNDEF; pend = mk_obj(p); set_prop(obj_window, "__ioPending", pend); }
    array_push(pend.u.obj, mk_obj(call));
    if (targs[0].type == V_FUNC) n_setTimeout(-1, targs, 2);
    return UNDEF;
}
static value_t n_io_fire(int t, value_t* a, int n) {
    (void)t; (void)a; (void)n;
    value_t pend = get_prop(obj_window, "__ioPending");
    if (pend.type != V_OBJ) return UNDEF;
    int arr = pend.u.obj;
    int cnt = objects[arr].len;
    for (int i = 0; i < cnt && !flow; i++) {
        value_t c = objects[arr].items[i];
        if (c.type != V_OBJ) continue;
        value_t args[2]; args[0] = get_prop(c.u.obj, "__arg"); args[1] = get_prop(c.u.obj, "__obs");
        value_t cb = get_prop(c.u.obj, "__cb");
        if (cb.type == V_FUNC) call_function(cb, args[1].type == V_OBJ ? args[1].u.obj : -1, args, 2);
    }
    /* drop the ones we ran (new ones may have been appended by the callbacks) */
    int left = objects[arr].len - cnt;
    for (int i = 0; i < left; i++) objects[arr].items[i] = objects[arr].items[cnt + i];
    objects[arr].len = left;
    return UNDEF;
}
static value_t n_IntersectionObserver(int t, value_t* a, int n) {
    (void)t;
    int o = new_object(O_PLAIN); if (o < 0) return UNDEF;
    if (n && a[0].type == V_FUNC) set_prop(o, "__cb", a[0]);
    def_native(o, "observe", n_io_observe);
    def_native(o, "unobserve", n_noop); def_native(o, "disconnect", n_noop); def_native(o, "takeRecords", n_noop);
    return mk_obj(o);
}
static value_t n_Observer(int t, value_t* a, int n) {
    (void)t; (void)a; (void)n;
    int o = new_object(O_PLAIN); if (o < 0) return UNDEF;
    def_native(o, "observe", n_noop); def_native(o, "unobserve", n_noop); def_native(o, "disconnect", n_noop); def_native(o, "takeRecords", n_noop);
    return mk_obj(o);
}
static value_t n_noop(int t, value_t* a, int n) { (void)t; (void)a; (void)n; return UNDEF; }
static value_t n_perf_now(int t, value_t* a, int n) { (void)t; (void)a; (void)n; return mk_int((int)(uptime_ticks * (1000 / TICKS_PER_SEC))); }

static value_t n_Object_keys(int t, value_t* a, int n) {
    (void)t; int r = new_array();
    if (n && (a[0].type == V_OBJ || a[0].type == V_FUNC)) {
        if (objects[a[0].u.obj].kind == O_ARRAY) { for (int i = 0; i < objects[a[0].u.obj].len; i++) { char b[16]; br_itoa(i, b); array_push(r, mk_str(js_strdup(b))); } }
        else for (int p = objects[a[0].u.obj].first_prop; p >= 0; p = props[p].next) array_push(r, mk_str(props[p].key));
    }
    return mk_obj(r);
}
static value_t n_Object_values(int t, value_t* a, int n) {
    (void)t; int r = new_array();
    if (n && a[0].type == V_OBJ) { if (objects[a[0].u.obj].kind == O_ARRAY) for (int i = 0; i < objects[a[0].u.obj].len; i++) array_push(r, objects[a[0].u.obj].items[i]); else for (int p = objects[a[0].u.obj].first_prop; p >= 0; p = props[p].next) array_push(r, props[p].val); }
    return mk_obj(r);
}
static value_t n_Object_entries(int t, value_t* a, int n) {
    (void)t; int r = new_array();
    if (n && a[0].type == V_OBJ) for (int p = objects[a[0].u.obj].first_prop; p >= 0; p = props[p].next) { int pair = new_array(); array_push(pair, mk_str(props[p].key)); array_push(pair, props[p].val); array_push(r, mk_obj(pair)); }
    return mk_obj(r);
}
static value_t n_Object_assign(int t, value_t* a, int n) {
    (void)t; if (!n || a[0].type != V_OBJ) return UNDEF;
    for (int k = 1; k < n; k++) if (a[k].type == V_OBJ) for (int p = objects[a[k].u.obj].first_prop; p >= 0; p = props[p].next) set_prop(a[0].u.obj, props[p].key, props[p].val);
    return a[0];
}
static value_t n_Object_create(int t, value_t* a, int n) { (void)t; int o = new_object(O_PLAIN); if (o >= 0 && n && a[0].type == V_OBJ) objects[o].proto = a[0].u.obj; return mk_obj(o); }
static value_t n_Object_freeze(int t, value_t* a, int n) { (void)t; return n ? a[0] : UNDEF; }
static value_t n_Array_isArray(int t, value_t* a, int n) { (void)t; return mk_bool(n && a[0].type == V_OBJ && objects[a[0].u.obj].kind == O_ARRAY); }
static value_t n_Array_from(int t, value_t* a, int n) {
    (void)t; int r = new_array();
    if (n && a[0].type == V_OBJ) {
        int src = a[0].u.obj;
        if (objects[src].kind == O_ARRAY) for (int i = 0; i < objects[src].len; i++) array_push(r, objects[src].items[i]);
        else { value_t len = get_prop(src, "length"); int l = (int)(to_num(len) >> FX_SHIFT); for (int i = 0; i < l && i < 4096; i++) array_push(r, UNDEF); }
    } else if (n && a[0].type == V_STR) for (int i = 0; a[0].u.s[i]; i++) array_push(r, mk_str(js_strdup_n(&a[0].u.s[i], 1)));
    if (n > 1 && a[1].type == V_FUNC) for (int i = 0; i < objects[r].len; i++) { value_t cb[2]; cb[0] = objects[r].items[i]; cb[1] = mk_int(i); objects[r].items[i] = call_function(a[1], -1, cb, 2); if (flow) break; }
    return mk_obj(r);
}
static value_t n_JSON_stringify(int t, value_t* a, int n) { (void)t; if (!n) return UNDEF; static char buf[8192]; int o = 0; json_out(a[0], buf, sizeof(buf), &o, 0); buf[o] = 0; return mk_str(js_strdup_n(buf, o)); }
static value_t n_JSON_parse(int t, value_t* a, int n) { (void)t; if (!n) return UNDEF; const char* p = to_string(a[0]); return json_parse_value(&p); }

/* Math with fixed point */
static uint32_t rng_state = 0x12345678;
static value_t n_Math_random(int t, value_t* a, int n) { (void)t; (void)a; (void)n; rng_state = rng_state * 1664525u + 1013904223u + uptime_ticks; return mk_num((fx_t)(rng_state >> 16)); }
static value_t n_Math_floor(int t, value_t* a, int n) { (void)t; if (!n) return mk_num(NAN_FX); fx_t v = to_num(a[0]); if (is_nan(v)) return mk_num(v); return mk_num(v & ~(FX_ONE - 1)); }
static value_t n_Math_ceil(int t, value_t* a, int n) { (void)t; if (!n) return mk_num(NAN_FX); fx_t v = to_num(a[0]); if (is_nan(v)) return mk_num(v); return mk_num((v + FX_ONE - 1) & ~(FX_ONE - 1)); }
static value_t n_Math_round(int t, value_t* a, int n) { (void)t; if (!n) return mk_num(NAN_FX); fx_t v = to_num(a[0]); if (is_nan(v)) return mk_num(v); return mk_num((v + FX_ONE / 2) & ~(FX_ONE - 1)); }
static value_t n_Math_trunc(int t, value_t* a, int n) { (void)t; if (!n) return mk_num(NAN_FX); fx_t v = to_num(a[0]); return mk_num(v < 0 ? -((-v) & ~(FX_ONE - 1)) : (v & ~(FX_ONE - 1))); }
static value_t n_Math_abs(int t, value_t* a, int n) { (void)t; if (!n) return mk_num(NAN_FX); fx_t v = to_num(a[0]); return mk_num(v < 0 ? -v : v); }
static value_t n_Math_sign(int t, value_t* a, int n) { (void)t; if (!n) return mk_num(NAN_FX); fx_t v = to_num(a[0]); return mk_int(v > 0 ? 1 : v < 0 ? -1 : 0); }
static value_t n_Math_max(int t, value_t* a, int n) { (void)t; fx_t m = -(fx_t)0x7FFFFFFFFFFFLL; if (!n) return mk_num(m); for (int i = 0; i < n; i++) { fx_t v = to_num(a[i]); if (is_nan(v)) return mk_num(v); if (v > m) m = v; } return mk_num(m); }
static value_t n_Math_min(int t, value_t* a, int n) { (void)t; fx_t m = (fx_t)0x7FFFFFFFFFFELL; if (!n) return mk_num(m); for (int i = 0; i < n; i++) { fx_t v = to_num(a[i]); if (is_nan(v)) return mk_num(v); if (v < m) m = v; } return mk_num(m); }
static value_t n_Math_pow(int t, value_t* a, int n) { (void)t; if (n < 2) return mk_num(NAN_FX); return binary_op("**", a[0], a[1]); }
static value_t n_Math_sqrt(int t, value_t* a, int n) {
    (void)t; if (!n) return mk_num(NAN_FX);
    fx_t v = to_num(a[0]); if (v < 0 || is_nan(v)) return mk_num(NAN_FX);
    /* integer sqrt on v << 16 gives sqrt in fixed point */
    uint64_t x = (uint64_t)v << FX_SHIFT, r = 0, bit = (uint64_t)1 << 62;
    while (bit > x) bit >>= 2;
    while (bit) { if (x >= r + bit) { x -= r + bit; r = (r >> 1) + bit; } else r >>= 1; bit >>= 2; }
    return mk_num((fx_t)r);
}
static value_t n_Math_hypot(int t, value_t* a, int n) { (void)t; fx_t s = 0; for (int i = 0; i < n; i++) { fx_t v = to_num(a[i]); s += (v * v) >> FX_SHIFT; } value_t arg = mk_num(s); return n_Math_sqrt(0, &arg, 1); }
/* sin/cos via a 256-entry quarter table built at init (integer only) */
static fx_t sin_table[257];
static void build_sin_table(void) {
    /* Bhaskara-free approach: Taylor in fixed point for angles 0..pi/2 */
    for (int i = 0; i <= 256; i++) {
        /* angle = i/256 * pi/2 in 16.16 -> use polynomial sin(x) ~ x - x^3/6 + x^5/120 - x^7/5040 */
        int64_t x = ((int64_t)i * 102944) / 256;         /* pi/2 in 16.16 = 102944 */
        int64_t x2 = (x * x) >> FX_SHIFT;
        int64_t x3 = (x2 * x) >> FX_SHIFT;
        int64_t x5 = (x3 * x2) >> FX_SHIFT;
        int64_t x7 = (x5 * x2) >> FX_SHIFT;
        sin_table[i] = (fx_t)(x - x3 / 6 + x5 / 120 - x7 / 5040);
        if (sin_table[i] > FX_ONE) sin_table[i] = FX_ONE;
    }
}
static fx_t fx_sin(fx_t ang) {
    const int64_t TWO_PI = 411775;       /* 2*pi in 16.16 */
    int64_t a = ang % TWO_PI; if (a < 0) a += TWO_PI;
    int quadrant = (int)(a / 102944);
    int64_t rem = a % 102944;
    int idx = (int)((rem * 256) / 102944);
    if (idx > 256) idx = 256;
    fx_t s;
    switch (quadrant & 3) {
    case 0: s = sin_table[idx]; break;
    case 1: s = sin_table[256 - idx]; break;
    case 2: s = -sin_table[idx]; break;
    default: s = -sin_table[256 - idx]; break;
    }
    return s;
}
static value_t n_Math_sin(int t, value_t* a, int n) { (void)t; if (!n) return mk_num(NAN_FX); return mk_num(fx_sin(to_num(a[0]))); }
static value_t n_Math_cos(int t, value_t* a, int n) { (void)t; if (!n) return mk_num(NAN_FX); return mk_num(fx_sin(to_num(a[0]) + 102944)); }
static value_t n_Math_atan2(int t, value_t* a, int n) {
    (void)t; if (n < 2) return mk_num(NAN_FX);
    fx_t y = to_num(a[0]), x = to_num(a[1]);
    /* approximation: atan(z) ~ z*(pi/4) - z*(|z|-1)*(0.2447+0.0663|z|) */
    fx_t ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    fx_t z = (ay > ax) ? ((ax << FX_SHIFT) / (ay ? ay : 1)) : ((ay << FX_SHIFT) / (ax ? ax : 1));
    fx_t az = z;
    fx_t r = (z * 51472) >> FX_SHIFT;                  /* z * pi/4 */
    fx_t corr = (((z * (az - FX_ONE)) >> FX_SHIFT) * (16036 + ((4345 * az) >> FX_SHIFT))) >> FX_SHIFT;
    r -= corr;
    if (ay > ax) r = 102944 - r;
    if (x < 0) r = 205887 - r;
    if (y < 0) r = -r;
    return mk_num(r);
}
static value_t n_Math_log(int t, value_t* a, int n) { (void)t; if (!n) return mk_num(NAN_FX); fx_t v = to_num(a[0]); if (v <= 0) return mk_num(NAN_FX); /* ln via log2 */ int e = 0; fx_t m = v; while (m >= 2 * FX_ONE) { m >>= 1; e++; } while (m < FX_ONE) { m <<= 1; e--; } fx_t f = m - FX_ONE; fx_t ln1p = f - ((f * f) >> 17) + ((((f * f) >> FX_SHIFT) * f) / 3 >> FX_SHIFT); return mk_num(ln1p + (fx_t)e * 45426); }
static value_t n_Math_exp(int t, value_t* a, int n) { (void)t; if (!n) return mk_num(NAN_FX); fx_t x = to_num(a[0]); fx_t r = FX_ONE, term = FX_ONE; for (int i = 1; i < 20; i++) { term = ((term * x) >> FX_SHIFT) / i; r += term; } return mk_num(r); }

/* timers */
static value_t n_setTimeout(int t, value_t* a, int n) {
    (void)t;
    if (!n || a[0].type != V_FUNC) return mk_int(0);
    int ms = n > 1 ? (int)(to_num(a[1]) >> FX_SHIFT) : 0;
    for (int i = 0; i < JS_MAX_TIMERS; i++) if (!timers[i].active) {
        timers[i].active = 1; timers[i].fn = a[0].u.obj; timers[i].due = (int)uptime_ticks + ms; timers[i].interval = 0; timers[i].id = timer_next_id++;
        return mk_int(timers[i].id);
    }
    return mk_int(0);
}
static value_t n_setInterval(int t, value_t* a, int n) {
    (void)t;
    if (!n || a[0].type != V_FUNC) return mk_int(0);
    int ms = n > 1 ? (int)(to_num(a[1]) >> FX_SHIFT) : 0;
    if (ms < 16) ms = 16;
    for (int i = 0; i < JS_MAX_TIMERS; i++) if (!timers[i].active) {
        timers[i].active = 1; timers[i].fn = a[0].u.obj; timers[i].due = (int)uptime_ticks + ms; timers[i].interval = ms; timers[i].id = timer_next_id++;
        return mk_int(timers[i].id);
    }
    return mk_int(0);
}
static value_t n_clearTimeout(int t, value_t* a, int n) { (void)t; if (!n) return UNDEF; int id = (int)(to_num(a[0]) >> FX_SHIFT); for (int i = 0; i < JS_MAX_TIMERS; i++) if (timers[i].active && timers[i].id == id) timers[i].active = 0; return UNDEF; }
static value_t n_requestAnimationFrame(int t, value_t* a, int n) { value_t args[2]; args[0] = n ? a[0] : UNDEF; args[1] = mk_int(16); return n_setTimeout(t, args, 2); }

/* ------------------------------------------------------------- DOM */

static int wrap_node(br_node_t* n) {
    if (!n) return -1;
    if (n->js_obj >= 0 && n->js_obj < obj_count && objects[n->js_obj].kind == O_DOM && objects[n->js_obj].dom_id == n->id) return n->js_obj;
    int o = new_object(O_DOM);
    if (o < 0) return -1;
    objects[o].dom_id = n->id;
    objects[o].proto = proto_element;
    n->js_obj = o;
    return o;
}

static br_node_t* unwrap(int o) {
    if (o < 0 || o >= obj_count || objects[o].kind != O_DOM) return NULL;
    return br_dom_node(objects[o].dom_id);
}

static value_t node_or_null(br_node_t* n) { if (!n) return mk_null(); int o = wrap_node(n); return o >= 0 ? mk_obj(o) : mk_null(); }

static br_node_t* prev_sibling(br_node_t* n) {
    if (!n || !n->parent) return NULL;
    br_node_t* c = n->parent->first_child;
    br_node_t* prev = NULL;
    while (c && c != n) { prev = c; c = c->next; }
    return prev;
}

static void collect_matching(br_node_t* n, const char* sel, int arr, int only_first, br_node_t** first) {
    if (br_stack_headroom() < BR_STACK_MIN) return;
    if (!n || (*first && only_first)) return;
    if (n->type == BR_NODE_ELEMENT && br_css_match_selector_string(n, sel)) {
        if (only_first) { *first = n; return; }
        array_push(arr, node_or_null(n));
    }
    for (br_node_t* c = n->first_child; c; c = c->next) collect_matching(c, sel, arr, only_first, first);
}

static void collect_by_tag(br_node_t* n, const char* tag, int arr) {
    if (br_stack_headroom() < BR_STACK_MIN) return;
    if (!n) return;
    if (n->type == BR_NODE_ELEMENT && (br_streq(tag, "*") || br_strieq(n->tag, tag))) array_push(arr, node_or_null(n));
    for (br_node_t* c = n->first_child; c; c = c->next) collect_by_tag(c, tag, arr);
}

static void collect_by_class(br_node_t* n, const char* cls, int arr) {
    if (br_stack_headroom() < BR_STACK_MIN) return;
    if (!n) return;
    if (n->type == BR_NODE_ELEMENT && br_has_class(n, cls)) array_push(arr, node_or_null(n));
    for (br_node_t* c = n->first_child; c; c = c->next) collect_by_class(c, cls, arr);
}

static const char* style_prop_to_css(const char* camel) {
    /* backgroundColor -> background-color */
    static char buf[48];
    int o = 0;
    for (int i = 0; camel[i] && o < 46; i++) {
        char c = camel[i];
        if (c >= 'A' && c <= 'Z') { buf[o++] = '-'; buf[o++] = (char)(c + 32); }
        else buf[o++] = c;
    }
    buf[o] = 0;
    if (br_streq(buf, "css-text")) return "cssText";
    return buf;
}

/* element.style proxy: a plain object whose sets are mirrored into the
 * style attribute. We keep it simple: style object props are written back
 * into the style attribute string on each set. */
static value_t make_style_proxy(br_node_t* n) {
    int o = new_object(O_DOM);
    if (o < 0) return UNDEF;
    objects[o].dom_id = n->id;
    objects[o].name = "style";
    /* populate from the existing inline style */
    const char* inl = br_attr(n, "style");
    if (inl) {
        const char* p = inl;
        while (*p) {
            while (*p == ' ' || *p == ';') p++;
            const char* ns = p; while (*p && *p != ':' && *p != ';') p++;
            if (*p != ':') break;
            const char* name = js_strdup_n(ns, (int)(p - ns)); p++;
            while (*p == ' ') p++;
            const char* vs = p; while (*p && *p != ';') p++;
            const char* val = js_strdup_n(vs, (int)(p - vs));
            /* camelCase the name */
            char cam[48]; int o2 = 0;
            for (int i = 0; name[i] && o2 < 46; i++) { if (name[i] == '-' && name[i + 1]) { i++; cam[o2++] = (char)((name[i] >= 'a' && name[i] <= 'z') ? name[i] - 32 : name[i]); } else cam[o2++] = name[i]; }
            cam[o2] = 0;
            set_prop(o, js_strdup(cam), mk_str(val));
        }
    }
    return mk_obj(o);
}

static void style_proxy_set(int o, const char* key, value_t v) {
    br_node_t* n = br_dom_node(objects[o].dom_id);
    if (!n) return;
    if (br_streq(key, "cssText")) {
        br_set_attr(n, "style", to_string(v));
        objects[o].first_prop = -1; objects[o].last_prop = -1; objects[o].prop_count = 0;
        br_js_dom_changed();
        return;
    }
    set_prop(o, js_strdup(key), mk_str(js_strdup(to_string(v))));
    /* rebuild the style attribute from all props */
    static char css[1024]; int c = 0;
    for (int p = objects[o].first_prop; p >= 0; p = props[p].next) {
        if (props[p].val.type != V_STR || !props[p].val.u.s[0]) continue;
        const char* nm = style_prop_to_css(props[p].key);
        for (int i = 0; nm[i] && c < 1000; i++) css[c++] = nm[i];
        css[c++] = ':';
        for (int i = 0; props[p].val.u.s[i] && c < 1000; i++) css[c++] = props[p].val.u.s[i];
        css[c++] = ';';
    }
    css[c] = 0;
    br_set_attr(n, "style", css);
    br_js_dom_changed();
}

static value_t make_classlist(br_node_t* n) {
    int o = new_object(O_DOM);
    if (o < 0) return UNDEF;
    objects[o].dom_id = n->id;
    objects[o].name = "classList";
    return mk_obj(o);
}

static value_t make_dataset(br_node_t* n) {
    int o = new_object(O_PLAIN);
    if (o < 0) return UNDEF;
    for (int i = 0; i < n->attr_count; i++) {
        if (br_streq_prefix(n->attrs[i].name, "data-")) {
            char cam[48]; int o2 = 0; const char* nm = n->attrs[i].name + 5;
            for (int k = 0; nm[k] && o2 < 46; k++) { if (nm[k] == '-' && nm[k + 1]) { k++; cam[o2++] = (char)((nm[k] >= 'a' && nm[k] <= 'z') ? nm[k] - 32 : nm[k]); } else cam[o2++] = nm[k]; }
            cam[o2] = 0;
            set_prop(o, js_strdup(cam), mk_str(n->attrs[i].value));
        }
    }
    return mk_obj(o);
}

static value_t children_array(br_node_t* n, int elements_only) {
    int a = new_array();
    if (a < 0) return UNDEF;
    for (br_node_t* c = n->first_child; c; c = c->next) {
        if (elements_only && c->type != BR_NODE_ELEMENT) continue;
        array_push(a, node_or_null(c));
    }
    return mk_obj(a);
}

static value_t dom_get(int o, const char* key, int* handled) {
    *handled = 1;
    br_node_t* n = br_dom_node(objects[o].dom_id);
    if (!n) { *handled = 0; return UNDEF; }
    if (objects[o].name && br_streq(objects[o].name, "style")) {
        prop_t* p = find_own(o, key);
        if (p) return p->val;
        if (br_streq(key, "cssText")) { const char* s = br_attr(n, "style"); return mk_str(s ? s : ""); }
        return mk_str("");
    }
    if (objects[o].name && br_streq(objects[o].name, "classList")) {
        if (br_streq(key, "length")) { const char* c = br_attr(n, "class"); int cnt = 0; if (c) { int in = 0; for (int i = 0; c[i]; i++) { if (c[i] != ' ' && !in) { cnt++; in = 1; } else if (c[i] == ' ') in = 0; } } return mk_int(cnt); }
        *handled = 0; return UNDEF;
    }
    /* own JS props first (event handlers, expando) */
    prop_t* own = find_own(o, key);
    if (own) return own->val;

    if (n->type == BR_NODE_DOCUMENT) {
        if (br_streq(key, "body")) return node_or_null(n->body);
        if (br_streq(key, "head")) return node_or_null(n->head);
        if (br_streq(key, "documentElement")) return node_or_null(n->first_child);
        if (br_streq(key, "title")) return mk_str(brs.title);
        if (br_streq(key, "URL") || br_streq(key, "documentURI")) return mk_str(js_strdup(brs.url));
        if (br_streq(key, "readyState")) return mk_str("complete");
        if (br_streq(key, "cookie") || br_streq(key, "referrer")) return mk_str("");
        if (br_streq(key, "location")) return get_prop(obj_window, "location");
        if (br_streq(key, "defaultView")) return mk_obj(obj_window);
        if (br_streq(key, "activeElement")) return node_or_null(brs.focus_input >= 0 ? br_dom_node(brs.focus_input) : n->body);
        if (br_streq(key, "forms")) { int a = new_array(); collect_by_tag(n, "form", a); return mk_obj(a); }
        if (br_streq(key, "images")) { int a = new_array(); collect_by_tag(n, "img", a); return mk_obj(a); }
        if (br_streq(key, "links")) { int a = new_array(); collect_by_tag(n, "a", a); return mk_obj(a); }
        if (br_streq(key, "hidden")) return mk_bool(0);
        if (br_streq(key, "nodeType")) return mk_int(9);
        if (br_streq(key, "childNodes") || br_streq(key, "children")) return children_array(n, br_streq(key, "children"));
    }
    if (br_streq(key, "innerHTML")) { static char buf[16384]; br_node_inner_html(n, buf, sizeof(buf)); return mk_str(js_strdup(buf)); }
    if (br_streq(key, "outerHTML")) { static char buf[16384]; buf[0] = 0; br_node_inner_html(n, buf, sizeof(buf)); return mk_str(js_strdup(buf)); }
    if (br_streq(key, "textContent") || br_streq(key, "innerText") || br_streq(key, "text") || br_streq(key, "nodeValue") || br_streq(key, "data") || br_streq(key, "wholeText")) {
        if (n->type == BR_NODE_TEXT) return mk_str(n->text);
        static char buf[8192]; br_node_text_content(n, buf, sizeof(buf)); return mk_str(js_strdup(buf));
    }
    if (br_streq(key, "tagName") || br_streq(key, "nodeName")) {
        if (n->type == BR_NODE_TEXT) return mk_str("#text");
        char up[32]; int i = 0; for (; n->tag[i] && i < 31; i++) up[i] = (char)((n->tag[i] >= 'a' && n->tag[i] <= 'z') ? n->tag[i] - 32 : n->tag[i]); up[i] = 0;
        return mk_str(js_strdup(up));
    }
    if (br_streq(key, "localName")) return mk_str(n->tag);
    if (br_streq(key, "nodeType")) return mk_int(n->type == BR_NODE_TEXT ? 3 : 1);
    if (br_streq(key, "id")) { const char* v = br_attr(n, "id"); return mk_str(v ? v : ""); }
    if (br_streq(key, "className")) { const char* v = br_attr(n, "class"); return mk_str(v ? v : ""); }
    if (br_streq(key, "classList")) return make_classlist(n);
    if (br_streq(key, "style")) return make_style_proxy(n);
    if (br_streq(key, "dataset")) return make_dataset(n);
    if (br_streq(key, "parentNode") || br_streq(key, "parentElement")) return node_or_null(n->parent && n->parent->type != BR_NODE_DOCUMENT ? n->parent : (n->parent ? n->parent : NULL));
    if (br_streq(key, "children")) return children_array(n, 1);
    if (br_streq(key, "childNodes")) return children_array(n, 0);
    if (br_streq(key, "firstChild")) return node_or_null(n->first_child);
    if (br_streq(key, "lastChild")) return node_or_null(n->last_child);
    if (br_streq(key, "firstElementChild")) { br_node_t* c = n->first_child; while (c && c->type != BR_NODE_ELEMENT) c = c->next; return node_or_null(c); }
    if (br_streq(key, "lastElementChild")) { br_node_t* c = n->first_child; br_node_t* last = NULL; while (c) { if (c->type == BR_NODE_ELEMENT) last = c; c = c->next; } return node_or_null(last); }
    if (br_streq(key, "nextSibling")) return node_or_null(n->next);
    if (br_streq(key, "nextElementSibling")) { br_node_t* c = n->next; while (c && c->type != BR_NODE_ELEMENT) c = c->next; return node_or_null(c); }
    if (br_streq(key, "previousSibling")) return node_or_null(prev_sibling(n));
    if (br_streq(key, "previousElementSibling")) { br_node_t* c = prev_sibling(n); while (c && c->type != BR_NODE_ELEMENT) c = prev_sibling(c); return node_or_null(c); }
    if (br_streq(key, "childElementCount")) { int c = 0; for (br_node_t* k = n->first_child; k; k = k->next) if (k->type == BR_NODE_ELEMENT) c++; return mk_int(c); }
    if (br_streq(key, "value")) {
        if (n->type == BR_NODE_ELEMENT && (br_streq(n->tag, "input") || br_streq(n->tag, "textarea") || br_streq(n->tag, "select"))) {
            if (br_streq(n->tag, "textarea") && !n->value) { static char buf[2048]; br_node_text_content(n, buf, sizeof(buf)); return mk_str(js_strdup(buf)); }
            if (br_streq(n->tag, "select")) { for (br_node_t* c = n->first_child; c; c = c->next) if (c->type == BR_NODE_ELEMENT && br_streq(c->tag, "option") && br_attr(c, "selected")) { const char* v = br_attr(c, "value"); if (v) return mk_str(v); static char b[256]; br_node_text_content(c, b, sizeof(b)); return mk_str(js_strdup(b)); } br_node_t* c = n->first_child; while (c && !(c->type == BR_NODE_ELEMENT && br_streq(c->tag, "option"))) c = c->next; if (c) { const char* v = br_attr(c, "value"); if (v) return mk_str(v); static char b[256]; br_node_text_content(c, b, sizeof(b)); return mk_str(js_strdup(b)); } return mk_str(""); }
            if (!n->value) { const char* v = br_attr(n, "value"); n->value = br_strdup(v ? v : ""); }
            return mk_str(n->value);
        }
        const char* v = br_attr(n, "value"); return mk_str(v ? v : "");
    }
    if (br_streq(key, "checked")) return mk_bool(n->checked || (br_attr(n, "checked") != NULL && !n->value));
    if (br_streq(key, "disabled")) return mk_bool(br_attr(n, "disabled") != NULL);
    if (br_streq(key, "hidden")) return mk_bool(br_attr(n, "hidden") != NULL);
    if (br_streq(key, "href") || br_streq(key, "src") || br_streq(key, "action")) { const char* v = br_attr(n, key); if (!v) return mk_str(""); char abs[BR_URL_MAX]; br_resolve_url(brs.url, v, abs, sizeof(abs)); return mk_str(js_strdup(abs)); }
    if (br_streq(key, "type") || br_streq(key, "name") || br_streq(key, "title") || br_streq(key, "alt") || br_streq(key, "placeholder") || br_streq(key, "lang") || br_streq(key, "target") || br_streq(key, "rel") || br_streq(key, "htmlFor") || br_streq(key, "method")) {
        const char* v = br_attr(n, br_streq(key, "htmlFor") ? "for" : key); return mk_str(v ? v : (br_streq(key, "type") && br_streq(n->tag, "input") ? "text" : ""));
    }
    if (br_streq(key, "offsetWidth") || br_streq(key, "clientWidth") || br_streq(key, "scrollWidth")) return mk_int(n->lw);
    if (br_streq(key, "offsetHeight") || br_streq(key, "clientHeight") || br_streq(key, "scrollHeight")) return mk_int(n->lh);
    if (br_streq(key, "offsetTop") || br_streq(key, "scrollTop")) return mk_int(br_streq(key, "scrollTop") ? 0 : n->ly);
    if (br_streq(key, "offsetLeft") || br_streq(key, "scrollLeft")) return mk_int(br_streq(key, "scrollLeft") ? 0 : n->lx);
    if (br_streq(key, "ownerDocument")) return mk_obj(obj_document);
    if (br_streq(key, "isConnected")) return mk_bool(1);
    if (br_streq(key, "options")) { int a = new_array(); for (br_node_t* c = n->first_child; c; c = c->next) if (c->type == BR_NODE_ELEMENT && br_streq(c->tag, "option")) array_push(a, node_or_null(c)); return mk_obj(a); }
    if (br_streq(key, "selectedIndex")) { int i = 0; for (br_node_t* c = n->first_child; c; c = c->next) if (c->type == BR_NODE_ELEMENT && br_streq(c->tag, "option")) { if (br_attr(c, "selected")) return mk_int(i); i++; } return mk_int(0); }
    if (br_streq(key, "attributes")) { int a = new_array(); for (int i = 0; i < n->attr_count; i++) { int p = new_object(O_PLAIN); set_prop(p, "name", mk_str(n->attrs[i].name)); set_prop(p, "value", mk_str(n->attrs[i].value)); array_push(a, mk_obj(p)); } return mk_obj(a); }
    if (br_streq(key, "length") && n->type == BR_NODE_ELEMENT && br_streq(n->tag, "form")) { int c = 0; for (br_node_t* k = n->first_child; k; k = k->next) c++; return mk_int(c); }
    /* on* handlers default to null */
    if (key[0] == 'o' && key[1] == 'n') return mk_null();
    /* attribute fallback (e.g. el.title already handled; custom props) */
    if (n->type == BR_NODE_ELEMENT) { const char* v = br_attr(n, key); if (v) return mk_str(v); }
    *handled = 0;
    return UNDEF;
}

static int dom_set(int o, const char* key, value_t v) {
    br_node_t* n = br_dom_node(objects[o].dom_id);
    if (!n) return 0;
    if (objects[o].name && br_streq(objects[o].name, "style")) { style_proxy_set(o, key, v); return 1; }
    if (objects[o].name && br_streq(objects[o].name, "classList")) return 1;
    if (n->type == BR_NODE_DOCUMENT) {
        if (br_streq(key, "title")) { br_strlcpy(brs.title, to_string(v), sizeof(brs.title)); br_request_repaint(); return 1; }
        if (br_streq(key, "cookie")) return 1;
        return 0;   /* store as expando (onload etc.) */
    }
    if (br_streq(key, "innerHTML")) { const char* s = to_string(v); br_html_parse_fragment(n, s, (int)strlen(s)); br_js_dom_changed(); return 1; }
    if (br_streq(key, "outerHTML")) { const char* s = to_string(v); if (n->parent) { br_html_parse_fragment(n, s, (int)strlen(s)); } br_js_dom_changed(); return 1; }
    if (br_streq(key, "textContent") || br_streq(key, "innerText") || br_streq(key, "nodeValue") || br_streq(key, "data")) {
        const char* s = to_string(v);
        if (n->type == BR_NODE_TEXT) { n->text = br_strdup(s); br_js_dom_changed(); return 1; }
        br_node_remove_children(n);
        if (s[0]) { br_node_t* t = br_node_new(BR_NODE_TEXT); if (t) { t->text = br_strdup(s); br_node_append(n, t); } }
        br_js_dom_changed();
        return 1;
    }
    if (br_streq(key, "id") || br_streq(key, "title") || br_streq(key, "href") || br_streq(key, "src") || br_streq(key, "alt") || br_streq(key, "type") || br_streq(key, "name") || br_streq(key, "placeholder") || br_streq(key, "target") || br_streq(key, "lang") || br_streq(key, "width") || br_streq(key, "height") || br_streq(key, "action") || br_streq(key, "method")) {
        br_set_attr(n, key, to_string(v)); br_js_dom_changed(); return 1;
    }
    if (br_streq(key, "htmlFor")) { br_set_attr(n, "for", to_string(v)); return 1; }
    if (br_streq(key, "className")) { br_set_attr(n, "class", to_string(v)); br_js_dom_changed(); return 1; }
    if (br_streq(key, "value")) { n->value = br_strdup(to_string(v)); if (br_streq(n->tag, "select")) { for (br_node_t* c = n->first_child; c; c = c->next) if (c->type == BR_NODE_ELEMENT && br_streq(c->tag, "option")) { const char* ov = br_attr(c, "value"); static char ob[256]; if (!ov) { br_node_text_content(c, ob, sizeof(ob)); ov = ob; } if (br_streq(ov, n->value)) br_set_attr(c, "selected", "selected"); else { for (int i = 0; i < c->attr_count; i++) if (br_streq(c->attrs[i].name, "selected")) { c->attrs[i] = c->attrs[c->attr_count - 1]; c->attr_count--; break; } } } } br_request_repaint(); return 1; }
    if (br_streq(key, "checked")) { n->checked = truthy(v); n->value = br_strdup("x"); br_request_repaint(); return 1; }
    if (br_streq(key, "disabled")) { if (truthy(v)) br_set_attr(n, "disabled", ""); else { for (int i = 0; i < n->attr_count; i++) if (br_streq(n->attrs[i].name, "disabled")) { n->attrs[i] = n->attrs[n->attr_count - 1]; n->attr_count--; break; } } br_js_dom_changed(); return 1; }
    if (br_streq(key, "hidden")) { if (truthy(v)) br_set_attr(n, "hidden", ""); else { for (int i = 0; i < n->attr_count; i++) if (br_streq(n->attrs[i].name, "hidden")) { n->attrs[i] = n->attrs[n->attr_count - 1]; n->attr_count--; break; } } br_js_dom_changed(); return 1; }
    if (br_streq(key, "selectedIndex")) { int want = (int)(to_num(v) >> FX_SHIFT), i = 0; for (br_node_t* c = n->first_child; c; c = c->next) if (c->type == BR_NODE_ELEMENT && br_streq(c->tag, "option")) { if (i == want) br_set_attr(c, "selected", "selected"); else { for (int k = 0; k < c->attr_count; k++) if (br_streq(c->attrs[k].name, "selected")) { c->attrs[k] = c->attrs[c->attr_count - 1]; c->attr_count--; break; } } i++; } br_request_repaint(); return 1; }
    if (br_streq(key, "scrollTop") || br_streq(key, "scrollLeft")) return 1;
    return 0;   /* expando / on* handler stored as a normal prop */
}

static void class_toggle(br_node_t* n, const char* cls, int mode) {
    /* mode: 0 remove, 1 add, 2 toggle */
    const char* cur_cls = br_attr(n, "class");
    char out[512]; int o = 0; int had = 0;
    if (cur_cls) {
        const char* p = cur_cls;
        while (*p) {
            while (*p == ' ') p++;
            const char* st = p; while (*p && *p != ' ') p++;
            int l = (int)(p - st);
            if (l == 0) break;
            if (l == (int)strlen(cls) && br_streq_n(st, l, cls)) { had = 1; if (mode == 0 || mode == 2) continue; }
            if (o) out[o++] = ' ';
            for (int i = 0; i < l && o < 500; i++) out[o++] = st[i];
        }
    }
    if ((mode == 1 || (mode == 2 && !had)) && !had) { if (o) out[o++] = ' '; for (int i = 0; cls[i] && o < 500; i++) out[o++] = cls[i]; }
    out[o] = 0;
    br_set_attr(n, "class", out);
    br_js_dom_changed();
}

static value_t make_event(const char* type, br_node_t* target) {
    int e = new_object(O_PLAIN);
    if (e < 0) return UNDEF;
    set_prop(e, "type", mk_str(type));
    set_prop(e, "target", node_or_null(target));
    set_prop(e, "currentTarget", node_or_null(target));
    set_prop(e, "defaultPrevented", mk_bool(0));
    set_prop(e, "bubbles", mk_bool(1));
    set_prop(e, "key", mk_str(""));
    set_prop(e, "keyCode", mk_int(0));
    set_prop(e, "clientX", mk_int(0));
    set_prop(e, "clientY", mk_int(0));
    def_native(e, "preventDefault", n_noop);
    def_native(e, "stopPropagation", n_noop);
    def_native(e, "stopImmediatePropagation", n_noop);
    return mk_obj(e);
}

static int dispatch_event_on(int o, const char* type, value_t ev, int* prevented);

static void call_handler(value_t fn, int this_obj, value_t ev) {
    if (fn.type != V_FUNC) return;
    call_function(fn, this_obj, &ev, 1);
    if (flow == F_THROW) { br_js_console(concat("Uncaught ", js_error_msg)); flow = F_NONE; }
    flow = F_NONE;
}

static int dispatch_event_on(int o, const char* type, value_t ev, int* prevented) {
    int handled = 0;
    /* on<type> property */
    char on[32]; on[0] = 'o'; on[1] = 'n'; int i = 0; for (; type[i] && i < 28; i++) on[2 + i] = type[i]; on[2 + i] = 0;
    prop_t* p = find_own(o, on);
    if (p && p->val.type == V_FUNC) { call_handler(p->val, o, ev); handled = 1; }
    /* listeners array: "__listeners_<type>" */
    char lk[48]; const char* pre = "__listeners_"; int k = 0; for (; pre[k]; k++) lk[k] = pre[k]; for (i = 0; type[i] && k < 46; i++) lk[k++] = type[i]; lk[k] = 0;
    prop_t* lp = find_own(o, lk);
    if (lp && lp->val.type == V_OBJ) {
        int arr = lp->val.u.obj;
        for (int j = 0; j < objects[arr].len; j++) { call_handler(objects[arr].items[j], o, ev); handled = 1; }
    }
    if (ev.type == V_OBJ) { value_t dp = get_prop(ev.u.obj, "defaultPrevented"); if (truthy(dp)) *prevented = 1; }
    return handled;
}

/* preventDefault sets defaultPrevented: implement via a native bound to the event */
static value_t n_preventDefault(int t, value_t* a, int n) { (void)a; (void)n; if (t >= 0) set_prop(t, "defaultPrevented", mk_bool(1)); return UNDEF; }

static int dispatch_bubbling(br_node_t* target, const char* type, const char* onattr) {
    value_t ev = make_event(type, target);
    if (ev.type == V_OBJ) def_native(ev.u.obj, "preventDefault", n_preventDefault);
    int prevented = 0;
    for (br_node_t* n = target; n; n = n->parent) {
        int o = wrap_node(n);
        if (o < 0) break;
        if (ev.type == V_OBJ) set_prop(ev.u.obj, "currentTarget", mk_obj(o));
        /* inline attribute handler, e.g. onclick="..." */
        if (onattr && n->type == BR_NODE_ELEMENT) {
            const char* code = br_attr(n, onattr);
            if (code && !find_own(o, onattr)) {
                /* compile once into a function object: function(event){ code } */
                const char* src = concat("(function(event){", concat(code, "\n})"));
                int save_pos = pos;
                int save_tok = tok_count;
                if (tokenize(src, (int)strlen(src))) {
                    pos = 0; js_error = 0;
                    int fn_node = parse_assign();
                    if (!js_error) { value_t f = eval(fn_node, global_scope); if (f.type == V_FUNC) set_prop(o, js_strdup(onattr), f); }
                }
                pos = save_pos; tok_count = save_tok;
                flow = F_NONE;
            }
        }
        dispatch_event_on(o, type, ev, &prevented);
        if (ev.type == V_OBJ) { value_t sp = get_prop(ev.u.obj, "__stopped"); if (truthy(sp)) break; }
        if (n->type == BR_NODE_DOCUMENT) break;
    }
    /* window-level listeners */
    if (obj_window >= 0) dispatch_event_on(obj_window, type, ev, &prevented);
    return prevented;
}

static br_node_t* create_element(const char* tag) {
    br_node_t* n = br_node_new(BR_NODE_ELEMENT);
    if (!n) return NULL;
    char* t = br_strdup(tag);
    for (int i = 0; t[i]; i++) if (t[i] >= 'A' && t[i] <= 'Z') t[i] = (char)(t[i] + 32);
    n->tag = t;
    n->js_obj = -1;
    return n;
}

static void insert_before(br_node_t* parent, br_node_t* child, br_node_t* ref) {
    if (!parent || !child) return;
    if (!ref || ref->parent != parent) { br_node_append(parent, child); return; }
    /* detach child */
    br_node_append(parent, child);            /* appends at end (and detaches) */
    /* now move it before ref: unlink from end */
    br_node_t* c = parent->first_child; br_node_t* prev = NULL;
    while (c && c != child) { prev = c; c = c->next; }
    if (!c) return;
    if (prev) prev->next = NULL; else parent->first_child = NULL;
    parent->last_child = prev;
    /* insert before ref */
    if (parent->first_child == ref) { child->next = ref; parent->first_child = child; return; }
    c = parent->first_child;
    while (c && c->next != ref) c = c->next;
    if (c) { child->next = ref; c->next = child; }
}

static void remove_node(br_node_t* n) {
    if (!n || !n->parent) return;
    br_node_t* p = n->parent;
    if (p->first_child == n) p->first_child = n->next;
    else { br_node_t* c = p->first_child; while (c && c->next != n) c = c->next; if (c) c->next = n->next; }
    if (p->last_child == n) { br_node_t* c = p->first_child; br_node_t* last = NULL; while (c) { last = c; c = c->next; } p->last_child = last; }
    n->parent = NULL; n->next = NULL;
}

static br_node_t* clone_node(br_node_t* n, int deep) {
    if (br_stack_headroom() < BR_STACK_MIN) return NULL;
    br_node_t* c = br_node_new(n->type);
    if (!c) return NULL;
    c->tag = n->tag; c->text = n->text; c->attr_count = n->attr_count;
    for (int i = 0; i < n->attr_count; i++) c->attrs[i] = n->attrs[i];
    c->js_obj = -1;
    if (deep) for (br_node_t* k = n->first_child; k; k = k->next) { br_node_t* kc = clone_node(k, 1); if (kc) br_node_append(c, kc); }
    return c;
}

value_t js_dom_call(int o, const char* key, value_t* a, int n, int* handled) {
    *handled = 1;
    br_node_t* el = br_dom_node(objects[o].dom_id);
    if (!el) { *handled = 0; return UNDEF; }
    if (objects[o].name && br_streq(objects[o].name, "classList")) {
        if (br_streq(key, "add")) { for (int i = 0; i < n; i++) class_toggle(el, to_string(a[i]), 1); return UNDEF; }
        if (br_streq(key, "remove")) { for (int i = 0; i < n; i++) class_toggle(el, to_string(a[i]), 0); return UNDEF; }
        if (br_streq(key, "toggle")) { if (!n) return UNDEF; const char* c = to_string(a[0]); if (n > 1) class_toggle(el, c, truthy(a[1]) ? 1 : 0); else class_toggle(el, c, 2); return mk_bool(br_has_class(el, c)); }
        if (br_streq(key, "contains")) return mk_bool(n && br_has_class(el, to_string(a[0])));
        if (br_streq(key, "replace")) { if (n > 1) { class_toggle(el, to_string(a[0]), 0); class_toggle(el, to_string(a[1]), 1); } return UNDEF; }
        *handled = 0; return UNDEF;
    }
    if (objects[o].name && br_streq(objects[o].name, "style")) {
        if (br_streq(key, "setProperty")) { if (n > 1) style_proxy_set(o, to_string(a[0]), a[1]); return UNDEF; }
        if (br_streq(key, "getPropertyValue")) { if (!n) return mk_str(""); prop_t* p = find_own(o, to_string(a[0])); return p ? p->val : mk_str(""); }
        if (br_streq(key, "removeProperty")) { if (n) style_proxy_set(o, to_string(a[0]), mk_str("")); return UNDEF; }
        *handled = 0; return UNDEF;
    }
    /* querying */
    if (br_streq(key, "getElementById")) return node_or_null(n ? br_find_by_id(el, to_string(a[0])) : NULL);
    if (br_streq(key, "querySelector")) { if (!n) return mk_null(); br_node_t* first = NULL; const char* sel = to_string(a[0]); /* selector lists */ const char* p = sel; while (*p && !first) { const char* q = p; while (*q && *q != ',') q++; const char* one = js_strdup_n(p, (int)(q - p)); collect_matching(el, one, -1, 1, &first); p = *q ? q + 1 : q; } return node_or_null(first); }
    if (br_streq(key, "querySelectorAll")) { int arr = new_array(); if (n) { br_node_t* f = NULL; const char* sel = to_string(a[0]); const char* p = sel; while (*p) { const char* q = p; while (*q && *q != ',') q++; const char* one = js_strdup_n(p, (int)(q - p)); collect_matching(el, one, arr, 0, &f); p = *q ? q + 1 : q; } } return mk_obj(arr); }
    if (br_streq(key, "getElementsByTagName")) { int arr = new_array(); if (n) collect_by_tag(el, to_string(a[0]), arr); return mk_obj(arr); }
    if (br_streq(key, "getElementsByClassName")) { int arr = new_array(); if (n) collect_by_class(el, to_string(a[0]), arr); return mk_obj(arr); }
    if (br_streq(key, "getElementsByName")) { int arr = new_array(); if (n) { for (int i = 0; i < br_dom_node_count(); i++) { br_node_t* k = br_dom_node(i); const char* v = br_attr(k, "name"); if (k->type == BR_NODE_ELEMENT && v && br_streq(v, to_string(a[0]))) array_push(arr, node_or_null(k)); } } return mk_obj(arr); }
    if (br_streq(key, "closest")) { if (!n) return mk_null(); for (br_node_t* p = el; p && p->type == BR_NODE_ELEMENT; p = p->parent) if (br_css_match_selector_string(p, to_string(a[0]))) return node_or_null(p); return mk_null(); }
    if (br_streq(key, "matches")) return mk_bool(n && el->type == BR_NODE_ELEMENT && br_css_match_selector_string(el, to_string(a[0])));
    if (br_streq(key, "contains")) { if (!n || a[0].type != V_OBJ) return mk_bool(0); br_node_t* t = unwrap(a[0].u.obj); while (t) { if (t == el) return mk_bool(1); t = t->parent; } return mk_bool(0); }
    /* creation */
    if (br_streq(key, "createElement")) { br_node_t* c = create_element(n ? to_string(a[0]) : "div"); return node_or_null(c); }
    if (br_streq(key, "createTextNode")) { br_node_t* t = br_node_new(BR_NODE_TEXT); if (!t) return mk_null(); t->text = br_strdup(n ? to_string(a[0]) : ""); t->js_obj = -1; return node_or_null(t); }
    if (br_streq(key, "createDocumentFragment")) { br_node_t* c = create_element("fragment"); return node_or_null(c); }
    if (br_streq(key, "createEvent")) return make_event("Event", el);
    if (br_streq(key, "cloneNode")) return node_or_null(clone_node(el, n && truthy(a[0])));
    /* tree mutation */
    if (br_streq(key, "appendChild") || br_streq(key, "append") || br_streq(key, "prepend")) {
        for (int i = 0; i < n; i++) {
            br_node_t* c = NULL;
            if (a[i].type == V_OBJ) c = unwrap(a[i].u.obj);
            else { c = br_node_new(BR_NODE_TEXT); if (c) { c->text = br_strdup(to_string(a[i])); c->js_obj = -1; } }
            if (!c) continue;
            if (c->type == BR_NODE_ELEMENT && br_streq(c->tag, "fragment")) {
                br_node_t* k = c->first_child;
                while (k) { br_node_t* nx = k->next; br_node_append(el, k); k = nx; }
                c->first_child = c->last_child = NULL;
            } else if (br_streq(key, "prepend")) insert_before(el, c, el->first_child);
            else br_node_append(el, c);
        }
        br_js_dom_changed();
        return n ? a[0] : UNDEF;
    }
    if (br_streq(key, "insertBefore")) { if (n && a[0].type == V_OBJ) { br_node_t* c = unwrap(a[0].u.obj); br_node_t* ref = (n > 1 && a[1].type == V_OBJ) ? unwrap(a[1].u.obj) : NULL; insert_before(el, c, ref); br_js_dom_changed(); } return n ? a[0] : UNDEF; }
    if (br_streq(key, "removeChild")) { if (n && a[0].type == V_OBJ) { br_node_t* c = unwrap(a[0].u.obj); if (c && c->parent == el) remove_node(c); br_js_dom_changed(); } return n ? a[0] : UNDEF; }
    if (br_streq(key, "remove")) { remove_node(el); br_js_dom_changed(); return UNDEF; }
    if (br_streq(key, "replaceChild")) { if (n > 1 && a[0].type == V_OBJ && a[1].type == V_OBJ) { br_node_t* nn = unwrap(a[0].u.obj); br_node_t* old = unwrap(a[1].u.obj); if (nn && old && old->parent == el) { insert_before(el, nn, old); remove_node(old); } br_js_dom_changed(); } return n > 1 ? a[1] : UNDEF; }
    if (br_streq(key, "replaceWith")) { if (n && a[0].type == V_OBJ && el->parent) { br_node_t* nn = unwrap(a[0].u.obj); if (nn) { insert_before(el->parent, nn, el); remove_node(el); } br_js_dom_changed(); } return UNDEF; }
    if (br_streq(key, "insertAdjacentHTML")) {
        if (n > 1) {
            const char* where = to_string(a[0]); const char* html = to_string(a[1]);
            br_node_t* frag = create_element("fragment");
            if (frag) {
                br_html_parse_fragment(frag, html, (int)strlen(html));
                br_node_t* k = frag->first_child;
                if (br_streq(where, "beforeend")) { while (k) { br_node_t* nx = k->next; br_node_append(el, k); k = nx; } }
                else if (br_streq(where, "afterbegin")) { br_node_t* first = el->first_child; while (k) { br_node_t* nx = k->next; insert_before(el, k, first); k = nx; } }
                else if (br_streq(where, "beforebegin") && el->parent) { while (k) { br_node_t* nx = k->next; insert_before(el->parent, k, el); k = nx; } }
                else if (br_streq(where, "afterend") && el->parent) { br_node_t* ref = el->next; while (k) { br_node_t* nx = k->next; insert_before(el->parent, k, ref); k = nx; } }
            }
            br_js_dom_changed();
        }
        return UNDEF;
    }
    if (br_streq(key, "insertAdjacentElement")) { if (n > 1 && a[1].type == V_OBJ) { const char* where = to_string(a[0]); br_node_t* c = unwrap(a[1].u.obj); if (c) { if (br_streq(where, "beforeend")) br_node_append(el, c); else if (br_streq(where, "afterbegin")) insert_before(el, c, el->first_child); else if (br_streq(where, "beforebegin") && el->parent) insert_before(el->parent, c, el); else if (el->parent) insert_before(el->parent, c, el->next); } br_js_dom_changed(); } return UNDEF; }
    if (br_streq(key, "insertAdjacentText")) { if (n > 1) { br_node_t* t = br_node_new(BR_NODE_TEXT); if (t) { t->text = br_strdup(to_string(a[1])); t->js_obj = -1; const char* where = to_string(a[0]); if (br_streq(where, "afterbegin")) insert_before(el, t, el->first_child); else br_node_append(el, t); } br_js_dom_changed(); } return UNDEF; }
    /* attributes */
    if (br_streq(key, "getAttribute")) { if (!n) return mk_null(); const char* v = br_attr(el, to_string(a[0])); return v ? mk_str(v) : mk_null(); }
    if (br_streq(key, "setAttribute")) { if (n > 1) { br_set_attr(el, to_string(a[0]), to_string(a[1])); br_js_dom_changed(); } return UNDEF; }
    if (br_streq(key, "hasAttribute")) return mk_bool(n && br_attr(el, to_string(a[0])) != NULL);
    if (br_streq(key, "removeAttribute")) { if (n) { const char* nm = to_string(a[0]); for (int i = 0; i < el->attr_count; i++) if (br_strieq(el->attrs[i].name, nm)) { el->attrs[i] = el->attrs[el->attr_count - 1]; el->attr_count--; break; } br_js_dom_changed(); } return UNDEF; }
    if (br_streq(key, "toggleAttribute")) { if (n) { const char* nm = to_string(a[0]); if (br_attr(el, nm)) { for (int i = 0; i < el->attr_count; i++) if (br_strieq(el->attrs[i].name, nm)) { el->attrs[i] = el->attrs[el->attr_count - 1]; el->attr_count--; break; } } else br_set_attr(el, nm, ""); br_js_dom_changed(); } return UNDEF; }
    if (br_streq(key, "getBoundingClientRect")) { int r = new_object(O_PLAIN); set_prop(r, "x", mk_int(el->lx)); set_prop(r, "y", mk_int(el->ly - brs.scroll_y)); set_prop(r, "left", mk_int(el->lx)); set_prop(r, "top", mk_int(el->ly - brs.scroll_y)); set_prop(r, "width", mk_int(el->lw)); set_prop(r, "height", mk_int(el->lh)); set_prop(r, "right", mk_int(el->lx + el->lw)); set_prop(r, "bottom", mk_int(el->ly + el->lh - brs.scroll_y)); return mk_obj(r); }
    /* events */
    if (br_streq(key, "addEventListener") || br_streq(key, "attachEvent")) {
        if (n > 1 && a[1].type == V_FUNC) {
            const char* type = to_string(a[0]);
            if (type[0] == 'o' && type[1] == 'n' && br_streq(key, "attachEvent")) type += 2;
            char lk[48]; const char* pre = "__listeners_"; int k = 0; for (; pre[k]; k++) lk[k] = pre[k]; for (int i = 0; type[i] && k < 46; i++) lk[k++] = type[i]; lk[k] = 0;
            prop_t* lp = find_own(o, lk);
            int arr;
            if (lp && lp->val.type == V_OBJ) arr = lp->val.u.obj; else { arr = new_array(); set_prop(o, js_strdup(lk), mk_obj(arr)); }
            array_push(arr, a[1]);
            /* DOMContentLoaded / load fire right after the scripts ran: queue as a timer */
            if (br_streq(type, "DOMContentLoaded") || br_streq(type, "load") || br_streq(type, "readystatechange")) { value_t targs[2]; targs[0] = a[1]; targs[1] = mk_int(0); n_setTimeout(-1, targs, 2); }
        }
        return UNDEF;
    }
    if (br_streq(key, "removeEventListener")) {
        if (n > 1) {
            const char* type = to_string(a[0]);
            char lk[48]; const char* pre = "__listeners_"; int k = 0; for (; pre[k]; k++) lk[k] = pre[k]; for (int i = 0; type[i] && k < 46; i++) lk[k++] = type[i]; lk[k] = 0;
            prop_t* lp = find_own(o, lk);
            if (lp && lp->val.type == V_OBJ) { int arr = lp->val.u.obj; for (int i = 0; i < objects[arr].len; i++) if (strict_eq(objects[arr].items[i], a[1])) { for (int j = i + 1; j < objects[arr].len; j++) objects[arr].items[j - 1] = objects[arr].items[j]; objects[arr].len--; break; } }
        }
        return UNDEF;
    }
    if (br_streq(key, "dispatchEvent")) { if (n && a[0].type == V_OBJ) { value_t t = get_prop(a[0].u.obj, "type"); int prevented = 0; set_prop(a[0].u.obj, "target", mk_obj(o)); dispatch_event_on(o, to_string(t), a[0], &prevented); return mk_bool(!prevented); } return mk_bool(1); }
    if (br_streq(key, "click")) { br_js_dispatch_click(el); return UNDEF; }
    if (br_streq(key, "focus")) { if (el->type == BR_NODE_ELEMENT && br_streq(el->tag, "input")) { brs.focus_input = el->id; br_request_repaint(); } return UNDEF; }
    if (br_streq(key, "blur")) { if (brs.focus_input == el->id) brs.focus_input = -1; return UNDEF; }
    if (br_streq(key, "select") || br_streq(key, "scrollIntoView") || br_streq(key, "scrollTo") || br_streq(key, "scroll") || br_streq(key, "submit") || br_streq(key, "reset") || br_streq(key, "normalize") || br_streq(key, "requestFullscreen")) {
        if (br_streq(key, "scrollIntoView")) { brs.scroll_y = el->ly; br_request_repaint(); }
        if (br_streq(key, "reset")) { for (int i = 0; i < br_dom_node_count(); i++) { br_node_t* k = br_dom_node(i); if (k->type == BR_NODE_ELEMENT && br_streq(k->tag, "input")) { k->value = NULL; k->checked = 0; } } br_request_repaint(); }
        return UNDEF;
    }
    if (br_streq(key, "hasChildNodes")) return mk_bool(el->first_child != NULL);
    if (br_streq(key, "toString")) return mk_str("[object HTMLElement]");
    if (br_streq(key, "write") || br_streq(key, "writeln")) {
        /* document.write during load appends to body */
        if (n && br_doc && br_doc->body) { const char* s = to_string(a[0]); br_node_t* frag = create_element("fragment"); if (frag) { br_html_parse_fragment(frag, s, (int)strlen(s)); br_node_t* k = frag->first_child; while (k) { br_node_t* nx = k->next; br_node_append(br_doc->body, k); k = nx; } } br_js_dom_changed(); }
        return UNDEF;
    }
    if (br_streq(key, "open") || br_streq(key, "close") || br_streq(key, "execCommand") || br_streq(key, "hasFocus")) return mk_bool(1);
    if (br_streq(key, "createElementNS")) { br_node_t* c = create_element(n > 1 ? to_string(a[1]) : "div"); return node_or_null(c); }
    if (br_streq(key, "createComment")) { br_node_t* t = br_node_new(BR_NODE_TEXT); if (t) { t->text = ""; t->js_obj = -1; } return node_or_null(t); }
    if (br_streq(key, "elementFromPoint")) return mk_null();
    if (br_streq(key, "getComputedStyle")) return make_style_proxy(el);
    if (br_streq(key, "checkValidity") || br_streq(key, "reportValidity")) return mk_bool(1);
    *handled = 0;
    return UNDEF;
}

/* window-level natives that need DOM access */
static value_t n_getComputedStyle(int t, value_t* a, int n) { (void)t; if (n && a[0].type == V_OBJ) { br_node_t* el = unwrap(a[0].u.obj); if (el) { value_t st = make_style_proxy(el); /* add computed colour/display */ if (st.type == V_OBJ) { char col[16]; uint32_t c = el->style.color; col[0] = '#'; static const char hx[] = "0123456789abcdef"; col[1] = hx[(c >> 20) & 15]; col[2] = hx[(c >> 16) & 15]; col[3] = hx[(c >> 12) & 15]; col[4] = hx[(c >> 8) & 15]; col[5] = hx[(c >> 4) & 15]; col[6] = hx[c & 15]; col[7] = 0; if (!find_own(st.u.obj, "color")) set_prop(st.u.obj, "color", mk_str(js_strdup(col))); if (!find_own(st.u.obj, "display")) set_prop(st.u.obj, "display", mk_str(el->style.display == BR_DISPLAY_NONE ? "none" : el->style.display == BR_DISPLAY_INLINE ? "inline" : "block")); } return st; } } return mk_obj(new_object(O_PLAIN)); }
static value_t n_scrollTo(int t, value_t* a, int n) { (void)t; if (n >= 2) { brs.scroll_y = (int)(to_num(a[1]) >> FX_SHIFT); if (brs.scroll_y < 0) brs.scroll_y = 0; br_request_repaint(); } else if (n == 1 && a[0].type == V_OBJ) { value_t top = get_prop(a[0].u.obj, "top"); if (top.type == V_NUM) { brs.scroll_y = (int)(top.u.n >> FX_SHIFT); br_request_repaint(); } } return UNDEF; }
static void navigate_to(const char* url, int replace) {
    char abs[BR_URL_MAX];
    br_resolve_url(brs.url, url, abs, sizeof(abs));
    /* "#fragment" of the current document only scrolls */
    int hl = 0; while (abs[hl] && abs[hl] != '#') hl++;
    if (abs[hl] == '#') {
        int cl = 0; while (brs.url[cl] && brs.url[cl] != '#') cl++;
        int same = (hl == cl);
        for (int i = 0; same && i < hl; i++) if (abs[i] != brs.url[i]) same = 0;
        if (same) {
            br_node_t* t = abs[hl + 1] ? br_find_by_id(br_doc, abs + hl + 1) : NULL;
            brs.scroll_y = t ? t->ly : 0;
            br_request_repaint();
            return;
        }
    }
    br_strlcpy(brs.address, abs, sizeof(brs.address));
    brs.needs_layout = 2;
    brs.nav_replace = replace;
}
static value_t n_location_assign(int t, value_t* a, int n) { (void)t; if (n) navigate_to(to_string(a[0]), 0); return UNDEF; }
static value_t n_location_replace(int t, value_t* a, int n) { (void)t; if (n) navigate_to(to_string(a[0]), 1); return UNDEF; }
static value_t n_location_reload(int t, value_t* a, int n) { (void)t; (void)a; (void)n; br_strlcpy(brs.address, brs.url, sizeof(brs.address)); brs.needs_layout = 2; brs.nav_replace = 1; return UNDEF; }
static value_t n_history_back(int t, value_t* a, int n) { (void)t; (void)a; (void)n; brs.needs_layout = 3; return UNDEF; }
static value_t n_storage_getItem(int t, value_t* a, int n) { if (!n) return mk_null(); prop_t* p = find_own(t, concat("__", to_string(a[0]))); return p ? p->val : mk_null(); }
static value_t n_storage_setItem(int t, value_t* a, int n) { if (n > 1) set_prop(t, js_strdup(concat("__", to_string(a[0]))), mk_str(js_strdup(to_string(a[1])))); return UNDEF; }
static value_t n_storage_removeItem(int t, value_t* a, int n) { if (n) del_prop(t, concat("__", to_string(a[0]))); return UNDEF; }
static value_t n_encodeURIComponent(int t, value_t* a, int n) {
    (void)t; if (!n) return mk_str("");
    const char* s = to_string(a[0]); char* d = js_alloc_str((int)strlen(s) * 3); int o = 0;
    static const char hx[] = "0123456789ABCDEF";
    for (int i = 0; s[i] && o < JS_STR_MAX - 3; i++) { unsigned char c = (unsigned char)s[i]; if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') d[o++] = (char)c; else { d[o++] = '%'; d[o++] = hx[c >> 4]; d[o++] = hx[c & 15]; } }
    d[o] = 0; return mk_str(d);
}
static value_t n_decodeURIComponent(int t, value_t* a, int n) {
    (void)t; if (!n) return mk_str("");
    const char* s = to_string(a[0]); char* d = js_alloc_str((int)strlen(s)); int o = 0;
    for (int i = 0; s[i] && o < JS_STR_MAX; i++) { if (s[i] == '%' && s[i + 1] && s[i + 2]) { int h = (s[i + 1] <= '9' ? s[i + 1] - '0' : (s[i + 1] | 32) - 'a' + 10) * 16 + (s[i + 2] <= '9' ? s[i + 2] - '0' : (s[i + 2] | 32) - 'a' + 10); d[o++] = (char)h; i += 2; } else if (s[i] == '+') d[o++] = ' '; else d[o++] = s[i]; }
    d[o] = 0; return mk_str(d);
}
static value_t n_String_fromCharCode(int t, value_t* a, int n) { (void)t; char* d = js_alloc_str(n); for (int i = 0; i < n; i++) { int c = (int)(to_num(a[i]) >> FX_SHIFT); d[i] = (char)(c > 0 && c < 128 ? c : '?'); } d[n] = 0; return mk_str(d); }
static value_t n_Number_isInteger(int t, value_t* a, int n) { (void)t; return mk_bool(n && a[0].type == V_NUM && (a[0].u.n & (FX_ONE - 1)) == 0 && !is_nan(a[0].u.n)); }
static value_t n_fetch(int t, value_t* a, int n) { (void)t; (void)a; (void)n; br_js_console("fetch() is not supported in this browser"); int p = new_object(O_PLAIN); def_native(p, "then", n_noop); def_native(p, "catch", n_noop); return mk_obj(p); }
static value_t n_matchMedia(int t, value_t* a, int n) { (void)t; (void)a; (void)n; int r = new_object(O_PLAIN); set_prop(r, "matches", mk_bool(0)); def_native(r, "addListener", n_noop); def_native(r, "addEventListener", n_noop); return mk_obj(r); }
static value_t n_window_addEventListener(int t, value_t* a, int n) {
    if (n > 1 && a[1].type == V_FUNC) {
        const char* type = to_string(a[0]);
        if (br_streq(type, "load") || br_streq(type, "DOMContentLoaded")) { value_t targs[2]; targs[0] = a[1]; targs[1] = mk_int(0); return n_setTimeout(-1, targs, 2); }
        char lk[48]; const char* pre = "__listeners_"; int k = 0; for (; pre[k]; k++) lk[k] = pre[k]; for (int i = 0; type[i] && k < 46; i++) lk[k++] = type[i]; lk[k] = 0;
        prop_t* lp = find_own(t, lk); int arr;
        if (lp && lp->val.type == V_OBJ) arr = lp->val.u.obj; else { arr = new_array(); set_prop(t, js_strdup(lk), mk_obj(arr)); }
        array_push(arr, a[1]);
    }
    return UNDEF;
}

/* ------------------------------------------------------- environment */

static void setup_globals(void) {
    obj_global = new_object(O_PLAIN);
    obj_window = obj_global;
    proto_object = new_object(O_PLAIN);
    proto_array = new_object(O_PLAIN);
    proto_string = new_object(O_PLAIN);
    proto_function = new_object(O_PLAIN);
    proto_number = new_object(O_PLAIN);
    proto_element = new_object(O_PLAIN);
    objects[proto_object].proto = -1;
    objects[proto_array].proto = proto_object;
    objects[proto_string].proto = proto_object;
    objects[proto_function].proto = proto_object;
    objects[proto_element].proto = proto_object;
    int g = obj_global;
    set_prop(g, "window", mk_obj(g));
    set_prop(g, "self", mk_obj(g));
    set_prop(g, "globalThis", mk_obj(g));
    set_prop(g, "top", mk_obj(g));
    set_prop(g, "parent", mk_obj(g));
    set_prop(g, "NaN", mk_num(NAN_FX));
    set_prop(g, "Infinity", mk_num((fx_t)0x7FFFFFFFFFFELL));
    set_prop(g, "undefined", UNDEF);
    set_prop(g, "innerWidth", mk_int(brs.layout_width));
    set_prop(g, "innerHeight", mk_int(brs.win ? brs.win->rect.client_h : 400));
    set_prop(g, "outerWidth", mk_int(brs.layout_width));
    set_prop(g, "outerHeight", mk_int(brs.win ? brs.win->rect.client_h : 400));
    set_prop(g, "devicePixelRatio", mk_int(1));
    set_prop(g, "scrollY", mk_int(0)); set_prop(g, "scrollX", mk_int(0)); set_prop(g, "pageYOffset", mk_int(0));
    set_prop(g, "name", mk_str(""));
    set_prop(g, "closed", mk_bool(0));

    def_native(g, "alert", n_alert);
    def_native(g, "confirm", n_confirm);
    def_native(g, "prompt", n_prompt);
    def_native(g, "parseInt", n_parseInt);
    def_native(g, "parseFloat", n_parseFloat);
    def_native(g, "isNaN", n_isNaN);
    def_native(g, "isFinite", n_isFinite);
    def_native(g, "setTimeout", n_setTimeout);
    def_native(g, "setInterval", n_setInterval);
    def_native(g, "clearTimeout", n_clearTimeout);
    def_native(g, "clearInterval", n_clearTimeout);
    def_native(g, "requestAnimationFrame", n_requestAnimationFrame);
    def_native(g, "cancelAnimationFrame", n_clearTimeout);
    def_native(g, "queueMicrotask", n_setTimeout);
    def_native(g, "getComputedStyle", n_getComputedStyle);
    def_native(g, "scrollTo", n_scrollTo);
    def_native(g, "scroll", n_scrollTo);
    def_native(g, "scrollBy", n_noop);
    def_native(g, "addEventListener", n_window_addEventListener);
    def_native(g, "removeEventListener", n_noop);
    def_native(g, "dispatchEvent", n_noop);
    def_native(g, "encodeURIComponent", n_encodeURIComponent);
    def_native(g, "encodeURI", n_encodeURIComponent);
    def_native(g, "decodeURIComponent", n_decodeURIComponent);
    def_native(g, "decodeURI", n_decodeURIComponent);
    def_native(g, "escape", n_encodeURIComponent);
    def_native(g, "unescape", n_decodeURIComponent);
    def_native(g, "fetch", n_fetch);
    def_native(g, "matchMedia", n_matchMedia);
    def_native(g, "open", n_location_assign);
    def_native(g, "close", n_noop);
    def_native(g, "focus", n_noop);
    def_native(g, "blur", n_noop);
    def_native(g, "print", n_noop);
    def_native(g, "postMessage", n_noop);
    def_native(g, "getSelection", n_noop);

    /* constructors */
    def_native(g, "String", n_String); objects[get_prop(g, "String").u.obj].name = "String";
    def_native(g, "Number", n_Number); objects[get_prop(g, "Number").u.obj].name = "Number";
    def_native(g, "Boolean", n_Boolean); objects[get_prop(g, "Boolean").u.obj].name = "Boolean";
    def_native(g, "Array", n_Array); objects[get_prop(g, "Array").u.obj].name = "Array";
    def_native(g, "Object", n_Object); objects[get_prop(g, "Object").u.obj].name = "Object";
    def_native(g, "Function", n_noop); objects[get_prop(g, "Function").u.obj].name = "Function";
    def_native(g, "Error", n_Error); objects[get_prop(g, "Error").u.obj].name = "Error";
    def_native(g, "TypeError", n_Error); def_native(g, "RangeError", n_Error); def_native(g, "SyntaxError", n_Error); def_native(g, "ReferenceError", n_Error);
    def_native(g, "Date", n_Date); objects[get_prop(g, "Date").u.obj].name = "Date";
    def_native(g, "RegExp", n_RegExp); objects[get_prop(g, "RegExp").u.obj].name = "RegExp";
    def_native(g, "Promise", n_Promise); objects[get_prop(g, "Promise").u.obj].name = "Promise";
    def_native(g, "Map", n_Object); def_native(g, "Set", n_Object); def_native(g, "WeakMap", n_Object); def_native(g, "Symbol", n_String);
    def_native(g, "IntersectionObserver", n_IntersectionObserver); def_native(g, "MutationObserver", n_Observer); def_native(g, "ResizeObserver", n_Observer); def_native(g, "PerformanceObserver", n_Observer);
    def_native(g, "__ioFire", n_io_fire);
    def_native(g, "Event", n_Object); def_native(g, "CustomEvent", n_Object); def_native(g, "Image", n_Object); def_native(g, "XMLHttpRequest", n_Object);
    def_native(g, "HTMLElement", n_Object); objects[get_prop(g, "HTMLElement").u.obj].name = "HTMLElement";
    def_native(g, "Element", n_Object); objects[get_prop(g, "Element").u.obj].name = "Element";
    def_native(g, "Node", n_Object); objects[get_prop(g, "Node").u.obj].name = "Node";
    set_prop(get_prop(g, "Array").u.obj, "prototype", mk_obj(proto_array));
    set_prop(get_prop(g, "Object").u.obj, "prototype", mk_obj(proto_object));
    set_prop(get_prop(g, "String").u.obj, "prototype", mk_obj(proto_string));
    set_prop(get_prop(g, "Function").u.obj, "prototype", mk_obj(proto_function));
    set_prop(get_prop(g, "Number").u.obj, "prototype", mk_obj(proto_number));
    set_prop(get_prop(g, "HTMLElement").u.obj, "prototype", mk_obj(proto_element));
    set_prop(get_prop(g, "Element").u.obj, "prototype", mk_obj(proto_element));

    int O = get_prop(g, "Object").u.obj;
    def_native(O, "keys", n_Object_keys); def_native(O, "values", n_Object_values); def_native(O, "entries", n_Object_entries);
    def_native(O, "assign", n_Object_assign); def_native(O, "create", n_Object_create); def_native(O, "freeze", n_Object_freeze);
    def_native(O, "defineProperty", n_noop); def_native(O, "getPrototypeOf", n_noop); def_native(O, "fromEntries", n_Object);
    int A = get_prop(g, "Array").u.obj;
    def_native(A, "isArray", n_Array_isArray); def_native(A, "from", n_Array_from); def_native(A, "of", n_Array);
    int S = get_prop(g, "String").u.obj;
    def_native(S, "fromCharCode", n_String_fromCharCode);
    int N = get_prop(g, "Number").u.obj;
    def_native(N, "isInteger", n_Number_isInteger); def_native(N, "isNaN", n_isNaN); def_native(N, "isFinite", n_isFinite); def_native(N, "parseInt", n_parseInt); def_native(N, "parseFloat", n_parseFloat);
    set_prop(N, "MAX_SAFE_INTEGER", mk_num((fx_t)0x7FFFFFFFFFFELL)); set_prop(N, "EPSILON", mk_num(1));
    int D = get_prop(g, "Date").u.obj;
    def_native(D, "now", n_Date_now);

    obj_math = new_object(O_PLAIN);
    set_prop(g, "Math", mk_obj(obj_math));
    set_prop(obj_math, "PI", mk_num(205887));
    set_prop(obj_math, "E", mk_num(178145));
    def_native(obj_math, "random", n_Math_random); def_native(obj_math, "floor", n_Math_floor); def_native(obj_math, "ceil", n_Math_ceil);
    def_native(obj_math, "round", n_Math_round); def_native(obj_math, "trunc", n_Math_trunc); def_native(obj_math, "abs", n_Math_abs); def_native(obj_math, "sign", n_Math_sign);
    def_native(obj_math, "max", n_Math_max); def_native(obj_math, "min", n_Math_min); def_native(obj_math, "pow", n_Math_pow); def_native(obj_math, "sqrt", n_Math_sqrt);
    def_native(obj_math, "sin", n_Math_sin); def_native(obj_math, "cos", n_Math_cos); def_native(obj_math, "atan2", n_Math_atan2); def_native(obj_math, "hypot", n_Math_hypot);
    def_native(obj_math, "log", n_Math_log); def_native(obj_math, "exp", n_Math_exp);
    build_sin_table();

    obj_json = new_object(O_PLAIN);
    set_prop(g, "JSON", mk_obj(obj_json));
    def_native(obj_json, "stringify", n_JSON_stringify); def_native(obj_json, "parse", n_JSON_parse);

    obj_console = new_object(O_PLAIN);
    set_prop(g, "console", mk_obj(obj_console));
    def_native(obj_console, "log", n_console_log); def_native(obj_console, "info", n_console_log); def_native(obj_console, "warn", n_console_log);
    def_native(obj_console, "error", n_console_log); def_native(obj_console, "debug", n_console_log); def_native(obj_console, "table", n_console_log);
    def_native(obj_console, "group", n_noop); def_native(obj_console, "groupEnd", n_noop); def_native(obj_console, "time", n_noop); def_native(obj_console, "timeEnd", n_noop); def_native(obj_console, "clear", n_noop); def_native(obj_console, "assert", n_noop); def_native(obj_console, "trace", n_noop); def_native(obj_console, "dir", n_console_log);

    /* document */
    if (br_doc) {
        obj_document = wrap_node(br_doc);
        set_prop(g, "document", mk_obj(obj_document));
    }

    /* location */
    int loc = new_object(O_PLAIN);
    obj_location = loc;
    set_prop(g, "location", mk_obj(loc));
    set_prop(loc, "href", mk_str(js_strdup(brs.url)));
    {
        const char* u = brs.url; const char* p = u;
        while (*p && *p != ':') p++;
        set_prop(loc, "protocol", mk_str(js_strdup_n(u, (int)(p - u) + 1)));
        if (p[0] == ':' && p[1] == '/' && p[2] == '/') p += 3; else if (*p == ':') p++;
        const char* hs = p; while (*p && *p != '/' && *p != '?' && *p != '#') p++;
        set_prop(loc, "host", mk_str(js_strdup_n(hs, (int)(p - hs))));
        const char* hn = hs; const char* q = hs; while (q < p && *q != ':') q++;
        set_prop(loc, "hostname", mk_str(js_strdup_n(hn, (int)(q - hn))));
        set_prop(loc, "port", mk_str(q < p ? js_strdup_n(q + 1, (int)(p - q - 1)) : ""));
        set_prop(loc, "origin", mk_str(js_strdup_n(u, (int)(p - u))));
        const char* ps = p; while (*p && *p != '?' && *p != '#') p++;
        set_prop(loc, "pathname", mk_str(ps < p ? js_strdup_n(ps, (int)(p - ps)) : "/"));
        const char* ss = p; while (*p && *p != '#') p++;
        set_prop(loc, "search", mk_str(js_strdup_n(ss, (int)(p - ss))));
        set_prop(loc, "hash", mk_str(js_strdup(p)));
    }
    def_native(loc, "assign", n_location_assign); def_native(loc, "replace", n_location_replace); def_native(loc, "reload", n_location_reload); def_native(loc, "toString", n_noop);

    int hist = new_object(O_PLAIN);
    set_prop(g, "history", mk_obj(hist));
    set_prop(hist, "length", mk_int(brs.history_len));
    def_native(hist, "back", n_history_back); def_native(hist, "pushState", n_noop); def_native(hist, "replaceState", n_noop); def_native(hist, "go", n_history_back); def_native(hist, "forward", n_noop);

    int nav = new_object(O_PLAIN);
    set_prop(g, "navigator", mk_obj(nav));
    set_prop(nav, "userAgent", mk_str("Mozilla/4.0 (compatible; SharkNavigator/1.0; SharkOS)"));
    set_prop(nav, "language", mk_str("en-US"));
    set_prop(nav, "platform", mk_str("SharkOS i386"));
    set_prop(nav, "onLine", mk_bool(net_configured ? 1 : 0));
    set_prop(nav, "cookieEnabled", mk_bool(0));
    set_prop(nav, "appName", mk_str("Shark Navigator"));
    set_prop(nav, "vendor", mk_str("SharkOS"));
    set_prop(nav, "hardwareConcurrency", mk_int(1));

    int scr = new_object(O_PLAIN);
    set_prop(g, "screen", mk_obj(scr));
    set_prop(scr, "width", mk_int((int)screen_width)); set_prop(scr, "height", mk_int((int)screen_height));
    set_prop(scr, "availWidth", mk_int((int)screen_width)); set_prop(scr, "availHeight", mk_int((int)screen_height));
    set_prop(scr, "colorDepth", mk_int(32));

    int perf = new_object(O_PLAIN);
    set_prop(g, "performance", mk_obj(perf));
    def_native(perf, "now", n_perf_now);
    def_native(perf, "mark", n_noop); def_native(perf, "measure", n_noop);

    int ls = new_object(O_PLAIN);
    set_prop(g, "localStorage", mk_obj(ls)); set_prop(g, "sessionStorage", mk_obj(ls));
    def_native(ls, "getItem", n_storage_getItem); def_native(ls, "setItem", n_storage_setItem); def_native(ls, "removeItem", n_storage_removeItem); def_native(ls, "clear", n_noop);
}

/* ------------------------------------------------------------ public */

void br_js_reset(void) {
    node_count = 0; list_count = 0; obj_count = 0; prop_count = 0; str_used = 0; scope_count = 0; arr_used = 0;
    tok_count = 0; steps = 0; js_aborted = 0; js_error = 0; flow = F_NONE; depth = 0; eval_nest = 0; parse_nest = 0;
    scope_pinned = 0;
    memset(timers, 0, sizeof(timers));
    for (int i = 0; i < br_dom_node_count(); i++) br_dom_node(i)->js_obj = -1;
    obj_global = obj_window = obj_document = obj_location = -1;
    global_scope = new_scope(-1, -1);
    setup_globals();
    scopes[global_scope].this_obj = obj_window;
}

/* "origin: message" without touching the string pool (it may be exhausted). */
static void console_error(const char* origin, const char* what, const char* msg) {
    static char line[200];
    int o = 0;
    for (int i = 0; origin[i] && o < 60; i++) line[o++] = origin[i];
    for (int i = 0; what[i] && o < (int)sizeof(line) - 1; i++) line[o++] = what[i];
    for (int i = 0; msg[i] && o < (int)sizeof(line) - 1; i++) line[o++] = msg[i];
    line[o] = 0;
    br_js_console(line);
}

static void run_source(const char* src, int len, const char* origin) {
    if (!src || len <= 0) return;
    steps = 0; js_aborted = 0; js_error = 0; flow = F_NONE; depth = 0; eval_nest = 0; parse_nest = 0;
    if (!tokenize(src, len)) { console_error(origin, ": ", js_error_msg); js_error = 0; flow = F_NONE; return; }
    pos = 0;
    int prog = parse_program();
    if (js_error) {
        console_error(origin, ": ", js_error_msg);
        js_error = 0; flow = F_NONE;
        return;
    }
    exec(prog, global_scope);
    if (flow == F_THROW) {
        console_error(origin, ": Uncaught ", flow_val.type == V_STR ? flow_val.u.s : to_string(flow_val));
    }
    flow = F_NONE;
    /* scripts may have changed the DOM: re-layout */
    br_js_dom_changed();
}

void br_js_run_source(const char* src, int len, const char* origin) { run_source(src, len, origin); }

static char ext_script[96 * 1024];

static void run_scripts(br_node_t* n) {
    if (!n || br_stack_headroom() < BR_STACK_MIN) return;
    if (n->type == BR_NODE_ELEMENT && br_streq(n->tag, "script")) {
        const char* type = br_attr(n, "type");
        if (type && !br_strieq(type, "text/javascript") && !br_strieq(type, "module") && !br_strieq(type, "application/javascript") && !br_strieq(type, "text/ecmascript") && type[0]) return;
        const char* src = br_attr(n, "src");
        if (src && src[0]) {
            char abs[BR_URL_MAX];
            br_resolve_url(brs.url, src, abs, sizeof(abs));
            br_status("Loading script...");
            int len = br_fetch_resource(abs, (uint8_t*)ext_script, sizeof(ext_script) - 1);
            if (len > 0) { ext_script[len] = 0; run_source(ext_script, len, src); }
            else br_js_console(concat("failed to load script ", src));
        } else if (n->first_child && n->first_child->type == BR_NODE_TEXT) {
            run_source(n->first_child->text, (int)strlen(n->first_child->text), "inline script");
        }
        return;
    }
    /* iterate children with tolerance for scripts that mutate siblings */
    for (br_node_t* c = n->first_child; c; c = c->next) run_scripts(c);
}

void br_js_run_document(br_node_t* doc) {
    if (!doc) return;
    run_scripts(doc);
    /* body onload */
    if (doc->body) {
        const char* onload = br_attr(doc->body, "onload");
        if (onload) run_source(onload, (int)strlen(onload), "onload");
    }
    /* window.onload assigned by script */
    if (obj_window >= 0) {
        prop_t* p = find_own(obj_window, "onload");
        if (p && p->val.type == V_FUNC) { value_t ev = make_event("load", doc); call_handler(p->val, obj_window, ev); }
        if (obj_document >= 0) {
            prop_t* d = find_own(obj_document, "onreadystatechange");
            if (d && d->val.type == V_FUNC) { value_t ev = make_event("readystatechange", doc); call_handler(d->val, obj_document, ev); }
        }
    }
}

void br_js_dispatch_click(br_node_t* n) {
    if (!n || obj_window < 0) return;
    steps = 0; js_aborted = 0; flow = F_NONE; depth = 0; eval_nest = 0;
    /* checkbox toggles before handlers see it */
    if (n->type == BR_NODE_ELEMENT && br_streq(n->tag, "input")) {
        const char* type = br_attr(n, "type");
        if (type && br_strieq(type, "checkbox")) { n->checked = !(n->checked || (br_attr(n, "checked") && !n->value)); n->value = br_strdup("x"); }
        else if (type && br_strieq(type, "radio")) {
            const char* name = br_attr(n, "name");
            for (int i = 0; i < br_dom_node_count(); i++) { br_node_t* k = br_dom_node(i); const char* t2 = br_attr(k, "type"); const char* n2 = br_attr(k, "name"); if (k->type == BR_NODE_ELEMENT && t2 && br_strieq(t2, "radio") && name && n2 && br_streq(n2, name)) { k->checked = 0; k->value = br_strdup("x"); } }
            n->checked = 1; n->value = br_strdup("x");
        }
    }
    /* label for= */
    if (n->type == BR_NODE_ELEMENT && br_streq(n->tag, "label")) {
        const char* f = br_attr(n, "for");
        if (f) { br_node_t* t = br_find_by_id(br_doc, f); if (t && t != n) { br_js_dispatch_click(t); return; } }
    }
    int prevented = dispatch_bubbling(n, "click", "onclick");
    /* default actions */
    if (!prevented) {
        /* submit buttons fire the form's submit */
        br_node_t* form = NULL;
        if (n->type == BR_NODE_ELEMENT && (br_streq(n->tag, "button") || br_streq(n->tag, "input"))) {
            const char* type = br_attr(n, "type");
            int is_submit = br_streq(n->tag, "button") ? (!type || br_strieq(type, "submit")) : (type && br_strieq(type, "submit"));
            if (is_submit) { for (br_node_t* p = n->parent; p; p = p->parent) if (p->type == BR_NODE_ELEMENT && br_streq(p->tag, "form")) { form = p; break; } }
            if (type && br_strieq(type, "reset")) { for (int i = 0; i < br_dom_node_count(); i++) { br_node_t* k = br_dom_node(i); if (k->type == BR_NODE_ELEMENT && br_streq(k->tag, "input")) { k->value = NULL; k->checked = 0; } } }
        }
        if (form) {
            int sp = dispatch_bubbling(form, "submit", "onsubmit");
            if (!sp) {
                /* GET submit: build query string and navigate */
                const char* action = br_attr(form, "action");
                const char* method = br_attr(form, "method");
                if (!method || !br_strieq(method, "post")) {
                    static char url[BR_URL_MAX * 2];
                    br_resolve_url(brs.url, action && action[0] ? action : brs.url, url, BR_URL_MAX);
                    int first = 1;
                    for (int i = 0; i < br_dom_node_count(); i++) {
                        br_node_t* k = br_dom_node(i);
                        if (k->type != BR_NODE_ELEMENT || !(br_streq(k->tag, "input") || br_streq(k->tag, "select") || br_streq(k->tag, "textarea"))) continue;
                        br_node_t* pf = k->parent; while (pf && pf != form) pf = pf->parent; if (pf != form) continue;
                        const char* name = br_attr(k, "name"); if (!name) continue;
                        const char* type = br_attr(k, "type");
                        if (type && (br_strieq(type, "submit") || br_strieq(type, "button") || br_strieq(type, "reset"))) continue;
                        if (type && (br_strieq(type, "checkbox") || br_strieq(type, "radio")) && !(k->checked || (br_attr(k, "checked") && !k->value))) continue;
                        const char* val = k->value ? k->value : (br_attr(k, "value") ? br_attr(k, "value") : "");
                        if (type && (br_strieq(type, "checkbox") || br_strieq(type, "radio"))) val = br_attr(k, "value") ? br_attr(k, "value") : "on";
                        br_strlcat(url, first ? "?" : "&", sizeof(url)); first = 0;
                        br_strlcat(url, name, sizeof(url)); br_strlcat(url, "=", sizeof(url));
                        value_t ev = mk_str(val); value_t enc = n_encodeURIComponent(-1, &ev, 1);
                        br_strlcat(url, enc.u.s, sizeof(url));
                    }
                    br_strlcpy(brs.address, url, sizeof(brs.address));
                    brs.needs_layout = 2;
                    return;
                }
                br_status("POST forms are not supported");
            }
            return;
        }
        /* links */
        for (br_node_t* p = n; p; p = p->parent) {
            if (p->type == BR_NODE_ELEMENT && br_streq(p->tag, "a")) {
                const char* href = br_attr(p, "href");
                if (href && !(href[0] == '#' && href[1] == 0) && !br_streq_prefix(href, "javascript:")) {
                    char abs[BR_URL_MAX];
                    br_resolve_url(brs.url, href, abs, sizeof(abs));
                    br_strlcpy(brs.address, abs, sizeof(brs.address));
                    brs.needs_layout = 2;
                } else if (href && br_streq_prefix(href, "javascript:")) {
                    run_source(href + 11, (int)strlen(href + 11), "javascript: link");
                } else if (href && href[0] == '#') {
                    if (href[1]) { br_node_t* t = br_find_by_id(br_doc, href + 1); if (t) { brs.scroll_y = t->ly; br_request_repaint(); } }
                    else { brs.scroll_y = 0; br_request_repaint(); }
                }
                break;
            }
        }
    }
    br_request_repaint();
}

void br_js_dispatch_input(br_node_t* n) {
    if (!n || obj_window < 0) return;
    steps = 0; js_aborted = 0; flow = F_NONE; depth = 0; eval_nest = 0;
    dispatch_bubbling(n, "input", "oninput");
    dispatch_bubbling(n, "keyup", "onkeyup");
    br_request_repaint();
}

void br_js_dispatch_change(br_node_t* n) {
    if (!n || obj_window < 0) return;
    steps = 0; js_aborted = 0; flow = F_NONE; depth = 0; eval_nest = 0;
    dispatch_bubbling(n, "change", "onchange");
}

int br_js_has_timers(void) {
    for (int i = 0; i < JS_MAX_TIMERS; i++) if (timers[i].active) return 1;
    return 0;
}

void br_js_tick(void) {
    if (obj_window < 0) return;
    int now = (int)uptime_ticks;
    for (int i = 0; i < JS_MAX_TIMERS; i++) {
        if (!timers[i].active) continue;
        if (now - timers[i].due < 0) continue;
        value_t fn = mk_obj(timers[i].fn);
        if (timers[i].interval) timers[i].due = now + timers[i].interval;
        else timers[i].active = 0;
        steps = 0; js_aborted = 0; flow = F_NONE; depth = 0; eval_nest = 0;
        value_t ev = mk_int(now);
        call_function(fn, obj_window, &ev, 1);
        if (flow == F_THROW) br_js_console(concat("Uncaught (timer): ", to_string(flow_val)));
        flow = F_NONE;
        /* scripts running under the step budget keep the UI responsive */
        if (steps > JS_STEP_BUDGET) timers[i].active = 0;
    }
}

/* keyboard events for focused inputs: Enter triggers form submit */
void br_js_dispatch_key(br_node_t* n, char c) {
    if (!n || obj_window < 0) return;
    steps = 0; js_aborted = 0; flow = F_NONE; depth = 0; eval_nest = 0;
    value_t ev = make_event("keydown", n);
    if (ev.type == V_OBJ) {
        const char* keyname = c == '\n' ? "Enter" : c == '\b' ? "Backspace" : c == 27 ? "Escape" : c == '\t' ? "Tab" : js_strdup_n(&c, 1);
        set_prop(ev.u.obj, "key", mk_str(keyname));
        set_prop(ev.u.obj, "keyCode", mk_int(c == '\n' ? 13 : c == '\b' ? 8 : c == 27 ? 27 : (int)(unsigned char)c));
        set_prop(ev.u.obj, "which", mk_int(c == '\n' ? 13 : (int)(unsigned char)c));
        def_native(ev.u.obj, "preventDefault", n_preventDefault);
    }
    int prevented = 0;
    for (br_node_t* p = n; p; p = p->parent) {
        int o = wrap_node(p); if (o < 0) break;
        const char* code = br_attr(p, "onkeydown");
        if (code && p->type == BR_NODE_ELEMENT && !find_own(o, "onkeydown")) run_source(code, (int)strlen(code), "onkeydown");
        dispatch_event_on(o, "keydown", ev, &prevented);
        dispatch_event_on(o, "keypress", ev, &prevented);
        if (p->type == BR_NODE_DOCUMENT) break;
    }
    if (obj_window >= 0) dispatch_event_on(obj_window, "keydown", ev, &prevented);
    if (c == '\n' && !prevented) {
        /* Enter in a text input submits its form */
        for (br_node_t* p = n->parent; p; p = p->parent) {
            if (p->type == BR_NODE_ELEMENT && br_streq(p->tag, "form")) {
                /* find a submit button, else submit directly */
                br_node_t* btn = NULL;
                for (int i = 0; i < br_dom_node_count() && !btn; i++) {
                    br_node_t* k = br_dom_node(i);
                    if (k->type != BR_NODE_ELEMENT) continue;
                    const char* type = br_attr(k, "type");
                    int is_submit = (br_streq(k->tag, "button") && (!type || br_strieq(type, "submit"))) || (br_streq(k->tag, "input") && type && br_strieq(type, "submit"));
                    if (!is_submit) continue;
                    br_node_t* pf = k->parent; while (pf && pf != p) pf = pf->parent;
                    if (pf == p) btn = k;
                }
                if (btn) br_js_dispatch_click(btn);
                else { int sp = dispatch_bubbling(p, "submit", "onsubmit"); (void)sp; }
                break;
            }
        }
    }
}
