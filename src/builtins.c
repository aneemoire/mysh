/*
    builtins.c - встроенные команды: cd, pwd, echo, help, exit, jobs, fg, bg,
    kill, wait.

    Встроенная команда выполняется самим интерпретатором (или дочерним
    процессом, если она стоит в конвейере, подоболочке или запущена в фоне).
    Интерфейс модуля (builtin_exists, builtin_run) объявлен в executor.h.
*/
#include "executor.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "jobs.h"

#define EXIT_GENERAL      1
#define EXIT_USAGE        2   // неверное использование команды
#define EXIT_NO_SUCH_JOB  127 // wait: нет такого задания или процесса
#define EXIT_RANGE        256 // код выхода приводится к 0..255
#define CWD_BUF_INIT      128
#define MAX_SIGNAL        64

typedef int (*builtin_fn)(int argc, char **argv, exec_ctx *ctx);

typedef struct {
    const char *name;
    builtin_fn fn;
    const char *help;
} builtin;

static int bi_cd(int argc, char **argv, exec_ctx *ctx);
static int bi_pwd(int argc, char **argv, exec_ctx *ctx);
static int bi_echo(int argc, char **argv, exec_ctx *ctx);
static int bi_help(int argc, char **argv, exec_ctx *ctx);
static int bi_exit(int argc, char **argv, exec_ctx *ctx);
static int bi_jobs(int argc, char **argv, exec_ctx *ctx);
static int bi_fg(int argc, char **argv, exec_ctx *ctx);
static int bi_bg(int argc, char **argv, exec_ctx *ctx);
static int bi_kill(int argc, char **argv, exec_ctx *ctx);
static int bi_wait(int argc, char **argv, exec_ctx *ctx);

static const builtin BUILTINS[] = {
    { "cd", bi_cd, "cd [каталог] сменить текущий каталог (без аргумента - $HOME)" },
    { "pwd", bi_pwd, "pwd вывести текущий каталог" },
    { "echo", bi_echo, "echo [-n] [аргументы] вывести аргументы (-n: без перевода строки)" },
    { "help", bi_help, "help эта справка" },
    { "exit", bi_exit, "exit [код] завершить интерпретатор" },
    { "jobs", bi_jobs, "jobs список заданий" },
    { "fg", bi_fg, "fg [%n] перевести задание на передний план" },
    { "bg", bi_bg, "bg [%n] продолжить остановленное задание в фоне" },
    { "kill", bi_kill, "kill [-s СИГНАЛ | -СИГНАЛ] цель ... послать сигнал процессу или заданию" },
    { "wait", bi_wait, "wait [pid|%n] дождаться завершения заданий" },
};
#define BUILTINS_COUNT (sizeof BUILTINS / sizeof BUILTINS[0])

static const builtin *find_builtin(const char *name)
{
    for (size_t i = 0; i < BUILTINS_COUNT; i++)
        if (strcmp(BUILTINS[i].name, name) == 0)
            return &BUILTINS[i];
    return NULL;
}

bool builtin_exists(const char *name)
{
    return find_builtin(name) != NULL;
}

int builtin_run(const simple_command *s, exec_ctx *ctx)
{
    const builtin *b = find_builtin(s->argv[0]);
    int rc = b->fn((int)s->argv_count, s->argv, ctx);

    if (fflush(stdout) != 0) {
        fprintf(stderr, "mysh: %s: write error: %s\n", s->argv[0], strerror(errno));
        clearerr(stdout);
        if (rc == 0)
            rc = EXIT_GENERAL;
    }
    return rc;
}

// cd, pwd

static char *current_dir(void)
{
    size_t size = CWD_BUF_INIT;

    for (;;) {
        char *buf = malloc(size);

        if (buf == NULL)
            return NULL;
        if (getcwd(buf, size) != NULL)
            return buf;
        free(buf);
        if (errno != ERANGE)
            return NULL;
        size *= 2;
    }
}

