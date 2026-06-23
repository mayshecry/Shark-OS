#include "kernel.h"
#include "sharkscript.h"
static int s_strlen(const char* s) {
    int n = 0; while (s[n]) n++; return n;
}
static int s_strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return *(unsigned char*)a - *(unsigned char*)b;
}
static void s_strcpy(char* d, const char* s) {
    while ((*d++ = *s++));
}
static int s_strncmp(const char* a, const char* b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (a[i] == 0) return 0;
    }
    return 0;
}
static char* s_strstr(const char* h, const char* n) {
    if (!*n) return (char*)h;
    int nl = s_strlen(n);
    for (; *h; h++) {
        if (s_strncmp(h, n, nl) == 0) return (char*)h;
    }
    return 0;
}
static int s_atoi(const char* s) {
    int r = 0, sign = 1;
    while (*s == ' ') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { r = r * 10 + (*s - '0'); s++; }
    return r * sign;
}
static int s_isspace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}
static int s_isdigit(char c) {
    return c >= '0' && c <= '9';
}
static char* s_trim(char* s) {
    while (s_isspace(*s)) s++;
    if (*s == 0) return s;
    char* e = s + s_strlen(s) - 1;
    while (e > s && s_isspace(*e)) e--;
    *(e+1) = 0;
    return s;
}
static int s_starts(const char* s, const char* p) {
    return s_strncmp(s, p, s_strlen(p)) == 0;
}
static void s_itoa(int v, char* buf) {
    char tmp[16];
    int i = 0, neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    if (v == 0) tmp[i++] = '0';
    else while (v) { tmp[i++] = '0' + (v % 10); v /= 10; }
    if (neg) *buf++ = '-';
    while (i > 0) *buf++ = tmp[--i];
    *buf = 0;
}
typedef enum {
    OP_NOP = 0,
    OP_USE, OP_TIMER_START, OP_TIMER_END, OP_SET, OP_SET_EXPR,
    OP_SET_HEADER, OP_TIME, OP_BREAK, OP_INCREMENT, OP_LOOP, OP_WHILE,
    OP_SYSTEM, OP_BASH_KILL, OP_NUKE, OP_EXEC, OP_INPUT, OP_POST,
    OP_IF_COMPLEX, OP_SLEEP, OP_CALL, OP_PRINT, OP_LOG, OP_FETCH,
    OP_ELSE, OP_BLOCK, OP_IF_PRINT, OP_IF_CALL, OP_IF_BREAK,
    OP_PARALLEL_LOOP, OP_EMPTY_PARALLEL_LOOP, OP_SEARCH, OP_READ_FILE,
    OP_TOKENIZE, OP_ARRAY_GET, OP_ARRAY_SET, OP_ARRAY_LEN, OP_INDEX_OF,
    OP_SERVE, OP_PUT, OP_PATCH, OP_DELETE, OP_JSON_EXTRACT, OP_SUBSTRING,
    OP_DISCORD_CONNECT, OP_DISCORD_LIMIT, OP_MATH_LOOP, OP_SYS_WRITE,
    OP_SYS_READ_FILE, OP_SYS_EXIT, OP_SYS_YIELD, OP_REPLACE, OP_LIST_FILES,
    OP_FILE_EXISTS, OP_GET_ENV, OP_GET_HARDWARE, OP_IF_CALL_FUNC,
    OP_ENDLOOP, OP_ENDFUNCTION, OP_ENDWHILE, OP_MAX
} opcode_t;
typedef enum {
    LOG_NOP = 0, LOG_OR, LOG_AND, LOG_LT, LOG_GT, LOG_EQ, LOG_NE,
    LOG_CONTAINS, LOG_PROTO, LOG_MALICIOUS, LOG_VAR, LOG_CONST
} logic_op_t;
typedef struct logic_expr {
    logic_op_t op;
    struct logic_expr* left;
    struct logic_expr* right;
    char value[256];
    int int_val;
} logic_expr_t;
typedef struct instruction {
    opcode_t op;
    char value[256];
    char message[512];
    struct instruction* body;
    int body_count;
    logic_expr_t* condition;
    int int_value;
    int is_static;
    int duration_ms;
    int needs_iteration;
    int is_single_print_loop;
} instruction_t;
typedef struct {
    instruction_t* main;
    int main_count;
    char functions[64][64];
    instruction_t* function_bodies[64];
    int function_counts[64];
    int function_count;
    char imports[16][64];
    int import_count;
    int uses_bypass_time;
} compiled_script_t;
#define MAX_MAIN 256
#define MAX_FUNCS 16
#define MAX_FBODY 256
#define MAX_BLOCK 32
static instruction_t g_main[MAX_MAIN];
static int g_mc;
static instruction_t g_block[MAX_BLOCK];
static int g_bd;
static char g_cfunc[64];
static instruction_t g_fb[MAX_FUNCS][MAX_FBODY];
static int g_fc[MAX_FUNCS];
static int g_inf;
static logic_expr_t* parse_logic_expr(const char* str) {
    static logic_expr_t pool[256];
    static int pool_idx = 0;
    if (pool_idx >= 256) return 0;
    logic_expr_t* expr = &pool[pool_idx++];
    char buf[256];
    s_strcpy(buf, str);
    char* s = s_trim(buf);
    char* or_pos = 0;
    int depth = 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] == '(') depth++;
        else if (s[i] == ')') depth--;
        else if (depth == 0 && s[i] == '|' && s[i+1] == '|') { or_pos = &s[i]; break; }
    }
    if (or_pos) {
        *or_pos = 0;
        expr->op = LOG_OR;
        expr->left = parse_logic_expr(s);
        expr->right = parse_logic_expr(or_pos + 2);
        return expr;
    }
    char* and_pos = 0;
    depth = 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] == '(') depth++;
        else if (s[i] == ')') depth--;
        else if (depth == 0 && s[i] == '&' && s[i+1] == '&') { and_pos = &s[i]; break; }
    }
    if (and_pos) {
        *and_pos = 0;
        expr->op = LOG_AND;
        expr->left = parse_logic_expr(s);
        expr->right = parse_logic_expr(and_pos + 2);
        return expr;
    }
    char* cmp_pos = 0;
    int cmp_op = 0;
    depth = 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] == '(') depth++;
        else if (s[i] == ')') depth--;
        else if (depth == 0) {
            if (s[i] == '=' && s[i+1] == '=') { cmp_pos = &s[i]; cmp_op = LOG_EQ; break; }
            if (s[i] == '!' && s[i+1] == '=') { cmp_pos = &s[i]; cmp_op = LOG_NE; break; }
            if (s[i] == '<' && s[i+1] != '=') { cmp_pos = &s[i]; cmp_op = LOG_LT; break; }
            if (s[i] == '>' && s[i+1] != '=') { cmp_pos = &s[i]; cmp_op = LOG_GT; break; }
            if (s[i] == 'C' && s_strncmp(&s[i], "CONTAINS", 8) == 0) { cmp_pos = &s[i]; cmp_op = LOG_CONTAINS; break; }
        }
    }
    if (cmp_pos) {
        int skip = (cmp_op == LOG_EQ || cmp_op == LOG_NE) ? 2 : 
                   (cmp_op == LOG_CONTAINS) ? 8 : 1;
        *cmp_pos = 0;
        expr->op = cmp_op;
        expr->left = parse_logic_expr(s);
        expr->right = parse_logic_expr(cmp_pos + skip);
        return expr;
    }
    if (s_strcmp(s, "MALICIOUS") == 0) { expr->op = LOG_MALICIOUS; return expr; }
    if (s_starts(s, "PROTO ")) { expr->op = LOG_PROTO; s_strcpy(expr->value, s_trim(s + 6)); return expr; }
    if (s[0] == '%') {
        int len = s_strlen(s);
        if (s[len-1] == '%') { s[len-1] = 0; expr->op = LOG_VAR; s_strcpy(expr->value, s + 1); return expr; }
    }
    expr->op = LOG_CONST;
    s_strcpy(expr->value, s);
    return expr;
}
static int is_math_op_char(char c) {
    return c == '+' || c == '-' || c == '*' || c == '/' || c == '%';
}
static int eval_math_int(const char* expr) {
    char tokens[64][64];
    int tc = 0;
    char buf[512];
    s_strcpy(buf, expr);
    char* p = buf;
    while (*p) {
        while (s_isspace(*p)) p++;
        if (*p == 0) break;
        if (is_math_op_char(*p) || *p == '(' || *p == ')') {
            tokens[tc][0] = *p; tokens[tc][1] = 0; tc++; p++;
        } else {
            int ti = 0;
            while (*p && !s_isspace(*p) && !is_math_op_char(*p) && *p != '(' && *p != ')')
                tokens[tc][ti++] = *p++;
            tokens[tc][ti] = 0; tc++;
        }
    }
    while (1) {
        int open = -1, close = -1;
        for (int i = 0; i < tc; i++) {
            if (s_strcmp(tokens[i], "(") == 0) open = i;
            else if (s_strcmp(tokens[i], ")") == 0) { close = i; break; }
        }
        if (open < 0 || close < 0) break;
        char stemp[64][64];
        int stc = 0;
        for (int i = open + 1; i < close; i++) s_strcpy(stemp[stc++], tokens[i]);
        int j = 0;
        for (int i = 0; i < stc; i++) {
            if (s_strcmp(stemp[i], "*") == 0 || s_strcmp(stemp[i], "/") == 0 || s_strcmp(stemp[i], "%") == 0) {
                if (j == 0) break;
                int l = s_atoi(stemp[j-1]);
                int r = s_atoi(stemp[i+1]);
                int res = 0;
                if (s_strcmp(stemp[i], "*") == 0) res = l * r;
                else if (s_strcmp(stemp[i], "/") == 0) res = (r != 0) ? l / r : 0;
                else res = (r != 0) ? l % r : 0;
                char tmp[64]; s_itoa(res, tmp); s_strcpy(stemp[j-1], tmp);
                for (int k = i + 2; k < stc; k++) s_strcpy(stemp[k-2], stemp[k]);
                stc -= 2; i--;
            }
        }
        int sub = s_atoi(stemp[0]);
        for (int i = 1; i < stc; i += 2) {
            int v = s_atoi(stemp[i+1]);
            if (s_strcmp(stemp[i], "+") == 0) sub += v;
            else sub -= v;
        }
        char tmp[64]; s_itoa(sub, tmp);
        s_strcpy(tokens[open], tmp);
        int shift = close - open;
        for (int i = close + 1; i < tc; i++) s_strcpy(tokens[i - shift], tokens[i]);
        tc -= shift;
    }
    int j = 0;
    for (int i = 0; i < tc; i++) {
        if (s_strcmp(tokens[i], "*") == 0 || s_strcmp(tokens[i], "/") == 0 || s_strcmp(tokens[i], "%") == 0) {
            if (j == 0) break;
            int l = s_atoi(tokens[j-1]);
            int r = s_atoi(tokens[i+1]);
            int res = 0;
            if (s_strcmp(tokens[i], "*") == 0) res = l * r;
            else if (s_strcmp(tokens[i], "/") == 0) res = (r != 0) ? l / r : 0;
            else res = (r != 0) ? l % r : 0;
            char tmp[64]; s_itoa(res, tmp); s_strcpy(tokens[j-1], tmp);
            for (int k = i + 2; k < tc; k++) s_strcpy(tokens[k-2], tokens[k]);
            tc -= 2; i--;
        }
    }
    int result = s_atoi(tokens[0]);
    for (int i = 1; i < tc; i += 2) {
        int v = s_atoi(tokens[i+1]);
        if (s_strcmp(tokens[i], "+") == 0) result += v;
        else result -= v;
    }
    return result;
}
static int parse_shx(const char* script, compiled_script_t* out) {
    out->main_count = 0;
    out->function_count = 0;
    out->import_count = 0;
    out->uses_bypass_time = 0;
    g_mc = 0;
    g_bd = 0;
    g_cfunc[0] = 0;
    for (int fi = 0; fi < MAX_FUNCS; fi++) g_fc[fi] = 0;
    g_inf = 0;
    char line[128];
    int li = 0, ci = 0;
    while (1) {
        char c = script[ci++];
        if (c == '\n' || c == 0) {
            line[li] = 0;
            li = 0;
            char* t = s_trim(line);
            if (t[0] == 0 || t[0] == '#') { if (c == 0) break; continue; }
            if (s_strcmp(t, "ENDLOOP") == 0 || s_strcmp(t, "ENDWHILE") == 0) {
                if (g_bd > 0) {
                    g_bd--;
                    if (g_inf) { int fi = out->function_count - 1; g_fb[fi][g_fc[fi]++] = g_block[g_bd]; }
                    else { g_main[g_mc++] = g_block[g_bd]; }
                }
                if (c == 0) break;
                continue;
            }
            if (s_strcmp(t, "ENDFUNCTION") == 0) { g_inf = 0; g_cfunc[0] = 0; if (c == 0) break; continue; }
            instruction_t inst;
            inst.op = OP_NOP;
            inst.value[0] = 0;
            inst.message[0] = 0;
            inst.body = 0;
            inst.body_count = 0;
            inst.condition = 0;
            inst.int_value = 0;
            inst.is_static = 1;
            inst.duration_ms = 0;
            inst.needs_iteration = 0;
            inst.is_single_print_loop = 0;
            if (s_starts(t, "PARALLEL LOOP ")) {
                inst.op = OP_PARALLEL_LOOP;
                char* n = s_trim(t + 14);
                inst.int_value = s_atoi(n);
                inst.needs_iteration = 1;
                if (s_strlen(n) > 0 && !s_isdigit(n[0])) { s_strcpy(inst.value, n); inst.is_static = 0; }
                g_block[g_bd++] = inst;
                if (c == 0) break;
                continue;
            }
            if (s_starts(t, "LOOP ")) {
                inst.op = OP_LOOP;
                char* n = s_trim(t + 5);
                inst.int_value = s_atoi(n);
                inst.needs_iteration = 1;
                if (s_strlen(n) > 0 && !s_isdigit(n[0])) { s_strcpy(inst.value, n); inst.is_static = 0; }
                g_block[g_bd++] = inst;
                if (c == 0) break;
                continue;
            }
            if (s_starts(t, "WHILE ")) {
                inst.op = OP_WHILE;
                inst.condition = parse_logic_expr(t + 6);
                g_block[g_bd++] = inst;
                if (c == 0) break;
                continue;
            }
            if (s_starts(t, "FUNCTION ")) {
                s_strcpy(g_cfunc, s_trim(t + 9));
                g_inf = 1;
                s_strcpy(out->functions[out->function_count], g_cfunc);
                out->function_counts[out->function_count] = 0;
                out->function_count++;
                if (c == 0) break;
                continue;
            }
            if (s_starts(t, "PRINT ")) {
                inst.op = OP_PRINT;
                char* m = s_trim(t + 6);
                if (m[0] == '"') { int l = s_strlen(m); if (l > 1 && m[l-1] == '"') { m[l-1] = 0; m++; } }
                s_strcpy(inst.message, m);
                for (int i = 0; inst.message[i]; i++) { if (inst.message[i] == '%') { inst.is_static = 0; break; } }
            }
            else if (s_starts(t, "SET ")) {
                char* r = s_trim(t + 4);
                char* sp = r;
                while (*sp && !s_isspace(*sp)) sp++;
                if (*sp) {
                    *sp = 0; s_strcpy(inst.value, r);
                    char* e = s_trim(sp + 1);
                    int hm = 0;
                    for (int i = 0; e[i]; i++) { if (is_math_op_char(e[i]) && i > 0) { hm = 1; break; } }
                    if (hm) { inst.op = OP_SET_EXPR; s_strcpy(inst.message, e); }
                    else { inst.op = OP_SET; s_strcpy(inst.message, e); }
                }
            }
            else if (s_starts(t, "INPUT ")) {
                inst.op = OP_INPUT;
                char* r = s_trim(t + 6);
                char* sp = r;
                while (*sp && !s_isspace(*sp)) sp++;
                if (*sp) {
                    *sp = 0; s_strcpy(inst.value, r);
                    char* p = s_trim(sp + 1);
                    if (p[0] == '"') { int l = s_strlen(p); if (l > 1 && p[l-1] == '"') { p[l-1] = 0; p++; } }
                    s_strcpy(inst.message, p);
                }
            }
            else if (s_starts(t, "IF ")) {
                char* r = s_trim(t + 3);
                char* cp = 0;
                for (int i = 0; r[i]; i++) {
                    if (s_strncmp(&r[i], " CALL ", 6) == 0) { cp = &r[i]; break; }
                    if (s_strncmp(&r[i], " CALL", 5) == 0 && r[i+5] == 0) { cp = &r[i]; break; }
                }
                if (cp) {
                    *cp = 0; inst.op = OP_IF_CALL;
                    inst.condition = parse_logic_expr(r);
                    s_strcpy(inst.message, s_trim(cp + 5));
                } else {
                    char* bp = 0;
                    for (int i = 0; r[i]; i++) { if (s_strncmp(&r[i], " BREAK", 6) == 0) { bp = &r[i]; break; } }
                    if (bp) {
                        *bp = 0; inst.op = OP_IF_BREAK;
                        inst.condition = parse_logic_expr(r);
                    } else {
                        char* pp = 0;
                        for (int i = 0; r[i]; i++) { if (s_strncmp(&r[i], " PRINT ", 7) == 0) { pp = &r[i]; break; } }
                        if (pp) {
                            *pp = 0; inst.op = OP_IF_PRINT;
                            inst.condition = parse_logic_expr(r);
                            char* m = s_trim(pp + 7);
                            if (m[0] == '"') { int l = s_strlen(m); if (l > 1 && m[l-1] == '"') { m[l-1] = 0; m++; } }
                            s_strcpy(inst.message, m);
                            for (int i = 0; inst.message[i]; i++) { if (inst.message[i] == '%') { inst.is_static = 0; break; } }
                        } else {
                            inst.op = OP_IF_COMPLEX;
                            inst.condition = parse_logic_expr(r);
                            g_block[g_bd++] = inst;
                            if (c == 0) break;
                            continue;
                        }
                    }
                }
            }
            else if (s_strcmp(t, "ELSE") == 0) { inst.op = OP_ELSE; }
            else if (s_starts(t, "CALL ")) { inst.op = OP_CALL; s_strcpy(inst.value, s_trim(t + 5)); }
            else if (s_starts(t, "SLEEP ")) { inst.op = OP_SLEEP; s_strcpy(inst.value, s_trim(t + 6)); }
            else if (s_strcmp(t, "BREAK") == 0) { inst.op = OP_BREAK; }
            else if (s_starts(t, "TIMER START")) { inst.op = OP_TIMER_START; s_strcpy(inst.value, s_trim(t + 11)); }
            else if (s_starts(t, "TIMER END")) { inst.op = OP_TIMER_END; s_strcpy(inst.value, s_trim(t + 9)); }
            else if (s_starts(t, "EXEC ")) {
                inst.op = OP_EXEC;
                char* m = s_trim(t + 5);
                if (m[0] == '"') { int l = s_strlen(m); if (l > 1 && m[l-1] == '"') { m[l-1] = 0; m++; } }
                s_strcpy(inst.message, m);
            }
            else if (s_starts(t, "READFILE ")) {
                inst.op = OP_READ_FILE;
                char* r = s_trim(t + 9);
                char* tp = s_strstr(r, " TO ");
                if (tp) { *tp = 0; s_strcpy(inst.value, s_trim(r)); s_strcpy(inst.message, s_trim(tp + 4)); }
            }
            else if (s_starts(t, "ARRAY ")) {
                inst.op = OP_TOKENIZE;
                char* r = s_trim(t + 6);
                char* ep = s_strstr(r, "=");
                if (ep) {
                    *ep = 0; s_strcpy(inst.value, s_trim(r));
                    char* rhs = s_trim(ep + 1);
                    if (s_starts(rhs, "TOKENIZE ")) {
                        char* tr = rhs + 9;
                        char* bp = s_strstr(tr, " BY ");
                        if (bp) {
                            *bp = 0; s_strcpy(inst.message, s_trim(tr));
                            int ml = s_strlen(inst.message);
                            inst.message[ml] = '|'; inst.message[ml+1] = 0;
                            s_strcpy(inst.message + ml + 1, s_trim(bp + 4));
                        }
                    }
                }
            }
            else if (s_starts(t, "ARRAYGET ")) {
                inst.op = OP_ARRAY_GET;
                char* r = s_trim(t + 9);
                char* sp = r;
                while (*sp && !s_isspace(*sp)) sp++;
                if (*sp) {
                    *sp = 0; s_strcpy(inst.value, s_trim(r));
                    char* r2 = s_trim(sp + 1);
                    char* tp = s_strstr(r2, " TO ");
                    if (tp) { *tp = 0; s_strcpy(inst.message, s_trim(r2));
                        int ml = s_strlen(inst.message); inst.message[ml] = '|'; inst.message[ml+1] = 0;
                        s_strcpy(inst.message + ml + 1, s_trim(tp + 4));
                    }
                }
            }
            else if (s_starts(t, "ARRAYSET ")) {
                inst.op = OP_ARRAY_SET;
                char* r = s_trim(t + 9);
                char* sp = r;
                while (*sp && !s_isspace(*sp)) sp++;
                if (*sp) {
                    *sp = 0; s_strcpy(inst.value, s_trim(r));
                    char* r2 = s_trim(sp + 1);
                    char* ep = s_strstr(r2, "=");
                    if (ep) { *ep = 0; s_strcpy(inst.message, s_trim(r2));
                        int ml = s_strlen(inst.message); inst.message[ml] = '|'; inst.message[ml+1] = 0;
                        s_strcpy(inst.message + ml + 1, s_trim(ep + 1));
                    }
                }
            }
            else if (s_starts(t, "ARRAYLEN ")) {
                inst.op = OP_ARRAY_LEN;
                char* r = s_trim(t + 9);
                char* tp = s_strstr(r, " TO ");
                if (tp) { *tp = 0; s_strcpy(inst.value, s_trim(r)); s_strcpy(inst.message, s_trim(tp + 4)); }
            }
            else if (s_starts(t, "INDEXOF ")) {
                inst.op = OP_INDEX_OF;
                char* r = s_trim(t + 8);
                char* sp = r;
                while (*sp && !s_isspace(*sp)) sp++;
                if (*sp) {
                    *sp = 0; s_strcpy(inst.value, s_trim(r));
                    char* r2 = s_trim(sp + 1);
                    char* tp = s_strstr(r2, " TO ");
                    if (tp) { *tp = 0; s_strcpy(inst.message, s_trim(r2));
                        int ml = s_strlen(inst.message); inst.message[ml] = '|'; inst.message[ml+1] = 0;
                        s_strcpy(inst.message + ml + 1, s_trim(tp + 4));
                    }
                }
            }
            else if (s_starts(t, "REPLACE ")) {
                inst.op = OP_REPLACE;
                char* r = s_trim(t + 8);
                char* sp = r;
                while (*sp && !s_isspace(*sp)) sp++;
                if (*sp) {
                    *sp = 0; s_strcpy(inst.value, s_trim(r));
                    char* r2 = s_trim(sp + 1);
                    char* sp2 = r2;
                    while (*sp2 && !s_isspace(*sp2)) sp2++;
                    if (*sp2) {
                        *sp2 = 0; s_strcpy(inst.message, s_trim(r2));
                        int ml = s_strlen(inst.message); inst.message[ml] = '|'; inst.message[ml+1] = 0;
                        char* r3 = s_trim(sp2 + 1);
                        char* tp = s_strstr(r3, " TO ");
                        if (tp) { *tp = 0; s_strcpy(inst.message + ml + 1, s_trim(r3));
                            int ml2 = s_strlen(inst.message); inst.message[ml2] = '|'; inst.message[ml2+1] = 0;
                            s_strcpy(inst.message + ml2 + 1, s_trim(tp + 4));
                        }
                    }
                }
            }
            else if (s_starts(t, "SUBSTRING ")) {
                inst.op = OP_SUBSTRING;
                char* r = s_trim(t + 10);
                char* sp = r;
                while (*sp && !s_isspace(*sp)) sp++;
                if (*sp) {
                    *sp = 0; s_strcpy(inst.value, s_trim(r));
                    char* r2 = s_trim(sp + 1);
                    char* sp2 = r2;
                    while (*sp2 && !s_isspace(*sp2)) sp2++;
                    if (*sp2) {
                        *sp2 = 0; s_strcpy(inst.message, s_trim(r2));
                        int ml = s_strlen(inst.message); inst.message[ml] = '|'; inst.message[ml+1] = 0;
                        char* r3 = s_trim(sp2 + 1);
                        char* tp = s_strstr(r3, " TO ");
                        if (tp) { *tp = 0; s_strcpy(inst.message + ml + 1, s_trim(r3));
                            int ml2 = s_strlen(inst.message); inst.message[ml2] = '|'; inst.message[ml2+1] = 0;
                            s_strcpy(inst.message + ml2 + 1, s_trim(tp + 4));
                        }
                    }
                }
            }
            else if (s_starts(t, "LISTFILES ")) {
                inst.op = OP_LIST_FILES;
                char* r = s_trim(t + 10);
                char* tp = s_strstr(r, " TO ");
                if (tp) { *tp = 0; s_strcpy(inst.value, s_trim(r)); s_strcpy(inst.message, s_trim(tp + 4)); }
            }
            else if (s_starts(t, "FILEEXISTS ")) {
                inst.op = OP_FILE_EXISTS;
                char* r = s_trim(t + 11);
                char* tp = s_strstr(r, " TO ");
                if (tp) { *tp = 0; s_strcpy(inst.value, s_trim(r)); s_strcpy(inst.message, s_trim(tp + 4)); }
            }
            else if (s_starts(t, "GETENV ")) {
                inst.op = OP_GET_ENV;
                char* r = s_trim(t + 7);
                char* tp = s_strstr(r, " TO ");
                if (tp) { *tp = 0; s_strcpy(inst.value, s_trim(r)); s_strcpy(inst.message, s_trim(tp + 4)); }
            }
            else if (s_starts(t, "GETHARDWARE ")) { inst.op = OP_GET_HARDWARE; s_strcpy(inst.message, s_trim(t + 12)); }
            else if (s_starts(t, "TIME ")) { inst.op = OP_TIME; s_strcpy(inst.value, s_trim(t + 5)); }
            else if (s_starts(t, "LOG ")) {
                inst.op = OP_LOG;
                char* m = s_trim(t + 4);
                if (m[0] == '"') { int l = s_strlen(m); if (l > 1 && m[l-1] == '"') { m[l-1] = 0; m++; } }
                s_strcpy(inst.message, m);
            }
            else if (s_starts(t, "INCREMENT ")) { inst.op = OP_INCREMENT; s_strcpy(inst.value, s_trim(t + 10)); }
            else if (s_starts(t, "BLOCK ")) { inst.op = OP_BLOCK; s_strcpy(inst.value, s_trim(t + 6)); }
            else if (s_starts(t, "NUKE ")) { inst.op = OP_NUKE; s_strcpy(inst.value, s_trim(t + 5)); }
            else if (s_starts(t, "JSONEXTRACT ")) {
                inst.op = OP_JSON_EXTRACT;
                char* r = s_trim(t + 12);
                char* sp = r;
                while (*sp && !s_isspace(*sp)) sp++;
                if (*sp) {
                    *sp = 0; s_strcpy(inst.value, s_trim(r));
                    char* r2 = s_trim(sp + 1);
                    char* tp = s_strstr(r2, " TO ");
                    if (tp) { *tp = 0; s_strcpy(inst.message, s_trim(r2));
                        int ml = s_strlen(inst.message); inst.message[ml] = '|'; inst.message[ml+1] = 0;
                        s_strcpy(inst.message + ml + 1, s_trim(tp + 4));
                    }
                }
            }
            else if (s_starts(t, "FETCH ")) {
                inst.op = OP_FETCH;
                char* r = s_trim(t + 6);
                char* tp = s_strstr(r, " TO ");
                if (tp) { *tp = 0; s_strcpy(inst.value, s_trim(r)); s_strcpy(inst.message, s_trim(tp + 4)); }
            }
            else if (s_starts(t, "SEARCH ")) {
                inst.op = OP_SEARCH;
                char* r = s_trim(t + 7);
                char* fp = s_strstr(r, " FOR ");
                if (fp) {
                    *fp = 0;
                    char* r2 = s_trim(fp + 5);
                    char* tp = s_strstr(r2, " TO ");
                    if (tp) { *tp = 0; s_strcpy(inst.message, s_trim(r));
                        int ml = s_strlen(inst.message); inst.message[ml] = '|'; inst.message[ml+1] = 0;
                        s_strcpy(inst.message + ml + 1, s_trim(r2));
                        s_strcpy(inst.value, s_trim(tp + 4));
                    }
                }
            }
            else if (s_starts(t, "DISCORD CONNECT ")) { inst.op = OP_DISCORD_CONNECT; s_strcpy(inst.value, s_trim(t + 16)); }
            else if (s_starts(t, "DISCORD LIMIT ")) { inst.op = OP_DISCORD_LIMIT; s_strcpy(inst.value, s_trim(t + 14)); }
            else if (s_starts(t, "SERVE ")) { inst.op = OP_SERVE; s_strcpy(inst.value, s_trim(t + 6)); }
            else if (s_starts(t, "MATHLOOP ")) {
                inst.op = OP_MATH_LOOP;
                char* r = s_trim(t + 9);
                char* sp = r;
                while (*sp && !s_isspace(*sp)) sp++;
                if (*sp) {
                    *sp = 0; s_strcpy(inst.value, s_trim(r));
                    char* r2 = s_trim(sp + 1);
                    char* sp2 = r2;
                    while (*sp2 && !s_isspace(*sp2)) sp2++;
                    if (*sp2) { *sp2 = 0; inst.int_value = s_atoi(s_trim(r2)); s_strcpy(inst.message, s_trim(sp2 + 1)); }
                }
            }
            else if (s_starts(t, "SYSWRITE ")) { inst.op = OP_SYS_WRITE; s_strcpy(inst.message, s_trim(t + 9)); }
            else if (s_starts(t, "SYSREAD ")) {
                inst.op = OP_SYS_READ_FILE;
                char* r = s_trim(t + 8);
                char* tp = s_strstr(r, " TO ");
                if (tp) { *tp = 0; s_strcpy(inst.value, s_trim(r)); s_strcpy(inst.message, s_trim(tp + 4)); }
            }
            else if (s_starts(t, "SYSEXIT ")) { inst.op = OP_SYS_EXIT; s_strcpy(inst.value, s_trim(t + 8)); }
            else if (s_strcmp(t, "SYSYIELD") == 0) { inst.op = OP_SYS_YIELD; }
            else if (s_starts(t, "USE ")) { inst.op = OP_USE; s_strcpy(inst.value, s_trim(t + 4)); }
            if (inst.op != OP_NOP) {
                if (g_bd > 0) {
                    g_block[g_bd - 1].body_count++;
                } else if (g_inf) {
                    int fi = out->function_count - 1;
                    g_fb[fi][g_fc[fi]++] = inst;
                } else {
                    g_main[g_mc++] = inst;
                }
            }
            if (c == 0) break;
        } else if (li < 127) {
            line[li++] = c;
        }
    }
    out->main = g_main;
    out->main_count = g_mc;
    for (int i = 0; i < out->function_count; i++) {
        out->function_bodies[i] = g_fb[i];
        out->function_counts[i] = g_fc[i];
    }
    return 0;
}
#define MAX_VARS 128
#define MAX_ARRAYS 32
#define MAX_ARRAY_ITEMS 32
#define MAX_REGS 1024
#define MAX_TIMERS 16
#define MAX_STR 256
typedef struct {
    char name[64];
    char val[MAX_STR];
} var_t;
typedef struct {
    char name[64];
    char items[MAX_ARRAY_ITEMS][MAX_STR];
    int count;
} array_t;
typedef struct {
    char name[64];
    int start_tick;
} timer_t;
static struct {
    char filename[64];
    var_t vars[MAX_VARS];
    int var_count;
    array_t arrays[MAX_ARRAYS];
    int array_count;
    char regs[MAX_REGS][MAX_STR];
    int reg_count;
    char reg_map[128][64];
    int reg_map_count;
    timer_t timers[MAX_TIMERS];
    int timer_count;
    int has_bg;
    int uses_bypass;
    int num_workers;
    int tick;
} engine;
static int find_var(const char* name) {
    for (int i = 0; i < engine.var_count; i++)
        if (s_strcmp(engine.vars[i].name, name) == 0) return i;
    return -1;
}
static void set_var(const char* name, const char* val) {
    int i = find_var(name);
    if (i >= 0) s_strcpy(engine.vars[i].val, val);
    else {
        if (engine.var_count < MAX_VARS) {
            s_strcpy(engine.vars[engine.var_count].name, name);
            s_strcpy(engine.vars[engine.var_count].val, val);
            engine.var_count++;
        }
    }
}
static const char* get_var(const char* name) {
    int i = find_var(name);
    if (i >= 0) return engine.vars[i].val;
    return "";
}
static int get_reg_id(const char* name) {
    for (int i = 0; i < engine.reg_map_count; i++)
        if (s_strcmp(engine.reg_map[i], name) == 0) return i;
    if (engine.reg_map_count < 128) {
        s_strcpy(engine.reg_map[engine.reg_map_count], name);
        engine.reg_map_count++;
        return engine.reg_map_count - 1;
    }
    return 0;
}
static void set_reg(int id, const char* val) {
    if (id >= 0 && id < MAX_REGS) s_strcpy(engine.regs[id], val);
}
static const char* get_reg(int id) {
    if (id >= 0 && id < MAX_REGS) return engine.regs[id];
    return "";
}
static int find_array(const char* name) {
    for (int i = 0; i < engine.array_count; i++)
        if (s_strcmp(engine.arrays[i].name, name) == 0) return i;
    return -1;
}
static int find_timer(const char* name) {
    for (int i = 0; i < engine.timer_count; i++)
        if (s_strcmp(engine.timers[i].name, name) == 0) return i;
    return -1;
}
static void convert_colors(const char* in, char* out) {
    while (*in) {
        if (*in == '&' && in[1]) {
            in++;
            switch (*in) {
                case '0': s_strcpy(out, "\x1b[30m"); out += 5; break;
                case '1': s_strcpy(out, "\x1b[34m"); out += 5; break;
                case '2': s_strcpy(out, "\x1b[32m"); out += 5; break;
                case '3': s_strcpy(out, "\x1b[36m"); out += 5; break;
                case '4': s_strcpy(out, "\x1b[31m"); out += 5; break;
                case '5': s_strcpy(out, "\x1b[35m"); out += 5; break;
                case '6': s_strcpy(out, "\x1b[33m"); out += 5; break;
                case '7': s_strcpy(out, "\x1b[37m"); out += 5; break;
                case '8': s_strcpy(out, "\x1b[90m"); out += 5; break;
                case '9': s_strcpy(out, "\x1b[94m"); out += 5; break;
                case 'a': s_strcpy(out, "\x1b[92m"); out += 5; break;
                case 'b': s_strcpy(out, "\x1b[96m"); out += 5; break;
                case 'c': s_strcpy(out, "\x1b[91m"); out += 5; break;
                case 'd': s_strcpy(out, "\x1b[95m"); out += 5; break;
                case 'e': s_strcpy(out, "\x1b[93m"); out += 5; break;
                case 'f': s_strcpy(out, "\x1b[97m"); out += 5; break;
                case 'l': s_strcpy(out, "\x1b[1m"); out += 5; break;
                case 'm': s_strcpy(out, "\x1b[9m"); out += 5; break;
                case 'n': s_strcpy(out, "\x1b[4m"); out += 5; break;
                case 'o': s_strcpy(out, "\x1b[3m"); out += 5; break;
                case 'r': s_strcpy(out, "\x1b[0m"); out += 5; break;
                default: *out++ = '&'; *out++ = *in; break;
            }
            in++;
        } else {
            *out++ = *in++;
        }
    }
    *out = 0;
}
static void expand_vars(const char* input, char* out, int iteration, int core) {
    char result[MAX_STR * 2];
    int ri = 0;
    for (int i = 0; input[i] && ri < MAX_STR * 2 - 1; i++) {
        if (input[i] == '%') {
            int j = i + 1;
            while (input[j] && input[j] != '%') j++;
            if (input[j] == '%') {
                char varname[64];
                int vn = 0;
                for (int k = i + 1; k < j && vn < 63; k++) varname[vn++] = input[k];
                varname[vn] = 0;
                const char* val = "";
                if (s_strcmp(varname, "ITER") == 0) {
                    static char tmp[16]; s_itoa(iteration, tmp); val = tmp;
                } else if (s_strcmp(varname, "CORE") == 0) {
                    static char tmp[16]; s_itoa(core, tmp); val = tmp;
                } else if (s_strcmp(varname, "BYPASS_TIME") == 0) {
                    val = get_reg(get_reg_id("BYPASS_TIME"));
                    if (val[0] == 0) val = "0";
                } else {
                    val = get_var(varname);
                    if (val[0] == 0) {
                        int rid = get_reg_id(varname);
                        val = get_reg(rid);
                    }
                }
                while (*val && ri < MAX_STR * 2 - 1) result[ri++] = *val++;
                i = j;
            } else {
                result[ri++] = input[i];
            }
        } else {
            result[ri++] = input[i];
        }
    }
    result[ri] = 0;
    convert_colors(result, out);
}
static int eval_logic(logic_expr_t* expr, int iteration, int core) {
    if (!expr) return 0;
    switch (expr->op) {
        case LOG_OR: return eval_logic(expr->left, iteration, core) || eval_logic(expr->right, iteration, core);
        case LOG_AND: return eval_logic(expr->left, iteration, core) && eval_logic(expr->right, iteration, core);
        case LOG_LT: case LOG_GT: case LOG_EQ: case LOG_NE: {
            char ls[MAX_STR], rs[MAX_STR];
            expand_vars(expr->left->value, ls, iteration, core);
            expand_vars(expr->right->value, rs, iteration, core);
            int li = s_atoi(ls), ri = s_atoi(rs);
            switch (expr->op) {
                case LOG_LT: return li < ri;
                case LOG_GT: return li > ri;
                case LOG_EQ: return li == ri;
                case LOG_NE: return li != ri;
                default: return 0;
            }
        }
        case LOG_CONTAINS: {
            char ls[MAX_STR], rs[MAX_STR];
            expand_vars(expr->left->value, ls, iteration, core);
            expand_vars(expr->right->value, rs, iteration, core);
            return s_strstr(ls, rs) != 0;
        }
        case LOG_MALICIOUS: return 0;
        case LOG_PROTO: return 0;
        case LOG_VAR: { const char* v = get_var(expr->value); return s_strcmp(v, "true") == 0 || s_strcmp(v, "1") == 0; }
        case LOG_CONST: return s_strcmp(expr->value, "true") == 0 || s_strcmp(expr->value, "1") == 0;
        default: return 0;
    }
}
static int execute_insts(instruction_t* insts, int count, int iteration, int core);
static int handle_inst(instruction_t* ins, int iteration, int core, int* last_if) {
    char expanded[MAX_STR * 2];
    switch (ins->op) {
        case OP_PRINT: {
            expand_vars(ins->message, expanded, iteration, core);
            terminal_writestring("[shs] ");
            terminal_writestring(expanded);
            terminal_writestring("\n");
            return 0;
        }
        case OP_SET: {
            expand_vars(ins->message, expanded, iteration, core);
            set_var(ins->value, expanded);
            return 0;
        }
        case OP_SET_EXPR: {
            expand_vars(ins->message, expanded, iteration, core);
            int result = eval_math_int(expanded);
            char tmp[64]; s_itoa(result, tmp);
            set_var(ins->value, tmp);
            return 0;
        }
        case OP_INCREMENT: {
            int v = s_atoi(get_var(ins->value)) + 1;
            char tmp[16]; s_itoa(v, tmp);
            set_var(ins->value, tmp);
            return 0;
        }
        case OP_INPUT: {
            expand_vars(ins->message, expanded, iteration, core);
            terminal_writestring(expanded);
            char input_buf[256];
            int ipos = 0;
            while (1) {
                char c = keyboard_getchar();
                if (c == '\n' || c == '\r') break;
                if (ipos < 255) input_buf[ipos++] = c;
            }
            input_buf[ipos] = 0;
            set_var(ins->value, input_buf);
            return 0;
        }
        case OP_LOOP: {
            int count = ins->int_value;
            if (!ins->is_static) {
                expand_vars(ins->value, expanded, iteration, core);
                count = s_atoi(expanded);
            }
            for (int i = 0; i < count; i++)
                if (execute_insts(ins->body, ins->body_count, i, core)) return 1;
            return 0;
        }
        case OP_PARALLEL_LOOP: {
            int count = ins->int_value;
            if (!ins->is_static) {
                expand_vars(ins->value, expanded, iteration, core);
                count = s_atoi(expanded);
            }
            for (int i = 0; i < count; i++)
                if (execute_insts(ins->body, ins->body_count, i, i % 4 + 1)) return 1;
            terminal_writestring("[shs] PARALLEL LOOP COMPLETE: ");
            char tmp[16]; s_itoa(count, tmp);
            terminal_writestring(tmp);
            terminal_writestring(" iterations\n");
            return 0;
        }
        case OP_WHILE: {
            while (eval_logic(ins->condition, iteration, core)) {
                if (execute_insts(ins->body, ins->body_count, iteration, core)) break;
            }
            return 0;
        }
        case OP_IF_COMPLEX: {
            if (eval_logic(ins->condition, iteration, core)) {
                *last_if = 1;
                return execute_insts(ins->body, ins->body_count, iteration, core);
            }
            *last_if = 0;
            return 0;
        }
        case OP_IF_PRINT: {
            if (eval_logic(ins->condition, iteration, core)) {
                *last_if = 1;
                expand_vars(ins->message, expanded, iteration, core);
                terminal_writestring("[shs] ");
                terminal_writestring(expanded);
                terminal_writestring("\n");
            } else *last_if = 0;
            return 0;
        }
        case OP_IF_CALL: {
            if (eval_logic(ins->condition, iteration, core)) *last_if = 1;
            else *last_if = 0;
            return 0;
        }
        case OP_IF_BREAK: {
            if (eval_logic(ins->condition, iteration, core)) { *last_if = 1; return 1; }
            *last_if = 0;
            return 0;
        }
        case OP_ELSE: {
            if (!*last_if) {
                if (s_strcmp(ins->value, "ELSE_PRINT") == 0) {
                    expand_vars(ins->message, expanded, iteration, core);
                    terminal_writestring("[shs] ");
                    terminal_writestring(expanded);
                    terminal_writestring("\n");
                }
            }
            return 0;
        }
        case OP_CALL: {
            terminal_writestring("[shs] CALL ");
            terminal_writestring(ins->value);
            terminal_writestring("\n");
            return 0;
        }
        case OP_BREAK: return 1;
        case OP_TIMER_START: {
            int i = find_timer(ins->value);
            if (i < 0 && engine.timer_count < MAX_TIMERS) {
                s_strcpy(engine.timers[engine.timer_count].name, ins->value);
                engine.timers[engine.timer_count].start_tick = engine.tick;
                engine.timer_count++;
            } else if (i >= 0) engine.timers[i].start_tick = engine.tick;
            return 0;
        }
        case OP_TIMER_END: {
            int i = find_timer(ins->value);
            if (i >= 0) {
                int elapsed = engine.tick - engine.timers[i].start_tick;
                if (elapsed < 0) elapsed = 0;
                char tmp[16]; s_itoa(elapsed, tmp);
                set_var(ins->value, tmp);
            }
            return 0;
        }
        case OP_SLEEP: {
            expand_vars(ins->value, expanded, iteration, core);
            int ms = s_atoi(expanded);
            for (volatile int i = 0; i < ms * 10000; i++);
            return 0;
        }
        case OP_TIME: {
            char tmp[16]; s_itoa(engine.tick, tmp);
            set_var(ins->value, tmp);
            return 0;
        }
        case OP_LOG: {
            expand_vars(ins->message, expanded, iteration, core);
            terminal_writestring("[shs LOG] ");
            terminal_writestring(expanded);
            terminal_writestring("\n");
            return 0;
        }
        case OP_READ_FILE: {
            expand_vars(ins->value, expanded, iteration, core);
            struct fs_node* f = search_path(expanded);
            if (f && f->type == FS_FILE) {
                set_var(ins->message, f->content);
            } else {
                set_var(ins->message, "ERR:FILE_NOT_FOUND");
            }
            return 0;
        }
        case OP_EXEC: {
            expand_vars(ins->message, expanded, iteration, core);
            terminal_writestring("[shs EXEC] ");
            terminal_writestring(expanded);
            terminal_writestring("\n");
            return 0;
        }
        case OP_BLOCK: case OP_NUKE: {
            terminal_writestring("[shs] Process blocked/nuked\n");
            return 0;
        }
        case OP_TOKENIZE: {
            const char* src = get_var(ins->value);
            char delim[8] = " ";
            char msg_copy[MAX_STR];
            s_strcpy(msg_copy, ins->message);
            char* pipe = s_strstr(msg_copy, "|");
            if (pipe) { *pipe = 0; s_strcpy(delim, pipe + 1); }
            int ai = find_array(ins->value);
            if (ai < 0 && engine.array_count < MAX_ARRAYS) {
                ai = engine.array_count;
                s_strcpy(engine.arrays[ai].name, ins->value);
                engine.arrays[ai].count = 0;
                engine.array_count++;
            }
            if (ai >= 0) {
                engine.arrays[ai].count = 0;
                char buf[MAX_STR]; s_strcpy(buf, src);
                char* tok = buf;
                while (*tok) {
                    char* end = s_strstr(tok, delim);
                    if (!end) end = tok + s_strlen(tok);
                    char old = *end; *end = 0;
                    if (engine.arrays[ai].count < MAX_ARRAY_ITEMS)
                        s_strcpy(engine.arrays[ai].items[engine.arrays[ai].count++], tok);
                    if (old == 0) break;
                    *end = old; tok = end + s_strlen(delim);
                }
            }
            return 0;
        }
        case OP_ARRAY_GET: {
            int ai = find_array(ins->value);
            if (ai >= 0) {
                char msg_copy[MAX_STR]; s_strcpy(msg_copy, ins->message);
                char* pipe = s_strstr(msg_copy, "|");
                if (pipe) {
                    *pipe = 0;
                    int idx = s_atoi(msg_copy);
                    if (idx >= 0 && idx < engine.arrays[ai].count)
                        set_var(pipe + 1, engine.arrays[ai].items[idx]);
                }
            }
            return 0;
        }
        case OP_ARRAY_SET: {
            int ai = find_array(ins->value);
            if (ai < 0 && engine.array_count < MAX_ARRAYS) {
                ai = engine.array_count;
                s_strcpy(engine.arrays[ai].name, ins->value);
                engine.arrays[ai].count = 0;
                engine.array_count++;
            }
            if (ai >= 0) {
                char msg_copy[MAX_STR]; s_strcpy(msg_copy, ins->message);
                char* pipe = s_strstr(msg_copy, "|");
                if (pipe) {
                    *pipe = 0;
                    int idx = s_atoi(msg_copy);
                    if (idx >= 0 && idx < MAX_ARRAY_ITEMS) {
                        expand_vars(pipe + 1, expanded, iteration, core);
                        s_strcpy(engine.arrays[ai].items[idx], expanded);
                        if (idx >= engine.arrays[ai].count) engine.arrays[ai].count = idx + 1;
                    }
                }
            }
            return 0;
        }
        case OP_ARRAY_LEN: {
            int ai = find_array(ins->value);
            char tmp[16];
            s_itoa(ai >= 0 ? engine.arrays[ai].count : 0, tmp);
            set_var(ins->message, tmp);
            return 0;
        }
        case OP_INDEX_OF: {
            expand_vars(ins->value, expanded, iteration, core);
            char msg_copy[MAX_STR]; s_strcpy(msg_copy, ins->message);
            char* pipe = s_strstr(msg_copy, "|");
            if (pipe) {
                *pipe = 0;
                char* pos = s_strstr(expanded, msg_copy);
                char tmp[16];
                s_itoa(pos ? (int)(pos - expanded) : -1, tmp);
                set_var(pipe + 1, tmp);
            }
            return 0;
        }
        case OP_REPLACE: {
            expand_vars(ins->value, expanded, iteration, core);
            char msg_copy[MAX_STR]; s_strcpy(msg_copy, ins->message);
            char* p1 = s_strstr(msg_copy, "|");
            if (p1) {
                *p1 = 0; char* p2 = s_strstr(p1 + 1, "|");
                if (p2) {
                    *p2 = 0;
                    char* search = msg_copy, *replace = p1 + 1, *target = p2 + 1;
                    char result[MAX_STR * 2]; int ri = 0;
                    char* pos = expanded;
                    while (*pos) {
                        if (s_strncmp(pos, search, s_strlen(search)) == 0) {
                            for (int k = 0; replace[k] && ri < MAX_STR * 2 - 1; k++) result[ri++] = replace[k];
                            pos += s_strlen(search);
                        } else result[ri++] = *pos++;
                    }
                    result[ri] = 0;
                    set_var(target, result);
                }
            }
            return 0;
        }
        case OP_SUBSTRING: {
            expand_vars(ins->value, expanded, iteration, core);
            char msg_copy[MAX_STR]; s_strcpy(msg_copy, ins->message);
            char* p1 = s_strstr(msg_copy, "|");
            if (p1) {
                *p1 = 0; char* p2 = s_strstr(p1 + 1, "|");
                if (p2) {
                    *p2 = 0;
                    int start = s_atoi(msg_copy), len = s_atoi(p1 + 1);
                    char* target = p2 + 1;
                    char result[MAX_STR]; int ri = 0;
                    int slen = s_strlen(expanded);
                    if (start < 0) start = 0;
                    if (start > slen) start = slen;
                    for (int i = start; i < start + len && i < slen && ri < MAX_STR - 1; i++)
                        result[ri++] = expanded[i];
                    result[ri] = 0;
                    set_var(target, result);
                }
            }
            return 0;
        }
        case OP_LIST_FILES: {
            expand_vars(ins->value, expanded, iteration, core);
            terminal_writestring("[shs] LISTFILES ");
            terminal_writestring(expanded);
            terminal_writestring("\n");
            return 0;
        }
        case OP_FILE_EXISTS: { set_var(ins->message, "false"); return 0; }
        case OP_GET_ENV: { set_var(ins->message, ""); return 0; }
        case OP_GET_HARDWARE: { set_var(ins->message, "{\"os\":\"SharkOS\",\"arch\":\"x86\",\"cpus\":1}"); return 0; }
        case OP_JSON_EXTRACT: {
            expand_vars(ins->value, expanded, iteration, core);
            char msg_copy[MAX_STR]; s_strcpy(msg_copy, ins->message);
            char* pipe = s_strstr(msg_copy, "|");
            if (pipe) {
                *pipe = 0;
                char search_key[64];
                s_strcpy(search_key, "\""); s_strcpy(search_key + 1, msg_copy);
                int sl = s_strlen(search_key);
                search_key[sl] = '"'; search_key[sl+1] = ':'; search_key[sl+2] = 0;
                char* pos = s_strstr(expanded, search_key);
                if (pos) {
                    pos += s_strlen(search_key);
                    while (*pos == ' ' || *pos == '"') pos++;
                    char val[MAX_STR]; int vi = 0;
                    while (*pos && *pos != '"' && *pos != ',' && *pos != '}' && vi < MAX_STR - 1)
                        val[vi++] = *pos++;
                    val[vi] = 0;
                    set_var(pipe + 1, val);
                }
            }
            return 0;
        }
        case OP_FETCH: {
            expand_vars(ins->value, expanded, iteration, core);
            terminal_writestring("[shs FETCH] ");
            terminal_writestring(expanded);
            terminal_writestring("\n");
            set_var(ins->message, "{}");
            return 0;
        }
        case OP_SEARCH: {
            terminal_writestring("[shs SEARCH] Searching...\n");
            set_var(ins->value, "0");
            return 0;
        }
        case OP_DISCORD_CONNECT: { terminal_writestring("[shs] Discord not available in kernel\n"); return 0; }
        case OP_DISCORD_LIMIT: return 0;
        case OP_SERVE: { terminal_writestring("[shs] HTTP server not available in kernel\n"); return 0; }
        case OP_MATH_LOOP: {
            int reg_id = get_reg_id(ins->value);
            int iterations = ins->int_value;
            const char* expr = ins->message;
            int a = 1, b = 0, m = 1;
            if (s_strstr(expr, "*") && s_strstr(expr, "+") && s_strstr(expr, "%")) {
                char tmp[MAX_STR]; s_strcpy(tmp, expr);
                char* parts[8]; int pc = 0;
                char* tok = tmp;
                while (*tok) {
                    while (s_isspace(*tok)) tok++;
                    if (*tok == 0) break;
                    parts[pc++] = tok;
                    while (*tok && !s_isspace(*tok)) tok++;
                    if (*tok) *tok++ = 0;
                }
                if (pc >= 7) { a = s_atoi(parts[2]); b = s_atoi(parts[4]); m = s_atoi(parts[6]); }
            }
            int start_val = s_atoi(get_reg(reg_id));
            if (start_val == 0) start_val = s_atoi(get_var(ins->value));
            int pow_mod(int base, int exp, int mod) {
                int res = 1; base %= mod;
                while (exp > 0) {
                    if (exp & 1) res = (res * base) % mod;
                    base = (base * base) % mod;
                    exp >>= 1;
                }
                return res;
            }
            int sum_pow_mod(int base, int k, int mod) {
                if (k == 0) return 0;
                if (k == 1) return 1;
                if (k % 2 == 0) {
                    int half = sum_pow_mod(base, k/2, mod);
                    return (half * (1 + pow_mod(base, k/2, mod))) % mod;
                }
                return (1 + base * sum_pow_mod(base, k-1, mod)) % mod;
            }
            int final_val = (pow_mod(a, iterations, m) * start_val + b * sum_pow_mod(a, iterations, m)) % m;
            char tmp[16]; s_itoa(final_val, tmp);
            set_reg(reg_id, tmp);
            set_var(ins->value, tmp);
            terminal_writestring("[shs] MATH OPTIMIZER: ");
            s_itoa(iterations, tmp); terminal_writestring(tmp);
            terminal_writestring(" iterations processed\n");
            return 0;
        }
        case OP_SYS_WRITE: {
            expand_vars(ins->message, expanded, iteration, core);
            terminal_writestring(expanded);
            return 0;
        }
        case OP_SYS_READ_FILE: {
            expand_vars(ins->value, expanded, iteration, core);
            struct fs_node* f = search_path(expanded);
            if (f && f->type == FS_FILE) {
                set_var(ins->message, f->content);
            }
            return 0;
        }
        case OP_SYS_EXIT: { return 1; }
        case OP_SYS_YIELD: { return 0; }
        case OP_USE: {
            terminal_writestring("[shs] USE ");
            terminal_writestring(ins->value);
            terminal_writestring("\n");
            return 0;
        }
        default: return 0;
    }
}
static int execute_insts(instruction_t* insts, int count, int iteration, int core) {
    int last_if = 0;
    for (int i = 0; i < count; i++) {
        engine.tick++;
        if (handle_inst(&insts[i], iteration, core, &last_if)) return 1;
    }
    return 0;
}
void shs_init_engine(void) {
    s_strcpy(engine.filename, "shs");
    engine.var_count = 0;
    engine.array_count = 0;
    engine.reg_count = 0;
    engine.reg_map_count = 0;
    engine.timer_count = 0;
    engine.has_bg = 0;
    engine.uses_bypass = 0;
    engine.num_workers = 1;
    engine.tick = 0;
}
void shs_run_script(const char* script_content, const char* filename) {
    compiled_script_t script;
    if (parse_shx(script_content, &script) != 0) {
        terminal_writestring("[shs] Failed to parse script\n");
        return;
    }
    s_strcpy(engine.filename, filename);
    terminal_writestring("[shs] Running script: ");
    terminal_writestring(filename);
    terminal_writestring("\n");
    execute_insts(script.main, script.main_count, 0, 0);
}
void shs_run_file(const char* path) {
    struct fs_node* f = search_path(path);
    if (!f || f->type != FS_FILE) {
        terminal_writestring("[shs] Script not found: ");
        terminal_writestring(path);
        terminal_writestring("\n");
        return;
    }
    shs_run_script(f->content, path);
}