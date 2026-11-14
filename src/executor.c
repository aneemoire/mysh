/*
    executor.c — выполнение дерева команд

    Любая внешняя команда, подоболочка, фоновый список и встроенная команда внутри конвейера выполняются в дочернем процессе; 
    процессы одного конвейера образуют задание (модуль jobs). 
    Встроенная команда, стоящая одна на переднем плане, выполняется в самом интерпретаторе:
    иначе cd, exit и подобные команды не могли бы менять его состояние.
*/
#include "executor.h"
#include "redirect.h"
#include "jobs.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define EXIT_GENERAL      1   // ошибка самого интерпретатора (fork, нехватка памяти)
#define EXIT_CANNOT_EXEC  126 // команда найдена, но не может быть выполнена 
#define EXIT_NOT_FOUND    127 // команда не найдена

typedef struct {
    int in_fd;
    int out_fd;
    int unused_fd;
    bool use_pgroups;
    pid_t pgid; 
    bool take_terminal; 
    bool ignore_keys; 
} spawn_opts;

typedef struct {
    const command *cmd;
    const and_or *ao;
} work;

static int exec_and_or(const and_or *ao, exec_ctx *ctx);

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

static void child_fail(const char *what)
{
    fprintf(stderr, "mysh: %s: %s\n", what, strerror(errno));
    _exit(EXIT_GENERAL);
}

