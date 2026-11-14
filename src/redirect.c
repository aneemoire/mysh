/*
    redirect.c - применение перенаправлений

    Каждое перенаправление: открыть файл, поставить дескриптор на место
    стандартного потока (dup2), закрыть лишний дескриптор.
*/
#include "redirect.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define REDIR_FILE_MODE 0666 
#define SAVE_MIN_FD 10  

static int open_flags(redir_op op)
{
    switch (op) {
    case REDIR_IN:     return O_RDONLY;
    case REDIR_OUT:    return O_WRONLY | O_CREAT | O_TRUNC;
    case REDIR_APPEND: return O_WRONLY | O_CREAT | O_APPEND;
    }
    return O_RDONLY;
}

static int target_fd(redir_op op)
{
    return op == REDIR_IN ? STDIN_FILENO : STDOUT_FILENO;
}


static int open_retry(const char *path, int flags)
{
    int fd;

    do
        fd = open(path, flags, REDIR_FILE_MODE);
    while (fd < 0 && errno == EINTR);
    return fd;
}

static int dup2_retry(int from, int to)
{
    int r;

    do
        r = dup2(from, to);
    while (r < 0 && errno == EINTR);
    return r;
}

static void report(const char *file, int err)
{
    fprintf(stderr, "mysh: %s: %s\n", file, strerror(err));
}

static int apply_one(const redirection *r)
{
    int target = target_fd(r->op);
    int fd = open_retry(r->file, open_flags(r->op));

    if (fd < 0) {
        report(r->file, errno);
        return -1;
    }
    if (fd == target) 
        return 0;
    if (dup2_retry(fd, target) < 0) {
        int err = errno;

        close(fd);
        report(r->file, err);
        return -1;
    }
    if (close(fd) < 0) {
        report(r->file, errno);
        return -1;
    }
    return 0;
}

int redir_apply(const redirection *redirs, size_t count)
{
    for (size_t i = 0; i < count; i++)
        if (apply_one(&redirs[i]) < 0)
            return -1;
    return 0;
}

static int save_fd(int fd, int *copy)
{
    *copy = fcntl(fd, F_DUPFD_CLOEXEC, SAVE_MIN_FD);
    if (*copy >= 0)
        return 0;
    if (errno == EBADF)
        return 0;
    fprintf(stderr, "mysh: dup: %s\n", strerror(errno));
    return -1;
}

int redir_save(redir_saved *saved)
{
    saved->in = saved->out = -1;
    if (save_fd(STDIN_FILENO, &saved->in) < 0)
        return -1;
    if (save_fd(STDOUT_FILENO, &saved->out) < 0) {
        if (saved->in >= 0)
            close(saved->in);
        return -1;
    }
    return 0;
}

static int restore_fd(int copy, int target)
{
    if (copy < 0)
        return close(target) < 0 && errno != EBADF ? -1 : 0;
    if (dup2_retry(copy, target) < 0) {
        fprintf(stderr, "mysh: dup2: %s\n", strerror(errno));
        close(copy);
        return -1;
    }
    if (close(copy) < 0) {
        fprintf(stderr, "mysh: close: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

int redir_restore(const redir_saved *saved)
{
    int rc = 0;

    if (restore_fd(saved->in, STDIN_FILENO) < 0)
        rc = -1;
    if (restore_fd(saved->out, STDOUT_FILENO) < 0)
        rc = -1;
    return rc;
}