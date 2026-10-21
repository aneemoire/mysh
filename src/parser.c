/*
parser.c - синтаксический анализ
Разбор методом рекурсивного спуска: каждому правилу грамматики соответствует функция parse_<имя_правила>, которая читает часть token_list и строит часть дерева. 
Сейчас реализовано только правило simple (и redirection внутри него)
*/
#include "parser.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define ARGV_INIT  8 // начальная ёмкость временного массива слов, штук 
#define REDIR_INIT 4 // начальная ёмкость временного массива redirs, штук


typedef struct {
    const token *items;
    size_t count;
    size_t pos;
} parser_state;

static const token *peek(const parser_state *p)
{
    return &p->items[p->pos];
}

static void advance(parser_state *p)
{
    if (p->pos + 1 < p->count)
        p->pos++;
}


static char *dup_str(const char *s)
{
    size_t len = strlen(s);
    char *copy = malloc(len + 1);
    if (copy == NULL)
        return NULL;
    memcpy(copy, s, len + 1);
    return copy;
}

static char *make_error(const char *text)
{
    const char *prefix = "syntax error: ";
    size_t len = strlen(prefix) + strlen(text);
    char *msg = malloc(len + 1);
    if (msg == NULL)
        return NULL;
    memcpy(msg, prefix, strlen(prefix));
    memcpy(msg + strlen(prefix), text, strlen(text) + 1);
    return msg;
}

typedef struct { char **data; size_t count; size_t cap; } str_vec;

static bool str_vec_push(str_vec *v, char *s)
{
    if (v->count == v->cap) {
        size_t new_cap = v->cap ? v->cap * 2 : ARGV_INIT;
        char **p = realloc(v->data, new_cap * sizeof *p);
        if (p == NULL)
            return false;
        v->data = p;
        v->cap = new_cap;
    }
    v->data[v->count++] = s;
    return true;
}

typedef struct { redirection *data; size_t count; size_t cap; } redir_vec;

static bool redir_vec_push(redir_vec *v, redirection r)
{
    if (v->count == v->cap) {
        size_t new_cap = v->cap ? v->cap * 2 : REDIR_INIT;
        redirection *p = realloc(v->data, new_cap * sizeof *p);
        if (p == NULL)
            return false;
        v->data = p;
        v->cap = new_cap;
    }
    v->data[v->count++] = r;
    return true;
}

static bool at_redirection(const parser_state *p)
{
    token_type t = peek(p)->type;
    return t == TOK_LESS || t == TOK_GREAT || t == TOK_DGREAT;
}

static bool parse_redirection(parser_state *p, redirection *out, char **error)
{
    redir_op op;
    switch (peek(p)->type) {
    case TOK_LESS:   op = REDIR_IN;     break;
    case TOK_GREAT:  op = REDIR_OUT;    break;
    default:         op = REDIR_APPEND; break;
    }
    advance(p);

    if (peek(p)->type != TOK_WORD) {
        *error = make_error("expected a word after redirection operator");
        return false;
    }

    char *file = dup_str(peek(p)->text);
    if (file == NULL) {
        *error = NULL;
        return false;
    }
    advance(p);

    out->op = op;
    out->file = file;
    return true;
}

static void simple_command_free(simple_command *cmd)
{
    for (size_t i = 0; i < cmd->argv_count; i++)
        free(cmd->argv[i]);
    free(cmd->argv);
    for (size_t i = 0; i < cmd->redirs_count; i++)
        free(cmd->redirs[i].file);
    free(cmd->redirs);
}

static bool parse_simple(parser_state *p, simple_command *out, char **error)
{
    str_vec words = { .data = NULL, .count = 0, .cap = 0 };
    redir_vec redirs = { .data = NULL, .count = 0, .cap = 0 };

    for (;;) {
        if (at_redirection(p)) {
            redirection r;
            if (!parse_redirection(p, &r, error))
                goto fail;
            if (!redir_vec_push(&redirs, r)) {
                free(r.file);
                *error = NULL;
                goto fail;
            }
            continue;
        }

        if (peek(p)->type == TOK_WORD) {
            char *w = dup_str(peek(p)->text);
            if (w == NULL) {
                *error = NULL;
                goto fail;
            }
            if (!str_vec_push(&words, w)) {
                free(w);
                *error = NULL;
                goto fail;
            }
            advance(p);
            continue;
        }

        break;
    }

    if (words.count == 0) {
        *error = make_error("expected a command name");
        goto fail;
    }

    out->argv = words.data;
    out->argv_count = words.count;
    out->redirs = redirs.data;
    out->redirs_count = redirs.count;
    return true;

fail:
    for (size_t i = 0; i < words.count; i++)
        free(words.data[i]);
    free(words.data);
    for (size_t i = 0; i < redirs.count; i++)
        free(redirs.data[i].file);
    free(redirs.data);
    return false;
}

static void command_free(command *cmd)
{
    if (cmd->type == CMD_SIMPLE) {
        simple_command_free(&cmd->as.simple);
    } else {
        ast_list_free(cmd->as.subshell.body);
        free(cmd->as.subshell.body);
        for (size_t i = 0; i < cmd->as.subshell.redirs_count; i++)
            free(cmd->as.subshell.redirs[i].file);
        free(cmd->as.subshell.redirs);
    }
}

