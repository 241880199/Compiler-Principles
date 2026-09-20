#include <stdio.h>
#include <stdlib.h>
#include "tree.h"
#include "report.h"

extern FILE *yyin;
extern int   yyparse(void);
extern Node *root;

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <input-file>\n", argv[0]);
        return 1;
    }
    if (!(yyin = fopen(argv[1], "r"))) {
        perror(argv[1]);
        return 1;
    }
    yyparse();
    /* Task 2 在此处补上：if (!hasError()) printTree(root); */
    return 0;
}
