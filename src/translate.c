/* AST -> IR。本文件当前含**形状判别表**、**表达式翻译**与 translateProgram 的骨架；
 * 语句/函数/程序级的翻译函数在后续任务里逐条加上来。
 *
 * 判别表放在这里而不是散在各翻译函数里：它是整层唯一"读懂语法树结构"的地方，
 * 判错时错的是整棵树的语义，且生成的 IR 往往"看着挺像那么回事"——
 * 端到端（parser + 真机）测不出来。集中一处 + Test/unit/test_expkind.c 才钉得住。
 *
 * ── 表达式翻译规则表（Task 8 Step 1；下面每条都在本文件里有对应的实现分支）──
 *
 *   INT              -> place := #v
 *   ID               -> place := 该变量
 *   Exp ASSIGNOP     -> 直接写进目标变量，不经过临时变量（见 translateAssign）
 *   Exp1 op Exp2     -> t1 := Exp1; t2 := Exp2; place := t1 op t2   （op 是 + - * /）
 *   -Exp1            -> t1 := Exp1; place := #0 - t1
 *   (Exp1)           -> translateExp(Exp1, place)      （括号不产生任何 IR）
 *   Exp 关系/与/或   -> 走条件翻译 translateCondAsValue（Task 9）
 *   !Exp             -> translateNotAsValue（Task 9）
 *   read()           -> translateCall0（Task 10）
 *   f()              -> translateCall0（Task 10）
 *   f(Args)          -> translateCallN（Task 10）
 *   write(Exp)       -> translateCallN（Task 10）
 *   Exp1[Exp2]       -> translateIndex（Task 11）
 *   Exp1.ID          -> translateField（选做 3.1，本实验不实现）
 *
 * 表中出现却尚未实现的那些函数，本文件一律落**响亮失败的桩**（见下面的桩一栏）：
 * 构建能链接，任何过早走到那些路径的情况都会当场 abort，而不是静默产出一份
 * "看着挺像那么回事"的错 IR。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "translate.h"
#include "ir.h"
#include "symtab.h"
#include "type.h"
#include "util.h"

/* 该孩子是不是名字为 name 的**终结符**结点。NULL 一律算"不是"：
 * 空产生式（DefList/StmtList/OptTag/ExtDefList 的 ε）与 `error` 的语义值
 * 都可能让孩子为空指针，判别函数不该为此崩溃。 */
static int isTok(Node *n, const char *name) {
    return n && n->kind == NODE_TOKEN && strcmp(n->name, name) == 0;
}

ExpKind classifyExp(Node *e) {
    if (e->nchild == 1) {
        if (isTok(e->child[0], "INT"))   return EX_INT;
        if (isTok(e->child[0], "ID"))    return EX_ID;
        /* ★ 剩下的 `Exp : FLOAT`（src/syntax.y）会落到这里：它与 `Exp : INT` **形状
         * 完全相同**（nchild==1、child[0] 都是终结符），只有 token 名字能区分。
         * 要求书假设 1 保证实验三输入**不含浮点**，故正常判分路径上这条不可达；
         * 但**不可达不等于可以静默** —— 若在此兜底 `return EX_INT`，一个浮点字面量
         * 会被当成整数，下游拿它的 lexeme（"1.5"）当整数用，生成一份"看着挺像那么回事"
         * 的错 IR，端到端（parser + 真机）测不出来。故与全表其它未识别分支一致：
         * **响亮失败**（诊断 + abort），绝不猜一个类型。 */
        goto bad;
    }
    if (e->nchild == 2) return EX_UNARY;  /* MINUS Exp / NOT Exp */
    if (e->nchild == 3) {
        if (isTok(e->child[0], "LP")) return EX_PAREN;
        if (isTok(e->child[0], "ID") && isTok(e->child[1], "LP")) return EX_CALL0;
        /* ★ 本行的顺序不是随意的：`Exp DOT ID` 与二元运算**形状完全相同**
         * （nchild==3、child[1] 都是终结符），唯一的差别就是 child[1] 是不是 DOT。
         * 故 DOT 必须先于兜底的 EX_BINOP 判掉 —— 删掉本行，`a.x` 会被当成二元运算。 */
        if (isTok(e->child[1], "DOT")) return EX_FIELD;
        return EX_BINOP;
    }
    if (e->nchild == 4) {
        if (isTok(e->child[0], "ID")) return EX_CALLN;   /* ID LP Args RP */
        if (isTok(e->child[1], "LB")) return EX_INDEX;   /* Exp LB Exp RB */
    }
bad:
    /* 诊断带上 nchild、行号与**实际的孩子 token 名**：看日志的人一眼就知道是 FLOAT
     * 还是别的没见过的形状。(none) 兜底只为"打印诊断时别再崩一次"，不是在给未知形状
     * 找台阶下 —— 紧随其后的 abort() 才是这些分支的收场。 */
    fprintf(stderr, "internal error: unclassified Exp (nchild=%d) at line %d, child[0]=%s\n",
            e->nchild, nodeLine(e),
            (e->nchild >= 1 && e->child[0] && e->child[0]->name) ? e->child[0]->name : "(none)");
    abort();
}

