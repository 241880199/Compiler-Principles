#include "optimize.h"
#include <limits.h>
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

/* 操作数是否是名为 name 的标号（OP_LABEL）。 */
static int labelIsNamed(Operand o, const char *name) {
    return o && o->kind == OP_LABEL && o->name && strcmp(o->name, name) == 0;
}

/* 从 from（含）向后、在**同一函数内**找 `LABEL lab :`；遇到下一个 FUNCTION 即停。
 * 只用于单赋值例外的 O9 形态判定（见 isCondValuePair 的第二条分支）。 */
static int labelDefinedAfter(InterCodeList *hh, InterCodeList *from, Operand lab) {
    InterCodeList *p;
    if (!lab || lab->kind != OP_LABEL || !lab->name) return 0;
    for (p = from; p != hh; p = p->next) {
        if (p->code && p->code->kind == IR_FUNCTION) return 0;
        if (p->code && p->code->kind == IR_LABEL &&
            labelIsNamed(p->code->u.one.x, lab->name))
            return 1;
    }
    return 0;
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
 *   · 同一函数；
 *   · 两种合法形态之一：
 *       (a) 两次定义之间跨过至少一个 LABEL，**且本次定义的前一条指令就是 LABEL**
 *           （真实模板是 `LABEL l_true :` 紧跟 `t := #1`；只要求“中间有 LABEL”会放过
 *           `t := #0 … LABEL x … 别的代码 … t := #1`）；
 *       (b) **Task 19 的 O9 之后**：前一条指令是 `IF … GOTO L`，且 L 在同一函数内、
 *           位于本次定义之后。O9 把 `IF c GOTO l_true / GOTO l_false / LABEL l_true /
 *           t := #1 / LABEL l_false` 改写成 `IF !c GOTO l_false / t := #1 /
 *           LABEL l_false` —— 原模板的 LABEL 被删掉，但“条件成立时才执行 t := #1”
 *           的语义与形状都没变。没有这一分支，任何含条件取值表达式的真实程序都会在
 *           O9 之后被后置断言误杀。判据仍是“IF 确实跳过本次定义”（向后找目标标号），
 *           不接受向后跳或目标不在本函数的 IF。
 * 其余任何第二次赋值（包括本单测的 `t1 := #5` / `t1 := #6`）仍然响亮 abort。 */
static int isCondValuePair(const TempDef *d, InterCode *c,
                           int labelCount, int funcSerial, int prevKind,
                           int guardedByIf) {
    if (d->defs != 1 ||
        !d->lastDefIsConst || d->lastDefConst != 0 ||
        funcSerial != d->funcSerialAtDef ||
        c->kind != IR_ASSIGN ||
        !c->u.assign.y || c->u.assign.y->kind != OP_CONST ||
        c->u.assign.y->value != 1)
        return 0;
    return (prevKind == IR_LABEL && labelCount > d->labelCountAtDef) ||
           guardedByIf;
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
    InterCodeList *hh = irHead(h), *p, *prevNode = NULL;
    TempDefTable defined;
    const char *func = NULL;
    int index = 0, labelCount = 0, funcSerial = 0, prevKind = -1;

    defined.count = 0;
    if (!hh) return;
    for (p = hh->next; p != hh; p = p->next) {
        InterCode *c = p->code;
        Operand def;
        Operand reads[3];
        int n, i, guardedByIf = 0;

        index++;
        if (!c) { prevKind = -1; prevNode = p; continue; }
        if (c->kind == IR_FUNCTION) {
            funcSerial++;
            if (c->u.one.x && c->u.one.x->name) func = c->u.one.x->name;
        }
        if (c->kind == IR_LABEL) labelCount++;

        /* O9 后的条件取值模板：前一条 IF 必须向后跳过本次定义（目标标号在本函数
         * 内、位于本次定义之后），才承认这是“条件赋值”，见 isCondValuePair(b)。 */
        if (prevNode && prevNode->code && prevNode->code->kind == IR_IF)
            guardedByIf = labelDefinedAfter(hh, p, prevNode->code->u.iff.z);

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
            } else if (isCondValuePair(d, c, labelCount, funcSerial, prevKind,
                                       guardedByIf)) {
                d->defs = 2;
                d->labelCountAtDef = labelCount;
                d->lastDefIsConst = 1;
                d->lastDefConst = 1;
            } else {
                tempViolation(def->name, "被赋值两次", index, func);
            }
        }

        prevKind = c->kind;
        prevNode = p;
    }
}

