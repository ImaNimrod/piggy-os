#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <locale.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define EXIT_NORMAL         0
#define EXIT_NULL_OR_ZERO   1
#define EXIT_SYNTAX_ERROR   2
#define EXIT_OTHER_ERROR    3

#define SIZEOF_ARRAY(xs) (sizeof((xs)) / sizeof((xs)[0]))

struct expr_operator {
    const char* operator;
    char* (*evaluate)(const char*, const char*, const char*);
};

static char* evaluate_or(const char* left, const char* right, const char* op);
static char* evaluate_and(const char* left, const char* right, const char* op);
static char* evaluate_comparison(const char* left, const char* right, const char* op);
static char* evaluate_arithmetic(const char* left, const char* right, const char* op);
static char* evaluate_match(const char* left, const char* right, const char* op);

static char* interpret(char** tokens, size_t num_tokens);
static char* interpret_binary_operator(char** tokens, size_t num_tokens, const void* operator_index);
static char* interpret_left_associative(char** tokens, size_t num_tokens, struct expr_operator* operator, char* (*next)(char**, size_t, const void*), const void* next_context);
static char* interpret_literal(char** tokens, size_t num_tokens, const void* ctx);
static char* interpret_parentheses(char** tokens, size_t num_tokens, const void* ctx);

static struct expr_operator operators[] = {
    { "|", evaluate_or },
    { "&", evaluate_and },
    { "<", evaluate_comparison },
    { "<=", evaluate_comparison },
    { "=", evaluate_comparison },
    { "!=", evaluate_comparison },
    { ">=", evaluate_comparison },
    { ">", evaluate_comparison },
    { "+", evaluate_arithmetic },
    { "-", evaluate_arithmetic },
    { "*", evaluate_arithmetic },
    { "/", evaluate_arithmetic },
    { "%", evaluate_arithmetic },
    { ":", evaluate_match },
};

static inline void div0_error(void) {
    errx(EXIT_SYNTAX_ERROR, "division by zero");
}

static inline void overflow_error(const char* type) {
    errx(EXIT_SYNTAX_ERROR, "%s overflow", type);
}

static inline void syntax_error(void) {
    errx(EXIT_SYNTAX_ERROR, "syntax error");
}

static inline void print_integer(intmax_t value, char** ret) {
    if (asprintf(ret, "%" PRIiMAX, value) < 0) {
        err(EXIT_OTHER_ERROR, "asprintf");
    }
}

static inline char* xstrdup(const char* str) {
    char* ret = strdup(str);
    if (!ret) {
        errx(EXIT_OTHER_ERROR, "strdup");
    }
    return ret;
}

static bool get_integer(const char* token, bool strict, intmax_t* ret) {
    errno = 0;

    char* end_ptr;
    *ret = strtoimax(token, &end_ptr, 10);

    bool success = errno == 0 && end_ptr != token && *end_ptr == '\0';
    if (strict && !success) {
        errx(EXIT_SYNTAX_ERROR, "non-integer argument");
    }

    return success;
}

static bool is_null_or_zero(const char* token) {
    if (strcmp(token, "") == 0) {
        return true;
    }

    intmax_t value;
    return get_integer(token, false, &value) && value == 0;
}

static char* evaluate_or(const char* left, const char* right, const char* op) {
    (void) op;

    if (is_null_or_zero(left)) {
        return xstrdup(right);
    }

    return xstrdup(left);
}

static char* evaluate_and(const char* left, const char* right, const char* op) {
    (void) op;

    if (is_null_or_zero(left) || is_null_or_zero(right)) {
        return xstrdup("0");
    }

    return xstrdup(left);
}

static char* evaluate_comparison(const char* left, const char* right, const char* op) {
    intmax_t left_value;
    intmax_t right_value;

    int comparison;
    if (get_integer(left, false, &left_value) && get_integer(right, false, &right_value)) {
        comparison = left_value < right_value ? -1 : (left_value > right_value ? 1 : 0);
    } else {
        comparison = strcoll(left, right);
    }

    bool result;

    if (strcmp(op, "=") == 0) {
        result = comparison == 0;
    } else if (strcmp(op, ">") == 0) {
        result = comparison > 0;
    } else if (strcmp(op, ">=") == 0) {
        result = comparison >= 0;
    } else if (strcmp(op, "<") == 0) {
        result = comparison < 0;
    } else if (strcmp(op, "<=") == 0) {
        result = comparison <= 0;
    } else {
        result = comparison != 0;
    }

    return xstrdup(result ? "1" : "0");
}

