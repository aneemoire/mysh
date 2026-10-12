#ifndef MYSH_GETLINE_H
#define MYSH_GETLINE_H

#include <stddef.h>
#include <stdio.h>

long mysh_getline(char **buf, size_t *cap, FILE *stream);

#endif 