/* ============================================================================
 *  O1 常量折叠 / O2 代数化简（Task 17）
 *
 *  规则表（与 task-17-brief.md Step 1 逐条对应）：
 *
 *  O1（两侧都是 OP_CONST 的 ARITH ⇒ 原地改成 ASSIGN x := #结果）
 *      #3 + #4      ⇒ #7          #2 - #5       ⇒ #-3
 *      #6 * #7      ⇒ #42         #7 / #2       ⇒ #3
 *      #-7 / #2     ⇒ #-4         #7 / #-2      ⇒ #-4    （向下取整，P1）
 *      #-7 / #-2    ⇒ #3          #5 / #0       ⇒ 不折叠（除零交运行期）
 *      任意结果超出 int 范围 ⇒ 不折叠（参考机是 Python 无界整数，折成 C int 会改语义；
 *      INT_MIN / -1 也由此自然落空）。
 *
 *  O2（单侧常量恒等式；只作用 IR_ADD/SUB/MUL/DIV）
 *      y + #0 / #0 + y  ⇒ y       y - #0        ⇒ y
 *      y * #1 / #1 * y  ⇒ y       y / #1        ⇒ y
 *      y * #0 / #0 * y  ⇒ #0      #0 - y        ⇒ 不动（取负，删不掉）
 *
 *  O1 在框架里先跑：两侧常量的输入由 O1 处理；若 O1 因除零/溢出拒绝折叠，
 *  O2 也**不得**接手（否则会把“留待运行期”的语义悄悄改掉）。
 *
 *  原地改指令：先保存 x（union 里 assign.x 与 binop.x 同址），再改 kind 与字段；
 *  不新建/删除结点。被丢弃的旧操作数交给进程退出回收（同 deleteCode 的口径）。
 * ============================================================================ */

static int isArith(InterCode *c) {
    return c && (c->kind == IR_ADD || c->kind == IR_SUB ||
                 c->kind == IR_MUL || c->kind == IR_DIV);
}

/* 向下取整除法（虚拟机语义 P1）。C99 的 / 向零截断，故按余数符号修正：
 *   -7 / 2  ⇒ q=-3, r=-1 ⇒ q-- = -4
 *    7 / -2 ⇒ q=-3, r= 1 ⇒ q-- = -4
 *   -7 / -2 ⇒ q= 3, r=-1 ⇒ 不变 =  3
 * b == 0 由调用方排除。 */
static long long floorDiv(long long a, long long b) {
    long long q = a / b;
    long long r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) q--;
    return q;
}

/* 计算常量二元运算。成功返回 1 并写 *out；除零或结果溢出 int 返回 0（不折叠）。 */
static int evalConstBinop(int kind, int a, int b, int *out) {
    long long r;
    switch (kind) {
    case IR_ADD: r = (long long)a + (long long)b; break;
    case IR_SUB: r = (long long)a - (long long)b; break;
    case IR_MUL: r = (long long)a * (long long)b; break;
    case IR_DIV:
        if (b == 0) return 0;
        r = floorDiv((long long)a, (long long)b);
        break;
    default:
        return 0;
    }
    if (r < (long long)INT_MIN || r > (long long)INT_MAX) return 0;
    *out = (int)r;
    return 1;
}

/* 原地把 ARITH 改成 `x := rhs`（先取 x，避免被 union 覆盖）。 */
static void makeAssign(InterCode *c, Operand rhs) {
    Operand x = c->u.binop.x;
    c->kind = IR_ASSIGN;
    c->u.assign.x = x;
    c->u.assign.y = rhs;
}

static int isConstVal(Operand o, int v) {
    return o && o->kind == OP_CONST && o->value == v;
}

/* O1：两侧常量 ⇒ ASSIGN。返回本次改动条数。 */
static int passConstFold(InterCodeList *h) {
    InterCodeList *hh = irHead(h), *p;
    int changed = 0;

    if (!hh) return 0;
    for (p = hh->next; p != hh; p = p->next) {
        InterCode *c = p->code;
        int v;
        if (!isArith(c)) continue;
        if (!c->u.binop.y || c->u.binop.y->kind != OP_CONST) continue;
        if (!c->u.binop.z || c->u.binop.z->kind != OP_CONST) continue;
        if (!evalConstBinop(c->kind, c->u.binop.y->value,
                            c->u.binop.z->value, &v)) continue;
        makeAssign(c, opConst(v));
        changed++;
    }
    return changed;
}

