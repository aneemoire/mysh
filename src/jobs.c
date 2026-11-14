/*
    jobs.c - таблица заданий, сигналы, терминал

    Модель: у задания есть список процессов; 
    состояние задания выводится из состояний процессов (Done - все завершены, Stopped - все живые остановлены, иначе Running). 
    Статусы собираются вызовом waitpid(-1, ...) с флагами WUNTRACED | WCONTINUED: 
    так один вызов видит и завершение, и остановку, и продолжение любого процесса, в том числе фонового.
*/
#include "jobs.h"

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define TTY_FD STDIN_FILENO 
#define STATE_WIDTH 24
#define STATE_TEXT_SIZE 64
#define EXIT_GENERAL 1
#define EXIT_SIGNAL_BASE 128 
#define EXIT_LOST_PROCESS 127

typedef struct {
    pid_t pid;
    bool done;
    bool stopped;
    int status;
} proc;

struct job {
    int id;
    pid_t pgid;
    proc *procs;
    size_t count;
    size_t cap;
    char *text;
    job_state state;
    bool foreground;
    bool notified;
    job *next;
};

static const int JOB_SIGNALS[] = { SIGINT, SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU };
#define JOB_SIGNALS_COUNT (sizeof JOB_SIGNALS / sizeof JOB_SIGNALS[0])

static volatile sig_atomic_t sigchld_pending;
static job *job_list;
static int current_id;
static int previous_id;
static bool job_control;
static pid_t shell_pgid;

static void on_sigchld(int sig)
{
    int saved_errno = errno;

    (void)sig;
    sigchld_pending = 1;
    errno = saved_errno;
}

static int set_handler(int sig, void (*handler)(int), int flags)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = handler;
    sa.sa_flags = flags;
    sigemptyset(&sa.sa_mask);
    if (sigaction(sig, &sa, NULL) < 0) {
        fprintf(stderr, "mysh: sigaction(%d): %s\n", sig, strerror(errno));
        return -1;
    }
    return 0;
}

static bool take_sigchld(void)
{
    bool was = sigchld_pending != 0;

    sigchld_pending = 0;
    return was;
}

int jobs_child_signals_default(void)
{
    for (size_t i = 0; i < JOB_SIGNALS_COUNT; i++)
        if (set_handler(JOB_SIGNALS[i], SIG_DFL, 0) < 0)
            return -1;
    return 0;
}

int jobs_child_signals_ignore_keys(void)
{
    if (set_handler(SIGINT, SIG_IGN, 0) < 0 || set_handler(SIGQUIT, SIG_IGN, 0) < 0)
        return -1;
    return 0;
}

static int proc_code(const proc *p)
{
    if (WIFEXITED(p->status))
        return WEXITSTATUS(p->status);
    if (WIFSIGNALED(p->status))
        return EXIT_SIGNAL_BASE + WTERMSIG(p->status);
    if (WIFSTOPPED(p->status))
        return EXIT_SIGNAL_BASE + WSTOPSIG(p->status);
    return EXIT_GENERAL;
}

static void update_state(job *j)
{
    size_t alive = 0, stopped = 0;

    for (size_t i = 0; i < j->count; i++) {
        if (!j->procs[i].done) {
            alive++;
            if (j->procs[i].stopped)
                stopped++;
        }
    }
    if (alive == 0)
        j->state = JOB_DONE;
    else if (stopped == alive)
        j->state = JOB_STOPPED;
    else
        j->state = JOB_RUNNING;
}

static int stop_signal(const job *j)
{
    for (size_t i = 0; i < j->count; i++)
        if (j->procs[i].stopped && !j->procs[i].done)
            return WSTOPSIG(j->procs[i].status);
    return SIGTSTP;
}

static void apply_status(pid_t pid, int status)
{
    for (job *j = job_list; j != NULL; j = j->next) {
        for (size_t i = 0; i < j->count; i++) {
            proc *p = &j->procs[i];

            if (p->pid != pid)
                continue;
            if (WIFSTOPPED(status)) {
                p->stopped = true;
                p->status = status;
            } else if (WIFCONTINUED(status)) {
                p->stopped = false;
                j->notified = false;
            } else {
                p->done = true;
                p->stopped = false;
                p->status = status;
            }
            update_state(j);
            return;
        }
    }
}

