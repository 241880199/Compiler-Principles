#include <stdio.h>
#include "report.h"

int curTokenLine = 1;

static int errorFlag = 0;

/* 最近一次 type A（词法）错误所在的行号，-1 表示尚未出现过。
 * 用途见 reportError 中的说明。 */
static int lastALine = -1;

void reportError(char type, int line, const char *msg) {
    errorFlag = 1;

    if (type == 'A') {
        lastALine = line;
    } else if (type == 'B' && line == lastALine) {
        /* 抑制"同行下游噪声"的打印：词法器发现非法数字/非法浮点数后**不返回
         * 词法单元**（见 lexical.l 的数字规则），语法分析器于是看到 `int i = ;`
         * 这样的残缺输入，在同一行再报一条 type B。这条 B 不是独立错误。
         *
         * 输入文件保证同一行不出现多个错误（要求书 2.1.3），故同一行上 A 之后的
         * B 必然是 A 的下游噪声，可以安全抑制。
         * 注意：只抑制**打印**，errorFlag 已在上面置位，hasError() 行为不变。 */
        return;
    }

    printf("Error type %c at Line %d: %s.\n", type, line, msg);
}

int hasError(void) { return errorFlag; }