/* O2：单侧常量恒等式。返回本次改动条数。 */
static int passAlgebra(InterCodeList *h) {
    InterCodeList *hh = irHead(h), *p;
    int changed = 0;

    if (!hh) return 0;
    for (p = hh->next; p != hh; p = p->next) {
        InterCode *c = p->code;
        Operand y, z;
        if (!isArith(c)) continue;
        y = c->u.binop.y;
        z = c->u.binop.z;
        if (!y || !z) continue;
        /* 两侧常量归 O1；这里不接手（含 O1 因除零/溢出拒绝的输入）。 */
        if (y->kind == OP_CONST && z->kind == OP_CONST) continue;
        switch (c->kind) {
        case IR_ADD:
            if (isConstVal(z, 0))            { makeAssign(c, y); changed++; }
            else if (isConstVal(y, 0))       { makeAssign(c, z); changed++; }
            break;
        case IR_SUB:
            if (isConstVal(z, 0))            { makeAssign(c, y); changed++; }
            /* `#0 - y` 是取负，不能删。 */
            break;
        case IR_MUL:
            if (isConstVal(z, 1))            { makeAssign(c, y); changed++; }
            else if (isConstVal(y, 1))       { makeAssign(c, z); changed++; }
            else if (isConstVal(z, 0))       { makeAssign(c, opConst(0)); changed++; }
            else if (isConstVal(y, 0))       { makeAssign(c, opConst(0)); changed++; }
            break;
        case IR_DIV:
            if (isConstVal(z, 1))            { makeAssign(c, y); changed++; }
            break;
        default:
            /* isArith 已过滤，其余 kind 不可达；写出来是为了 -Wswitch 零警告。 */
            break;
        }
    }
    return changed;
}

/* ============================================================================
 *  读使用位置口径（O3/O4 共用，**只有这一张表**）
 *
 *  哪些位置是“读”、其中哪些**不能**被常量替换，集中在这里判断；O3/O4 不另写一份。
 *
 *  可替换（可内联）的读位置：
 *      IR_ASSIGN.y
 *      IR_ADD/SUB/MUL/DIV.y、z
 *      IR_DEREF_ASSIGN.y        （`*x := t` 的值）
 *      IR_IF.x、y
 *      IR_GOTO/RETURN/ARG/WRITE.x
 *
 *  **不可替换**的读位置（硬性排除；替换后会产出非法 IR 或改变语义）：
 *      IR_ASSIGN_DEREF.y        （`place := *t` 的基址）
 *      IR_DEREF_ASSIGN.x        （`*t := y` 的基址）
 *      IR_ADDR.y                （`x := &t`，会打成 `&#5`）
 *      IR_PARAM.x               （`PARAM #5` 会被参考机装载器拒绝）
 *      IR_READ.x                （目标位置，不是可替换的读；替换会让 t 变成第二次定义）
 *
 *  目标位置（ASSIGN.x / ARITH.x / CALL.x / DEC.x）本来就不是读，绝不替换。
 *  ★ IR_READ.x 在 codeReadOperands 里**同时**计作读（用于使用计数），
 *    但在这里是“不可替换”；两件事分开判断，勿合并。
 *
 *  所有操作都以 operandEqual 为准（按 kind 比较，变量比名字）。
 * ============================================================================ */

/* 检查 c 中操作数 from 的一处读位置。
 * canReplace 为真且 to 非 NULL 时就地替换；total 与 blocked 累加计数。 */
static void noteReadUse(Operand *slot, Operand from, Operand to, int canReplace,
                        int *total, int *blocked) {
    if (!slot || !*slot || !operandEqual(*slot, from)) return;
    (*total)++;
    if (!canReplace) {
        (*blocked)++;
        return;
    }
    if (to) *slot = to;
}

/* 遍历 c 中 from 的每一处**读**；to 非 NULL 时把可替换位置改成 to。
 * 返回读出现总次数（与 codeReadOperands 的口径一致）；*blocked 累加其中
 * 落在“不可替换”位置的次数。 */
static int scanReadUses(InterCode *c, Operand from, Operand to, int *blocked) {
    int total = 0;

    if (!c || !from) return 0;
    switch (c->kind) {
    case IR_ASSIGN:
        noteReadUse(&c->u.assign.y, from, to, 1, &total, blocked);
        break;
    case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV:
        noteReadUse(&c->u.binop.y, from, to, 1, &total, blocked);
        noteReadUse(&c->u.binop.z, from, to, 1, &total, blocked);
        break;
    case IR_ADDR:
        noteReadUse(&c->u.assign.y, from, to, 0, &total, blocked);
        break;
    case IR_ASSIGN_DEREF:
        noteReadUse(&c->u.deref.y, from, to, 0, &total, blocked);
        break;
    case IR_DEREF_ASSIGN:
        noteReadUse(&c->u.deref.x, from, to, 0, &total, blocked);  /* 基址 */
        noteReadUse(&c->u.deref.y, from, to, 1, &total, blocked);  /* 值 */
        break;
    case IR_IF:
        noteReadUse(&c->u.iff.x, from, to, 1, &total, blocked);
        noteReadUse(&c->u.iff.y, from, to, 1, &total, blocked);
        noteReadUse(&c->u.iff.z, from, to, 0, &total, blocked);    /* 标号目标 */
        break;
    case IR_GOTO: case IR_RETURN: case IR_ARG: case IR_WRITE:
        noteReadUse(&c->u.one.x, from, to, 1, &total, blocked);
        break;
    case IR_PARAM: case IR_READ:
        noteReadUse(&c->u.one.x, from, to, 0, &total, blocked);
        break;
    case IR_CALL: case IR_LABEL: case IR_FUNCTION: case IR_DEC:
        break;   /* CALL.x 是定义；函数名/标号/声明都不是读 */
    }
    return total;
}