static int wait_any(bool block, pid_t *pid, int *status)
{
    int flags = WUNTRACED | WCONTINUED | (block ? 0 : WNOHANG);

    for (;;) {
        *pid = waitpid(-1, status, flags);
        if (*pid > 0)
            return 1;
        if (*pid == 0)
            return 0;
        if (errno == EINTR)
            continue;
        if (errno != ECHILD)
            fprintf(stderr, "mysh: waitpid: %s\n", strerror(errno));
        return -1;
    }
}

static bool reap_blocking(void)
{
    pid_t pid;
    int status;

    if (wait_any(true, &pid, &status) != 1)
        return false;
    apply_status(pid, status);
    return true;
}

static void force_done(job *j)
{
    for (size_t i = 0; i < j->count; i++) {
        if (!j->procs[i].done) {
            j->procs[i].done = true;
            j->procs[i].stopped = false;
            j->procs[i].status = EXIT_LOST_PROCESS << 8;
        }
    }
    update_state(j);
}

static void wait_while_running(job *j)
{
    while (j->count > 0 && j->state == JOB_RUNNING) {
        if (!reap_blocking()) {
            force_done(j);
            break;
        }
    }
}

static job *pick_other(int exclude)
{
    job *best = NULL;

    for (job *j = job_list; j != NULL; j = j->next)
        if (!j->foreground && j->id != exclude)
            best = j;
    return best;
}

static void set_current(const job *j)
{
    if (current_id == j->id)
        return;
    previous_id = current_id;
    current_id = j->id;
}

static void remove_job(job *j)
{
    job **pp = &job_list;
    const job *other;

    while (*pp != j)
        pp = &(*pp)->next;
    *pp = j->next;

    if (current_id == j->id)
        current_id = previous_id == j->id ? 0 : previous_id;
    if (previous_id == j->id || previous_id == current_id)
        previous_id = 0;
    if (current_id == 0 && (other = pick_other(0)) != NULL)
        current_id = other->id;
    if (previous_id == 0 && (other = pick_other(current_id)) != NULL)
        previous_id = other->id;

    free(j->procs);
    free(j->text);
    free(j);
}

int jobs_init(bool want_job_control)
{
    job_control = want_job_control;
    if (set_handler(SIGCHLD, on_sigchld, SA_RESTART) < 0) {
        job_control = false;
        return -1;
    }
    if (!job_control)
        return 0;
    for (size_t i = 0; i < JOB_SIGNALS_COUNT; i++) {
        if (set_handler(JOB_SIGNALS[i], SIG_IGN, 0) < 0) {
            job_control = false;
            return -1;
        }
    }
    shell_pgid = getpid();
    if (setpgid(0, shell_pgid) < 0 && errno != EPERM) {
        fprintf(stderr, "mysh: setpgid: %s\n", strerror(errno));
        job_control = false;
        return -1;
    }
    shell_pgid = getpgrp();
    if (tcsetpgrp(TTY_FD, shell_pgid) < 0) {
        fprintf(stderr, "mysh: tcsetpgrp: %s\n", strerror(errno));
        job_control = false;
        return -1;
    }
    return 0;
}

void jobs_reset_after_fork(void)
{
    job *next;

    for (job *j = job_list; j != NULL; j = next) {
        next = j->next;
        free(j->procs);
        free(j->text);
        free(j);
    }
    job_list = NULL;
    current_id = previous_id = 0;
    job_control = false;
}

job *jobs_create(char *text)
{
    job *j = calloc(1, sizeof *j);
    job **pp = &job_list;
    int id = 1;

    if (j == NULL) {
        free(text);
        return NULL;
    }
    while (*pp != NULL && (*pp)->id == id) {
        id++;
        pp = &(*pp)->next;
    }
    j->id = id;
    j->text = text;
    j->state = JOB_RUNNING;
    j->foreground = true;
    j->next = *pp;
    *pp = j;
    return j;
}

bool jobs_reserve(job *j, size_t count)
{
    proc *p = realloc(j->procs, count * sizeof *p);

    if (p == NULL)
        return false;
    j->procs = p;
    j->cap = count;
    return true;
}

