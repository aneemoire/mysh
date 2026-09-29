#include "lexer.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define WORD_BUF_INIT 64 // начальная ёмкость буфера для слова, байт
#define TOKENS_INIT 16 // начальная ёмкость массива токенов, штук

// буфер для слова, который будет заполняться по мере чтения символов

typedef struct {
    char *data; 
    size_t len;
    size_t cap;
} strbuf;

static bool strbuf_init(strbuf *b)
{
    b->data = malloc(WORD_BUF_INIT);
    b->len = 0;
    b->cap = WORD_BUF_INIT;
    return b->data != NULL;
}

static bool strbuf_push(strbuf *b, char c)
{
    if (b->len == b->cap) {
        char *p = realloc(b->data, b->cap * 2);
        if (p == NULL)
            return false;
        b->data = p;
        b->cap *= 2;
    }
    b->data[b->len++] = c;
    return true;
}

// список токенов

static bool list_push(token_list *l, token_type type, char *text)
{
    if (l->count == l->cap) {
        size_t new_cap = l->cap ? l->cap * 2 : TOKENS_INIT;
        token *p = realloc(l->items, new_cap * sizeof *p);
        if (p == NULL)
            return false;
        l->items = p;
        l->cap = new_cap;
    }
    l->items[l->count++] = (token){ .type = type, .text = text };
    return true;
}

void token_list_free(token_list *l)
{
    for (size_t i = 0; i < l->count; i++)
        free(l->items[i].text);
    free(l->items);
    l->items = NULL;
    l->count = 0;
    l->cap = 0;
}


// Если в буфере есть слово — превращает его в токен TOK_WORD и очищает буфер

static bool flush_word(token_list *l, strbuf *b)
{
    if (b->len == 0)
        return true;

    char *text = malloc(b->len + 1);
    if (text == NULL)
        return false;
    memcpy(text, b->data, b->len);
    text[b->len] = '\0';

    if (!list_push(l, TOK_WORD, text)) {
        free(text);
        return false;
    }
    b->len = 0;
    return true;
}

// основная функция

lex_status lex(const char *s, token_list *out)
{
    *out = (token_list){ .items = NULL, .count = 0, .cap = 0 };

    strbuf word;
    if (!strbuf_init(&word))
        return LEX_NOMEM;

    size_t i = 0;
    for (;;) {
        char c = s[i];

        if (c == '\0') {
            break;
        } else if (c == ' ' || c == '\t') {
            if (!flush_word(out, &word))
                goto oom;
            i++;
        } else {
            if (!strbuf_push(&word, c))
                goto oom;
            i++;
        }
    }

    if (!flush_word(out, &word) || !list_push(out, TOK_EOF, NULL))
        goto oom;

    free(word.data);
    return LEX_OK;

oom:
    free(word.data);
    token_list_free(out);
    return LEX_NOMEM;
}

// Отладочный вывод 

void token_list_dump(const token_list *list, FILE *out)
{
    for (size_t i = 0; i < list->count; i++) {
        const token *t = &list->items[i];
        switch (t->type) {
        case TOK_WORD:
            fprintf(out, "WORD    [%s]\n", t->text); 
            break;
        case TOK_EOF:
            fprintf(out, "EOF\n");
            break;
        }
    }
}