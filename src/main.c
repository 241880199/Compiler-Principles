#include <stdio.h>
#include <stdlib.h>
#include "tree.h"
#include "report.h"
#include "semantic.h"

extern FILE *yyin;
extern int   yyparse(void);
extern int   yylex(void);
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

    /* 词法错误与语法分析是否走完**无关**，不该因为语法分析中止而丢失。
     * （实验一已修的问题，原样保留。） */
    if (hasError()) {
        while (yylex() != 0) { }
    } else {
        /* 要求书保证实验二的输入不含词法/语法错误，故只有这一支会被走到。
         * 无错时不打印语法树 —— 实验二要求"不输出任何内容"，
         * 且实验一指导书明说打印代码"后面的实验不会再用"。 */
        semanticAnalyze(root);
    }
    return 0;
}