void jobs_add_proc(job *j, pid_t pid)
{
    j->procs[j->count++] = (proc){ .pid = pid };
    update_state(j);
}

void jobs_set_pgid(job *j, pid_t pgid)
{
    j->pgid = pgid;
}

void jobs_abort(job *j)
{
    for (size_t i = 0; i < j->count; i++)
        if (!j->procs[i].done && kill(j->procs[i].pid, SIGKILL) < 0 && errno != ESRCH)
            fprintf(stderr, "mysh: kill: %s\n", strerror(errno));
    wait_while_running(j);
    remove_job(j);
}


static void describe_state(const job *j, char *buf, size_t size)
{
    const proc *last = &j->procs[j->count - 1];

    switch (j->state) {
    case JOB_RUNNING:
        snprintf(buf, size, "Running");
        return;
    case JOB_STOPPED:
        snprintf(buf, size, "Stopped");
        return;
    case JOB_DONE:
        break;
    }
    if (WIFSIGNALED(last->status))
        snprintf(buf, size, "%s", strsignal(WTERMSIG(last->status)));
    else if (WEXITSTATUS(last->status) != 0)
        snprintf(buf, size, "Exit %d", WEXITSTATUS(last->status));
    else
        snprintf(buf, size, "Done");
}

static void print_line(FILE *out, const job *j)
{
    char mark = j->id == current_id ? '+' : j->id == previous_id ? '-' : ' ';
    char state[STATE_TEXT_SIZE];

    describe_state(j, state, sizeof state);
    fprintf(out, "[%d]%c  %-*s%s%s\n", j->id, mark, STATE_WIDTH, state, j->text,
            j->state == JOB_RUNNING ? " &" : "");
}

void jobs_print_all(FILE *out)
{
    job *next;

    jobs_reap();
    for (job *j = job_list; j != NULL; j = next) {
        next = j->next;
        if (j->foreground)
            continue;
        print_line(out, j);
        if (j->state == JOB_DONE)
            remove_job(j);
    }
}

void jobs_notify(void)
{
    job *next;

    jobs_reap();
    for (job *j = job_list; j != NULL; j = next) {
        next = j->next;
        if (j->foreground)
            continue;
        if (j->state == JOB_DONE) {
            print_line(stderr, j);
            remove_job(j);
        } else if (j->state == JOB_STOPPED && !j->notified) {
            print_line(stderr, j);
            j->notified = true;
        }
    }
}

void jobs_reap(void)
{
    pid_t pid;
    int status;

    if (!take_sigchld())
        return;
    while (wait_any(false, &pid, &status) == 1)
        apply_status(pid, status);
}

static void tty_give(pid_t pgid)
{
    if (tcsetpgrp(TTY_FD, pgid) < 0)
        fprintf(stderr, "mysh: tcsetpgrp: %s\n", strerror(errno));
}

static void purge_done_background(void)
{
    job *next;

    for (job *j = job_list; j != NULL; j = next) {
        next = j->next;
        if (!j->foreground && j->state == JOB_DONE)
            remove_job(j);
    }
}

int jobs_run_foreground(job *j)
{
    bool tty = job_control && j->pgid > 0;
    const proc *last;
    int rc;

    if (j->count == 0) {
        remove_job(j);
        return EXIT_GENERAL;
    }
    j->foreground = true;
    if (tty)
        tty_give(j->pgid);
    wait_while_running(j);
    if (tty)
        tty_give(shell_pgid);

    if (j->state == JOB_STOPPED) {
        j->foreground = false;
        j->notified = true;
        set_current(j);
        rc = EXIT_SIGNAL_BASE + stop_signal(j);
        if (job_control) {
            fputc('\n', stderr);
            print_line(stderr, j);
        }
        return rc;
    }
    last = &j->procs[j->count - 1];
    rc = proc_code(last);
    if (job_control && WIFSIGNALED(last->status) && WTERMSIG(last->status) == SIGINT)
        fputc('\n', stderr);
    remove_job(j);
    if (!job_control)
        purge_done_background();
    return rc;
}

