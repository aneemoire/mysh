#ifndef LEXER_H
#define LEXER_H

#include <stddef.h>
#include <stdio.h>

typedef enum {
    TOK_WORD,
    TOK_PIPE,    // |
    TOK_AMP,     // &
    TOK_SEMI,    // ;
    TOK_AND_IF,  // &&
    TOK_OR_IF,   // ||
    TOK_LPAREN,  // (
    TOK_RPAREN,  // )
    TOK_LESS,    // <
    TOK_GREAT,   // >
    TOK_DGREAT,  // >>
    TOK_NEWLINE,
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

void token_list_dump(const token_list *list, FILE *out);

#endif
