/* 表达式翻译的单测（Task 8）—— **`place` 作为继承属性**的守护者。
 *
 * 为什么这条测试非有不可：
 *   `x = 5;` 的"朴素写法"是 `t1 := #5` + `v1 := t1` 两条 IR，本仓库的写法是
 *   `v1 := #5` 一条 —— 把 `x` 自己当 place 传给右子表达式（要求书 4.1.6 样例 1
 *   自己给出的"优化后版本"正是这个形态）。两种写法**跑出来的程序结果完全相同**，
 *   端到端（parser + 参考机 + .exp 比对）**一个字都察觉不到**差别，差异只体现在
 *   IR 的行数（也就是指令条数）上。故它只能靠"逐字节比对产物文本"来钉 ——
 *   这正是本文件 stdout 那一条用例存在的唯一理由，也是报告亮点 1 的论据。
 *
 * 判据分两层（与 Test/unit/test_expkind.c 同一套约定）：
 *   · stdout = **唯一**一条用例的 IR 文本，由 Makefile 的 `translate-test` 目标
 *     与 translate_exp_expected.txt 逐字节 diff（那才是"守护"本身）；
 *   · 其余用例（二元运算、取负、链式赋值、括号、值不被需要的表达式）写 stderr、
 *     以退出码为判据。它们**不得**往 stdout 写一个字节 —— 会污染上面那条比对。
 *
 * ★★ **这些期望是刻意钉住"当前形态"的**：下面多处断言硬编码了 `v1`/`v2`/`t1`/`t2`
 *    以及"经过临时变量"的形态（例如 `x = 1 + 2;` 期望 `t1 := #1` / `t2 := #2` /
 *    `v1 := t1 + t2`）。要求书与本任务的设计文档都**没有**规定这套编号 —— 它们守护的
 *    其实就是上面那条主张："该是一条 IR 的时候就是一条，不是两条"。
 *    将来的优化 pass（Task 16 起：常量折叠、结果直达、立即数内联…）**必然会**改变
 *    这些形态（行数会变少、临时变量名会变）。届时**必须有意地、逐条地修订这些期望**，
 *    把新形态重新钉死；**不许**用"放宽断言 / 改成子串匹配 / 只数行数"之类的方式让它们
 *    变绿 —— 那等于把本文件唯一的守护（逐字节钉住 place 协议）悄悄废掉。 */

/* ★ 必须在任何 include 之前：Makefile 用 `-std=c99`（严格 ISO C99），open_memstream
 * 在该模式下不声明，漏这行会得到 implicit declaration 警告（"零 gcc 警告"当场变红）。 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tree.h"
#include "translate.h"
#include "ir.h"

static int failures = 0;

/* 把一段 IR 渲染成字符串（不碰 stdout）。 */
static char *render(InterCodeList *t) {
    char *buf = NULL;
    size_t len = 0;
    FILE *f = open_memstream(&buf, &len);
    if (!f) { fprintf(stderr, "FAIL render: open_memstream 失败\n"); failures++; return NULL; }
    printIR(f, t);
    fclose(f);
    return buf;
}

static void expectIR(InterCodeList *t, const char *want, const char *what) {
    char *got = render(t);
    if (!got) return;
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "FAIL %s\n  期望: <%s>\n  实测: <%s>\n", what, want, got);
        failures++;
    } else {
        fprintf(stderr, "ok   %s\n", what);
    }
    free(got);
}

/* 构造 `ID = Exp`（三孩子：ID 左值、ASSIGNOP、右表达式）。 */
static Node *mkAssign(const char *name, Node *rhs) {
    return newNode("Exp", 3, newNode("Exp", 1, newToken("ID", 1, name)),
                   newToken("ASSIGNOP", 1, "="), rhs);
}

