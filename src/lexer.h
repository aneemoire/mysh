#ifndef LEXER_H
#define LEXER_H

#include <stddef.h>

typedef enum {
    TOK_WORD,
    TOK_EOF
} token_type;

typedef struct {
    token_type type;
    char *text;
} token;

typedef struct {
    token *items;
    size_t count; // сколько тоекенов в списке
    size_t cap; // сколько токенов может вместить список
} token_list;

typedef enum {
    LEX_OK,
    LEX_NOMEM
} lex_status;

lex_status lex(const char *input, token_list *out);

void token_list_free(token_list *list);

#endif
