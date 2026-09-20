#include <stdio.h>
#include "tree.h"

int main(void) {
    Node *t;

    /* 树 1：Program → ExtDefList → ε
       整棵子树不含词法单元 → 产生了 ε → 不应打印任何内容 */
    printTree(newNode("Program", 1, newNode("ExtDefList", 0)));

    printf("===== 分隔 =====\n");

    /* 树 2：行号全部由最左词素推导；覆盖 有词素/无词素 两类词法单元 */
    t = newNode("Program", 1,
            newNode("ExtDefList", 1,
                newNode("ExtDef", 3,
                    newNode("Specifier", 1, newToken("TYPE", 1, "int")),
                    newNode("FunDec", 3,
                        newToken("ID", 1, "inc"),
                        newToken("LP", 1, NULL),
                        newToken("RP", 1, NULL)),
                    newToken("SEMI", 1, NULL))));
    printTree(t);

    printf("===== 分隔 =====\n");

    /* 树 3：第一个子结点是 ε 时不占行、不占位，行号应取自其后第一个词素 */
    t = newNode("DefList", 2,
            newNode("DefList", 0),
            newNode("Def", 2,
                newNode("Specifier", 1, newToken("TYPE", 3, "float")),
                newToken("SEMI", 3, NULL)));
    printTree(t);

    printf("===== 分隔 =====\n");

    /* 树 4：数值词素 */
    t = newNode("Exp", 2,
            newToken("INT", 4, "83"),
            newToken("FLOAT", 4, "0.000105"));
    printTree(t);

    return 0;
}