/* 把 c 中 from 的所有**可替换**读位置改成 to，返回替换条数。 */
static int replaceReadOperand(InterCode *c, Operand from, Operand to) {
    int blocked = 0;
    int total = scanReadUses(c, from, to, &blocked);
    return total - blocked;
}

/* 检查 c 中 t 的读使用是否都落在可内联位置：只要有不可内联的一处，
 * 就把 *ok 置 0。返回 t 在 c 中作为读出现的次数。 */
static int canInlineConstUse(InterCode *c, Operand t, int *ok) {
    int blocked = 0;
    int total = scanReadUses(c, t, NULL, &blocked);
    if (blocked) *ok = 0;
    return total;
}

/* ============================================================================
 *  内层重扫的防御上限（Task 19b，审查 Minor 3）
 *
 *  下面几个 pass 都采用“改一次就从头重扫”的 `for (;;)`。正常路径靠各自的规则
 *  保证每次改写都朝不动点前进，但内层循环**绕过了外层 20 轮上限**：一旦将来某条
 *  防御（例如 O8 的 gotoChainSafe）被改坏，这里不会打印 20 轮警告，而是**挂死**
 *  （Task 19 的变异 3 已实测）。故给每个 pass 一次调用内的改写次数加一个远高于
 *  真实 IR 改写量的防御上限；超限即判定不收敛，必须响亮 abort。
 *
 *  ★ 这是防未来防御被改坏的兜底，**不是正常路径**：10000 远大于本仓库测试集里
 *    任何程序（包括 lab3 五个用例组）的改写量。
 * ============================================================================ */
#define PER_PASS_REWRITE_MAX 10000

static void noteRewrite(const char *pass, int changed) {
    if (changed > PER_PASS_REWRITE_MAX) {
        fprintf(stderr,
                "internal error: %s 改写次数超过上限（疑似不收敛）\n", pass);
        abort();
    }
}

/* ============================================================================
 *  O4 结果直达（Task 18）+ O5 冗余拷贝消除（Task 22b）
 *
 *  O5：`x := x`（两边都是同一个 OP_VAR）⇒ 删除。
 *  只处理 OP_VAR 同名：临时变量 `t := t` 若首次出现会被 assertTempsSingleAssign
 *  按“赋值前使用”拦下，若第二次定义会被“重复赋值”拦下，故 O5 实际只作用于
 *  非临时变量（`vN := vN` 等）。与 O4 共用 PER_PASS_REWRITE_MAX 防御上限。
 *
 *  O4 紧邻两条：
 *      t := <rhs>        ← t 是临时变量
 *      x := t            ← 下一条 IR_ASSIGN，且 y 恰为 t
 *  ⇒ 第二条整体改成第一条的计算、目的地换成 x，删除第一条。
 *
 *  条件：
 *    · irTempDefCount(h, t) == 1 且 irCountOperandUses(h, t) == 1；
 *    · 源指令只允许 IR_ASSIGN 与 IR_ADD/SUB/MUL/DIV —— 后四者是设计文档 §7 的
 *      “t := …; x := t” 本义（brief 用例 `t1 := v2 + #1 / v1 := t1` 需要它）。
 *      IR_ADDR / IR_ASSIGN_DEREF 刻意不作 O4 的源（brief 明确排除）；
 *      CALL / READ / DEC 也不作源（它们的“结果”不是可直接搬走的值）。
 *    · 第二条必须 IR_ASSIGN 且 y == t；x 是目标，不参与使用计数。
 *
 *  只在本基本块内改紧邻两条，不跨 LABEL/FUNCTION。删除后用“从头再扫”处理同一
 *  pass 内的链式拷贝；固定点外层继续收敛。
 * ============================================================================ */

