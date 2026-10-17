#include "optimize.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 实验三选做 3.2 的编译期开关只影响翻译层；优化器没有对应的关闭需求，
 * 但保留这个宏是为了与计划里的形状一致，也便于将来单独关掉优化做对拍。 */
#ifndef LAB3_OPT
#define LAB3_OPT 1
#endif

/* ============================================================================
 *  操作数口径：临时变量识别 / 读操作数表
 *
 *  这两件事被三处共用，必须只有**一份**定义，否则断言与计数会各说各话：
 *    · irCountOperandUses（公共接口，供优化 pass 与单测使用）；
 *    · assertTempsSingleAssign（先定义后使用的检查）；
 *    · Task 17–19 的子 pass。
 * ============================================================================ */

int irIsTemp(Operand o) {
    const char *p;
    /* 只认 OP_VAR：标号标签、常量、地址（&x）都不是临时变量。
     * ★ 不用 atoi 判：atoi("t1x") == 0 且 atoi("t") == 0，两者都会被误判。 */
    if (!o || o->kind != OP_VAR || !o->name) return 0;
    p = o->name;
    if (*p != 't') return 0;
    p++;
    if (*p < '0' || *p > '9') return 0;      /* 至少一位数字 */
    while (*p >= '0' && *p <= '9') p++;
    return *p == '\0';                       /* t1x、t12a 之类一律为假 */
}

static int operandEqual(Operand a, Operand b) {
    if (!a || !b) return 0;
    if (a->kind != b->kind) return 0;
    if (a->kind == OP_CONST) return a->value == b->value;
    return a->name && b->name && strcmp(a->name, b->name) == 0;
}

/* 本指令**读**的操作数（source/use），按出现顺序放进 out[]，返回个数（0..3）。
 *
 * ★ 口径表（与 Task 16 brief Step 3 逐条对应，改这里之前先改那张表）：
 *
 *   IR_ASSIGN     x := y             读 y
 *   IR_ADD/SUB/MUL/DIV x := y op z   读 y, z
 *   IR_ADDR       x := &y            读 y
 *   IR_ASSIGN_DEREF  x := *y         读 y
 *   IR_DEREF_ASSIGN  *x := y         读 x, y
 *   IR_CALL       x := CALL f        不读 x（定义），也不读函数名 f
 *   IR_IF         x relop y GOTO z   读 x, y, z（z 是标号）
 *   IR_GOTO/RETURN/ARG/PARAM/READ/WRITE  读 x
 *   IR_LABEL/FUNCTION                不读（名字不是变量使用）
 *   IR_DEC        x size             不读（声明不是读）
 *
 * 注意 IR_READ：它在“定义”意义上是把读入值写进 x，但在本项目的使用计数口径里
 * 要求把 x 计作一次读（见上表）—— 两件事分开判断，勿合并。
 *
 * 同一操作数在一行里出现多次（如 `x := y + y`），out[] 会给出多个元素，
 * 调用方自然数多次。
 */
static int codeReadOperands(InterCode *c, Operand out[3]) {
    int n = 0;
    if (!c) return 0;
    switch (c->kind) {
    case IR_ASSIGN:
        out[n++] = c->u.assign.y;
        break;
    case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV:
        out[n++] = c->u.binop.y;
        out[n++] = c->u.binop.z;
        break;
    case IR_ADDR:
        out[n++] = c->u.assign.y;
        break;
    case IR_ASSIGN_DEREF:
        out[n++] = c->u.deref.y;
        break;
    case IR_DEREF_ASSIGN:
        out[n++] = c->u.deref.x;   /* 写内存，地址仍是“读” */
        out[n++] = c->u.deref.y;
        break;
    case IR_CALL:
        break;                     /* x 是定义；f 是函数名 */
    case IR_IF:
        out[n++] = c->u.iff.x;
        out[n++] = c->u.iff.y;
        out[n++] = c->u.iff.z;
        break;
    case IR_GOTO: case IR_RETURN: case IR_ARG: case IR_PARAM:
    case IR_READ: case IR_WRITE:
        out[n++] = c->u.one.x;
        break;
    case IR_LABEL: case IR_FUNCTION: case IR_DEC:
        break;
    }
    return n;
}

int irCountOperandUses(InterCodeList *head, Operand o) {
    InterCodeList *h = irHead(head), *p;
    int count = 0;
    if (!h || !o) return 0;
    for (p = h->next; p != h; p = p->next) {
        Operand reads[3];
        int n = codeReadOperands(p->code, reads), i;
        for (i = 0; i < n; i++)
            if (operandEqual(reads[i], o)) count++;   /* 同行多次出现就数多次 */
    }
    return count;
}

/* 该指令**定义**的操作数；没有定义的指令返回 NULL。
 *
 * 有定义：ASSIGN / ADD / SUB / MUL / DIV / ADDR / ASSIGN_DEREF / CALL / READ / DEC。
 * 无定义：LABEL / FUNCTION / GOTO / IF / RETURN / ARG / PARAM / WRITE / DEREF_ASSIGN
 *         （`*x := y` 写的是 x 指向的内存，不是对临时变量 x 的定义）。
 * 只有名字是 tN 的操作数才进入断言射程；DEC 的 x 虽是变量，也照此过滤。
 *
 * ★ 放在 `#if LAB3_OPT` **之外**：公共查询 irTempDefCount 与单赋值断言必须共用
 *   同一份“定义”口径，且 `LAB3_OPT=0` 的构建也要能链接。 */