static char* evaluate_arithmetic(const char* left, const char* right, const char* op) {
    intmax_t left_value;
    get_integer(left, true, &left_value);

    intmax_t right_value;
    get_integer(right, true, &right_value);

    intmax_t result_value = 0;

    switch (op[0]) {
        case '+':
            if (__builtin_add_overflow(left_value, right_value, &result_value)) {
                overflow_error("addition");
            }
            break;
        case '-':
            if (__builtin_sub_overflow(left_value, right_value, &result_value)) {
                overflow_error("subtraction");
            }
            break;
        case '*':
            if (__builtin_mul_overflow(left_value, right_value, &result_value)) {
                overflow_error("multiplication");
            }
            break;
        case '/':
            if (right_value == 0) {
                div0_error();
            }
            if (left_value == INTMAX_MIN && right_value == -1) {
                overflow_error("division");
            }

            result_value = left_value / right_value;
            break;
        case '%':
            if (right_value == 0) {
                div0_error();
            }
            if (left_value == INTMAX_MIN && right_value == -1) {
                overflow_error("division");
            }

            result_value = left_value % right_value;
            break;
    }

    char* result;
    print_integer(result_value, &result);
    return result;
}

static char* evaluate_match(const char* left, const char* right, const char* op) {
    (void) op;

    regex_t regex;

    int status = regcomp(&regex, right, 0);
    if (status != 0) {
        size_t len = regerror(status, &regex, NULL, 0);
        char* errmsg = malloc(len);

        if (errmsg) {
            regerror(status, &regex, errmsg, len);
            errx(EXIT_SYNTAX_ERROR, "compiling regular expression: %s", errmsg);
        } else {
            errx(EXIT_SYNTAX_ERROR, "compiling regular expression failed");
        }
    }

    char* result;

    regmatch_t rm[2];
    int matched = regexec(&regex, left, 2, rm, 0);

    if (matched == 0 && rm[0].rm_so == 0) {
        if (regex.re_nsub >= 1 && rm[1].rm_so != -1) {
            result = strndup(left + rm[1].rm_so, rm[1].rm_eo - rm[1].rm_so);
            if (!result) {
                err(EXIT_OTHER_ERROR, "strndup");
            }
        } else {
            print_integer(rm[0].rm_eo, &result);
        }
    } else {
        result = xstrdup("0");
    }

    regfree(&regex);
    return result;
}

static char* interpret(char** tokens, size_t num_tokens) {
    if (num_tokens == 0) {
        syntax_error();
    }

    size_t operator_index = 0;
    return interpret_binary_operator(tokens, num_tokens, &operator_index);
}

static char* interpret_binary_operator(char** tokens, size_t num_tokens, const void* context) {
    size_t index = *(const size_t*) context;
    size_t next_index = index + 1;

    char* (*next)(char**, size_t, const void*);
    const void* next_context;

    if (next_index == SIZEOF_ARRAY(operators)) {
        next = interpret_parentheses;
        next_context = NULL;
    } else {
        next = interpret_binary_operator;
        next_context = &next_index;
    }

    struct expr_operator* binop = &operators[index];
    return interpret_left_associative(tokens, num_tokens, binop, next, next_context);
}

static char* interpret_left_associative(char** tokens, size_t num_tokens, struct expr_operator* operator, char* (*next)(char**, size_t, const void*), const void* next_context) {
    size_t depth = 0;

    for (size_t n = num_tokens; n != 0; n--) {
        size_t i = n - 1;
        if (!strcmp(tokens[i], ")")) {
            depth++;
            continue;
        }

        if (!strcmp(tokens[i], "(")) {
            if (depth == 0) {
                syntax_error();
            }
            depth--;
            continue;
        }

        if (depth != 0) {
            continue;
        }

        if (strcmp(tokens[i], operator->operator) != 0) {
            continue;
        }

        if (i == 0 || (i + 1 == num_tokens)) {
            syntax_error();
        }

        char** left_tokens = tokens;
        size_t num_left_tokens = i;

        char** right_tokens = tokens + i + 1;
        size_t num_right_tokens = num_tokens - (i + 1);

        char* left_value = interpret_left_associative(left_tokens, num_left_tokens, operator, next, next_context);
        char* right_value = next(right_tokens, num_right_tokens, next_context);
        char* value = operator->evaluate(left_value, right_value, operator->operator);

        free(left_value);
        free(right_value);
        return value;
    }

    if (depth != 0) {
        syntax_error();
    }

    return next(tokens, num_tokens, next_context);
}

static char* interpret_literal(char** tokens, size_t num_tokens, const void* context) {
    (void) context;

    if (num_tokens != 1) {
        syntax_error();
    }

    return xstrdup(tokens[0]);
}

static char* interpret_parentheses(char** tokens, size_t num_tokens, const void* ctx) {
    if (2 <= num_tokens && strcmp(tokens[0], "(") == 0 && strcmp(tokens[num_tokens-1], ")") == 0) {
        return interpret(tokens + 1, num_tokens - 2);
    }

    return interpret_literal(tokens, num_tokens, ctx);
}

static void usage(void) {
    fprintf(stderr, "usage: expr EXPRESSION\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "");

    while (getopt(argc, argv, "") != -1) {
        usage();
    }

    argc -= optind;
    argv += optind;

    char* value = interpret(argv, argc);
    puts(value);

    int ret = is_null_or_zero(value) ? EXIT_NULL_OR_ZERO : EXIT_NORMAL;

    free(value);
    return ret;
}
