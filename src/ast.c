/*
    ast.c — память и печать дерева команд
*/

#include "ast.h"
 
#include <stdlib.h>
#include <string.h>

#define VEC_INIT 4 

struct ast_block {
    ast_block *next;
    _Alignas(max_align_t) char data[];
};

void *ast_alloc(ast_block **mem, size_t size)
{
    ast_block *b = calloc(1, sizeof *b + size);
 
    if (b == NULL)
        return NULL;
    b->next = *mem;
    *mem = b;
    return b->data;
}
 
void *ast_vec_add(ast_block **mem, ast_vec *v, size_t elem)
{
    if (v->n == v->cap) {
        size_t cap = v->cap ? v->cap * 2 : VEC_INIT;
        void *data = ast_alloc(mem, cap * elem);
 
        if (data == NULL)
            return NULL;
        if (v->n > 0) 
            memcpy(data, v->data, v->n * elem);
        v->data = data;
        v->cap = cap;
    }
    return (char *)v->data + v->n++ * elem;
}
 
char *ast_strdup(ast_block **mem, const char *s)
{
    size_t n = strlen(s) + 1;
    char *copy = ast_alloc(mem, n);
 
    if (copy != NULL)
        memcpy(copy, s, n);
    return copy;
}
 
void ast_list_free(ast_list *list)
{
    ast_block *next;
 
    for (ast_block *b = list->mem; b != NULL; b = next) {
        next = b->next;
        free(b);
    }
    *list = (ast_list){ 0 };
}
 
static const char *const REDIR_SYM[] = {
    [REDIR_IN] = "<", [REDIR_OUT] = ">", [REDIR_APPEND] = ">>"
};
 
static void indent(FILE *out, int depth)
{
    fprintf(out, "%*s", depth * 2, "");
}
 
static void dump_list(const ast_list *l, FILE *out, int depth);
 
static void dump_command(const command *c, FILE *out, int depth)
{
    indent(out, depth);
    if (c->type == CMD_SIMPLE) {
        const simple_command *s = &c->as.simple;
 
        fputs("SIMPLE argv=[", out);
        for (size_t i = 0; i < s->argv_count; i++)
            fprintf(out, "%s%s", i ? " " : "", s->argv[i]);
        fputc(']', out);
        for (size_t i = 0; i < s->redirs_count; i++)
            fprintf(out, " %s%s", REDIR_SYM[s->redirs[i].op], s->redirs[i].file);
        fputc('\n', out);
        return;
    }
    fputs("SUBSHELL\n", out);
    dump_list(c->as.subshell.body, out, depth + 1);
    for (size_t i = 0; i < c->as.subshell.redirs_count; i++) {
        indent(out, depth + 1);
        fprintf(out, "REDIR %s%s\n", REDIR_SYM[c->as.subshell.redirs[i].op],
                c->as.subshell.redirs[i].file);
    }
}
 
static void dump_pipeline(const pipeline *pl, FILE *out, int depth)
{
    if (pl->count > 1) {
        indent(out, depth++);
        fputs("PIPELINE\n", out);
    }
    for (size_t i = 0; i < pl->count; i++)
        dump_command(&pl->commands[i], out, depth);
}
 
static void dump_and_or(const and_or *ao, FILE *out, int depth)
{
    if (ao->count > 1) {
        indent(out, depth++);
        fputs("AND_OR\n", out);
    }
    for (size_t i = 0; i < ao->count; i++) {
        dump_pipeline(&ao->pipelines[i], out, depth);
        if (i + 1 < ao->count) {
            indent(out, depth);
            fputs(ao->ops[i] == AND_IF_OP ? "&&\n" : "||\n", out);
        }
    }
}
 
static void dump_list(const ast_list *l, FILE *out, int depth)
{
    for (size_t i = 0; i < l->count; i++) {
        dump_and_or(&l->items[i], out, depth);
        if (l->seps[i] != SEP_NONE) {
            indent(out, depth);
            fputs(l->seps[i] == SEP_SEQ ? ";\n" : "&\n", out);
        }
    }
}
 
void ast_list_dump(const ast_list *list, FILE *out)
{
    if (list->count == 0)
        fputs("(empty)\n", out);
    else
        dump_list(list, out, 0);
}