static Operand codeDefines(InterCode *c) {
    if (!c) return NULL;
    switch (c->kind) {
    case IR_ASSIGN:
    case IR_ADDR:
        return c->u.assign.x;
    case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV:
        return c->u.binop.x;
    case IR_ASSIGN_DEREF:
        return c->u.deref.x;
    case IR_CALL:
        return c->u.call.x;
    case IR_READ:
        return c->u.one.x;
    case IR_DEC:
        return c->u.dec.x;
    default:
        return NULL;
    }
}

int irTempDefCount(InterCodeList *head, Operand o) {
    InterCodeList *h = irHead(head), *p;
    int count = 0;
    if (!h || !irIsTemp(o)) return 0;
    for (p = h->next; p != h; p = p->next)
        if (operandEqual(codeDefines(p->code), o)) count++;
    return count;
}

#if LAB3_OPT

/* ============================================================================
 *  临时变量单赋值 / 先定义后使用断言
 * ============================================================================ */

#define MAX_TEMP_NAMES 4096

typedef struct {
    const char *name;
    int defs;               /* 已见定义条数 */
    int labelCountAtDef;    /* 上一次定义时的全局 LABEL 计数（判断中间是否跨控制流） */
    int funcSerialAtDef;    /* 上一次定义所在函数（FUNCTION 出现序号） */
    int lastDefIsConst;     /* 上一次定义是否为 `t := #c` */
    int lastDefConst;       /* 若是，c 的值 */
} TempDef;

typedef struct {
    TempDef entry[MAX_TEMP_NAMES];
    int count;
} TempDefTable;

static TempDef *tempDefFind(TempDefTable *t, const char *name) {
    int i;
    for (i = 0; i < t->count; i++)
        if (strcmp(t->entry[i].name, name) == 0) return &t->entry[i];
    return NULL;
}

static TempDef *tempDefAdd(TempDefTable *t, const char *name) {
    /* 与项目既有纪律一致：定长表溢出必须**响亮** abort，不静默丢弃。
     * 静默丢弃会把“第二次赋值”漏成“第一次”，断言随即失效。 */
    TempDef *d;
    if (t->count >= MAX_TEMP_NAMES) {
        fprintf(stderr, "internal error: 临时变量定义表溢出（上限 %d，遇到 %s）\n",
                MAX_TEMP_NAMES, name);
        abort();
    }
    d = &t->entry[t->count++];
    d->name = name;
    d->defs = 0;
    d->labelCountAtDef = 0;
    d->funcSerialAtDef = 0;
    d->lastDefIsConst = 0;
    d->lastDefConst = 0;
    return d;
}

static void tempViolation(const char *name, const char *what, int index,
                          const char *func) {
    if (func)
        fprintf(stderr, "internal error: 临时变量 %s %s（第 %d 条指令，函数 %s）\n",
                name, what, index, func);
    else
        fprintf(stderr, "internal error: 临时变量 %s %s（第 %d 条指令）\n",
                name, what, index);
    abort();
}

/* 指导书表 2“条件表达式作为值”模板（translateCondAsValue）的例外：
 *
 *     t := #0
 *     <条件代码，至少跨过一个 LABEL>
 *  LABEL l_true :
 *     t := #1
 *  LABEL l_false :
 *     IF t != #0 …
 *
 * 这是本仓库**刻意**照抄的模板（见 translate.c 中该函数的注释与设计文档 §6.2），
 * 它按设计会在同一临时变量上写两次。因此“全程序只赋值一次”不能按字面无条件执行，
 * 否则连 lab3 现有的 logic.cmm / Test/sample/prec.cmm 都会被当成内部错误。
 *
 * 例外判据**刻意收得很窄**，以保住断言的判别力：
 *   · 恰好是第二次定义（第三次起一律 abort）；
 *   · 上一次是 `t := #0`，这一次是 `t := #1`；
 *   · 两次定义之间跨过至少一个 LABEL，**且本次定义的前一条指令就是 LABEL**
 *     （真实模板是 `LABEL l_true :` 紧跟 `t := #1`；只要求“中间有 LABEL”会放过
 *     `t := #0 … LABEL x … 别的代码 … t := #1`）；
 *   · 同一函数。
 * 其余任何第二次赋值（包括本单测的 `t1 := #5` / `t1 := #6`）仍然响亮 abort。 */
static int isCondValuePair(const TempDef *d, InterCode *c,
                           int labelCount, int funcSerial, int prevKind) {
    return d->defs == 1 &&
           d->lastDefIsConst && d->lastDefConst == 0 &&
           labelCount > d->labelCountAtDef &&
           prevKind == IR_LABEL &&
           funcSerial == d->funcSerialAtDef &&
           c->kind == IR_ASSIGN &&
           c->u.assign.y && c->u.assign.y->kind == OP_CONST &&
           c->u.assign.y->value == 1;
}

