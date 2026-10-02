#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lexer.h"

#define EXIT_SYNTAX 2

static const char *PROMPT_PRIMARY = "mysh> ";
static const char *PROMPT_SECONDARY = "> ";

// разбивает строку и печатает токены и возвращает код возврата

static int dump_string(const char *input) {
    token_list tokens;
    lex_status status = lex(input, &tokens);
    if (status != LEX_OK) {
        fprintf(stderr, "mysh: %s\n", lex_status_message(status));
        return status == LEX_NOMEM ? 1 : EXIT_SYNTAX;
    }
    token_list_dump(&tokens, stdout);
    token_list_free(&tokens);
    return 0;
}

static bool append(char **acc, size_t *acc_len, const char *line, size_t n)
{
    char *p = realloc(*acc, *acc_len + n + 1);
    if (p == NULL)
        return false;
    memcpy(p + *acc_len, line, n + 1);
    *acc = p;
    *acc_len += n;
    return true;
}

static int dump_stdin(void)
{
    const bool interactive = isatty(STDIN_FILENO);
    char *line = NULL, *acc = NULL;
    size_t line_cap = 0, acc_len = 0;
    int rc = 0;
    ssize_t n;

    if (interactive)
        fputs(PROMPT_PRIMARY, stdout);

    while ((n = getline(&line, &line_cap, stdin)) != -1) {
        if (!append(&acc, &acc_len, line, (size_t)n)) {
            fprintf(stderr, "mysh: out of memory\n");
            rc = 1;
            goto done;
        }

        token_list tokens;
        lex_status status = lex(acc, &tokens);

        if (status == LEX_UNCLOSED_QUOTE || status == LEX_TRAILING_ESCAPE) {
            if (interactive)
                fputs(PROMPT_SECONDARY, stdout);
            continue;
        }

        if (status == LEX_NOMEM) {
            fprintf(stderr, "mysh: out of memory\n");
            rc = 1;
            goto done;
        }

        if (status != LEX_OK) {
            fprintf(stderr, "mysh: %s\n", lex_status_message(status));
            rc = EXIT_SYNTAX;
            if (!interactive)
                goto done;
        } else {
            token_list_dump(&tokens, stdout);
            token_list_free(&tokens);
            rc = 0;
        }

        acc_len = 0; 
        if (interactive)
            fputs(PROMPT_PRIMARY, stdout);
    }

    if (acc_len > 0) { 
        fprintf(stderr, "mysh: syntax error: unexpected EOF\n");
        rc = EXIT_SYNTAX;
    }
    if (interactive)
        fputc('\n', stdout); 

done:
    free(line);
    free(acc);
    return rc;
}


int main(int argc, char **argv) {
    if (argc == 4 && strcmp(argv[1], "--dump-tokens") == 0 && strcmp(argv[2], "-c") == 0)
        return dump_string(argv[3]);

    if (argc == 2 && strcmp(argv[1], "--dump-tokens") == 0)
        return dump_stdin();

    fprintf(stderr, "usage: mysh --dump-tokens [-c 'command']\n");
    return 1;
}