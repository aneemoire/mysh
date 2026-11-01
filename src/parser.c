/*
parser.c - синтаксический анализ
Разбор методом рекурсивного спуска: каждому правилу грамматики соответствует функция parse_<имя_правила>, которая читает часть token_list и строит часть дерева. 
Сейчас реализовано только правило simple (и redirection внутри него)
*/
#include "parser.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define MSG_SIZE 96 // Максимальная длина сообщения об ошибке
#define MAX_NEST_DEPTH 1000 // Максимальная глубина вложенности скобок

typedef struct {
    const token *tok; // Указатель на текущий токен
    int depth; // Глубина вложенности скобок
    ast_block **mem; // Куда складывается вся выделенная память дерева
    const char *err; // Сообщение об ошибке
    char msg[MSG_SIZE]; // Буфер для сообщения об ошибке
} parser_state;

static const char *const TOKEN_TEXT[] = {
    [TOK_PIPE] = "|",
    [TOK_AMP] = "&",
    [TOK_SEMI] = ";",
    [TOK_AND_IF] = "&&",
    [TOK_OR_IF] = "||", 
    [TOK_LPAREN] = "(", 
    [TOK_RPAREN] = ")", 
    [TOK_LESS] = "<",
    [TOK_GREAT] = ">", 
    [TOK_DGREAT] = ">>", 
    [TOK_NEWLINE] = "newline",
    [TOK_EOF] = "end of input"
};

static void next(parser_state *p)
{
    if (p->tok->type != TOK_EOF) 
        p->tok++;
}
 
static bool fail(parser_state *p, const char *msg)
{
    p->err = msg;
    return false;
}

static bool unexpected(parser_state *p)
{
    const char *shown = p->tok->type == TOK_WORD ? p->tok->text : TOKEN_TEXT[p->tok->type];
 
    snprintf(p->msg, sizeof p->msg, "syntax error: unexpected token '%.40s'", shown);
    return fail(p, p->msg);
}
 
static bool is_redir(token_type t)
{
    return t == TOK_LESS || t == TOK_GREAT || t == TOK_DGREAT;
}
 
static void skip_newlines(parser_state *p)
{
    while (p->tok->type == TOK_NEWLINE)
        next(p);
}

static bool at_list_end(const parser_state *p)
{
    return p->tok->type == TOK_EOF || p->tok->type == TOK_RPAREN;
}
 
static bool parse_redirection(parser_state *p, ast_vec *redirs)
{
    token_type t = p->tok->type;
    redirection *r = ast_vec_add(p->mem, redirs, sizeof *r);
 
    if (r == NULL)
        return false;
    r->op = t == TOK_LESS ? REDIR_IN : t == TOK_GREAT ? REDIR_OUT : REDIR_APPEND;
    next(p);
    if (p->tok->type != TOK_WORD)
        return fail(p, "syntax error: expected a word after redirection operator");
    r->file = ast_strdup(p->mem, p->tok->text);
    next(p);
    return r->file != NULL;
}

static bool parse_simple(parser_state *p, simple_command *s)
{
    ast_vec words = { 0 }, redirs = { 0 };
 
    for (;;) {
        if (is_redir(p->tok->type)) {
            if (!parse_redirection(p, &redirs))
                return false;
        } else if (p->tok->type == TOK_WORD) {
            char **w = ast_vec_add(p->mem, &words, sizeof *w);
 
            if (w == NULL || (*w = ast_strdup(p->mem, p->tok->text)) == NULL)
                return false;
            next(p);
        } else {
            break;
        }
    }
    if (words.n == 0) 
        return fail(p, "syntax error: expected a command name");
    *s = (simple_command){ words.data, words.n, redirs.data, redirs.n };
    return true;
}

static bool parse_list(parser_state *p, ast_list *l);

