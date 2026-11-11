/*
    shell.c - главный цикл интерпретатора
*/
#include "shell.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ast.h"
#include "executor.h"
#include "lexer.h"
#include "mysh_getline.h"
#include "parser.h"

#define EXIT_GENERAL 1 // нехватка памяти и прочие ошибки самого интерпретатора
#define EXIT_SYNTAX  2 // синтаксическая ошибка

static const char *PROMPT_PRIMARY = "mysh> ";
static const char *PROMPT_SECONDARY = "> ";

typedef enum {
    OUT_DONE,  
    OUT_INCOMPLETE,
    OUT_SYNTAX_ERROR, 
    OUT_FATAL 
} outcome;

// парсит токены и выполняет или печатает результат
static outcome run_tokens(const token_list *tokens, run_mode mode, int last, int *rc)
{
    ast_list tree;
    char *error = NULL;

    switch (parse(tokens, &tree, &error)) {
    case PARSE_OK:
        break;
    case PARSE_SYNTAX_ERROR:
        fprintf(stderr, "mysh: %s\n", error);
        free(error);
        *rc = EXIT_SYNTAX;
        return OUT_SYNTAX_ERROR;
    case PARSE_NOMEM:
        fprintf(stderr, "mysh: out of memory\n");
        *rc = EXIT_GENERAL;
        return OUT_FATAL;
    }
    if (mode == RUN_DUMP_AST) {
        ast_list_dump(&tree, stdout);
        *rc = 0;
    } else if (tree.count == 0) {
        *rc = last;
    } else {
        *rc = exec_list(&tree);
    }
    ast_list_free(&tree);
    return OUT_DONE;
}

// обрабатывает одну полную команду
static outcome process(const char *input, run_mode mode, int last, int *rc, lex_status *lex_st)
{
    token_list tokens;
    outcome out;

    *lex_st = lex(input, &tokens);
    if (*lex_st == LEX_UNCLOSED_QUOTE || *lex_st == LEX_TRAILING_ESCAPE)
        return OUT_INCOMPLETE;
    if (*lex_st != LEX_OK) { /* остался LEX_NOMEM */
        fprintf(stderr, "mysh: %s\n", lex_status_message(*lex_st));
        *rc = EXIT_GENERAL;
        return OUT_FATAL;
    }
    if (mode == RUN_DUMP_TOKENS) {
        token_list_dump(&tokens, stdout);
        *rc = 0;
        out = OUT_DONE;
    } else {
        out = run_tokens(&tokens, mode, last, rc);
    }
    token_list_free(&tokens);
    return out;
}

int shell_run_string(const char *input, run_mode mode)
{
    lex_status lex_st;
    int rc = 0;

    if (process(input, mode, 0, &rc, &lex_st) == OUT_INCOMPLETE) {
        fprintf(stderr, "mysh: %s\n", lex_status_message(lex_st));
        return EXIT_SYNTAX;
    }
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

static void prompt(bool interactive, const char *text)
{
    if (interactive)
        fputs(text, stderr);
}

int shell_run_stdin(run_mode mode)
{
    const bool interactive = isatty(STDIN_FILENO);
    char *line = NULL, *acc = NULL;
    size_t line_cap = 0, acc_len = 0;
    int last = 0;
    long n;

    if (!interactive)
        setvbuf(stdin, NULL, _IONBF, 0);

    prompt(interactive, PROMPT_PRIMARY);
    while ((n = mysh_getline(&line, &line_cap, stdin)) != -1) {
        lex_status lex_st;
        int rc = last;
        outcome out;

        if (!append(&acc, &acc_len, line, (size_t)n)) {
            fprintf(stderr, "mysh: out of memory\n");
            last = EXIT_GENERAL;
            goto done;
        }
        out = process(acc, mode, last, &rc, &lex_st);
        if (out == OUT_INCOMPLETE) {
            prompt(interactive, PROMPT_SECONDARY);
            continue;
        }
        last = rc;

        if (out == OUT_FATAL || (out == OUT_SYNTAX_ERROR && !interactive))
            goto done;
        acc_len = 0;
        prompt(interactive, PROMPT_PRIMARY);
    }

    if (acc_len > 0) {
        fprintf(stderr, "mysh: syntax error: unexpected EOF\n");
        last = EXIT_SYNTAX;
    }
    if (interactive)
        fputc('\n', stderr);
done:
    free(line);
    free(acc);
    return last;
}