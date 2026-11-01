/*
    parser.h - синтаксический анализ: поток лексем -> дерево команд (AST).
    Модуль строит явную структуру данных (дерево), а не выполняет действия по ходу разбора. 
    Не делает системных вызовов и ничего не знает об исполнителе - на входе token_list от лексера, на выходе ast_list.
*/
#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>
#include <stdio.h>

#include "ast.h"
#include "lexer.h"


typedef enum {
    PARSE_OK,
    PARSE_SYNTAX_ERROR,
    PARSE_NOMEM
} parse_status;

parse_status parse(const token_list *tokens, ast_list *out, char **error);

#endif