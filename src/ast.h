/*
    ast.h — дерево команд (AST): типы, выделение памяти, печать.
    Дерево — явная структура данных, строится парсером до исполнения.
    Вся память дерева (узлы, массивы, строки) выделяется через ast_alloc и освобождается одним вызовом ast_list_free(корень),
    поэтому по узлам дерево никогда не освобождают.
*/
#ifndef AST_H
#define AST_H

#include <stddef.h>
#include <stdio.h>

typedef struct ast_block ast_block;

typedef enum { REDIR_IN, REDIR_OUT, REDIR_APPEND } redir_op; /* < > >> */
 
typedef struct {
    redir_op op;
    char *file;
} redirection;

typedef struct {
    char **argv;   
    size_t argv_count;
    redirection *redirs;
    size_t redirs_count;
} simple_command;

typedef struct ast_list ast_list;

typedef struct {
    ast_list *body;        
    redirection *redirs;
    size_t redirs_count;
} subshell_command;

typedef enum { CMD_SIMPLE, CMD_SUBSHELL } command_type;
 
typedef struct {
    command_type type;
    union {
        simple_command simple;
        subshell_command subshell;
    } as;
} command;

typedef struct {
    command *commands;
    size_t count;
} pipeline;
 
typedef enum { AND_IF_OP, OR_IF_OP } and_or_op;
 
typedef struct {
    pipeline *pipelines;
    and_or_op *ops; 
    size_t count;  
} and_or;

typedef enum {
    SEP_SEQ,
    SEP_BG, 
    SEP_NONE 
} list_sep;

struct ast_list {
    and_or *items;
    list_sep *seps;  
    size_t count;
    ast_block *mem; 
};

void *ast_alloc(ast_block **mem, size_t size);

typedef struct { void *data; size_t n, cap; } ast_vec;

void *ast_vec_add(ast_block **mem, ast_vec *v, size_t elem);

char *ast_strdup(ast_block **mem, const char *s);

void ast_list_free(ast_list *list);

void ast_list_dump(const ast_list *list, FILE *out);

#endif