void ast_list_free(ast_list *list)
{
    for (size_t i = 0; i < list->count; i++) {
        and_or *ao = &list->items[i];
        for (size_t j = 0; j < ao->count; j++) {
            pipeline *pl = &ao->pipelines[j];
            for (size_t k = 0; k < pl->count; k++)
                command_free(&pl->commands[k]);
            free(pl->commands);
        }
        free(ao->pipelines);
        free(ao->ops);
    }
    free(list->items);
    free(list->seps);
    list->items = NULL;
    list->seps = NULL;
    list->count = 0;
}


static void print_indent(FILE *out, int depth)
{
    fprintf(out, "%*s", depth * 2, "");
}

static const char *redir_symbol(redir_op op)
{
    switch (op) {
    case REDIR_IN:     return "<";
    case REDIR_OUT:    return ">";
    default:           return ">>";
    }
}

static void simple_command_dump(const simple_command *s, FILE *out, int depth)
{
    print_indent(out, depth);
    fprintf(out, "SIMPLE argv=[");
    for (size_t i = 0; i < s->argv_count; i++)
        fprintf(out, "%s%s", i ? " " : "", s->argv[i]);
    fprintf(out, "]");
    for (size_t i = 0; i < s->redirs_count; i++)
        fprintf(out, " %s%s", redir_symbol(s->redirs[i].op), s->redirs[i].file);
    fprintf(out, "\n");
}

static void ast_list_dump_at(const ast_list *list, FILE *out, int depth);

static void command_dump(const command *cmd, FILE *out, int depth)
{
    if (cmd->type == CMD_SIMPLE) {
        simple_command_dump(&cmd->as.simple, out, depth);
        return;
    }
    print_indent(out, depth);
    fprintf(out, "SUBSHELL\n");
    ast_list_dump_at(cmd->as.subshell.body, out, depth + 1);
    for (size_t i = 0; i < cmd->as.subshell.redirs_count; i++) {
        print_indent(out, depth + 1);
        fprintf(out, "REDIR %s%s\n", redir_symbol(cmd->as.subshell.redirs[i].op),
                cmd->as.subshell.redirs[i].file);
    }
}

static void pipeline_dump(const pipeline *pl, FILE *out, int depth)
{
    if (pl->count == 1) {
        command_dump(&pl->commands[0], out, depth);
        return;
    }
    print_indent(out, depth);
    fprintf(out, "PIPELINE\n");
    for (size_t i = 0; i < pl->count; i++)
        command_dump(&pl->commands[i], out, depth + 1);
}

static void and_or_dump(const and_or *ao, FILE *out, int depth)
{
    if (ao->count == 1) {
        pipeline_dump(&ao->pipelines[0], out, depth);
        return;
    }
    print_indent(out, depth);
    fprintf(out, "AND_OR\n");
    for (size_t i = 0; i < ao->count; i++) {
        pipeline_dump(&ao->pipelines[i], out, depth + 1);
        if (i + 1 < ao->count) {
            print_indent(out, depth + 1);
            fprintf(out, "%s\n", ao->ops[i] == AND_IF_OP ? "&&" : "||");
        }
    }
}

static void ast_list_dump_at(const ast_list *list, FILE *out, int depth)
{
    for (size_t i = 0; i < list->count; i++) {
        and_or_dump(&list->items[i], out, depth);
        if (list->seps[i] == SEP_SEQ) {
            print_indent(out, depth);
            fprintf(out, ";\n");
        } else if (list->seps[i] == SEP_BG) {
            print_indent(out, depth);
            fprintf(out, "&\n");
        }
    }
}

void ast_list_dump(const ast_list *list, FILE *out)
{
    ast_list_dump_at(list, out, 0);
}


parse_status parse(const token_list *tokens, ast_list *out, char **error)
{
    parser_state state = { .items = tokens->items, .count = tokens->count, .pos = 0 };

    simple_command simple;
    if (!parse_simple(&state, &simple, error))
        return *error == NULL ? PARSE_NOMEM : PARSE_SYNTAX_ERROR;

    command *cmd = malloc(sizeof *cmd);
    pipeline *pl = malloc(sizeof *pl);
    and_or *ao = malloc(sizeof *ao);
    list_sep *seps = malloc(sizeof *seps);
    if (cmd == NULL || pl == NULL || ao == NULL || seps == NULL) {
        free(cmd); free(pl); free(ao); free(seps);
        simple_command_free(&simple);
        return PARSE_NOMEM;
    }

    cmd->type = CMD_SIMPLE;
    cmd->as.simple = simple;
    pl->commands = cmd;
    pl->count = 1;
    ao->pipelines = pl;
    ao->ops = NULL;
    ao->count = 1;
    seps[0] = SEP_NONE;

    if (peek(&state)->type == TOK_NEWLINE)
        advance(&state);
    if (peek(&state)->type != TOK_EOF) {
        free(cmd); free(pl); free(ao); free(seps);
        simple_command_free(&simple);
        char *msg = make_error("unexpected token "
                                "(pipelines, lists and subshells are not supported yet)");
        if (msg == NULL)
            return PARSE_NOMEM;
        *error = msg;
        return PARSE_SYNTAX_ERROR;
    }

    out->items = ao;
    out->seps = seps;
    out->count = 1;
    return PARSE_OK;
}