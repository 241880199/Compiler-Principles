/* 程序级翻译单测（Task 10）—— translateProgram 的**真裁判**。
 *
 * ── 为什么必须有它 ─────────────────────────────────────────────────────────
 * Task 12 之前 `translateProgram` **没有调用方**（src/main.c 还没接上来），
 * 故 Task 10 写下的语句 / 复合语句 / 函数翻译在端到端跑通之前**没有任何判据**。
 * brief 给本任务的验收只有"用 c_oracle.py 生成 fact.cmm 的 .exp"—— 那条链
 * **完全不经过我们的编译器**（c_oracle 把 .cmm 当 C 交给 gcc），所以它
 * 一个字都证明不了本文件的被测对象。这正是本项目反复栽过的
 * 「**判据是对的，但没有人在跑它**」（Task 8 审查刚抓到过一次）。
 *
 * ⇒ 本单测**用仓库里真正的 lexer/parser**（链接 lex.yy.c + syntax.tab.c）解析一段
 * 小 `.cmm`，拿到真实的 AST，调 translateProgram，再把 printIR 的输出与
 * `translate_prog_expected.txt` **逐字节**比对。判据链上没有一处是我们自己的手抄：
 * 输入是真源码、树是真 parser 的产物、期望值是**实测输出钉住的**。
 *
 * ── 输入为什么长这样（Test/unit/lab3_prog_in.cmm）──────────────────────────
 * **刻意避开尚未实现的能力**：函数调用 / read / write / 数组下标都是 Task 11
 * 的桩，走到就 abort，混进来只会让本单测死在别人的任务上、而不是死在它要测的东西上。
 * 它覆盖的恰好是语句/复合语句/函数翻译的每一条分支：赋值语句、`if`、`if-else`、
 * `while`、`return`、**嵌套复合语句**（块内局部定义 + 块内语句）、两个函数与形参
 * （PARAM 按声明顺序）、以及 `if` 的无花括号单语句体。
 *
 * ── 关系条件（Task 10b 修 Critical 后加回）────────────────────────────────
 * 本夹具原本**不含**关系运算（条件写成裸变量 / `!x`），因为 `if (a > b)` 会让编译器
 * 当场 abort：`src/lexical.l` 的六个 RELOP 规则一律发 `emit(RELOP, "RELOP", NULL)`
 * ——**运算符文本没进 AST**，而 `translateCond` 要拿它算 `irRelopCode`。
 * Task 10b 修法：**另开专用字段** `Node.opText`（lexeme 保持 NULL，故语法树输出
 * 逐字节不变），lexical.l 的关系规则填它，`translateCond` 读它（见 tree.h / translate.c）。
 * ⇒ 现在 `if (a > b)`（单字符）与 `while (t <= b)`（双字符，顺带钉住 lexer 的最长匹配）
 *   都在判据射程内，逐字节钉住它们产出的 `IF v1 > v2 …` / `IF v3 <= v2 …`。
 * 兜底（裸变量求值到临时变量再与 #0 比）仍由 `main` 里的 `!x` 覆盖（NOT 会递归进兜底），
 * 故加回关系条件**没有**丢掉原来那两条被测分支。
 *
 * ── 输出协议 ───────────────────────────────────────────────────────────────
 * stdout = **唯一**被逐字节比对的产物（Makefile 的 `prog-test` 目标拿它与
 * translate_prog_expected.txt 做 diff）。诊断一律写 stderr（"不要往 stderr 之外的
 * 正常流程输出"反过来读：失败信息不能污染被比对的那个字节流）。
 *
 * 退出码：yyparse 失败 ⇒ 返回 2（被 `-o pipefail` 判红，而不是"打出一份空产物、
 * 与期望不符"这种含糊的红）。 */

#include <stdio.h>
#include <stdlib.h>
#include "tree.h"
#include "translate.h"
#include "ir.h"

/* syntax.y 里 `Node *root` 是文件级全局（Program 归约时赋值），lex.yy.c 里 `yyin`
 * 也是全局。这里显式声明而不 include 生成物，是为了让本测试**只依赖公开接口**。 */
extern Node *root;
extern FILE *yyin;
int yyparse(void);

int main(void) {
    /* 默认从 stdin 读源码：Makefile 里 `./test_translate_prog < lab3_prog_in.cmm`，
     * 与 test_lexer 同一套写法（flex 的 yyin 默认就是 stdin）。 */
    (void)yyin;

    if (yyparse() != 0) {
        fprintf(stderr, "FAIL translate-prog: 输入文件有语法错误（yyparse 返回非 0）\n");
        return 2;
    }
    if (root == NULL) {
        fprintf(stderr, "FAIL translate-prog: parser 未产出语法树（root == NULL）\n");
        return 2;
    }

    printIR(stdout, translateProgram(root));
    return 0;
}
