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