static int passCopyProp(InterCodeList *h) {
    InterCodeList *hh = irHead(h), *p;
    int changed = 0;

    if (!hh) return 0;
    for (;;) {
        int did = 0;
        for (p = hh->next; p != hh; p = p->next) {
            InterCode *c = p->code, *d;
            InterCodeList *q = p->next;
            Operand t, x;
            InterCode moved;

            /* O5：x := x（两边都是同一个 OP_VAR）⇒ 删除。先于 O4 处理：
             * 自拷贝不依赖使用计数，也没有可搬走的 rhs。 */
            if (c && c->kind == IR_ASSIGN &&
                c->u.assign.x && c->u.assign.y &&
                c->u.assign.x->kind == OP_VAR && c->u.assign.y->kind == OP_VAR &&
                strcmp(c->u.assign.x->name, c->u.assign.y->name) == 0) {
                deleteCode(p);
                changed++;
                noteRewrite("O5 冗余拷贝", changed);
                did = 1;
                break;
            }

            if (!c || (c->kind != IR_ASSIGN && !isArith(c))) continue;
            t = (c->kind == IR_ASSIGN) ? c->u.assign.x : c->u.binop.x;
            if (!irIsTemp(t)) continue;
            if (q == hh || !q->code || q->code->kind != IR_ASSIGN) continue;
            d = q->code;
            if (!operandEqual(d->u.assign.y, t)) continue;
            if (irTempDefCount(hh, t) != 1) continue;
            if (irCountOperandUses(hh, t) != 1) continue;

            x = d->u.assign.x;     /* 先保存目标（union 覆盖前） */
            moved = *c;            /* 浅拷贝；操作数共享，deleteCode 不释放它们 */
            deleteCode(p);         /* 删除 `t := <rhs>` */
            *d = moved;            /* 第二条改成同一计算，目的地换成 x */
            if (d->kind == IR_ASSIGN) d->u.assign.x = x;
            else                      d->u.binop.x = x;   /* isArith 保证 */
            changed++;
            noteRewrite("O4 结果直达", changed);
            did = 1;
            break;                 /* 链表已变，从头再扫 */
        }
        if (!did) break;
    }
    return changed;
}

/* ============================================================================
 *  O3 立即数内联（Task 18）
 *
 *  候选：`t := #c`（IR_ASSIGN，t 是临时变量，irTempDefCount(h, t) == 1）。
 *  只有 t 的**所有读使用**都落在可内联位置时才处理；任何一处落在
 *  scanReadUses 标出的不可替换位置，整个候选跳过（保留 `t := #c`）。
 *
 *  动作：用 opConst(c) 替换全部可内联读使用 → 删除候选 → changed++。
 *  用“处理一个就从 head 再扫”的循环，保证删除结点后迭代器不悬垂。
 *  条件取值模板（t 有两次定义）由 irTempDefCount == 1 整体排除。
 * ============================================================================ */

static int passInlineConst(InterCodeList *h) {
    InterCodeList *hh = irHead(h), *p;
    int changed = 0;

    if (!hh) return 0;
    for (;;) {
        int did = 0;
        for (p = hh->next; p != hh; p = p->next) {
            InterCode *c = p->code;
            Operand t, val;
            InterCodeList *q;
            int ok = 1;

            if (!c || c->kind != IR_ASSIGN) continue;
            t = c->u.assign.x;
            val = c->u.assign.y;
            if (!irIsTemp(t) || !val || val->kind != OP_CONST) continue;
            if (irTempDefCount(hh, t) != 1) continue;

            /* 第一遍只检查：任何一处不可内联 ⇒ 整个候选放弃。 */
            for (q = hh->next; q != hh; q = q->next) {
                canInlineConstUse(q->code, t, &ok);
                if (!ok) break;
            }
            if (!ok) continue;

            /* 第二遍替换。常量操作数可被多处共享（操作数不可变）。 */
            {
                Operand cst = opConst(val->value);
                for (q = hh->next; q != hh; q = q->next)
                    (void)replaceReadOperand(q->code, t, cst);
            }
            deleteCode(p);
            changed++;
            noteRewrite("O3 立即数内联", changed);
            did = 1;
            break;                 /* 链表已变，从头再扫 */
        }
        if (!did) break;
    }
    return changed;
}

/* ============================================================================
 *  框架：反复跑各子 pass，直到一轮下来不再有改动（不动点）
 *
 *  每个子 pass 返回**本次改动条数**；0 表示该 pass 已不动。
 *  Task 16 建框架；Task 17 填 O1/O2，Task 18 填 O3/O4，Task 19 填其余。
 * ============================================================================ */

