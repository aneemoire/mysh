/*
    redirect.h - применение перенаправлений ввода-вывода (< > >>).

    Модуль превращает список redirection из дерева команд в вызовы open и dup2.
    Вызывается в дочернем процессе после fork и до exec, поэтому затрагивает
    только выполняемую команду.
*/
#ifndef REDIRECT_H
#define REDIRECT_H

#include <stddef.h>

#include "ast.h"

int redir_apply(const redirection *redirs, size_t count);

typedef struct {
    int in;
    int out; 
} redir_saved;

int redir_save(redir_saved *saved);
int redir_restore(const redir_saved *saved);

#endif