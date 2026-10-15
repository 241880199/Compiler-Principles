/* 条件翻译的单测（Task 9）—— **短路** 与 **`label_true`/`label_false` 作为继承属性**
 * 这两条契约的守护者。指导书 4.2.6 的原话是"回填"只用于把跳转目标当**综合属性**的写法；
 * 本设计把两个目标当**继承属性**（函数参数）传下去，于是条件表达式内部要往外跳时
 * 目标已经在手上，**不需要回填、不需要待填链**。下面每一条期望都逐字节钉住这条路。
 *
 * 判据分两层（与 Test/unit/test_translate.c / test_expkind.c 同一套约定）：
 *   · stdout = **唯一**一条用例（`if (n > 0) write(1);` 的形状），由 Makefile 的
 *     `cond-test` 目标与 cond_expected.txt 逐字节 diff —— 那才是"守护"本身；
 *   · 其余用例写 stderr、以退出码为判据，**不得**往 stdout 写一个字节。
 *
 * ── stdout 那条为什么值得逐字节钉 ─────────────────────────────────────────
 * 它是设计文档 §6.1 那行"`if (n > 0) …` 省 1 条指令"的**落地证据**：两侧都是
 * "简单操作数"（ID 与 INT）时**直接内联进 IF**，只产出一条
 * `IF v1 > #0 GOTO label1`；朴素写法会先落一个 `t := #0` 再 `IF v1 > t GOTO …` 两条。
 * 两种写法跑出来的程序结果**完全相同**，端到端（parser + 参考机 + .exp）一个字都
 * 察觉不到差别 —— 差异只体现在 IR 的行数上。故它只能靠逐字节比对来钉。
 *
 * ── `if (n > 0) write(1);` 里的 `WRITE #1` 与两个 LABEL 是谁产的 ────────────
 * **不是** translateCond 产的：translateCond 只保证"真跳 label_true、假跳 label_false"，
 * 标号摆放与语句体由 Task 10 的语句翻译负责（`translate_Cond(cond, l1, l2)` +
 * `[LABEL l1]` + 体 + `[LABEL l2]`）。本任务还没写语句翻译，故下面**手工摆出**了
 * 等价的骨架 —— 它逐字就是 Task 10 将要产出的 IR。这样本条用例才真的在比
 * "条件翻译的产物嵌进 IF 语句后长什么样"，而不是只比一个孤零零的条件。
 *
 * ★★ 这些期望是**实测输出**（先跑、再判断），不是从 brief 的推演手抄的：
 *    brief 自己标注了"这是推演，不是实测"。实测与推演一致（见报告 §2）。 */

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

/* 单孩子 Exp（INT / ID）。 */
static Node *mkExp1(const char *tok, const char *lex) {
    return newNode("Exp", 1, newToken(tok, 1, lex));
}

/* 三孩子 Exp：`l OP r`。
 *
 * ★ **RELOP 一律经 `newRelop` 构造**（tree.h/tree.c）—— 那是词法器
 *   （src/lexical.l 的 emitRelop）与单测**共用**的同一条构造路径：文本进 `opText`、
 *   `lexeme` 为 NULL。此前这里写 `newToken("RELOP", 1, ">")`（文本塞在 lexeme 上）、
 *   而真词法器从不那么干 ⇒ 本文件那 9 条用例一直在**测一条真实产物不会走的路**，
 *   一条 Critical（RELOP 文本缺失）因此躲过了单测、审查、回归三张网。
 *   判据的输入必须与真实产物**同形**，故这里不再手工拼 RELOP 结点。
 *   `src/translate.c` 的 relopTextOf 也随之删掉了"opText 为空就回退读 lexeme"那条
 *   兼容路 —— 两边同形之后它再无存在理由（留着只会继续藏假绿）。
 *
 * AND/OR 的 lexeme 是 `&&`/`||`：判别与 irRelopCode 都只看 child[1] 的**名字**，
 * 故它们照旧走 newToken（真词法器对它们发的 lexeme 本来就是 NULL —— 见 lexical.l；
 * 这里填上纯粹是为了让结点带上一眼能读懂的操作符文本，不影响任何断言）。 */
static Node *mkBin(Node *l, const char *tok, const char *lex, Node *r) {
    Node *op = strcmp(tok, "RELOP") == 0 ? newRelop(lex, 1) : newToken(tok, 1, lex);
    return newNode("Exp", 3, l, op, r);
}

