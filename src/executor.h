/*
    executor.h - выполнение дерева команд.

    Третья фаза конвейера: на входе ast_list от парсера, на выходе код возврата. 
    Здесь появляются системные вызовы (fork, exec, pipe, dup2, waitpid).

    Выполняются все конструкции грамматики: простые команды (внешние и
    встроенные) с перенаправлениями, конвейеры, списки с ";", "&&", "||",
    запуск в фоне "&" и подоболочки.
*/
#ifndef EXECUTOR_H
#define EXECUTOR_H

#include <stdbool.h>

#include "ast.h"

typedef struct {
    bool job_control;
    int last_status;
    bool exit_requested;
    int exit_code;   
    bool exit_warned;
} exec_ctx;

int exec_list(const ast_list *list, exec_ctx *ctx);

bool builtin_exists(const char *name);

int builtin_run(const simple_command *s, exec_ctx *ctx);

#endif