StmtKind classifyStmt(Node *s) {
    if (s->nchild == 1) {
        /* 同族其它分支都先判 NULL 再判类型（见下面 nchild==2 那处）。`Stmt : CompSt`
         * 的孩子不会是 NULL，但 `error` 伪符号的语义值可能为 NULL —— 直接解引用会段错误，
         * 而全表的失败形态必须统一是**响亮的 abort**，不是随机崩溃。 */
        if (s->child[0] == NULL || s->child[0]->type != NT_COMPST) goto bad;
        return ST_COMPST;
    }
    if (s->nchild == 2) {
        /* `Stmt : Exp SEMI` 与 `Stmt : error SEMI` 同形。后者的 $1 是 error 伪符号，
         * 语义值不确定（可能是 NULL，也可能是栈上的残留）。要求书假设 7 保证输入
         * 无语法错误，故正常路径上不该出现；真出现了就**响亮失败**，
         * 绝不静默把它当表达式语句翻译下去。 */
        if (s->child[0] == NULL || s->child[0]->kind != NODE_GRAMMAR) goto bad;
        return ST_EXP;
    }
    if (s->nchild == 3) {
        if (isTok(s->child[0], "RETURN")) return ST_RETURN;
        goto bad;
    }
    if (s->nchild == 5) {
        /* ★ `IF LP Exp RP Stmt` 与 `WHILE LP Exp RP Stmt` **孩子数相同**（都是 5），
         * 只能靠 child[0] 的终结符名字区分。删掉下面 WHILE 那行，while 会被译成 if。 */
        if (isTok(s->child[0], "IF"))    return ST_IF;
        if (isTok(s->child[0], "WHILE")) return ST_WHILE;
        goto bad;
    }
    if (s->nchild == 7) return ST_IFELSE;
bad:
    fprintf(stderr, "internal error: unclassified Stmt (nchild=%d) at line %d\n",
            s->nchild, nodeLine(s));
    abort();
}

/* ══ 名字发生器（Task 8 Step 3）══════════════════════════════════════════════
 *
 * 变量与临时变量**共用编号空间**：`varNameOf` 产出 "v1","v2"…，`newTempName` 产出
 * "t1","t2"…。t/v 两个前缀不会撞，但共用计数器让"哪个名字是第几个出现的"始终单调，
 * 便于人读产物、也便于单测写期望（编号与登记顺序一一对应）。
 *
 * `varNameOf` 的表是**平的**：要求书假设 4 保证所有变量不重名（同名一律已被语义
 * 分析报错拦下），故不需要按作用域分层的映射 —— 一张源码名 -> IR 名的对照表就够。
 * 表项里的字符串一次 xstrdup 后就一直有效（返回给调用方长期持有，opVar 还会再
 * 复制一份），**绝不留任何指向栈缓冲区的指针**。 */
static int tmpSeq = 0, labelSeq = 0, varSeq = 0;
static struct { const char *src; const char *ir; } varMap[1024];
static int varMapN = 0;

char *newTempName(void) {
    char buf[32];
    snprintf(buf, sizeof buf, "t%d", ++tmpSeq);
    return xstrdup(buf);
}

char *newLabelName(void) {
    char buf[32];
    snprintf(buf, sizeof buf, "label%d", ++labelSeq);
    return xstrdup(buf);
}

char *varNameOf(const char *src) {
    int i;
    for (i = 0; i < varMapN; i++)
        if (strcmp(varMap[i].src, src) == 0) return (char *)varMap[i].ir;
    { char buf[32]; snprintf(buf, sizeof buf, "v%d", ++varSeq);
      varMap[varMapN].src = xstrdup(src); varMap[varMapN].ir = xstrdup(buf); varMapN++; }
    return (char *)varMap[varMapN - 1].ir;
}

void translateResetNames(void) { tmpSeq = labelSeq = varSeq = varMapN = 0; }

/* ══ 属于后续任务的函数：本任务只落**响亮失败的桩** ══════════════════════════
 *
 * translateExp 的形状要求它调这些函数，但它们的实现分别在 Task 9/10/11。
 * 桩不能"随便兜一个实现"：那会产出一份看着挺像那么回事的错 IR，端到端测不出来。
 * 故一律**诊断 + abort**（与本文件 classifyExp/classifyStmt 的失败形态一致）。
 * 每个桩在 Task N 落地时，把它替换成真实现即可。 */
