/*
    executor.c — выполнение дерева команд
*/
#include "executor.h"
#include "redirect.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define EXIT_GENERAL      1   // ошибка самого интерпретатора (fork, нехватка памяти)
#define EXIT_CANNOT_EXEC  126 // команда найдена, но не может быть выполнена 
#define EXIT_NOT_FOUND    127 // команда не найдена
#define EXIT_SIGNAL_BASE  128 // завершение по сигналу N даёт код 128 + N

static void exec_failed(const char *name)
{
    int err = errno;
    struct stat sb;
    const char *msg;

    if (err == ENOENT && strchr(name, '/') == NULL)
        msg = "command not found";
    else if (err == EACCES && stat(name, &sb) == 0 && S_ISDIR(sb.st_mode))
        msg = "Is a directory";
    else
        msg = strerror(err);
    fprintf(stderr, "mysh: %s: %s\n", name, msg);
    _exit(err == ENOENT ? EXIT_NOT_FOUND : EXIT_CANNOT_EXEC);
}

static int status_to_code(int status)
{
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return EXIT_SIGNAL_BASE + WTERMSIG(status);
    return EXIT_GENERAL;
}

static int run_external(char *const *argv,const redirection *redirs, size_t redirs_count)
{
    pid_t pid;
    int status;

    fflush(NULL);
    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "mysh: fork: %s\n", strerror(errno));
        return EXIT_GENERAL;
    }
    if (pid == 0) {
        if (redir_apply(redirs, redirs_count) < 0)
            _exit(EXIT_GENERAL);
        execvp(argv[0], argv);
        exec_failed(argv[0]);
    }
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            fprintf(stderr, "mysh: waitpid: %s\n", strerror(errno));
            return EXIT_GENERAL;
        }
    }
    return status_to_code(status);
}

static int exec_simple(const simple_command *s)
{
    char **argv;
    int rc;

    argv = malloc((s->argv_count + 1) * sizeof *argv);
    if (argv == NULL) {
        fprintf(stderr, "mysh: out of memory\n");
        return EXIT_GENERAL;
    }
    memcpy(argv, s->argv, s->argv_count * sizeof *argv);
    argv[s->argv_count] = NULL;
    rc = run_external(argv, s->redirs, s->redirs_count);
    free(argv);
    return rc;
}

static const char *find_unsupported(const ast_list *list)
{
    for (size_t i = 0; i < list->count; i++) {
        const and_or *ao = &list->items[i];
 
        if (list->seps[i] == SEP_BG)
            return "background (&)";
        for (size_t j = 0; j < ao->count; j++) {
            const pipeline *pl = &ao->pipelines[j];
 
            if (pl->count > 1)
                return "pipelines";
            if (pl->commands[0].type != CMD_SIMPLE)
                return "subshells";
        }
    }
    return NULL;
}
 
static int exec_pipeline(const pipeline *pl)
{
    return exec_simple(&pl->commands[0].as.simple);
}
 
static int exec_and_or(const and_or *ao)
{
    int rc = exec_pipeline(&ao->pipelines[0]);
 
    for (size_t i = 0; i < ao->count - 1; i++) {
        bool run = ao->ops[i] == AND_IF_OP ? rc == 0 : rc != 0;
 
        if (run)
            rc = exec_pipeline(&ao->pipelines[i + 1]);
    }
    return rc;
}

int exec_list(const ast_list *list)
{
    const char *what = find_unsupported(list);
    int rc = 0;
 
    if (what != NULL) {
        fprintf(stderr, "mysh: not implemented yet: %s\n", what);
        return EXIT_GENERAL;
    }
    for (size_t i = 0; i < list->count; i++)
        rc = exec_and_or(&list->items[i]);
    return rc;
}