static int bi_cd(int argc, char **argv, exec_ctx *ctx)
{
    const char *dir;
    char *old, *now;

    (void)ctx;
    if (argc > 2) {
        fprintf(stderr, "mysh: cd: too many arguments\n");
        return EXIT_GENERAL;
    }
    if (argc == 2) {
        dir = argv[1];
    } else {
        dir = getenv("HOME");
        if (dir == NULL) {
            fprintf(stderr, "mysh: cd: HOME not set\n");
            return EXIT_GENERAL;
        }
    }
    old = current_dir();
    if (chdir(dir) < 0) {
        fprintf(stderr, "mysh: cd: %s: %s\n", dir, strerror(errno));
        free(old);
        return EXIT_GENERAL;
    }
    now = current_dir();
    if (now != NULL && setenv("PWD", now, 1) < 0)
        fprintf(stderr, "mysh: cd: setenv: %s\n", strerror(errno));
    if (old != NULL && setenv("OLDPWD", old, 1) < 0)
        fprintf(stderr, "mysh: cd: setenv: %s\n", strerror(errno));
    free(old);
    free(now);
    return 0;
}

static int bi_pwd(int argc, char **argv, exec_ctx *ctx)
{
    char *dir;

    (void)ctx;
    if (argc > 1 && argv[1][0] == '-' && argv[1][1] != '\0') {
        fprintf(stderr, "mysh: pwd: %s: invalid option\n", argv[1]);
        return EXIT_USAGE;
    }
    dir = current_dir();
    if (dir == NULL) {
        fprintf(stderr, "mysh: pwd: error retrieving current directory: %s\n", strerror(errno));
        return EXIT_GENERAL;
    }
    puts(dir);
    free(dir);
    return 0;
}

// echo, help
static bool is_n_option(const char *a)
{
    if (a[0] != '-' || a[1] != 'n')
        return false;
    for (const char *p = a + 1; *p != '\0'; p++)
        if (*p != 'n')
            return false;
    return true;
}

static int bi_echo(int argc, char **argv, exec_ctx *ctx)
{
    bool newline = true;
    int i = 1;

    (void)ctx;
    while (i < argc && is_n_option(argv[i])) {
        newline = false;
        i++;
    }
    for (int first = i; i < argc; i++) {
        if (i > first)
            fputc(' ', stdout);
        fputs(argv[i], stdout);
    }
    if (newline)
        fputc('\n', stdout);
    return 0;
}

static int bi_help(int argc, char **argv, exec_ctx *ctx)
{
    (void)argc;
    (void)argv;
    (void)ctx;
    puts("mysh: встроенные команды");
    for (size_t i = 0; i < BUILTINS_COUNT; i++)
        printf("  %s\n", BUILTINS[i].help);
    return 0;
}

// exit

static int bi_exit(int argc, char **argv, exec_ctx *ctx)
{
    long long code = ctx->last_status;

    if (argc > 1) {
        char *end;

        errno = 0;
        code = strtoll(argv[1], &end, 10);
        if (argv[1][0] == '\0' || *end != '\0' || errno == ERANGE) {
            fprintf(stderr, "mysh: exit: %s: numeric argument required\n", argv[1]);
            code = EXIT_USAGE;
        } else if (argc > 2) {
            fprintf(stderr, "mysh: exit: too many arguments\n");

            if (!ctx->job_control) {
                ctx->exit_requested = true;
                ctx->exit_code = EXIT_GENERAL;
            }
            return EXIT_GENERAL;
        }
    }
    if (ctx->job_control && jobs_has_stopped() && !ctx->exit_warned) {
        fprintf(stderr, "mysh: There are stopped jobs.\n");
        ctx->exit_warned = true;
        return EXIT_GENERAL;
    }
    if (ctx->job_control)
        fprintf(stderr, "exit\n");

    ctx->exit_requested = true;
    ctx->exit_code = (int)(((code % EXIT_RANGE) + EXIT_RANGE) % EXIT_RANGE);
    return ctx->exit_code;
}