static void notImplemented(const char *fn, int task) {
    fprintf(stderr, "internal error: %s 属于 Task %d，尚未实现\n", fn, task);
    abort();
}

/* Task 9：关系运算 / 与 / 或 —— 用短路求值算出一个 0/1 的值。 */
static InterCodeList *translateCondAsValue(InterCodeList *tail, Node *e, Operand place) {
    (void)tail; (void)e; (void)place;
    notImplemented("translateCondAsValue", 9);
    return NULL;
}

/* Task 9：逻辑非 —— 算出一个 0/1 的值。 */
static InterCodeList *translateNotAsValue(InterCodeList *tail, Node *e, Operand place) {
    (void)tail; (void)e; (void)place;
    notImplemented("translateNotAsValue", 9);
    return NULL;
}

/* Task 10：`f()` / `read()`。 */
static InterCodeList *translateCall0(InterCodeList *tail, Node *e, Operand place) {
    (void)tail; (void)e; (void)place;
    notImplemented("translateCall0", 10);
    return NULL;
}

/* Task 10：`f(Args)` / `write(Exp)`。 */
static InterCodeList *translateCallN(InterCodeList *tail, Node *e, Operand place) {
    (void)tail; (void)e; (void)place;
    notImplemented("translateCallN", 10);
    return NULL;
}

/* Task 11：数组下标。placeIsAddr 那条出参正是为它的左值形态预留的。 */
static InterCodeList *translateIndex(InterCodeList *tail, Node *e, Operand place, int *placeIsAddr) {
    (void)tail; (void)e; (void)place; (void)placeIsAddr;
    notImplemented("translateIndex", 11);
    return NULL;
}

/* 选做 3.1：结构体域 `Exp.ID`。**本实验不实现**，故它永远走桩。
 * 注意它只被 `Exp.ID` 这种**右值/左值表达式**触发（声明与定义里的 `.` 是另一条
 * 产生式，不经过这里）—— 故而"不实现"的代价是：输入里出现 `a.x` 会当场 abort，
 * 而不是静默产出错 IR。项目范围由 task-8-brief.md 钉死。 */
static InterCodeList *translateField(InterCodeList *tail, Node *e, Operand place) {
    (void)tail; (void)e; (void)place;
    fprintf(stderr, "internal error: translateField 属于选做 3.1，本实验不实现\n");
    abort();
}

/* Task 11：非 ID 左值的赋值（`a[i] = ...`）。 */
static InterCodeList *translateAssignToLValue(InterCodeList *tail, Node *e, Operand place) {
    (void)tail; (void)e; (void)place;
    notImplemented("translateAssignToLValue", 11);
    return NULL;
}

/* ══ 表达式翻译（Task 8 Step 2）═════════════════════════════════════════════
 * 规则表见文件顶部；每条规则与下面一个 case 一一对应。 */

static InterCodeList *translateBinArith(InterCodeList *tail, Node *e, Operand place, const char *op) {
    int kind = 0;
    Operand t1, t2;
    InterCode *c;

    if      (strcmp(op, "PLUS")  == 0) kind = IR_ADD;
    else if (strcmp(op, "MINUS") == 0) kind = IR_SUB;
    else if (strcmp(op, "STAR")  == 0) kind = IR_MUL;
    else if (strcmp(op, "DIV")   == 0) kind = IR_DIV;
    else {
        /* RELOP / AND / OR 走条件翻译，见 Task 9 */
        return translateCondAsValue(tail, e, place);
    }

    /* ★ place 为 NULL（值不被需要）时**照样翻译两侧**：它们里面可能有函数调用。
     * 只是最后那条运算不用生成了 —— 但它要的临时变量照领，
     * 于是"有 place"与"没 place"两条路径只差最后一行 IR。 */
    t1 = opVar(newTempName());
    tail = translateExp(tail, e->child[0], t1, NULL);
    t2 = opVar(newTempName());
    tail = translateExp(tail, e->child[2], t2, NULL);
    if (!place) return tail;

    c = newCode(kind);
    c->u.binop.x = place; c->u.binop.y = t1; c->u.binop.z = t2;
    return irAppend(tail, c);
}

