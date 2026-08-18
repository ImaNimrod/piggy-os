#ifndef _TOKENIZER_H
#define _TOKENIZER_H

enum token_type {
    TOKEN_WORD,
    TOKEN_PIPE
};

struct token {
    enum token_type type;
    char* text;
};

int tokenize(const char* line, struct token** out_tokens);
void tokens_free(struct token* tokens, size_t count);

#endif /* _TOKENIZER_H */
