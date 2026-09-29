#include <stdio.h>
#include <string.h>

#include "lexer.h"

int main(int argc, char **argv)
{
    if (argc == 4 && strcmp(argv[1], "--dump-tokens") == 0
                  && strcmp(argv[2], "-c") == 0) {
        token_list tokens;
        if (lex(argv[3], &tokens) != LEX_OK) {
            fprintf(stderr, "mysh: out of memory\n");
            return 1;
        }
        token_list_dump(&tokens, stdout);
        token_list_free(&tokens);
        return 0;
    }

    fprintf(stderr, "usage: mysh --dump-tokens -c 'command'\n");
    return 1;
}