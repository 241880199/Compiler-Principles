#ifndef IR_H
#define IR_H

#include <stdio.h>

/* 中间代码的数据结构、构造函数族与**唯一的打印点**。见 docs/plan.md Task 6。
 *
 * 两条模块纪律：
 *  1) 其它模块（translate.c）**不直接 printf**，一律经 printIR 出文本 —— "换输出格式"
 *     与"加优化"因此互不影响；
 *  2) 操作数一律由本文件的构造函数制造（字符串在**构造时复制**），调用方可以自由在栈
 *     缓冲区上拼名字。 */

typedef struct Operand_ *Operand;
struct Operand_ {
    /* OP_ADDR 表示「取地址」形式 `&x`：**这个 `&` 由 printOperand 加**（见下方
       「两个 & 的分工」）。它既是 `ARG &arr` 的操作数，也是 `x := &y + #8` 这类 ARITH
       的操作数（虚拟机 getValue 认识 `&` 前缀）。★ 别把它当 IR_ADDR 的 y。
       `*x` 不单设 kind：它只出现在赋值两侧，由 IR_ASSIGN_DEREF / IR_DEREF_ASSIGN 表达。 */
    enum { OP_VAR, OP_CONST, OP_LABEL, OP_FUNC, OP_ADDR } kind;
    const char *name;      /* OP_VAR / OP_LABEL / OP_FUNC / OP_ADDR；xstrdup 出来的副本 */
    int         value;     /* OP_CONST */
};

/* ── 文本里两个 `&` 的分工（Task 6 审查补记）────────────────────────────────
 * 打印出来的 `&` 有两个来源，**不要混用**：
 *   • IR_ADDR（`x := &y`）：这个 `&` 由 **printIR** 打印。故它的 `y` 必须是
 *     **裸名字**的 OP_VAR（`opVar`），**不能**是已带 `&` 的 OP_ADDR —— 否则打成 `&&x`。
 *     真机上是语法错，本项目参考机的装载器也会把它拒掉（表现为"莫名装载失败"）。
 *     printIR 对此做了**响亮失败**：y 若是 OP_ADDR 就带诊断 abort，而不是静默打出 `&&x`。
 *   • OP_ADDR（`&x`）：这个 `&` 由 **printOperand** 打印，用于 `ARG &arr`、
 *     以及 `x := &y + #4` 这类把地址当数值参与 ARITH 的场合。
 * 口诀：**要一个裸名字让 printIR 去加 `&` ⇒ 用 opVar；要一个自带 `&` 的地址操作数
 * （给 ARG / ARITH）⇒ 用 opAddr。** 传给 IR_ADDR 的 y 永远不该是 opAddr 的产物。 */
typedef struct InterCode {
    enum { IR_LABEL, IR_FUNCTION, IR_ASSIGN, IR_ADD, IR_SUB, IR_MUL, IR_DIV,
           IR_ADDR, IR_DEREF_ASSIGN, IR_ASSIGN_DEREF, IR_GOTO, IR_IF, IR_RETURN,
           IR_DEC, IR_ARG, IR_CALL, IR_PARAM, IR_READ, IR_WRITE } kind;
    union {
        struct { Operand x; } one;                     /* GOTO/RETURN/ARG/PARAM/READ/WRITE/LABEL/FUNCTION */
        struct { Operand x, y; } assign;               /* x := y / x := &y / x := *y */
        struct { Operand x, y, z; } binop;             /* x := y op z */
        struct { Operand x, y; } deref;                /* *x := y （IR_DEREF_ASSIGN） */
        struct { Operand x, y, z; int relop; } iff;    /* IF x relop y GOTO z */
        struct { Operand x; int size; } dec;           /* DEC x size */
        struct { Operand x, f; } call;                 /* x := CALL f */
    } u;
} InterCode;

typedef struct InterCodeList {
    InterCode *code;
    struct InterCodeList *prev, *next;
} InterCodeList;

Operand opVar(const char *name);
Operand opConst(int v);
Operand opLabel(const char *name);
Operand opFunc(const char *name);      /* ★ 传源码里的真实函数名 */
Operand opAddr(const char *name);

/* 关系运算符：0..5 依次为 < <= > >= == !=（与要求书的六个 relop 一一对应）。
 * 传进来的文本必须是这六个之一；否则 abort（大声失败）。 */
int  irRelopCode(const char *relopText);
const char *irRelopText(int code);

InterCode  *newCode(int kind);
InterCodeList *irList(void);                       /* 空的双向循环链表（哨兵结点） */
InterCodeList *irAppend(InterCodeList *tail, InterCode *c);
InterCodeList *irHead(InterCodeList *any);         /* 走到哨兵 */
/* 从链表摘除一个结点，并释放**指令与结点**本身。
 * ★ 刻意**不 free 操作数**：Operand 会在指令之间共享（translate.c 会把同一个 `place`
 *   操作数塞进多条指令，例如 `READ x` 与随后的 `x := #0`），逐条释放会留下悬垂指针。
 *   编译器是一次性进程，操作数内存交给进程退出回收即可。
 *   —— 这不是"漏了 free"，别顺手"修好"：那正是把这个悬垂指针 bug 重新引入。 */
void          deleteCode(InterCodeList *c);
void          printIR(FILE *out, InterCodeList *any);

int  operandIsConst(Operand o);
int  operandIsVarLike(Operand o);                  /* OP_VAR 或 OP_ADDR */
int  operandIsLabel(Operand o);                    /* 供优化 pass 统计标号引用 */

#endif
