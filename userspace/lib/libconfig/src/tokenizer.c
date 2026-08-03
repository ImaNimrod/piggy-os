#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "tokenizer.h"

static inline char get(struct tokenizer* tokenizer) {
    return tokenizer->text[tokenizer->pos++];
}

static inline char peek(struct tokenizer* tokenizer) {
    return tokenizer->text[tokenizer->pos];
}

static inline void skip_whitespace(struct tokenizer* tokenizer) {
    while (isspace(peek(tokenizer))) {
        get(tokenizer);
    }
}

static void skip_comment(struct tokenizer* tokenizer) {
    while (peek(tokenizer) && peek(tokenizer) != '\n') {
        get(tokenizer);
    }
}

static int is_ident_char(char c) {
    return c && !isspace(c) && c != '{' && c != '}' && c != ';' && c != '"' && c != '#';
}

static char* read_ident(struct tokenizer* tokenizer) {
    size_t start = tokenizer->pos;

    while (is_ident_char(peek(tokenizer))) {
        get(tokenizer);
    }

    size_t length = tokenizer->pos - start;

    char* value = malloc(length + 1);
    if (!value) {
        return NULL;
    }

    memcpy(value, tokenizer->text + start, length);
    value[length] = '\0';

    return value;
}

static char* read_string(struct tokenizer* tokenizer) {
    get(tokenizer);

    size_t capacity = 32;
    size_t length = 0;

    char* value = malloc(capacity);
    if (!value) {
        return NULL;
    }

    while (peek(tokenizer) && peek(tokenizer) != '"') {
        char c = get(tokenizer);

        if (c == '\\') {

            c = get(tokenizer);

            switch (c) {
                case 'n':
                    c = '\n';
                    break;
                case 't':
                    c = '\t';
                    break;
                case '\\':
                    c = '\\';
                    break;
                case '"':
                    c = '"';
                    break;
                default:
                    break;
            }
        }

        if (length + 1 >= capacity) {
            capacity *= 2;

            char* tmp = realloc(value, capacity);
            if (!tmp) {
                free(value);
                return NULL;
            }

            value = tmp;
        }

        value[length++] = c;
    }


    if (peek(tokenizer) == '"') {
        get(tokenizer);
    }

    value[length] = '\0';
    return value;
}

void tokenizer_init(struct tokenizer* tokenizer, const char* text) {
    tokenizer->text = text;
    tokenizer->pos = 0;
}

bool tokenizer_next(struct tokenizer* tokenizer, struct token* token) {
    memset(token, 0, sizeof(struct token));

    for (;;) {
        skip_whitespace(tokenizer);

        if (peek(tokenizer) == '#') {
            skip_comment(tokenizer);
            continue;
        }

        break;
    }

    char c = peek(tokenizer);
    if (!c) {
        token->type = TOKEN_END;
        return true;
    }

    switch (c) {
        case '{':
            get(tokenizer);

            token->type = TOKEN_LBRACE;
            return true;
        case '}':
            get(tokenizer);

            token->type = TOKEN_RBRACE;
            return true;
        case ';':
            get(tokenizer);

            token->type = TOKEN_SEMICOLON;
            return true;
        case '"':
            token->type = TOKEN_STRING;

            token->value = read_string(tokenizer);
            if (!token->value) {
                return false;
            }

            return true;
        default:
            token->type = TOKEN_IDENT;
            token->value = read_ident(tokenizer);

            if (!token->value) {
                return false;
            }

            return true;
    }
}

void token_free(struct token* token) {
    free(token->value);
    token->value = NULL;
}
