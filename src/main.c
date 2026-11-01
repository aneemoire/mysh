/*
    main.c — временная точка входа для проверки лексера.

    mysh --dump-tokens -c 'строка' 
        разбить одну строку и вывести токены
    mysh --dump-tokens    
        читать команды из stdin и выводить токены
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lexer.h"
#include "mysh_getline.h"
#include "parser.h"

#define EXIT_SYNTAX 2

typedef enum {
    DUMP_TOKENS,
    DUMP_AST 
} dump_mode;

static const char *PROMPT_PRIMARY = "mysh> ";
static const char *PROMPT_SECONDARY = "> ";

// парсит tokens и печатает дерево команд. возвращает код возврата.

static int dump_ast(const token_list *tokens)
{
    ast_list tree;
    char *error = NULL;
 
    switch (parse(tokens, &tree, &error)) {
    case PARSE_OK:
        ast_list_dump(&tree, stdout);
        ast_list_free(&tree);
        return 0;
    case PARSE_SYNTAX_ERROR:
        fprintf(stderr, "mysh: %s\n", error);
        free(error);
        return EXIT_SYNTAX;
    case PARSE_NOMEM:
        break;
    }
    fprintf(stderr, "mysh: out of memory\n");
    return 1;
}

static int dump_input(const char *input, dump_mode mode, lex_status *status)
{
    token_list tokens;
    int rc = 0;
 
    *status = lex(input, &tokens);
    if (*status != LEX_OK)
        return *status == LEX_NOMEM ? 1 : EXIT_SYNTAX;
 
    if (mode == DUMP_TOKENS)
        token_list_dump(&tokens, stdout);
    else
        rc = dump_ast(&tokens);
    token_list_free(&tokens);
    return rc;
}

// разбивает строку и печатает токены и возвращает код возврата

static int dump_string(const char *input, dump_mode mode)
{
    lex_status status;
    int rc = dump_input(input, mode, &status);
 
    if (status != LEX_OK)
        fprintf(stderr, "mysh: %s\n", lex_status_message(status));
    return rc;
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

static int dump_stdin(dump_mode mode)
{
    const bool interactive = isatty(STDIN_FILENO);
    char *line = NULL, *acc = NULL;
    size_t line_cap = 0, acc_len = 0;
    int rc = 0;
    long n;

    if (interactive)
        fputs(PROMPT_PRIMARY, stdout);

    while ((n = mysh_getline(&line, &line_cap, stdin)) != -1) {
        if (!append(&acc, &acc_len, line, (size_t)n)) {
            fprintf(stderr, "mysh: out of memory\n");
            rc = 1;
            goto done;
        }

        lex_status status;
        rc = dump_input(acc, mode, &status);

        if (status == LEX_UNCLOSED_QUOTE || status == LEX_TRAILING_ESCAPE) {
            if (interactive)
                fputs(PROMPT_SECONDARY, stdout);
            continue;
        }

        if (status != LEX_OK)
            fprintf(stderr, "mysh: %s\n", lex_status_message(status));

        if (rc == 1 || (rc == EXIT_SYNTAX && !interactive))
            goto done;

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
    if (argc >= 2) {
        const bool tokens = strcmp(argv[1], "--dump-tokens") == 0;

        if (tokens || strcmp(argv[1], "--dump-ast") == 0) {
            dump_mode mode = tokens ? DUMP_TOKENS : DUMP_AST;

            if (argc == 4 && strcmp(argv[2], "-c") == 0)
                return dump_string(argv[3], mode);
            if (argc == 2)
                return dump_stdin(mode);
        }
    }
    fprintf(stderr, "usage: mysh --dump-tokens|--dump-ast [-c 'command']\n");
    return 1;
}