/* ============================================================================
 *  O6–O9 控制流精简（Task 19）
 *
 *  规则（与 task-19-brief.md 的表逐条对应）：
 *    O6  `LABEL L :` 且全程序无 GOTO.l / IF….z / CALL.f 指向 L ⇒ 删标号；
 *    O7  `GOTO L` 的下一条**指令**就是 `LABEL L :`            ⇒ 删 GOTO；
 *    O8  `GOTO L1` 且 `LABEL L1 :` 的下一条是 `GOTO L2`
 *        （L2 有**同函数**定义）                              ⇒ 第一条改成 GOTO L2；
 *    O9  `IF c GOTO L1 / GOTO L2 / LABEL L1 : / BODY / LABEL L2 :`
 *        且 **IF 的目标就是 L1**、L1 全程序只被这一条 IF 引用   ⇒ `IF !c GOTO L2 / BODY /
 *        LABEL L2 :`（删 GOTO L2 与 LABEL L1）。
 *
 *  ★ 动态 vs 静态（不许夸大）：
 *    · O9 是**唯一**减少**动态**执行条数的一条：每走一次假分支省 1 条（那条 GOTO L2）。
 *      真分支路径执行条数不变（省掉的 GOTO 本来就不走）。
 *    · O6 每“落入”一次省 1 条；O8 每执行一次省 1 条（少一次中转跳转）。
 *    · **O7 的动态收益是 0**：删掉 GOTO L 后控制流落入下一条 `LABEL L`，省的 1 条
 *      GOTO 被多计入的 1 条 LABEL 抵消（虚拟机里 LABEL 也是一条指令）。它只降静态行数。
 *
 *  ★ 边界：所有标号查找/改写都在**同一 FUNCTION 内**；遇到下一个 FUNCTION 即停。
 *    翻译层的标号名当前全局唯一，但优化器不依赖这一点。
 *  ★ O8 防环：`LABEL L1: GOTO L2` + `LABEL L2: GOTO L1` 会让一步改写来回翻转。
 *    改写前沿 L2 的 `LABEL→GOTO` 链走一遍（最多 64 步；回到 L1 或出现重复标签即
 *    判定有环）并跳过。没有这道闸，环上的改写会在本 pass 的“改一次就重扫”里
 *    永远转下去（实测是挂起，不是外层 20 轮警告）——见 task-19-report.md 的变异 3。
 * ============================================================================ */

/* O6 的引用计数：GOTO.x / IF.z 按 OP_LABEL 比；CALL.f 是 **OP_FUNC**，只按名字比。
 * 全程序计数是保守的（安全）：跨函数同名也会被算作引用，因此宁可不删。
 * LABEL / FUNCTION 定义本身不是引用。 */
static int labelRefCount(InterCodeList *hh, const char *name) {
    InterCodeList *p;
    int n = 0;

    if (!hh || !name) return 0;
    for (p = hh->next; p != hh; p = p->next) {
        InterCode *c = p->code;
        if (!c) continue;
        switch (c->kind) {
        case IR_GOTO:
            if (labelIsNamed(c->u.one.x, name)) n++;
            break;
        case IR_IF:
            if (labelIsNamed(c->u.iff.z, name)) n++;
            break;
        case IR_CALL:
            /* CALL.f 是 OP_FUNC，不能用 operandEqual 去比 OP_LABEL。 */
            if (c->u.call.f && c->u.call.f->name &&
                strcmp(c->u.call.f->name, name) == 0)
                n++;
            break;
        default:
            break;
        }
    }
    return n;
}

/* p 所在函数区域的起点（含 `FUNCTION` 结点本身）；没有 FUNCTION 时是第一条指令。
 * 区域 = 一个 FUNCTION 结点直到下一个 FUNCTION 之前。 */
static InterCodeList *funcRegionStart(InterCodeList *hh, InterCodeList *p) {
    InterCodeList *q = p;

    if (p->code && p->code->kind == IR_FUNCTION) return p;
    while (q->prev != hh && q->prev->code &&
           q->prev->code->kind != IR_FUNCTION)
        q = q->prev;
    if (q->prev != hh && q->prev->code &&
        q->prev->code->kind == IR_FUNCTION)
        return q->prev;
    return q;
}

/* 区域终点（**不含**）：下一个 FUNCTION 结点，或哨兵。 */
static InterCodeList *funcRegionEnd(InterCodeList *hh, InterCodeList *p) {
    InterCodeList *q = p;

    while (q->next != hh &&
           !(q->next->code && q->next->code->kind == IR_FUNCTION))
        q = q->next;
    return q->next;
}