static bool parse_subshell(parser_state *p, subshell_command *s)
{
    ast_vec redirs = { 0 };
 
    if (p->depth >= MAX_NEST_DEPTH)
        return fail(p, "syntax error: nesting too deep");
    p->depth++;
    next(p);
    skip_newlines(p);
    s->body = ast_alloc(p->mem, sizeof *s->body);
    if (s->body == NULL || !parse_list(p, s->body))
        return false;
    if (p->tok->type != TOK_RPAREN)
        return p->tok->type == TOK_EOF
            ? fail(p, "syntax error: expected ')' before end of input")
            : unexpected(p);
    next(p); 
    while (is_redir(p->tok->type))
        if (!parse_redirection(p, &redirs))
            return false;
    p->depth--;
    s->redirs = redirs.data;
    s->redirs_count = redirs.n;
    return true;
}

static bool parse_command(parser_state *p, command *c)
{
    if (p->tok->type == TOK_LPAREN) {
        c->type = CMD_SUBSHELL;
        return parse_subshell(p, &c->as.subshell);
    }
    c->type = CMD_SIMPLE;
    return parse_simple(p, &c->as.simple);
}

static bool parse_pipeline(parser_state *p, pipeline *pl)
{
    ast_vec cmds = { 0 };
 
    for (;;) {
        command *c = ast_vec_add(p->mem, &cmds, sizeof *c);
 
        if (c == NULL || !parse_command(p, c))
            return false;
        if (p->tok->type != TOK_PIPE)
            break;
        next(p);
    }
    *pl = (pipeline){ cmds.data, cmds.n };
    return true;
}

static bool parse_and_or(parser_state *p, and_or *ao)
{
    ast_vec pls = { 0 }, ops = { 0 };
 
    for (;;) {
        pipeline *pl = ast_vec_add(p->mem, &pls, sizeof *pl);
        token_type t;
 
        if (pl == NULL || !parse_pipeline(p, pl))
            return false;
        t = p->tok->type;
        if (t != TOK_AND_IF && t != TOK_OR_IF)
            break;
        and_or_op *op = ast_vec_add(p->mem, &ops, sizeof *op);
 
        if (op == NULL)
            return false;
        *op = t == TOK_AND_IF ? AND_IF_OP : OR_IF_OP;
        next(p);
    }
    *ao = (and_or){ pls.data, ops.data, pls.n };
    return true;
}

static bool parse_list(parser_state *p, ast_list *l)
{
    ast_vec items = { 0 }, seps = { 0 };
 
    for (;;) {
        and_or *ao = ast_vec_add(p->mem, &items, sizeof *ao);
        list_sep *sep = ast_vec_add(p->mem, &seps, sizeof *sep);
        token_type t;
 
        if (ao == NULL || sep == NULL || !parse_and_or(p, ao))
            return false;
        t = p->tok->type;
        *sep = SEP_NONE;
        if (t == TOK_SEMI || t == TOK_AMP) {
            *sep = t == TOK_SEMI ? SEP_SEQ : SEP_BG;
            next(p);
            skip_newlines(p);
        } else if (t == TOK_NEWLINE) {
            skip_newlines(p);
            if (!at_list_end(p)) 
                *sep = SEP_SEQ;
        }

        if (*sep == SEP_NONE || at_list_end(p))
            break;
    }
    *l = (ast_list){ .items = items.data, .seps = seps.data, .count = items.n };
    return true;
}


parse_status parse(const token_list *tokens, ast_list *out, char **error)
{
    ast_block *mem = NULL;
    parser_state p = { .tok = tokens->items, .mem = &mem };
    ast_list list = { 0 };
    bool ok = true;
    char *msg;
 
    skip_newlines(&p);
    if (p.tok->type != TOK_EOF) {
        ok = parse_list(&p, &list);
        if (ok && p.tok->type != TOK_EOF) 
            ok = unexpected(&p);
    }
    if (ok) {
        list.mem = mem;
        *out = list; 
        return PARSE_OK;
    }
 
    ast_list_free(&(ast_list){ .mem = mem });
    if (p.err == NULL || (msg = malloc(strlen(p.err) + 1)) == NULL)
        return PARSE_NOMEM;
    strcpy(msg, p.err);
    *error = msg;
    return PARSE_SYNTAX_ERROR;
}