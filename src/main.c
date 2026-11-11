/*
    main.c - точка входа: разбор аргументов командной строки.

    mysh - интерактивный режим (или команды из stdin)
    mysh -c 'строка' - выполнить одну строку
    mysh < файл - выполнить команды из файла

    Отладочные режимы (вместо выполнения печатают результат и завершаются):
    mysh --dump-tokens [-c 'строка'] - токены
    mysh --dump-ast [-c 'строка'] - дерево команд
*/
#include <stdio.h>
#include <string.h>

#include "shell.h"

int main(int argc, char **argv)
{
    run_mode mode = RUN_EXEC;
    int i = 1;
 
    if (argc >= 2 && strcmp(argv[1], "--dump-tokens") == 0) {
        mode = RUN_DUMP_TOKENS;
        i = 2;
    } else if (argc >= 2 && strcmp(argv[1], "--dump-ast") == 0) {
        mode = RUN_DUMP_AST;
        i = 2;
    }
 
    if (argc == i + 2 && strcmp(argv[i], "-c") == 0)
        return shell_run_string(argv[i + 1], mode);
    if (argc == i)
        return shell_run_stdin(mode);
 
    fprintf(stderr, "usage: mysh [-c 'command'] | mysh --dump-tokens|--dump-ast [-c 'command']\n");
    return 1;
}