/* 在 at 所在函数内找 `LABEL name :`（可在 at 之前或之后）。 */
static InterCodeList *findLabelInFunc(InterCodeList *hh, InterCodeList *at,
                                      const char *name) {
    InterCodeList *s = funcRegionStart(hh, at);
    InterCodeList *e = funcRegionEnd(hh, at);
    InterCodeList *p;

    for (p = s; p != e; p = p->next)
        if (p->code && p->code->kind == IR_LABEL &&
            labelIsNamed(p->code->u.one.x, name))
            return p;
    return NULL;
}

/* 在 from **之后**、同一函数内找 `LABEL name :`。 */
static InterCodeList *findLabelAfterInFunc(InterCodeList *hh, InterCodeList *from,
                                           const char *name) {
    InterCodeList *e = funcRegionEnd(hh, from);
    InterCodeList *p;

    for (p = from->next; p != e; p = p->next)
        if (p->code && p->code->kind == IR_LABEL &&
            labelIsNamed(p->code->u.one.x, name))
            return p;
    return NULL;
}

#define GOTO_CHAIN_MAX 64

/* O8 防环：沿 L2 的 `LABEL cur : GOTO next` 链前看。
 *   · 回到 L1、重复标签、或超过 64 步 ⇒ 有环/不收敛 ⇒ 返回 0（跳过这次改写）；
 *   · 链在“无 LABEL 定义”或“下一条不是 GOTO”处终止 ⇒ 返回 1（安全）。
 * L1 == L2 的自环也在第一步就拒绝。 */
static int gotoChainSafe(InterCodeList *hh, InterCodeList *at,
                         const char *l1, const char *l2) {
    const char *seen[GOTO_CHAIN_MAX];
    const char *cur = l2;
    int n = 0, steps = 0;

    if (!l1 || !l2 || strcmp(l1, l2) == 0) return 0;
    while (steps++ < GOTO_CHAIN_MAX) {
        InterCodeList *lab;
        InterCode *nextc;
        int i;

        for (i = 0; i < n; i++)
            if (strcmp(seen[i], cur) == 0) return 0;   /* 重复标签 ⇒ 环 */
        if (strcmp(cur, l1) == 0) return 0;            /* 回到源标号 ⇒ 环 */
        seen[n++] = cur;

        lab = findLabelInFunc(hh, at, cur);
        if (!lab || lab->next == hh || !lab->next->code) return 1;
        nextc = lab->next->code;
        if (nextc->kind != IR_GOTO || !nextc->u.one.x ||
            nextc->u.one.x->kind != OP_LABEL || !nextc->u.one.x->name)
            return 1;                                  /* 链在此终止 */
        cur = nextc->u.one.x->name;
    }
    return 0;                                          /* 太长：保守跳过 */
}

/* O7 + O8：处理一个就从头重扫（deleteCode/改写后迭代器失效）。 */
static int passGotoSimplify(InterCodeList *h) {
    InterCodeList *hh = irHead(h), *p;
    int changed = 0;

    if (!hh) return 0;
    for (;;) {
        int did = 0;
        for (p = hh->next; p != hh; p = p->next) {
            InterCode *c = p->code;
            Operand tgt;
            InterCodeList *q;

            if (!c || c->kind != IR_GOTO) continue;
            tgt = c->u.one.x;
            if (!tgt || tgt->kind != OP_LABEL || !tgt->name) continue;
            q = p->next;

            /* O7：下一条指令就是 `LABEL L :` ⇒ 删 GOTO（动态收益 0，见上）。 */
            if (q != hh && q->code && q->code->kind == IR_LABEL &&
                labelIsNamed(q->code->u.one.x, tgt->name)) {
                deleteCode(p);
                changed++;
                noteRewrite("O7/O8 GOTO 精简", changed);
                did = 1;
                break;
            }

            /* O8：`LABEL L1 :` 的下一条是 `GOTO L2` ⇒ GOTO L1 改成 GOTO L2。
             * L1/L2 都必须在同一函数内有定义；防环后再改。 */
            {
                InterCodeList *l1node = findLabelInFunc(hh, p, tgt->name);
                InterCodeList *g;
                InterCode *gc;
                Operand l2op;

                if (!l1node) continue;
                g = l1node->next;
                if (g == hh || !g->code || g->code->kind != IR_GOTO) continue;
                gc = g->code;
                l2op = gc->u.one.x;
                if (!l2op || l2op->kind != OP_LABEL || !l2op->name) continue;
                if (!findLabelInFunc(hh, p, l2op->name)) continue;
                if (!gotoChainSafe(hh, p, tgt->name, l2op->name)) continue;

                /* 复用 GOTO L2 的 OP_LABEL 操作数（操作数共享是允许的）。 */
                c->u.one.x = l2op;
                changed++;
                noteRewrite("O7/O8 GOTO 精简", changed);
                did = 1;
                break;
            }
        }
        if (!did) break;
    }
    return changed;
}