// задания

static job *resolve_spec(const char *who, const char *spec)
{
    job *j;

    if (spec == NULL || strcmp(spec, "%+") == 0 || strcmp(spec, "%%") == 0 || strcmp(spec, "%") == 0) {
        j = jobs_current();
        if (j == NULL)
            fprintf(stderr, "mysh: %s: current: no such job\n", who);
        return j;
    }
    if (strcmp(spec, "%-") == 0) {
        j = jobs_previous();
        if (j == NULL)
            fprintf(stderr, "mysh: %s: previous: no such job\n", who);
        return j;
    }
    if (spec[0] == '%' && isdigit((unsigned char)spec[1])) {
        char *end;
        long id = strtol(spec + 1, &end, 10);

        if (*end == '\0' && id <= INT_MAX && (j = jobs_by_id((int)id)) != NULL)
            return j;
    }
    fprintf(stderr, "mysh: %s: %s: no such job\n", who, spec);
    return NULL;
}

static int bi_jobs(int argc, char **argv, exec_ctx *ctx)
{
    (void)ctx;
    if (argc > 1) {
        fprintf(stderr, "mysh: jobs: %s: invalid option\n", argv[1]);
        return EXIT_USAGE;
    }
    jobs_print_all(stdout);
    return 0;
}

static job *job_for_fg_bg(const char *who, int argc, char **argv, exec_ctx *ctx)
{
    job *j;

    if (!ctx->job_control) {
        fprintf(stderr, "mysh: %s: no job control\n", who);
        return NULL;
    }
    if (argc > 2) {
        fprintf(stderr, "mysh: %s: too many arguments\n", who);
        return NULL;
    }
    j = resolve_spec(who, argc == 2 ? argv[1] : NULL);
    if (j != NULL && job_get_state(j) == JOB_DONE) {
        fprintf(stderr, "mysh: %s: job has terminated\n", who);
        return NULL;
    }
    return j;
}

static int bi_fg(int argc, char **argv, exec_ctx *ctx)
{
    job *j = job_for_fg_bg("fg", argc, argv, ctx);

    if (j == NULL)
        return EXIT_GENERAL;
    puts(job_text(j));
    fflush(stdout);
    return jobs_continue_foreground(j);
}

static int bi_bg(int argc, char **argv, exec_ctx *ctx)
{
    job *j = job_for_fg_bg("bg", argc, argv, ctx);

    if (j == NULL)
        return EXIT_GENERAL;
    if (job_get_state(j) == JOB_RUNNING) {
        fprintf(stderr, "mysh: bg: job %d already in background\n", job_id(j));
        return 0;
    }
    return jobs_continue_background(j);
}

// kill

static const struct {
    const char *name;
    int number;
} SIGNAMES[] = {
    { "HUP", SIGHUP }, { "INT", SIGINT }, { "QUIT", SIGQUIT }, { "ILL", SIGILL },
    { "TRAP", SIGTRAP }, { "ABRT", SIGABRT }, { "BUS", SIGBUS }, { "FPE", SIGFPE },
    { "KILL", SIGKILL }, { "USR1", SIGUSR1 }, { "SEGV", SIGSEGV }, { "USR2", SIGUSR2 },
    { "PIPE", SIGPIPE }, { "ALRM", SIGALRM }, { "TERM", SIGTERM }, { "CHLD", SIGCHLD },
    { "CONT", SIGCONT }, { "STOP", SIGSTOP }, { "TSTP", SIGTSTP }, { "TTIN", SIGTTIN },
    { "TTOU", SIGTTOU },
};
#define SIGNAMES_COUNT (sizeof SIGNAMES / sizeof SIGNAMES[0])

