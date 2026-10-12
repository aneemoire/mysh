#include "getline.h"

#include <stdlib.h>

#define LINE_BUF_INIT 128 // начальная ёмкость буфера

long mysh_getline(char **buf, size_t *cap, FILE *stream)
{
    if (*buf == NULL || *cap == 0) {
        char *p = malloc(LINE_BUF_INIT);
        if (p == NULL)
            return -1;
        *buf = p;
        *cap = LINE_BUF_INIT;
    }

    size_t len = 0;
    for (;;) {
        int c = fgetc(stream);
        if (c == EOF) {
            if (len == 0)
                return -1;
            break;
        }

        if (len + 1 >= *cap) { 
            size_t new_cap = *cap * 2;
            char *p = realloc(*buf, new_cap);
            if (p == NULL)
                return -1;
            *buf = p;
            *cap = new_cap;
        }

        (*buf)[len++] = (char)c;
        if (c == '\n')
            break;
    }

    (*buf)[len] = '\0';
    return (long)len;
}