/* 关系取反表：`<`↔`>=`、`<=`↔`>`、`==`↔`!=`。
 * 编码见 ir.h：0=<, 1=<=, 2=>, 3=>=, 4===, 5=!=。 */
static int invertRelop(int code) {
    switch (code) {
    case 0: return 3;   /* <  ⇒ >= */
    case 1: return 2;   /* <= ⇒ >  */
    case 2: return 1;   /* >  ⇒ <= */
    case 3: return 0;   /* >= ⇒ <  */
    case 4: return 5;   /* == ⇒ != */
    case 5: return 4;   /* != ⇒ == */
    default:
        fprintf(stderr, "internal error: bad relop code %d in O9\n", code);
        abort();
    }
}

/* O9：条件反转 + fall-through（唯一减少动态条数的一条；假分支省 1 条）。 */
static int passCondInvert(InterCodeList *h) {
    InterCodeList *hh = irHead(h), *p;
    int changed = 0;

    if (!hh) return 0;
    for (;;) {
        int did = 0;
        for (p = hh->next; p != hh; p = p->next) {
            InterCode *c = p->code;
            InterCodeList *g, *l1node, *l2node;
            InterCode *gc, *lc;
            Operand l1op, l2op;

            if (!c || c->kind != IR_IF) continue;
            g = p->next;
            if (g == hh || !g->code || g->code->kind != IR_GOTO) continue;
            l1node = g->next;
            if (l1node == hh || !l1node->code ||
                l1node->code->kind != IR_LABEL) continue;

            gc = g->code;
            lc = l1node->code;
            l1op = lc->u.one.x;
            l2op = gc->u.one.x;
            if (!l1op || l1op->kind != OP_LABEL || !l1op->name) continue;
            if (!l2op || l2op->kind != OP_LABEL || !l2op->name) continue;
            if (strcmp(l1op->name, l2op->name) == 0) continue;

            /* ★ 前置条件：IF 自己的真分支目标必须就是紧跟在 GOTO 后面的 L1。
             * 只看“GOTO 后面那个 LABEL 叫 L1”是不够的：合法 IR 里 IF 可以跳去
             * 别的标号（如 l9），而恰好有一个同名 l1 的 LABEL 在这里、且只被别的
             * GOTO 引用一次。缺了这道校验，O9 会把 IF 的目标改成 L2、删掉
             * `GOTO L2` 与 `LABEL L1`，留下指向已删标号的悬空 GOTO（Task 19
             * 审查 Critical 1 的反例，见 test_opt.c 的 O9 目标不匹配用例）。 */
            if (!labelIsNamed(c->u.iff.z, l1op->name)) continue;

            /* L1 只能被这一条 IF 引用；否则删掉 LABEL 会改变别的跳转。 */
            if (labelRefCount(hh, l1op->name) != 1) continue;

            /* L2 的 LABEL 必须在本函数内、L1 之后；找不到则模式不成立。 */
            l2node = findLabelAfterInFunc(hh, l1node, l2op->name);
            if (!l2node) continue;

            /* IF 翻转并改指 L2；再删 `GOTO L2` 与 `LABEL L1 :`。 */
            c->u.iff.relop = invertRelop(c->u.iff.relop);
            c->u.iff.z = l2op;          /* 复用 OP_LABEL; 名称即 L2 */
            deleteCode(g);              /* 先删中间那条 GOTO L2 */
            deleteCode(l1node);         /* 再删 LABEL L1 */
            changed++;
            noteRewrite("O9 条件反转", changed);
            did = 1;
            break;                      /* 链表已变，从头重扫 */
        }
        if (!did) break;
    }
    return changed;
}

/* O6：无引用标号删除。**最后跑**（O7/O9 会制造出新的无引用标号）。
 * 删除后从表头重扫；count 在每次删除后重算，故不需要维护引用表。 */
static int passDeadLabel(InterCodeList *h) {
    InterCodeList *hh = irHead(h), *p;
    int changed = 0;

    if (!hh) return 0;
    for (;;) {
        int did = 0;
        for (p = hh->next; p != hh; p = p->next) {
            InterCode *c = p->code;
            Operand lab;

            if (!c || c->kind != IR_LABEL) continue;
            lab = c->u.one.x;
            if (!lab || lab->kind != OP_LABEL || !lab->name) continue;
            if (labelRefCount(hh, lab->name) != 0) continue;
            deleteCode(p);
            changed++;
            noteRewrite("O6 死标号删除", changed);
            did = 1;
            break;
        }
        if (!did) break;
    }
    return changed;
}

static int passesChanged;

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
