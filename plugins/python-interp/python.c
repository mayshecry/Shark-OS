
#include "sharkapi.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

extern void int_to_string(uint32_t value, char* buffer);

#define isdigit(c) ((c) >= '0' && (c) <= '9')
#define isalpha(c) (((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z'))
#define isspace(c) ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\r')
#define isalnum(c) (isalpha(c) || isdigit(c))

static int simple_atoi(const char* str) {
    int result = 0;
    int sign = 1;

    if (*str == '-') {
        sign = -1;
        str++;
    }

    while (*str && isdigit(*str)) {
        result = result * 10 + (*str - '0');
        str++;
    }

    return result * sign;
}

#define MAX_VARS 32
#define MAX_VAR_NAME 32
#define MAX_VAR_VALUE 256
#define MAX_TOKENS 128
#define MAX_TOKEN_LEN 64

typedef struct {
    char name[MAX_VAR_NAME];
    char value[MAX_VAR_VALUE];
} py_var_t;

typedef struct {
    py_var_t vars[MAX_VARS];
    int var_count;
} py_interpreter_t;

static py_interpreter_t g_py_interp = {0};

plugin_info_t plugin_info = {
    .version = SHARKAPI_VERSION,
    .name = "Python Interpreter",
    .author = "mayshecry",
    .description = "A simple Python interpreter for SharkOS",
    .major = 1,
    .minor = 0
};

typedef enum {
    TOKEN_UNKNOWN,
    TOKEN_NUMBER,
    TOKEN_STRING,
    TOKEN_IDENTIFIER,
    TOKEN_ASSIGN,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_MULT,
    TOKEN_DIV,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_COMMA,
    TOKEN_PRINT,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_FOR,
    TOKEN_IN,
    TOKEN_DEF,
    TOKEN_RANGE,
    TOKEN_EOF
} token_type_t;

typedef struct {
    token_type_t type;
    char value[MAX_TOKEN_LEN];
} token_t;

static int tokenize(const char* code, token_t* tokens, int max_tokens) {
    int token_count = 0;
    const char* p = code;

    while (*p && token_count < max_tokens) {

        while (*p && isspace(*p)) p++;
        if (!*p) break;

        if (*p == '"' || *p == '\'') {
            char quote = *p;
            char str_val[MAX_TOKEN_LEN] = {0};
            int len = 0;
            p++;
            while (*p && *p != quote && len < MAX_TOKEN_LEN - 1) {
                str_val[len++] = *p++;
            }
            if (*p == quote) p++;

            tokens[token_count].type = TOKEN_STRING;
            strcpy(tokens[token_count].value, str_val);
            token_count++;
            continue;
        }

        if (isdigit(*p)) {
            char num_str[MAX_TOKEN_LEN] = {0};
            int len = 0;
            while (isdigit(*p) && len < MAX_TOKEN_LEN - 1) {
                num_str[len++] = *p++;
            }
            num_str[len] = '\0';

            tokens[token_count].type = TOKEN_NUMBER;
            strcpy(tokens[token_count].value, num_str);
            token_count++;
            continue;
        }

        if (isalpha(*p) || *p == '_') {
            char ident[MAX_TOKEN_LEN] = {0};
            int len = 0;
            while ((isalnum(*p) || *p == '_') && len < MAX_TOKEN_LEN - 1) {
                ident[len++] = *p++;
            }
            ident[len] = '\0';

            tokens[token_count].type = TOKEN_IDENTIFIER;

            if (strcmp(ident, "print") == 0) {
                tokens[token_count].type = TOKEN_PRINT;
            } else if (strcmp(ident, "if") == 0) {
                tokens[token_count].type = TOKEN_IF;
            } else if (strcmp(ident, "else") == 0) {
                tokens[token_count].type = TOKEN_ELSE;
            } else if (strcmp(ident, "for") == 0) {
                tokens[token_count].type = TOKEN_FOR;
            } else if (strcmp(ident, "in") == 0) {
                tokens[token_count].type = TOKEN_IN;
            } else if (strcmp(ident, "def") == 0) {
                tokens[token_count].type = TOKEN_DEF;
            } else if (strcmp(ident, "range") == 0) {
                tokens[token_count].type = TOKEN_RANGE;
            }

            strcpy(tokens[token_count].value, ident);
            token_count++;
            continue;
        }

        switch (*p) {
            case '=':
                tokens[token_count].type = TOKEN_ASSIGN;
                p++;
                break;
            case '+':
                tokens[token_count].type = TOKEN_PLUS;
                p++;
                break;
            case '-':
                tokens[token_count].type = TOKEN_MINUS;
                p++;
                break;
            case '*':
                tokens[token_count].type = TOKEN_MULT;
                p++;
                break;
            case '/':
                tokens[token_count].type = TOKEN_DIV;
                p++;
                break;
            case '(':
                tokens[token_count].type = TOKEN_LPAREN;
                p++;
                break;
            case ')':
                tokens[token_count].type = TOKEN_RPAREN;
                p++;
                break;
            case ',':
                tokens[token_count].type = TOKEN_COMMA;
                p++;
                break;
            default:
                p++;
                continue;
        }

        token_count++;
    }

    tokens[token_count].type = TOKEN_EOF;
    return token_count;
}

