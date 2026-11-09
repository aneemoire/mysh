/*
    executor.h - выполнение дерева команд.

    Третья фаза конвейера: на входе ast_list от парсера, на выходе код возврата. 
    Здесь появляются системные вызовы (fork, exec, waitpid).

    Сейчас умеет выполнять только простую команду без перенаправлений;
    остальные конструкции отвергаются сообщением "not implemented yet".
*/
#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "ast.h"

int exec_list(const ast_list *list);

#endif