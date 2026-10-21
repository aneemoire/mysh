/*
    parser.h - синтаксический анализ: поток лексем -> дерево команд (AST).
    Модуль строит явную структуру данных (дерево), а не выполняет действия по ходу разбора. 
    Не делает системных вызовов и ничего не знает об исполнителе - на входе token_list от лексера, на выходе ast_list.
*/
#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>
#include <stdio.h>

#include "lexer.h"


typedef enum {
    REDIR_IN,     // < 
    REDIR_OUT,    // >  
    REDIR_APPEND  // >> 
} redir_op;

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
    size_t count;   // число pipelines 
} and_or;

typedef enum {
    SEP_SEQ,  // ;
    SEP_BG,   // & 
    SEP_NONE  // элемент последний, разделителя после него нет
} list_sep;

struct ast_list {
    and_or *items;
    list_sep *seps;
    size_t count;
};

typedef enum {
    PARSE_OK,
    PARSE_SYNTAX_ERROR,
    PARSE_NOMEM
} parse_status;

parse_status parse(const token_list *tokens, ast_list *out, char **error);

void ast_list_free(ast_list *list);

// Отладочный вывод для --dump-ast
void ast_list_dump(const ast_list *list, FILE *out);

#endif