#include "lexer.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define WORD_BUF_INIT 64 // начальная ёмкость буфера для слова, байт
#define TOKENS_INIT 16 // начальная ёмкость массива токенов, штук
#define OPERATOR_CHARS "|&;()<>\n"

typedef enum {
    MODE_NORMAL,
    MODE_SQUOTE, 
    MODE_DQUOTE 
} lex_mode;

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

static bool flush_word(token_list *l, strbuf *b,  bool *in_word)
{
    if (!*in_word)
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
    *in_word = false;
    return true;
}

// распознавание операторов

static token_type read_operator(const char *s, size_t *len)
{
    *len = 1;
    switch (s[0]) {
    case '|':
        if (s[1] == '|') { *len = 2; return TOK_OR_IF; }
        return TOK_PIPE;
    case '&':
        if (s[1] == '&') { *len = 2; return TOK_AND_IF; }
        return TOK_AMP;
    case '>':
        if (s[1] == '>') { *len = 2; return TOK_DGREAT; }
        return TOK_GREAT;
    case '<':  return TOK_LESS;
    case ';':  return TOK_SEMI;
    case '(':  return TOK_LPAREN;
    case ')':  return TOK_RPAREN;
    default:   return TOK_NEWLINE;
    }
}

// основная функция

lex_status lex(const char *s, token_list *out)
{
    *out = (token_list){ .items = NULL, .count = 0, .cap = 0 };

    strbuf word;
    if (!strbuf_init(&word))
        return LEX_NOMEM;

    lex_mode mode = MODE_NORMAL;
    bool in_word = false;
    lex_status status = LEX_OK; 
    size_t i = 0;
    for (;;) {
        char c = s[i];

        if (mode == MODE_SQUOTE) {
            if (c == '\0') {
                status = LEX_UNCLOSED_QUOTE;
                goto fail;
            }
            if (c == '\'')
                mode = MODE_NORMAL;
            else if (!strbuf_push(&word, c))
                goto oom;
            i++;

        } else if (mode == MODE_DQUOTE) {
            if (c == '\0') {
                status = LEX_UNCLOSED_QUOTE;
                goto fail;
            }
            if (c == '"'){
                mode = MODE_NORMAL;
                i++;
            } else if (c == '\\') {
                char n = s[i + 1];
                if (n == '"' || n == '\\') {    
                    if (!strbuf_push(&word, n))
                        goto oom;
                    i += 2;
                } else if (n == '\n') {
                    i += 2;
                } else {                         
                    if (!strbuf_push(&word, '\\'))
                        goto oom;
                    i++;
                }
            } else {
                if (!strbuf_push(&word, c))
                    goto oom;
                i++;
            }
        } else {
            if (c == '\0') {
                break; 
        } else if (c == ' ' || c == '\t') {
            if (!flush_word(out, &word, &in_word))
                goto oom;
            i++;
        } else if (strchr(OPERATOR_CHARS, c) != NULL) {
            size_t len;
            token_type type = read_operator(s + i, &len);
            if (!flush_word(out, &word, &in_word) || !list_push(out, type, NULL))
                goto oom;
            i += len;
        } else if (c == '\'') {
                mode = MODE_SQUOTE;
                in_word = true;
                i++;
        } else if (c == '"') {
                mode = MODE_DQUOTE;
                in_word = true;
                i++;
        } else if (c == '\\') {
                char n = s[i + 1];
                if (n == '\0') {
                    status = LEX_TRAILING_ESCAPE;
                    goto fail;
                }
                if (n == '\n') {
                    if (s[i + 2] == '\0') {
                        status = LEX_TRAILING_ESCAPE;
                        goto fail;
                    }
                    i += 2;
                } else {
                    if (!strbuf_push(&word, n))
                        goto oom;
                    in_word = true;
                    i += 2;
                }
            } else {
                if (!strbuf_push(&word, c))
                    goto oom;
                in_word = true;
                i++;
            }
        }   
    }

    if (!flush_word(out, &word, &in_word) || !list_push(out, TOK_EOF, NULL))
        goto oom;

    free(word.data);
    return LEX_OK;

oom:
    status = LEX_NOMEM;

fail:
    free(word.data);
    token_list_free(out);
    return status;

}

// Сообщения об ошибках

const char *lex_status_message(lex_status status)
{
    switch (status) {
    case LEX_OK:              return "ok";
    case LEX_UNCLOSED_QUOTE:  return "syntax error: unexpected EOF while looking for matching quote";
    case LEX_NOMEM:           return "out of memory";
    case LEX_TRAILING_ESCAPE: return "syntax error: unexpected EOF after backslash";
    }
    return "unknown error";
}

// Отладочный вывод 

void token_list_dump(const token_list *list, FILE *out)
{
    for (size_t i = 0; i < list->count; i++) {
        const token *t = &list->items[i];
        switch (t->type) {
        case TOK_WORD:    fprintf(out, "WORD    [%s]\n", t->text); break;
        case TOK_PIPE:    fprintf(out, "PIPE    |\n");   break;
        case TOK_AMP:     fprintf(out, "AMP     &\n");   break;
        case TOK_SEMI:    fprintf(out, "SEMI    ;\n");   break;
        case TOK_AND_IF:  fprintf(out, "AND_IF  &&\n");  break;
        case TOK_OR_IF:   fprintf(out, "OR_IF   ||\n");  break;
        case TOK_LPAREN:  fprintf(out, "LPAREN  (\n");   break;
        case TOK_RPAREN:  fprintf(out, "RPAREN  )\n");   break;
        case TOK_LESS:    fprintf(out, "LESS    <\n");   break;
        case TOK_GREAT:   fprintf(out, "GREAT   >\n");   break;
        case TOK_DGREAT:  fprintf(out, "DGREAT  >>\n");  break;
        case TOK_NEWLINE: fprintf(out, "NEWLINE \\n\n"); break;
        case TOK_EOF:     fprintf(out, "EOF\n");         break;
        }
    }
}