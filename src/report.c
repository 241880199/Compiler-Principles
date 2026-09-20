#include <stdio.h>
#include "report.h"

int curTokenLine = 1;

static int errorFlag = 0;

/* 最近一次 type A（词法）错误所在的行号，-1 表示尚未出现过。
 * 用途与局限见 reportError 中的说明。 */
static int lastALine = -1;

void reportError(char type, int line, const char *msg) {
    errorFlag = 1;

    if (type == 'A') {
        lastALine = line;
    } else if (type == 'B' && line == lastALine) {
        /* 抑制"同一行上、跟在本条 type A 之后报出"的 type B 的**打印**。
         *
         * 这不是因果判断，而是**按「已报告过的行号」做的近似**。要抑制的典型是下游
         * 噪声：词法器发现非法数字/非法浮点数后**不返回词法单元**（见 lexical.l 的
         * 数字规则），语法分析器于是看到 `int i = ;` 这样的残缺输入，在同一行再报一条
         * type B。抑制它的**理由不是"它必然是噪声"，而是官方选做样例 2 / 样例 4 的
         * 期望输出只含 type A 行、不含 type B** —— 不抑制就与官方样例不符。
         *
         * 代价（已知限制，见设计文档 §6.3）：判据只看行号，因此**也可能吞掉一条真实
         * 的、独立的语法错误**。反例如下（该输入不满足要求书"同一行不出现多个错误"
         * 的前提，属于明知的代价）：
         *
         *     int main() {
         *       int a = 1      ← 缺分号，真实错误
         *       ~ }            ← 未定义字符（type A）
         *     }
         *
         * 缺分号的那条 B 在第 4 行被检出，与 A 同行且在其后 → 被本抑制吞掉，只剩 A4。
         *
         * 判据是**顺序相关而非因果相关**：若 B 在 A 之前报出（例如
         * `int i = 1` 换行 `int j = 09; }`，缺分号先于 `09` 被检出），lastALine 尚未
         * 置位，两条都会打印；`int i = 09;` 这类输入同理（B 在 ASSIGNOP 处就报出）。
         *
         * 注意：只抑制**打印**，errorFlag 已在上面置位，hasError() 行为不变。 */
        return;
    }

    printf("Error type %c at Line %d: %s.\n", type, line, msg);
}

int hasError(void) { return errorFlag; }