static int parse_signal(const char *spec)
{
    if (isdigit((unsigned char)spec[0])) {
        char *end;
        long n = strtol(spec, &end, 10);

        return *end == '\0' && n <= MAX_SIGNAL ? (int)n : -1;
    }
    if (strncasecmp(spec, "SIG", 3) == 0)
        spec += 3;
    for (size_t i = 0; i < SIGNAMES_COUNT; i++)
        if (strcasecmp(spec, SIGNAMES[i].name) == 0)
            return SIGNAMES[i].number;
    return -1;
}

static int parse_kill_options(int argc, char **argv, int *sig, int *rc)
{
    int i = 1;

    for (; i < argc; i++) {
        const char *a = argv[i];
        const char *spec;

        if (strcmp(a, "--") == 0) {
            i++;
            break;
        }
        if (strcmp(a, "-s") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "mysh: kill: -s: option requires an argument\n");
                *rc = EXIT_GENERAL;
                return -1;
            }
            spec = argv[++i];
        } else if (a[0] == '-' && a[1] != '\0') {
            spec = a + 1;
        } else {
            break;
        }
        *sig = parse_signal(spec);
        if (*sig < 0) {
            fprintf(stderr, "mysh: kill: %s: invalid signal specification\n", spec);
            *rc = EXIT_GENERAL;
            return -1;
        }
    }
    if (i >= argc) {
        fprintf(stderr, "mysh: kill: usage: kill [-s sigspec | -sigspec] pid | %%jobspec ...\n");
        *rc = EXIT_USAGE;
        return -1;
    }
    return i;
}

static bool parse_pid(const char *s, pid_t *pid)
{
    char *end;
    long n;

    if (!isdigit((unsigned char)s[0]))
        return false;
    errno = 0;
    n = strtol(s, &end, 10);
    if (*end != '\0' || errno == ERANGE || n > INT_MAX)
        return false;
    *pid = (pid_t)n;
    return true;
}

static int bi_kill(int argc, char **argv, exec_ctx *ctx)
{
    int sig = SIGTERM, rc = 0;
    int i = parse_kill_options(argc, argv, &sig, &rc);

    (void)ctx;
    if (i < 0)
        return rc;
    for (; i < argc; i++) {
        const char *target = argv[i];
        pid_t pid;

        if (target[0] == '%') {
            job *j = resolve_spec("kill", target);

            if (j == NULL) {
                rc = EXIT_GENERAL;
            } else if (jobs_signal(j, sig) < 0) {
                fprintf(stderr, "mysh: kill: %s: %s\n", target, strerror(errno));
                rc = EXIT_GENERAL;
            }
        } else if (!parse_pid(target, &pid)) {
            fprintf(stderr, "mysh: kill: %s: arguments must be process or job IDs\n", target);
            rc = EXIT_GENERAL;
        } else if (kill(pid, sig) < 0) {
            fprintf(stderr, "mysh: kill: (%d) - %s\n", (int)pid, strerror(errno));
            rc = EXIT_GENERAL;
        }
    }
    return rc;
}

// wait

static int bi_wait(int argc, char **argv, exec_ctx *ctx)
{
    int rc = 0;

    (void)ctx;
    if (argc == 1) {
        jobs_wait_all();
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        pid_t pid = 0;
        job *j;

        if (arg[0] == '%') {
            j = resolve_spec("wait", arg);
            if (j == NULL) {
                rc = EXIT_NO_SUCH_JOB;
                continue;
            }
        } else if (!parse_pid(arg, &pid)) {
            fprintf(stderr, "mysh: wait: `%s': not a pid or valid job spec\n", arg);
            rc = EXIT_GENERAL;
            continue;
        } else if ((j = jobs_by_pid(pid)) == NULL) {
            fprintf(stderr, "mysh: wait: pid %s is not a child of this shell\n", arg);
            rc = EXIT_NO_SUCH_JOB;
            continue;
        }
        rc = jobs_wait(j, pid);
    }
    return rc;
}