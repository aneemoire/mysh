#include <stdio.h>
#include <string.h>

#include "lexer.h"

#define EXIT_SYNTAX 2

int main(int argc, char **argv)
{
    if (argc == 4 && strcmp(argv[1], "--dump-tokens") == 0
                  && strcmp(argv[2], "-c") == 0) {
        token_list tokens;
        lex_status status = lex(argv[3], &tokens);
        if (status != LEX_OK) {
            fprintf(stderr, "mysh: %s\n", lex_status_message(status));
            return status == LEX_NOMEM ? 1 : EXIT_SYNTAX;
        }
        token_list_dump(&tokens, stdout);
        token_list_free(&tokens);
        return 0;
    }

    fprintf(stderr, "usage: mysh --dump-tokens -c 'command'\n");
    return 1;
}