/* `!inner`：二孩子 Exp，child[0] 是 NOT 终结符。 */
static Node *mkNot(Node *inner) {
    return newNode("Exp", 2, newToken("NOT", 1, "!"), inner);
}

/* 构造 `ID = Exp`（三孩子），用于测"条件/逻辑非**作为值**"那几条。 */
static Node *mkAssign(const char *name, Node *rhs) {
    return newNode("Exp", 3, mkExp1("ID", name), newToken("ASSIGNOP", 1, "="), rhs);
}

/* Task 10 的语句骨架里的一项是 `[LABEL l]`。translate.c 里同名的是 static，故这里
 * 用 ir.h 的公开构造函数摆出等价的一条 —— 逐字节相同（`LABEL name :`）。 */
static InterCodeList *appendLabelIR(InterCodeList *t, const char *name) {
    InterCode *c = newCode(IR_LABEL);
    c->u.one.x = opLabel(name);
    return irAppend(t, c);
}

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

int main(void) {
    /* ══ ① stdout：被逐字节比对的**唯一**那条 —— `if (n > 0) write(1);` ══
     * 期望恰好 5 行、且条件只有**一条** IF（没有 `t := #0` 那一行）。
     * 把 operandForCond/operandIsSimple 拆掉（改成一律经临时变量），
     * 这里会多出 `t1 := #0` 并把 `#0` 换成 `t1`，diff 立刻变红。 */
    translateResetNames();
    {
        Node *cond = mkBin(mkExp1("ID", "n"), "RELOP", ">", mkExp1("INT", "0"));
        char *lt = newLabelName();          /* label1 */
        char *lf = newLabelName();          /* label2 */
        InterCodeList *t = irList();
        InterCode *w;
        t = translateCond(t, cond, lt, lf);
        t = appendLabelIR(t, lt);           /* Task 10 的语句骨架：LABEL label1 : */
        w = newCode(IR_WRITE);              /*                    WRITE #1      */
        w->u.one.x = opConst(1);
        t = irAppend(t, w);
        t = appendLabelIR(t, lf);           /*                    LABEL label2 : */
        printIR(stdout, t);
    }

    /* ══ ② 立即数/简单变量**直接内联进 IF** —— §6.1 那行"省 1 条指令"的本体 ══
     * 单独一条钉住"恰好两行、没有临时变量"：①的 diff 是整体比对，坏了不容易一眼
     * 看出是哪半出了问题，这条给了名字。 */
    translateResetNames();
    {
        Node *cond = mkBin(mkExp1("ID", "n"), "RELOP", ">", mkExp1("INT", "0"));
        char *lt = newLabelName(), *lf = newLabelName();
        InterCodeList *t = irList();
        t = translateCond(t, cond, lt, lf);
        expectIR(t, "IF v1 > #0 GOTO label1\nGOTO label2\n",
                 "n > 0：两侧都是简单操作数 => 一条 IF，不落临时变量");
    }

    /* ══ ③ 六个关系运算符的 opText -> irRelopCode 映射 ══
     * RELOP 结点的**运算符文本**在专用字段 `opText`（`< <= > >= == !=` 之一，
     * 由 newRelop 填；见 tree.h）。拼错一个名字，irRelopCode 会**响亮 abort**
     * （不会静默变成 `<`），但那只在真跑到时才发作；这条把六个都走一遍，
     * 逐字节钉住打印出来的符号就是源码里那个。 */
    {
        static const char *const relops[6] = { "<", "<=", ">", ">=", "==", "!=" };
        int i;
        for (i = 0; i < 6; i++) {
            char want[128];
            Node *cond;
            char *lt, *lf;
            InterCodeList *t;
            translateResetNames();
            cond = mkBin(mkExp1("ID", "n"), "RELOP", relops[i], mkExp1("INT", "0"));
            lt = newLabelName(); lf = newLabelName();
            t = irList();
            t = translateCond(t, cond, lt, lf);
            snprintf(want, sizeof want, "IF v1 %s #0 GOTO label1\nGOTO label2\n", relops[i]);
            expectIR(t, want, "RELOP opText 映射正确");
        }
    }

    /* ══ ④ NOT = **目标互换**（真跳假、假跳真）—— 本任务最容易写错的一处 ══
     * `!(n > 0)` 必须产出 `GOTO label2` 在前（条件成立跳**假**目标），而不是
     * 照抄 `n > 0` 的 `GOTO label1`。写成不互换，本条与⑤⑥都会红。 */
    translateResetNames();
    {
        Node *cond = mkNot(mkBin(mkExp1("ID", "n"), "RELOP", ">", mkExp1("INT", "0")));
        char *lt = newLabelName(), *lf = newLabelName();
        InterCodeList *t = irList();
        t = translateCond(t, cond, lt, lf);
        expectIR(t, "IF v1 > #0 GOTO label2\nGOTO label1\n",
                 "!(n > 0)：目标互换（真跳 label2、假跳 label1）");
    }

    /* ══ ⑤ AND 的**真短路**：Exp2 的代码排在 LABEL label3 **之后** ══
     * `a && b` 的形状是 `Cond(a, l3, lf)` + `LABEL l3` + `Cond(b, lt, lf)`：
     * a 为假直接跳到 lf，**根本落不到 l3**，Exp2 的代码因此从不执行。
     * 注意 `a`/`b` 是裸变量（不是 RELOP），故各自要走"求值到临时变量再与 #0 比"
     * 那一支 —— 这是指导书表 4 的"其它"分支，不是短路本身的开销。 */
    translateResetNames();
    {
        Node *cond = mkBin(mkExp1("ID", "a"), "AND", "&&", mkExp1("ID", "b"));
        char *lt = newLabelName(), *lf = newLabelName();
        InterCodeList *t = irList();
        t = translateCond(t, cond, lt, lf);
        expectIR(t,
                 "t1 := v1\n"
                 "IF t1 != #0 GOTO label3\n"
                 "GOTO label2\n"
                 "LABEL label3 :\n"
                 "t2 := v2\n"
                 "IF t2 != #0 GOTO label1\n"
                 "GOTO label2\n",
                 "a && b：Exp2 落在 LABEL label3 之后（真短路）");
    }

    /* ══ ⑥ OR 的**真短路**：Exp1 为真直接跳 lt，落不到 label3 ══ */
    translateResetNames();
    {
        Node *cond = mkBin(mkExp1("ID", "a"), "OR", "||", mkExp1("ID", "b"));
        char *lt = newLabelName(), *lf = newLabelName();
        InterCodeList *t = irList();
        t = translateCond(t, cond, lt, lf);
        expectIR(t,
                 "t1 := v1\n"
                 "IF t1 != #0 GOTO label1\n"
                 "GOTO label3\n"
                 "LABEL label3 :\n"
                 "t2 := v2\n"
                 "IF t2 != #0 GOTO label1\n"
                 "GOTO label2\n",
                 "a || b：Exp1 为真直接跳 label1（真短路）");
    }

    /* ══ ⑦ 短路守护者：`z && (b / z)` —— 除法必须排在 LABEL label3 之后 ══
     * 这是 Test/lab3/basic/logic.cmm 那条除零用例在单测层的对应物：z == 0 时
     * 控制流在 `GOTO label2` 处直接出局，**除法那条 IR 根本不被执行**。
     * 若有人"优化"掉短路（把 Exp2 先翻出来），`t2 := t3 / t4`（注意它甚至在
     * `LABEL label3` **之前**）会出现在 z 为假也要走的路径上 —— 本条立刻变红。 */
    translateResetNames();
    {
        Node *div = newNode("Exp", 3, newToken("LP", 1, "("),
                            mkBin(mkExp1("ID", "b"), "DIV", "/", mkExp1("ID", "z")),
                            newToken("RP", 1, ")"));
        Node *cond = mkBin(mkExp1("ID", "z"), "AND", "&&", div);
        char *lt = newLabelName(), *lf = newLabelName();
        InterCodeList *t = irList();
        t = translateCond(t, cond, lt, lf);
        expectIR(t,
                 "t1 := v1\n"
                 "IF t1 != #0 GOTO label3\n"
                 "GOTO label2\n"
                 "LABEL label3 :\n"
                 "t3 := v2\n"
                 "t4 := v1\n"
                 "t2 := t3 / t4\n"
                 "IF t2 != #0 GOTO label1\n"
                 "GOTO label2\n",
                 "z && (b / z)：除法在 LABEL label3 之后 => z==0 时不会执行");
    }

    /* ══ ⑧ `!Exp` 作为**值**：`x = !z;` ══
     * 设计文档 §6.2 的 `[place := #0]` + translate_Cond + `[LABEL l_true]`
     * `[place := #1]` + `[LABEL l_false]`。这里同时钉住"目标互换"进到了值形态里：
     * `IF t1 != #0 GOTO label2` 跳的是**假**目标（z 非 0 时 `!z` 为假）。 */
    translateResetNames();
    {
        InterCodeList *t = irList();
        t = translateExp(t, mkAssign("x", mkNot(mkExp1("ID", "z"))), NULL, NULL);
        expectIR(t,
                 "v1 := #0\n"
                 "t1 := v2\n"
                 "IF t1 != #0 GOTO label2\n"
                 "GOTO label1\n"
                 "LABEL label1 :\n"
                 "v1 := #1\n"
                 "LABEL label2 :\n",
                 "x = !z;（逻辑非作为值，且目标互换）");
    }

    /* ══ ⑨ `RELOP` 作为**值**：`x = n > 0;` ══ */
    translateResetNames();
    {
        Node *rel = mkBin(mkExp1("ID", "n"), "RELOP", ">", mkExp1("INT", "0"));
        InterCodeList *t = irList();
        t = translateExp(t, mkAssign("x", rel), NULL, NULL);
        expectIR(t,
                 "v1 := #0\n"
                 "IF v2 > #0 GOTO label1\n"
                 "GOTO label2\n"
                 "LABEL label1 :\n"
                 "v1 := #1\n"
                 "LABEL label2 :\n",
                 "x = n > 0;（关系运算作为值）");
    }

    /* ══ ⑩ 条件表达式的**值不被需要**时：`n > 0;` ══
     * 两个 `[place := …]` 都不产，但条件代码照常生成（它可能有副作用，
     * 且跳转本身是这段表达式的全部语义）。标号仍然要摆出来，否则 GOTO 悬空。 */
    translateResetNames();
    {
        Node *rel = mkBin(mkExp1("ID", "n"), "RELOP", ">", mkExp1("INT", "0"));
        InterCodeList *t = irList();
        t = translateExp(t, rel, NULL, NULL);
        expectIR(t,
                 "IF v1 > #0 GOTO label1\n"
                 "GOTO label2\n"
                 "LABEL label1 :\n"
                 "LABEL label2 :\n",
                 "n > 0;（值不被需要：不产 place 的 IR，条件代码照常）");
    }

    /* ══ ⑪ 钉住 `place` 与条件操作数**同名**时的"先清后读"（spec §11 P8）══
     * `x = !x;` 的 place 就是 x 自己（`v1`），而 `!x` 里的 `x` 是惰性在 IF 里读的。
     * 于是生成的顺序是**先 `v1 := #0`、再 `t1 := v1`** —— 读到的是刚清零的 0，
     * 而不是读之前 x 的旧值。这与 C 语义不同（C 里 `x = !x` 读旧值），但**逐字符合
     * 指导书表 2 的模板**（`code0 = [place := #0]` 排在最前）。
     *
     * ★ 本条**不是说这个行为对**，而是把它**钉死**：日后若有人"顺手修成 C 语义"
     * （先算进临时变量、或做别名门控），这条的实测会与期望不符、**当场变红**，
     * 而不是让输出悄悄变掉、判分口径又没对齐。改行为前先看 src/translate.c 里
     * `translateCondAsValue` 顶部那段注释：要改必须先确认判分口径。
     * 期望形态与用例⑧（`x = !z;`）对照着读：唯一的差别是条件操作数从 `v2`（z）
     * 变成 `v1`（x 自己）—— 而 `v1` 刚被 `v1 := #0` 清过。 */
    translateResetNames();
    {
        InterCodeList *t = irList();
        t = translateExp(t, mkAssign("x", mkNot(mkExp1("ID", "x"))), NULL, NULL);
        expectIR(t,
                 "v1 := #0\n"
                 "t1 := v1\n"                 /* <- 读的是刚被清掉的 v1，不是旧值 */
                 "IF t1 != #0 GOTO label2\n"
                 "GOTO label1\n"
                 "LABEL label1 :\n"
                 "v1 := #1\n"
                 "LABEL label2 :\n",
                 "x = !x;（place 与条件操作数同名：先清后读，钉住当前行为）");
    }

    fprintf(stderr, failures ? "FAILURES=%d\n" : "ALL PASS\n", failures);
    return failures ? 1 : 0;
}