int py_get_var(const char* name, char* out_value) {
    for (int i = 0; i < g_py_interp.var_count; i++) {
        if (strcmp(g_py_interp.vars[i].name, (char*)name) == 0) {
            strcpy(out_value, g_py_interp.vars[i].value);
            return 0;
        }
    }
    return -1;
}

void py_set_var(const char* name, const char* value) {
    for (int i = 0; i < g_py_interp.var_count; i++) {
        if (strcmp(g_py_interp.vars[i].name, (char*)name) == 0) {
            strcpy(g_py_interp.vars[i].value, (char*)value);
            return;
        }
    }

    if (g_py_interp.var_count < MAX_VARS) {
        strcpy(g_py_interp.vars[g_py_interp.var_count].name, (char*)name);
        strcpy(g_py_interp.vars[g_py_interp.var_count].value, (char*)value);
        g_py_interp.var_count++;
    }
}

int py_eval_expr(token_t* tokens, int start, int end, char* out_result) {
    char temp1[MAX_VAR_VALUE] = {0};
    char temp2[MAX_VAR_VALUE] = {0};

    if (start == end) {
        if (tokens[start].type == TOKEN_NUMBER) {
            strcpy(out_result, tokens[start].value);
            return 0;
        } else if (tokens[start].type == TOKEN_STRING) {
            strcpy(out_result, tokens[start].value);
            return 0;
        } else if (tokens[start].type == TOKEN_IDENTIFIER) {
            return py_get_var(tokens[start].value, out_result);
        }
    }

    for (int i = start; i <= end; i++) {
        if (tokens[i].type == TOKEN_PLUS || tokens[i].type == TOKEN_MINUS ||
            tokens[i].type == TOKEN_MULT || tokens[i].type == TOKEN_DIV) {

            py_eval_expr(tokens, start, i - 1, temp1);
            py_eval_expr(tokens, i + 1, end, temp2);

            int val1 = simple_atoi(temp1);
            int val2 = simple_atoi(temp2);
            int result;

            switch (tokens[i].type) {
                case TOKEN_PLUS: result = val1 + val2; break;
                case TOKEN_MINUS: result = val1 - val2; break;
                case TOKEN_MULT: result = val1 * val2; break;
                case TOKEN_DIV: result = val1 / val2; break;
                default: result = 0;
            }

            int_to_string(result, out_result);
            return 0;
        }
    }

    return -1;
}