int main(void) {
    /* ══ ① stdout：被逐字节比对的**唯一**那条 —— `x = 5;` ══
     * 期望恰好一行 `v1 := #5`。若有人把 translateAssign 改回朴素写法
     * （先算进临时变量再拷进 x），这里会多出一行 `t1 := #5`，diff 立刻变红。 */
    {
        InterCodeList *t = irList();
        translateExp(t, mkAssign("x", newNode("Exp", 1, newToken("INT", 1, "5"))), NULL, NULL);
        printIR(stdout, t);
    }

    /* ══ ② 同一件事的右操作数是变量时，同样只产出一行（`v1`/`v2` 由登记序决定） ══ */
    translateResetNames();
    {
        Node *rhs = newNode("Exp", 1, newToken("ID", 1, "y"));
        InterCodeList *t = irList();
        translateExp(t, mkAssign("x", rhs), NULL, NULL);
        /* x -> v1（外层先登记），y -> v2 */
        expectIR(t, "v1 := v2\n", "x = y; 只产出一行 v1 := v2");
    }

    /* ══ ③ 二元算术：两侧都进临时变量，最后一条写进 place ══ */
    translateResetNames();
    {
        Node *bin = newNode("Exp", 3, newNode("Exp", 1, newToken("INT", 1, "1")),
                            newToken("PLUS", 1, "+"), newNode("Exp", 1, newToken("INT", 1, "2")));
        InterCodeList *t = irList();
        translateExp(t, mkAssign("x", bin), NULL, NULL);
        expectIR(t, "t1 := #1\nt2 := #2\nv1 := t1 + t2\n", "x = 1 + 2;");
    }

    /* ══ ④ 取负：`place := #0 - t1`（不是负数常量折叠，也不是独立 NEG） ══ */
    translateResetNames();
    {
        Node *neg = newNode("Exp", 2, newToken("MINUS", 1, "-"),
                            newNode("Exp", 1, newToken("ID", 1, "y")));
        InterCodeList *t = irList();
        translateExp(t, mkAssign("x", neg), NULL, NULL);
        expectIR(t, "t1 := v2\nv1 := #0 - t1\n", "x = -y;");
    }

    /* ══ ⑤ 链式赋值：`y = (x = 5);` —— 内层赋值的 place 是外层的目标变量 `y`，
     *    故内层右值直接写进 `v2`（x），再由内层赋值表达式的"值"补一条 `v1 := v2`。
     *    两行都是必然的：内层既要写 x，又要把 x 的值交给外层的 y。 ══ */
    translateResetNames();
    {
        Node *inner = mkAssign("x", newNode("Exp", 1, newToken("INT", 1, "5")));
        InterCodeList *t = irList();
        translateExp(t, mkAssign("y", inner), NULL, NULL);
        expectIR(t, "v2 := #5\nv1 := v2\n", "y = (x = 5);");
    }

    /* ══ ⑥ 括号只是语法噪声：`x = (5);` 与 `x = 5;` 逐字节同形 ══ */
    translateResetNames();
    {
        Node *paren = newNode("Exp", 3, newToken("LP", 1, "("),
                              newNode("Exp", 1, newToken("INT", 1, "5")),
                              newToken("RP", 1, ")"));
        InterCodeList *t = irList();
        translateExp(t, mkAssign("x", paren), NULL, NULL);
        expectIR(t, "v1 := #5\n", "x = (5);");
    }

    /* ══ ⑦ place == NULL（表达式的值不被需要）：两侧仍必须照常翻译 ══
     * `1 + 2` 本身没有副作用，但 `f() + g()` 有 —— 若在 place 为 NULL 时"跳过运算"，
     * 顺手把两侧一起跳过，两个调用就消失了。故这里钉住"两侧照样算，只是不写结果"。 */
    translateResetNames();
    {
        Node *bin = newNode("Exp", 3, newNode("Exp", 1, newToken("INT", 1, "1")),
                            newToken("PLUS", 1, "+"), newNode("Exp", 1, newToken("INT", 1, "2")));
        InterCodeList *t = irList();
        translateExp(t, bin, NULL, NULL);
        expectIR(t, "t1 := #1\nt2 := #2\n", "1 + 2;（值不被需要时两侧仍照常翻译）");
    }

    /* ══ ⑧ placeIsAddr 出参：本任务一律置 0（Task 11 的数组元素左值才置 1） ══ */
    translateResetNames();
    {
        int flag = 99;
        InterCodeList *t = irList();
        translateExp(t, newNode("Exp", 1, newToken("ID", 1, "x")), NULL, &flag);
        if (flag != 0) { fprintf(stderr, "FAIL placeIsAddr: got %d want 0\n", flag); failures++; }
        else fprintf(stderr, "ok   placeIsAddr 置 0\n");
    }

    /* ══ ⑨ place == NULL 的一元取负：`-1;` —— 值不要，也不该为它产出任何 IR ══
     * 取负本身没有副作用（`-f();` 那种要等 Task 10 把调用桩换成真实现才能测），
     * 故 `-1;` 的期望**恰好是 0 行**：place 为 NULL 时既不给右子表达式领临时变量，
     * 也不生成 `place := #0 - t1`（见 translateExp 的 EX_UNARY 分支）。这条钉住
     * "place 为空 ⇒ 不产 IR"这条边界，与上面 ⑦ 的 `1 + 2;` 互为对照。 */
    translateResetNames();
    {
        Node *neg = newNode("Exp", 2, newToken("MINUS", 1, "-"),
                            newNode("Exp", 1, newToken("INT", 1, "1")));
        InterCodeList *t = irList();
        translateExp(t, neg, NULL, NULL);
        expectIR(t, "", "-1;（值不被需要时产出 0 行 IR）");
    }

    fprintf(stderr, failures ? "FAILURES=%d\n" : "ALL PASS\n", failures);
    return failures ? 1 : 0;
}
