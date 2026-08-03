#include <stdlib.h> 
#include <string.h> 

#include "parser.h" 
#include "tokenizer.h" 

struct parser {
    struct tokenizer tokenizer;
    struct token current;
};

static inline bool parser_next(struct parser* parser) {
    token_free(&parser->current);
    return tokenizer_next(&parser->tokenizer, &parser->current);
}

static config_node_t* node_create(const char* name) {
    config_node_t* node = calloc(1, sizeof(config_node_t));
    if (!node) {
        return NULL;
    }

    node->name = strdup(name);
    if (!node->name) {
        free(node);
        return NULL;
    }

    return node;
}

static int node_add_value(config_node_t* node, const char* value) {
    char** values = realloc(node->values, sizeof(char*) * (node->value_count + 1));
    if (!values) {
        return -1;
    }
    node->values = values;

    node->values[node->value_count] = strdup(value);
    if (!node->values[node->value_count]) {
        return -1;
    }

    node->value_count++;
    return 0;
}

static void node_append(config_node_t** list, config_node_t* node) {
    if (!*list) {
        *list = node;
        return;
    }

    config_node_t* iter = *list;

    while (iter->next) {
        iter = iter->next;
    }

    iter->next = node;
}

static bool parse_statement(struct parser* parser, config_node_t** result);

static bool parse_block(struct parser* parser, config_node_t** result, bool root) {
    config_node_t* head = NULL;

    while (parser->current.type != TOKEN_END) {
        if (!root && parser->current.type == TOKEN_RBRACE) {
            break;
        }

        config_node_t* node;

        if (!parse_statement(parser, &node)) {
            return false;
        }

        node_append(&head, node);
    }

    *result = head;
    return true;
}

static bool parse_statement(struct parser* parser, config_node_t** result) {
    if (parser->current.type != TOKEN_IDENT) {
        return false;
    }

    config_node_t* node = node_create(parser->current.value);
    if (!node) {
        return false;
    }

    if (!parser_next(parser)) {
        goto fail;
    }

    for (;;) {
        switch (parser->current.type) {
            case TOKEN_IDENT:
            case TOKEN_STRING:
                if (node_add_value(node, parser->current.value) < 0) {
                    goto fail;
                }

                if (!parser_next(parser)) {
                    goto fail;
                }

                break;
            case TOKEN_SEMICOLON:
                if (!parser_next(parser)) {
                    goto fail;
                }

                *result = node;
                return true;
            case TOKEN_LBRACE:
                if (!parser_next(parser)) {
                    goto fail;
                }

                if (!parse_block(parser, &node->children, false)) {
                    goto fail;
                }


                if (parser->current.type != TOKEN_RBRACE) {
                    goto fail;
                }

                if (!parser_next(parser)) {
                    goto fail;
                }

                *result = node;
                return true;
            default:
                goto fail;
        }
    }

fail:
    return false;
}

bool parse_config(config_t* config, const char* data) {
    struct parser parser = {};

    tokenizer_init(&parser.tokenizer, data);

    if (!parser_next(&parser)) {
        return false;
    }

    if (!parse_block(&parser, &config->root.children, true)) {
        token_free(&parser.current);
        return false;
    }

    token_free(&parser.current);

    return true;
}