int py_exec_statement(token_t* tokens, int count) {
    if (count == 0) return 0;

    if (tokens[0].type == TOKEN_PRINT && count >= 2 && tokens[1].type == TOKEN_LPAREN) {
        int i = 2;
        while (i < count && tokens[i].type != TOKEN_RPAREN) {
            int arg_start = i;
            while (i < count && tokens[i].type != TOKEN_COMMA && tokens[i].type != TOKEN_RPAREN) {
                i++;
            }
            if (arg_start < i) {
                char result[MAX_VAR_VALUE] = {0};
                if (py_eval_expr(tokens, arg_start, i - 1, result) == 0) {
                    sharkapi_print(result);
                } else {
                    for (int j = arg_start; j < i; j++) {
                        if (tokens[j].type == TOKEN_STRING ||
                            tokens[j].type == TOKEN_NUMBER ||
                            tokens[j].type == TOKEN_IDENTIFIER) {
                            sharkapi_print(tokens[j].value);
                        }
                    }
                }
            }
            if (i < count && tokens[i].type == TOKEN_COMMA) {
                sharkapi_print(" ");
                i++;
            }
        }
        sharkapi_println("");
        return 0;
    }

    for (int i = 0; i < count; i++) {
        if (tokens[i].type == TOKEN_ASSIGN) {
            char var_name[MAX_VAR_NAME];
            strcpy(var_name, tokens[i - 1].value);

            char result[MAX_VAR_VALUE];
            py_eval_expr(tokens, i + 1, count - 1, result);
            py_set_var(var_name, result);
            return 0;
        }
    }

    return 0;
}

void py_execute_code(const char* code) {
    token_t tokens[MAX_TOKENS];
    int token_count = tokenize(code, tokens, MAX_TOKENS);

    if (token_count > 0) {
        py_exec_statement(tokens, token_count);
    }
}

int plugin_init(void) {
    sharkapi_println("Python Interpreter loaded!");
    sharkapi_println("Type 'python' to start interactive mode or 'python <file>' to run a file");
    g_py_interp.var_count = 0;
    return 0;
}

void plugin_cleanup(void) {
    sharkapi_println("Python Interpreter unloaded");
}

int plugin_command(int argc, char** argv) {
    if (argc == 1) {

        sharkapi_println("Python 3.14 (SharkOS) - Interactive Shell");
        sharkapi_println("Type 'quit()' to exit");

        while (1) {
            sharkapi_print(">>> ");
            char input[256] = {0};
            int idx = 0;

            char c;
            while (1) {
            c = sharkapi_getchar();
            if (c == 0) continue;
            if (c == '\n' || c == '\r' || idx >= 255) {
                break;
            }
            if (c == 8) {
                if (idx > 0) {
                    idx--;
                    sharkapi_putchar(8);
                    sharkapi_putchar(' ');
                    sharkapi_putchar(8);
                }
            } else {
                input[idx++] = c;
                sharkapi_putchar(c);
            }
        }
        input[idx] = '\0';
        sharkapi_println("");

            if (strcmp(input, "quit()") == 0 || strcmp(input, "exit()") == 0) {
                break;
            }

            py_execute_code(input);
        }
    } else if (argc == 2) {
        const char* filename = argv[1];

        if (!sharkapi_file_exists(filename)) {
            sharkapi_printf("Error: File '%s' not found\n", filename);
            return -1;
        }

        file_handle_t file = sharkapi_fopen(filename, "r");
        if (!file) {
            sharkapi_printf("Error: Cannot open file '%s'\n", filename);
            return -1;
        }

        char file_contents[2048] = {0};
        size_t bytes_read = sharkapi_fread(file_contents, 1, sizeof(file_contents) - 1, file);
        sharkapi_fclose(file);

        if (bytes_read == 0) {
            sharkapi_println("Error: File is empty or cannot be read");
            return -1;
        }

        file_contents[bytes_read] = '\0';

        char line[512] = {0};
        int line_idx = 0;

        for (int i = 0; i < bytes_read && line_idx < sizeof(line) - 1; i++) {
            char ch = file_contents[i];

            if (ch == '\n' || ch == '\r') {
                if (line_idx > 0) {
                    line[line_idx] = '\0';
                    py_execute_code(line);
                    line_idx = 0;
                }
                if (ch == '\r' && i + 1 < bytes_read && file_contents[i + 1] == '\n') {
                    i++;
                }
            } else {
                line[line_idx++] = ch;
            }
        }

        if (line_idx > 0) {
            line[line_idx] = '\0';
            py_execute_code(line);
        }

        return 0;
    }

    return 0;
}

int plugin_init_entry(void) {
    return plugin_init();
}

void plugin_cleanup_entry(void) {
    plugin_cleanup();
}

int plugin_command_entry(int argc, char** argv) {
    return plugin_command(argc, argv);
}
