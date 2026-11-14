/*
    jobs.h - таблица заданий, сигналы и управление терминалом.

    Задание - один конвейер, одна команда или одна подоболочка; состоит из
    одного или нескольких процессов. Модуль хранит таблицу заданий, собирает
    статусы процессов через waitpid, ждёт задания переднего плана, передаёт им
    терминал и печатает состояния заданий. Здесь же настраиваются сигналы
    (SIGCHLD и клавиатурные сигналы интерактивного режима): всё глобальное
    состояние интерпретатора - таблица заданий и флаг, который выставляет
    обработчик SIGCHLD, - заключено в этом модуле.
*/
#ifndef JOBS_H
#define JOBS_H

#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>

typedef struct job job;

typedef enum {
    JOB_RUNNING,
    JOB_STOPPED,
    JOB_DONE
} job_state;

int jobs_init(bool job_control);

int jobs_child_signals_default(void);

int jobs_child_signals_ignore_keys(void);

void jobs_reset_after_fork(void);

job *jobs_create(char *text);

bool jobs_reserve(job *j, size_t count);

void jobs_add_proc(job *j, pid_t pid);

void jobs_set_pgid(job *j, pid_t pgid);

void jobs_abort(job *j);

int jobs_run_foreground(job *j);

void jobs_background(job *j, bool announce);

void jobs_reap(void);

void jobs_notify(void);

void jobs_print_all(FILE *out);

job *jobs_by_id(int id);
job *jobs_current(void);
job *jobs_previous(void);
job *jobs_by_pid(pid_t pid);

int job_id(const job *j);
const char *job_text(const job *j);
job_state job_get_state(const job *j);

int jobs_continue_foreground(job *j);
int jobs_continue_background(job *j);

int jobs_signal(job *j, int sig);

int jobs_wait(job *j, pid_t pid);

void jobs_wait_all(void);

bool jobs_has_stopped(void);

#endif