void jobs_background(job *j, bool announce)
{
    j->foreground = false;
    set_current(j);
    if (announce && j->count > 0)
        fprintf(stderr, "[%d] %d\n", j->id, (int)j->procs[j->count - 1].pid);
}

job *jobs_by_id(int id)
{
    for (job *j = job_list; j != NULL; j = j->next)
        if (j->id == id && !j->foreground)
            return j;
    return NULL;
}

job *jobs_current(void)
{
    return jobs_by_id(current_id);
}

job *jobs_previous(void)
{
    return jobs_by_id(previous_id);
}

job *jobs_by_pid(pid_t pid)
{
    for (job *j = job_list; j != NULL; j = j->next)
        for (size_t i = 0; i < j->count; i++)
            if (j->procs[i].pid == pid && !j->foreground)
                return j;
    return NULL;
}

int job_id(const job *j)
{
    return j->id;
}

const char *job_text(const job *j)
{
    return j->text;
}

job_state job_get_state(const job *j)
{
    return j->state;
}

int jobs_signal(job *j, int sig)
{
    int rc = 0, saved = 0;
    bool wake = j->state == JOB_STOPPED && (sig == SIGTERM || sig == SIGHUP);

    for (int pass = 0; pass < (wake ? 2 : 1); pass++) {
        int s = pass == 0 ? sig : SIGCONT;

        if (j->pgid > 0) {
            if (kill(-j->pgid, s) < 0 && rc == 0) {
                rc = -1;
                saved = errno;
            }
            continue;
        }
        for (size_t i = 0; i < j->count; i++) {
            if (j->procs[i].done)
                continue;
            if (kill(j->procs[i].pid, s) < 0 && errno != ESRCH && rc == 0) {
                rc = -1;
                saved = errno;
            }
        }
    }
    errno = saved;
    return rc;
}

static int resume(job *j)
{
    for (size_t i = 0; i < j->count; i++)
        j->procs[i].stopped = false;
    update_state(j);
    j->notified = false;
    if (jobs_signal(j, SIGCONT) < 0 && errno != ESRCH) {
        fprintf(stderr, "mysh: kill: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

int jobs_continue_foreground(job *j)
{
    if (resume(j) < 0)
        return EXIT_GENERAL;
    return jobs_run_foreground(j);
}

int jobs_continue_background(job *j)
{
    if (resume(j) < 0)
        return EXIT_GENERAL;
    j->foreground = false;
    set_current(j);
    printf("[%d]%c %s &\n", j->id, j->id == current_id ? '+' : j->id == previous_id ? '-' : ' ', j->text);
    return 0;
}

int jobs_wait(job *j, pid_t pid)
{
    proc *target = &j->procs[j->count - 1];
    int rc;

    if (pid != 0) {
        target = NULL;
        for (size_t i = 0; i < j->count; i++)
            if (j->procs[i].pid == pid)
                target = &j->procs[i];
        if (target == NULL)
            return EXIT_LOST_PROCESS;
    }
    while (!target->done && j->state != JOB_STOPPED) {
        if (!reap_blocking()) {
            force_done(j);
            break;
        }
    }
    rc = target->done ? proc_code(target) : EXIT_SIGNAL_BASE + stop_signal(j);
    if (j->state == JOB_DONE)
        remove_job(j);
    if (!job_control)
        purge_done_background();
    return rc;
}

void jobs_wait_all(void)
{
    job *next;

    for (;;) {
        bool pending = false;

        for (job *j = job_list; j != NULL; j = j->next)
            if (!j->foreground && j->state == JOB_RUNNING)
                pending = true;
        if (!pending)
            break;
        if (!reap_blocking()) {
            for (job *j = job_list; j != NULL; j = j->next)
                if (!j->foreground && j->state == JOB_RUNNING)
                    force_done(j);
            break;
        }
    }
    for (job *j = job_list; j != NULL; j = next) {
        next = j->next;
        if (!j->foreground && j->state == JOB_DONE)
            remove_job(j);
    }
}

bool jobs_has_stopped(void)
{
    for (job *j = job_list; j != NULL; j = j->next)
        if (!j->foreground && j->state == JOB_STOPPED)
            return true;
    return false;
}