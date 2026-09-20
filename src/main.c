#include <stdio.h>
#include <stdlib.h>
#include "tree.h"
#include "report.h"

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
     *
     * 顶层（ExtDef 层）没有 `error` 出口，Bison 在那里只能弹空栈并中止分析，
     * 而中止意味着**再也不会调用 yylex** —— 文件剩余部分的词法错误会被整片丢弃
     * （例如 `int a = 1;` 后跟 `int b = ~2;`，`~` 从未被报出）。
     *
     * 分析正常走完时 yylex 已停在 EOF，紧接着返回 0，本循环无副作用。
     * 不要为此改动 syntax.y 里 `error` 产生式的位置 —— 那会改变冲突数。 */
    if (hasError()) {
        while (yylex() != 0) { }
    }

    if (!hasError()) printTree(root);
    return 0;
}
