#include <ctype.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

#include "tokenizer.h"

static int token_push(struct token** tokens, size_t* count, size_t* capacity, enum token_type type, const char* text) {
    if (*count >= *capacity) {
        size_t new_capacity = *capacity ? *capacity * 2 : 16;

        struct token* new_tokens = realloc(*tokens, new_capacity * sizeof(struct token));
        if (!new_tokens) {
            return -1;
        }

        *tokens = new_tokens;
        *capacity = new_capacity;
    }

    struct token* token = &(*tokens)[(*count)++];
    token->type = type;

    if (text) {
        token->text = strdup(text);
        if (!token->text) {
            return -1;
        }
    } else {
        token->text = NULL;
    }

    return 0;
}

int tokenize(const char* line, struct token** out_tokens) {
    struct token* tokens = NULL;

    size_t count = 0;
    size_t capacity = 0;

    const char* p = line;

    while (*p) {
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }

        if (!*p) {
            break;
        }

        if (*p == '#') {
            break;
        }

        if (*p == '|') {
            if (token_push(&tokens, &count, &capacity, TOKEN_PIPE, NULL) < 0) {
                goto error;
            }

            p++;
            continue;
        }

        size_t word_capacity = 64;
        size_t word_length = 0;

        char* word = malloc(word_capacity);
        if (!word) {
            goto error;
        }

        bool in_single_quote = false;
        bool in_double_quote = false;

        while (*p) {
            char c = *p;

            if (!in_single_quote && !in_double_quote && (isspace((unsigned char)c) || c == '|' || c == '#')) {
                break;
            }

            if (!in_double_quote && c == '\'') {
                in_single_quote = !in_single_quote;
                p++;
                continue;
            }

            if (!in_single_quote && c == '"') {
                in_double_quote = !in_double_quote;
                p++;
                continue;
            }

            if (!in_single_quote && c == '\\') {
                p++;

                if (!*p) {
                    warnx("trailing backslash");
                    free(word);
                    goto error;
                }

                c = *p;
                p++;
            } else {
                p++;
            }

            if (word_length + 1 >= word_capacity) {
                word_capacity *= 2;

                char* new_word = realloc(word, word_capacity);
                if (!new_word) {
                    free(word);
                    goto error;
                }

                word = new_word;
            }

            word[word_length++] = c;
        }

        if (in_single_quote) {
            warnx("unterminated single quote");
            free(word);
            goto error;
        }

        if (in_double_quote) {
            warnx("unterminated double quote");
            free(word);
            goto error;
        }

        word[word_length] = '\0';

        if (token_push(&tokens, &count, &capacity, TOKEN_WORD, word) < 0) {
            free(word);
            goto error;
        }

        free(word);
    }

    *out_tokens = tokens;
    return count;

error:
    tokens_free(tokens, count);
    return -1;
}

void tokens_free(struct token* tokens, size_t count) {
    for (size_t i = 0; i < count; i++) {
        free(tokens[i].text);
    }

    free(tokens);
}
