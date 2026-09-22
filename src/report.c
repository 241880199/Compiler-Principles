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

/* **已打印过的**语义错误行号集合（含 0 个元素时表示尚未打印过）。
 * **与 type A/B 那条路径完全独立** —— 实验一的 lastALine 抑制逻辑有已知代价、
 * 且有 30 个用例依赖它；而实验二的输入不含 A/B 错误，两条路径不会相遇。
 * 合并两者的唯一效果是把实验一已验证的行为改坏。
 *
 * 为什么是**集合**而不是"上一次打印的行号"（2026-09-22 评审修正）：
 * 判据是要求书"每行最多只有一个错误" + §8 第 9 条"留最先检出的那条"，都是**无条件**的
 * —— 一条打印过的行，此后**任何时刻**再报都必须被抑制，与相邻与否无关。只记"上一个行号"
 * 只能挡住**连续**的同行错误：行号非单调时（L → M → L，M > L）第 L 行会被打印第二次，
 * 直接违反判据。
 *
 * **可达性是真实的**，不是理论构造：§7 的检查顺序表刻意**不**在错误 6 与错误 5 之间短路
 * `Exp ASSIGNOP Exp` —— 先在左子结点的行报 6，再分析右操作数（可以报在**更靠后**的行），
 * 最后回到赋值语句的行报 5。毒化只能在中间那条错误返回 UNKNOWN 时才救场，而**错误 9 返回的
 * 是函数返回类型而非 UNKNOWN**，所以 `(x) = f(1,2)` 这类（x 为 float、f 返回 int、实参不匹配）
 * 能真的走出 L → M → L。
 *
 * 容量：语义错误一共 19 种、单次运行的打印条数是个位数，128 个行号远超实际需要。
 * 真溢出时按**不抑制**处理（宁可多打印也不吞掉真实错误）—— 该分支在评测输入上不可达。 */
#define SEMANTIC_LINE_MAX 128
static int semPrintedLines[SEMANTIC_LINE_MAX];
static int semPrintedCount = 0;

int reportSemanticError(int code, int line, const char *msg) {
    int i;
    errorFlag = 1;   /* 必须在最前：被抑制的错误**也算**错误（与 reportError 同一约定，见上） */
    for (i = 0; i < semPrintedCount; i++) {
        if (semPrintedLines[i] == line) return 0;   /* 同行只留最先检出的（§8 第 9 条） */
    }
    if (semPrintedCount < SEMANTIC_LINE_MAX) semPrintedLines[semPrintedCount++] = line;
    printf("Error type %d at Line %d: %s.\n", code, line, msg);
    return 1;
}

int hasError(void) { return errorFlag; }