/* 赋值 —— **本设计的核心**。
 *
 * `x = <expr>;` 的朴素写法是"先把 <expr> 算进临时变量 t，再 `x := t`"，
 * 两条 IR 且多一个临时变量。这里改成**把 x 自己当 place 传给右子表达式**：
 * 右子表达式（以及它递归下去的所有子表达式）直接把结果算进 x。`x = 5;` 于是
 * 只剩一条 `v1 := #5` —— 与要求书 4.1.6 样例 1 的"优化后版本"逐字一致。
 *
 * 这不是"少一条指令"的小优化：`place` 是**继承属性**，这条协议沿着整棵表达式树
 * 传下去，凡"值最终要落到某个变量"的中间结果都少一条搬运指令。
 * 实测（Test/unit/test_translate.c 的 5 条用例，把本函数退回朴素写法后逐条对比）：
 *   x = 5;       1 行 -> 2 行      y = (x = 5);  2 行 -> 4 行
 *   x = y;       1 行 -> 2 行      x = 1 + 2;    3 行 -> 4 行
 *   x = -y;      2 行 -> 3 行
 * 越靠近树根赋值、嵌套越深，省下的搬运越多。
 *
 * 赋值**表达式**的值（`y = x = 5;` 里的内层）还要用，故 place 非 NULL 时补一条
 * `place := dst` —— 那一行是语义需要的，不是浪费：内层既要写 x，又要把 x 的值交给 y。 */
static InterCodeList *translateAssign(InterCodeList *tail, Node *e, Operand place) {
    Node *lhs = e->child[0];
    Operand dst;
    InterCode *c;

    if (classifyExp(lhs) != EX_ID) {
        /* 数组元素左值由 Task 11 处理；结构体域不实现 */
        return translateAssignToLValue(tail, e, place);
    }
    dst = opVar(varNameOf(lhs->child[0]->lexeme));
    tail = translateExp(tail, e->child[2], dst, NULL);   /* ★ 直接算进目标变量 */
    if (place) {                                         /* 赋值表达式的值还要用 */
        c = newCode(IR_ASSIGN);
        c->u.assign.x = place; c->u.assign.y = dst;
        tail = irAppend(tail, c);
    }
    return tail;
}

InterCodeList *translateExp(InterCodeList *tail, Node *e, Operand place, int *placeIsAddr) {
    if (placeIsAddr) *placeIsAddr = 0;
    switch (classifyExp(e)) {
    case EX_INT:
        if (place) { InterCode *c = newCode(IR_ASSIGN);
                     c->u.assign.x = place;
                     c->u.assign.y = opConst(atoi(e->child[0]->lexeme));
                     tail = irAppend(tail, c); }
        return tail;

    case EX_ID:
        if (place) { InterCode *c = newCode(IR_ASSIGN);
                     c->u.assign.x = place;
                     c->u.assign.y = opVar(varNameOf(e->child[0]->lexeme));
                     tail = irAppend(tail, c); }
        return tail;

    case EX_BINOP: {
        const char *op = e->child[1]->name;
        if (strcmp(op, "ASSIGNOP") == 0) return translateAssign(tail, e, place);
        return translateBinArith(tail, e, place, op);
    }

    case EX_UNARY:
        if (isTok(e->child[0], "NOT")) return translateNotAsValue(tail, e, place);
        /* 取负：`place := #0 - t1`。**用 IR_SUB 而不是另设一元 NEG** —— 参考机没有
         * 取负指令，而 `#0 - x` 与它逐位等价（且 `#0 - #0 == 0` 的边界也对）。 */
        if (place) {
            Operand t1 = opVar(newTempName());
            InterCode *c;
            tail = translateExp(tail, e->child[1], t1, NULL);
            c = newCode(IR_SUB);
            c->u.binop.x = place; c->u.binop.y = opConst(0); c->u.binop.z = t1;
            tail = irAppend(tail, c);
        } else {
            tail = translateExp(tail, e->child[1], NULL, NULL);
        }
        return tail;

    case EX_PAREN:
        /* 括号不产生任何 IR：把 place 一路传下去（连 placeIsAddr 一起，故 `(a[i])`
         * 在 Task 11 落地后仍能正确报"这是个地址"。） */
        return translateExp(tail, e->child[1], place, placeIsAddr);

    case EX_CALL0:
        return translateCall0(tail, e, place);                  /* Task 10 */

    case EX_CALLN:
        return translateCallN(tail, e, place);                  /* Task 10 */

    case EX_INDEX:
        return translateIndex(tail, e, place, placeIsAddr);     /* Task 11 */

    case EX_FIELD:
        return translateField(tail, e, place);                  /* 选做 3.1，不实现 */
    }
    return tail;
}

/* 骨架：翻译层的入口。修完后续任务后，这里会遍历 Program -> ExtDefList 生成 IR。
 * 现在返回空表（哨兵）而不是 NULL —— 下游一律按"带哨兵的循环链表"处理，
 * NULL 会让 irAppend/printIR 在多处需要特判。 */
InterCodeList *translateProgram(Node *root) {
    (void)root;
    return irList();
}