static int close_checked(int fd)
{
    if (fd >= 0 && close(fd) < 0) {
        fprintf(stderr, "mysh: close: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

static int move_fd(int from, int to)
{
    int r;

    if (from == to)
        return 0;
    do
        r = dup2(from, to);
    while (r < 0 && errno == EINTR);
    if (r < 0)
        return -1;
    return close(from);
}
static char **build_argv(const simple_command *s)
{
    char **argv = malloc((s->argv_count + 1) * sizeof *argv);

    if (argv == NULL) {
        fprintf(stderr, "mysh: out of memory\n");
        return NULL;
    }
    memcpy(argv, s->argv, s->argv_count * sizeof *argv);
    argv[s->argv_count] = NULL;
    return argv;
}

static void finish_child(exec_ctx *ctx, int rc)
{
    if (ctx->exit_requested)
        rc = ctx->exit_code;
    if (fflush(NULL) != 0 && rc == 0)
        rc = EXIT_GENERAL;
    _exit(rc);
}

static void child_setup_pipes(const spawn_opts *o)
{
    if ((o->in_fd >= 0 && move_fd(o->in_fd, STDIN_FILENO) < 0) ||
        (o->out_fd >= 0 && move_fd(o->out_fd, STDOUT_FILENO) < 0))
        child_fail("dup2");
    if (o->unused_fd > STDERR_FILENO && close_checked(o->unused_fd) < 0)
        _exit(EXIT_GENERAL);
}

static void child_run(const work *w, char *const *argv, exec_ctx *ctx, const spawn_opts *o)
{
    const command *c = w->cmd;
    const redirection *redirs = NULL;
    size_t redirs_count = 0;

    if (o->use_pgroups) {
        if (setpgid(0, o->pgid) < 0)
            child_fail("setpgid");
        if (o->take_terminal && tcsetpgrp(STDIN_FILENO, getpgrp()) < 0)
            child_fail("tcsetpgrp");
    }
    if (jobs_child_signals_default() < 0 || (o->ignore_keys && jobs_child_signals_ignore_keys() < 0))
        _exit(EXIT_GENERAL);
    child_setup_pipes(o);
    ctx->job_control = false; 

    if (w->ao != NULL) {
        jobs_reset_after_fork();
        finish_child(ctx, exec_and_or(w->ao, ctx));
    }
    if (c->type == CMD_SIMPLE) {
        redirs = c->as.simple.redirs;
        redirs_count = c->as.simple.redirs_count;
    } else {
        redirs = c->as.subshell.redirs;
        redirs_count = c->as.subshell.redirs_count;
    }
    if (redir_apply(redirs, redirs_count) < 0)
        _exit(EXIT_GENERAL);

    if (c->type == CMD_SUBSHELL) {
        jobs_reset_after_fork();
        finish_child(ctx, exec_list(c->as.subshell.body, ctx));
    }
    const char *name = argv[0];

    if (builtin_exists(name)) {
        int rc = builtin_run(&c->as.simple, ctx);

        free((void *)argv);
        finish_child(ctx, rc);
    }
    execvp(name, argv);
    free((void *)argv);
    exec_failed(name);
}

static void parent_setpgid(pid_t pid, pid_t pgid)
{
    if (setpgid(pid, pgid) < 0 && errno != EACCES && errno != ESRCH)
        fprintf(stderr, "mysh: setpgid: %s\n", strerror(errno));
}

static pid_t spawn(const work *w, exec_ctx *ctx, const spawn_opts *o)
{
    char **argv = NULL;
    pid_t pid;

    if (w->cmd != NULL && w->cmd->type == CMD_SIMPLE) {
        argv = build_argv(&w->cmd->as.simple);
        if (argv == NULL)
            return -1;
    }
    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "mysh: fork: %s\n", strerror(errno));
        free(argv);
        return -1;
    }
    if (pid == 0)
        child_run(w, argv, ctx, o);
    free(argv);
    if (o->use_pgroups)
        parent_setpgid(pid, o->pgid != 0 ? o->pgid : pid);
    return pid;
}

static int open_dev_null(void)
{
    int fd;

    do
        fd = open("/dev/null", O_RDONLY);
    while (fd < 0 && errno == EINTR);
    if (fd < 0)
        fprintf(stderr, "mysh: /dev/null: %s\n", strerror(errno));
    return fd;
}

static int launch(const pipeline *pl, const and_or *ao, exec_ctx *ctx, bool background)
{
    const size_t count = ao != NULL ? 1 : pl->count;
    const bool jc = ctx->job_control;
    char *text = ao != NULL ? ast_and_or_text(ao) : ast_pipeline_text(pl);
    int prev_read = -1;
    pid_t pgid = 0;
    bool ok = true;
    size_t started = 0;
    job *j;

    j = text != NULL ? jobs_create(text) : NULL;
    if (j == NULL || !jobs_reserve(j, count)) {
        fprintf(stderr, "mysh: out of memory\n");
        if (j != NULL)
            jobs_abort(j);
        return EXIT_GENERAL;
    }
    if (background && !jc && (prev_read = open_dev_null()) < 0) {
        jobs_abort(j);
        return EXIT_GENERAL;
    }
    fflush(NULL);

    for (size_t i = 0; i < count; i++) {
        int fds[2] = { -1, -1 };
        work w = ao != NULL ? (work){ .ao = ao } : (work){ .cmd = &pl->commands[i] };
        spawn_opts o;
        pid_t pid;

        if (i + 1 < count && pipe(fds) < 0) {
            fprintf(stderr, "mysh: pipe: %s\n", strerror(errno));
            ok = false;
            break;
        }
        o = (spawn_opts){ .in_fd = prev_read, .out_fd = fds[1], .unused_fd = fds[0],
                          .use_pgroups = jc, .pgid = pgid,
                          .take_terminal = jc && !background, .ignore_keys = !jc && background };
        pid = spawn(&w, ctx, &o);
        if (close_checked(prev_read) < 0 || close_checked(fds[1]) < 0)
            ok = false;
        prev_read = fds[0];
        if (pid < 0) {
            ok = false;
            break;
        }
        jobs_add_proc(j, pid);
        started++;
        if (jc && i == 0) {
            pgid = pid;
            jobs_set_pgid(j, pid);
        }
        if (!ok)
            break;
    }
    if (close_checked(prev_read) < 0)
        ok = false;

    if (!ok || started != count) {
        jobs_abort(j);
        return EXIT_GENERAL;
    }
    if (background) {
        jobs_background(j, jc);
        return 0;
    }
    return jobs_run_foreground(j);
}

static int exec_builtin_inline(const simple_command *s, exec_ctx *ctx)
{
    redir_saved saved;
    int rc;

    if (redir_save(&saved) < 0)
        return EXIT_GENERAL;
    if (redir_apply(s->redirs, s->redirs_count) < 0)
        rc = EXIT_GENERAL;
    else
        rc = builtin_run(s, ctx); 
    if (redir_restore(&saved) < 0 && rc == 0)
        rc = EXIT_GENERAL;
    return rc;
}

static bool is_exit_command(const pipeline *pl)
{
    return pl->count == 1 && pl->commands[0].type == CMD_SIMPLE &&
           strcmp(pl->commands[0].as.simple.argv[0], "exit") == 0;
}

static int exec_pipeline(const pipeline *pl, exec_ctx *ctx, bool background)
{
    const command *c = &pl->commands[0];

    jobs_reap();
    if (!is_exit_command(pl))
        ctx->exit_warned = false;
    if (pl->count == 1 && !background && c->type == CMD_SIMPLE && builtin_exists(c->as.simple.argv[0]))
        return exec_builtin_inline(&c->as.simple, ctx);
    return launch(pl, NULL, ctx, background);
}

static int exec_and_or(const and_or *ao, exec_ctx *ctx)
{
    int rc = exec_pipeline(&ao->pipelines[0], ctx, false);

    ctx->last_status = rc;
    for (size_t i = 0; i < ao->count - 1 && !ctx->exit_requested; i++) {
        bool run = ao->ops[i] == AND_IF_OP ? rc == 0 : rc != 0;

        if (run) {
            rc = exec_pipeline(&ao->pipelines[i + 1], ctx, false);
            ctx->last_status = rc;
        }
    }
    return rc;
}

static int exec_background(const and_or *ao, exec_ctx *ctx)
{
    if (ao->count == 1)
        return exec_pipeline(&ao->pipelines[0], ctx, true);
    return launch(NULL, ao, ctx, true);
}

int exec_list(const ast_list *list, exec_ctx *ctx)
{
    int rc = 0;

    for (size_t i = 0; i < list->count && !ctx->exit_requested; i++) {
        if (list->seps[i] == SEP_BG)
            rc = exec_background(&list->items[i], ctx);
        else
            rc = exec_and_or(&list->items[i], ctx);
        ctx->last_status = rc;
    }
    return rc;
}