/* 顺序扫描整条 IR：
 *   · tN 第二次被定义          ⇒ 诊断 + abort（上一条注释的模板除外）；
 *   · tN 在定义之前被读        ⇒ 诊断 + abort。
 *
 * ★ 每条指令内**先检查读、再登记定义**（Task 16b 修正）：若先登记，`t1 := t1`
 *   的首次出现会被当成“读到的正是刚登记的定义”而漏报。唯一例外是 `READ tN`：
 *   它的 x 既是定义也是“读自己”，该自读不算“赋值前使用”——只跳过与定义相同的
 *   那个读操作数，其余指令里“读与定义同操作数”（`t1 := t1`、`t1 := t1 + #1`）
 *   仍然响亮 abort。
 * 诊断尽量点名变量、指令序号与所在函数。 */
static void assertTempsSingleAssign(InterCodeList *h) {
    InterCodeList *hh = irHead(h), *p;
    TempDefTable defined;
    const char *func = NULL;
    int index = 0, labelCount = 0, funcSerial = 0, prevKind = -1;

    defined.count = 0;
    if (!hh) return;
    for (p = hh->next; p != hh; p = p->next) {
        InterCode *c = p->code;
        Operand def;
        Operand reads[3];
        int n, i;

        index++;
        if (!c) { prevKind = -1; continue; }
        if (c->kind == IR_FUNCTION) {
            funcSerial++;
            if (c->u.one.x && c->u.one.x->name) func = c->u.one.x->name;
        }
        if (c->kind == IR_LABEL) labelCount++;

        /* 先检查读：定义尚未登记，故 `t := t`（首次出现）会在这里被抓住。
         * READ 的自读（x 同时是定义）豁免。 */
        def = codeDefines(c);
        n = codeReadOperands(c, reads);
        for (i = 0; i < n; i++) {
            if (c->kind == IR_READ && def && operandEqual(reads[i], def))
                continue;
            if (irIsTemp(reads[i]) && !tempDefFind(&defined, reads[i]->name))
                tempViolation(reads[i]->name, "在被赋值之前使用", index, func);
        }

        /* 再登记定义（或判定条件取值例外的第二次定义）。 */
        if (def && irIsTemp(def)) {
            TempDef *d = tempDefFind(&defined, def->name);
            if (!d) {
                d = tempDefAdd(&defined, def->name);
                d->defs = 1;
                d->labelCountAtDef = labelCount;
                d->funcSerialAtDef = funcSerial;
                d->lastDefIsConst = (c->kind == IR_ASSIGN &&
                                     c->u.assign.y && c->u.assign.y->kind == OP_CONST);
                d->lastDefConst = d->lastDefIsConst ? c->u.assign.y->value : 0;
            } else if (isCondValuePair(d, c, labelCount, funcSerial, prevKind)) {
                d->defs = 2;
                d->labelCountAtDef = labelCount;
                d->lastDefIsConst = 1;
                d->lastDefConst = 1;
            } else {
                tempViolation(def->name, "被赋值两次", index, func);
            }
        }

        prevKind = c->kind;
    }
}

/* ============================================================================
 *  框架：反复跑各子 pass，直到一轮下来不再有改动（不动点）
 *
 *  每个子 pass 返回**本次改动条数**；0 表示该 pass 已不动。
 *  Task 16 全部是桩；Task 17–19 分别填实现，`optimizeIR` 不需要再改。
 * ============================================================================ */

static int passesChanged;

static int passConstFold(InterCodeList *h)   { (void)h; return 0; }
static int passAlgebra(InterCodeList *h)     { (void)h; return 0; }
static int passCopyProp(InterCodeList *h)    { (void)h; return 0; }
static int passInlineConst(InterCodeList *h) { (void)h; return 0; }
static int passDeadLabel(InterCodeList *h)   { (void)h; return 0; }
static int passGotoSimplify(InterCodeList *h){(void)h; return 0; }
static int passCondInvert(InterCodeList *h)  { (void)h; return 0; }

void optimizeIR(InterCodeList *head) {
    int rounds = 0;
    /* 优化前先验不变量：若输入本身已经违反单赋值，任何一个 pass 都可能把错误放大。 */
    assertTempsSingleAssign(head);
    do {
        passesChanged = 0;
        passesChanged += passConstFold(head);
        passesChanged += passAlgebra(head);
        passesChanged += passCopyProp(head);
        passesChanged += passInlineConst(head);
        passesChanged += passGotoSimplify(head);
        passesChanged += passCondInvert(head);
        passesChanged += passDeadLabel(head);
    } while (passesChanged > 0 && ++rounds < 20);
    /* 上限触发不得静默：说明至少有一个 pass 不收敛，改动仍在继续。 */
    if (passesChanged > 0)
        fprintf(stderr, "internal warning: optimizer hit 20-round limit\n");
    assertTempsSingleAssign(head);
}

#else
/* LAB3_OPT=0 时保留空实现，只用于“未优化”对拍。 */
void optimizeIR(InterCodeList *head) { (void)head; }
#endif
