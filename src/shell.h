/*
    shell.h - главный цикл интерпретатора.

    Связывает фазы: строка -> лексер -> парсер -> исполнитель,
    читает команды из разных источников (аргумент -c или стандартный ввод) и решает,
    как вести себя при ошибках в интерактивном и неинтерактивном режимах.
*/
#ifndef SHELL_H
#define SHELL_H

typedef enum {
    RUN_EXEC, // выполнять команды 
    RUN_DUMP_TOKENS, // только печатать токены (--dump-tokens)
    RUN_DUMP_AST // только печатать дерево команд (--dump-ast)
} run_mode;

int shell_run_string(const char *input, run_mode mode);

int shell_run_stdin(run_mode mode);

#endif