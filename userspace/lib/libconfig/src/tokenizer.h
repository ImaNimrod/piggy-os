#ifndef _TOKENIZER_H
#define _TOKENIZER_H

enum token_type {
    TOKEN_END,

    TOKEN_IDENT,
    TOKEN_STRING,

    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_SEMICOLON,
};

struct token {
    enum token_type type;
    char* value;
};

struct tokenizer {
    const char* text;
    size_t pos;
};

void tokenizer_init(struct tokenizer* tokenizer, const char* text);
bool tokenizer_next(struct tokenizer* tokenizer, struct token* token);
void token_free(struct token* token);

#endif /* _TOKENIZER_H */
