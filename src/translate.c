/* AST -> IR。本文件当前含**形状判别表**、**表达式翻译**、**条件翻译**、
 * **语句/复合语句/函数定义翻译**（Task 10）、**调用 / read·write / 数组下标**
 * （Task 11 一维；Task 13 高维 + 地址一次线性化）；translateProgram 已是真实现
 * （遍历 Program -> ExtDefList），只差 Task 12 把 src/main.c 接上来。
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
 *   Exp 关系/与/或   -> translateCondAsValue：`[place := #0]` + 条件翻译 + `place := #1`
 *   !Exp             -> translateNotAsValue（同上；NOT 的特别之处在 translateCond 的
 *                       目标互换那一行，两条路径共用同一个实现）
 *   条件本体         -> translateCond(tail, e, label_true, label_false)：**短路**，
 *                       两个跳转目标是**继承属性**（函数参数），不做回填（Task 9）
 *   read()           -> translateCall0（Task 11）
 *   f()              -> translateCall0（Task 11）
 *   f(Args)          -> translateCallN（Task 11）
 *   write(Exp)       -> translateCallN（Task 11）
 *   Exp1[Exp2]       -> translateIndex（Task 11；Task 13 起任意维数）
 *   Exp1.ID          -> translateField（选做 3.1，本实验不实现）
 *
 * 表中**尚未实现**的只剩 `translateField`（选做 3.1）—— 它一律落**响亮失败的桩**：
 * 构建能链接，任何过早走到那条路径的情况都会当场 abort，而不是静默产出一份
 * "看着挺像那么回事"的错 IR。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "translate.h"
#include "ir.h"
#include "symtab.h"
#include "type.h"
#include "util.h"

/* 选做 3.2 的编译期开关：1 = 实现（默认），0 = 仅必做构建（遇到高维数组/数组形参
 * 只打提示、不产 IR）。Makefile 恒定义它；这里的兜底只为独立编译时不至于静默切到
 * "仅必做"语义。取值只允许 0/1，写错在编译期响亮失败。 */
#ifndef LAB3_ENABLE_32
#define LAB3_ENABLE_32 1
#endif
#if LAB3_ENABLE_32 != 0 && LAB3_ENABLE_32 != 1
#error "LAB3_ENABLE_32 must be 0 or 1"
#endif

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
 * 变量与临时变量**共用编号空间**（前缀 `t`/`v` 区分，计数器各自独立）：`varNameOf`
 * 产出 "v1","v2"…，`newTempName` 产出 "t1","t2"…。两个前缀不会撞；各自的计数器让
 * "哪个名字是第几个出现的"始终单调，便于人读产物、也便于单测写期望（编号与登记
 * 顺序一一对应）——注意**不是**共用同一个计数器：生成一个临时变量不会消耗一个变量号。
 * 与 src/translate.h 中那句说明对齐。
 *
 * `varNameOf` 的表**按作用域深度分层**：语义层允许内层块用同名变量遮蔽外层
 * （`{ int x; }` 套在 `int x` 里），故表项记 `depth`，查找从表尾向前取**最内层可见**
 * 的那一条。同名只有在**同一 depth**重复登记时才是错误（语义闸漏了重复定义）；
 * 不同 depth 的同名是合法的遮蔽。`enterScope`/`leaveScope` 负责压/弹当前深度，
 * `leaveScope` 把该深度的条目从表尾删掉（含 xstrdup 的名字）。
 * 要求书假设 4（无全局变量）仍成立：depth 0 只在程序级，正常输入不会登记任何名字。
 * 表项里的字符串一次 xstrdup 后就一直有效（返回给调用方长期持有，opVar 还会再
 * 复制一份），**绝不留任何指向栈缓冲区的指针**。 */
static int tmpSeq = 0, labelSeq = 0, varSeq = 0;
typedef struct { const char *src; const char *ir; int depth; } VarMapEntry;
static VarMapEntry varMap[1024];
static int varMapN = 0;

/* 当前作用域深度：0 = 程序级；每个函数体 / 嵌套块各进一层。 */
static int curScope = 0;
/* 嵌套深度上限。超出即响亮 abort（同 varMap 的容量纪律）。 */
#define MAX_SCOPE_DEPTH 64

/* 进入一个新作用域。 */
static void enterScope(void) {
    curScope++;
    if (curScope > MAX_SCOPE_DEPTH) {
        fprintf(stderr, "internal error: 作用域嵌套超过 %d 层（curScope 定长 Depth）\n",
                MAX_SCOPE_DEPTH);
        abort();
    }
}

/* ══ 用户函数名保留表（最终整分支审查 Important 2）══════════════════════════
 *
 * 参考机的 labelTable **同时**登记 `LABEL` 与 `FUNCTION`（变量在另一张表），
 * 故用户函数 `label1` 与生成标号 `label1` 会在装载期撞成 DuplicatedLabelError。
 * 我们自己的编译器此前照常退出 0 —— 判分机的参考机却拒绝执行，属**静默产出
 * 不可执行 IR**。修法：翻译前把全部用户函数名登记为保留名，`newLabelName`
 * 生成候选后跳过它们（见下方）。
 *
 * 只保留**函数名**；变量名与标号不同表，不需要保留。表定长，溢出必须响亮
 * abort（同 varMap/varTypes 的纪律：静默越界写是本仓库最怕的失效）。 */
static char *reservedFuncNames[1024];
static int reservedFuncNamesN = 0;

static int isReservedFuncName(const char *name) {
    int i;
    for (i = 0; i < reservedFuncNamesN; i++)
        if (strcmp(reservedFuncNames[i], name) == 0) return 1;
    return 0;
}

/* ══ 源码名 -> 类型登记表（Task 13）══════════════════════════════════════════
 *
 * 为什么需要它：`arrayElemAddr` 要按**元素类型的字节数**求每一维的步长，而语义层
 * 在 `semanticAnalyze` 返回前已经把函数作用域**弹出**了 —— 翻译时 `symLookup` 看不到
 * 局部变量（更不消说函数形参）。要求书假设 4 保证无全局变量，故一张**翻译层
 * 自己的名字 -> 类型表**就足够且安全（作用域遮蔽见下）。该表与 varMap 一样按
 * `depth` 分层：嵌套块允许同名遮蔽，查找取**最内层可见**的那一条（见 varMap 处）。
 *
 * 只在 `translateVarDec`（局部变量）与 `translateVarListAsParams`（形参）里登记；
 * `translateResetNames` 一并清空。名字用 xstrdup 复制（AST 的 lexeme 虽然活到进程结束，
 * 但沿 varMap 的既有做法走，不依赖调用方保活）。
 *
 * `isArrayParam` 承载"形参本身即基地址"这条契约（Task 14，见 P5）：数组形参在
 * 真机上收到的**就是**基地址，因此
 *   · 体内下标：`arrayElemAddr` 以 `opVar(名字)` 作基地址，**不再取 `&`**；
 *   · 再作实参传递：`translateArgs` 直接 `opVar(名字)`，也不加 `&`。
 * 局部数组则相反：两处都用 `opAddr(名字)`（渲染成 `&vN`）。两条路径的区分点
 * **只此一处**（本标记），绝不靠调用点另猜。
 *
 * 表定长，写入前必须查容量（同 varMap 的纪律：溢出是**静默越界写**，不是响亮失败）。
 * **同一作用域内**重复登记同样响亮 abort：语义闸本应拦下；真发生了说明翻译层把
 * 同一个声明翻了两遍或语义闸漏了重复定义，两种都必须当场暴露，不能被后一次静默
 * 覆盖。**不同 depth 的同名是合法的遮蔽**（内层块 `int x` 套外层 `int x`），不 abort。 */
typedef struct {
    const char *name;
    Type        type;
    int         isArrayParam;
    int         depth;          /* 登记时的作用域深度（见 varMap 处的说明） */
} VarTypeEntry;

static VarTypeEntry varTypes[1024];
static int varTypesN = 0;

/* 离开当前作用域：把该深度的条目从两张表的表尾删掉，再退回上一层。
 *
 * 当前深度的条目一定连续地排在表尾：更深层的条目在 leaveScope 时已被删掉，
 * 而本层后续登记的新条目都追加在它们之后。故只需从表尾向前扫描。 */
static void leaveScope(void) {
    while (varMapN > 0 && varMap[varMapN - 1].depth == curScope) {
        varMapN--;
        free((void *)varMap[varMapN].src);
        free((void *)varMap[varMapN].ir);
    }
    while (varTypesN > 0 && varTypes[varTypesN - 1].depth == curScope) {
        varTypesN--;
        free((void *)varTypes[varTypesN].name);   /* Type 对象不归本文件所有 */
    }
    if (curScope == 0) {
        fprintf(stderr, "internal error: leaveScope 在程序级（curScope == 0）被调用\n");
        abort();
    }
    curScope--;
}

char *newTempName(void) {
    char buf[32];
    snprintf(buf, sizeof buf, "t%d", ++tmpSeq);
    return xstrdup(buf);
}

char *newLabelName(void) {
    char buf[32];
    /* 跳过用户函数名：参考机的 LABEL/FUNCTION 共用一张 labelTable，同名即
     * DuplicatedLabelError。循环只会因保留名连续命中而多转几圈，编号仍单调。 */
    do {
        snprintf(buf, sizeof buf, "label%d", ++labelSeq);
    } while (isReservedFuncName(buf));
    return xstrdup(buf);
}

/* 在当前作用域新增一条"源码名 -> 新 vN"登记（不查重；查重由 registerVarType 负责）。 */
static const char *addVarName(const char *src) {
    char buf[32];
    /* ★ 定长表，**写入前必须查容量**：溢出会**静默越界写** —— 没有诊断、没有 abort，
     * 还会踩到同 TU 里的其它静态变量（tmpSeq/varSeq 就在旁边），产出一份"看着挺像
     * 那么回事"的错 IR。这与本仓库"响亮失败"的纪律正好相反。课程用例远到不了 1024 个
     * 变量名，但低概率不是"可以静默"的理由，故超限即响亮 abort。 */
    if (varMapN >= (int)(sizeof varMap / sizeof varMap[0])) {
        fprintf(stderr, "internal error: varMap 溢出（>1024 个变量名）\n");
        abort();
    }
    snprintf(buf, sizeof buf, "v%d", ++varSeq);
    varMap[varMapN].src = xstrdup(src);
    varMap[varMapN].ir = xstrdup(buf);
    varMap[varMapN].depth = curScope;
    varMapN++;
    return varMap[varMapN - 1].ir;
}

const char *varNameOf(const char *src) {
    int i;
    /* 从表尾向前找：同名的多条里，最后登记的那条就是**最内层可见**的（遮蔽）。
     * depth <= curScope 是安全网 —— leaveScope 保证表里不会有更深的残留。
     * 找不到才新增（正常输入里名字都已由声明登记；这条兜底只为不静默返回 NULL）。 */
    for (i = varMapN - 1; i >= 0; i--)
        if (varMap[i].depth <= curScope && strcmp(varMap[i].src, src) == 0)
            return varMap[i].ir;
    return addVarName(src);
}

/* **声明**专用的名字登记：总是为当前作用域新增一条，即使外层已有同名条目。
 * 这正是"嵌套块同名遮蔽"与"使用外层变量"的分界点：
 *   · 声明（`int x`）→ 内层必须拿到**新的 vN**（本函数）；
 *   · 使用（`x = 1`）→ 由 varNameOf 取最内层可见条目。
 * 若声明也走 varNameOf，`{ int x; }` 会命中外层条目、复用外层 vN —— 内层赋值
 * 直接改写外层变量（nested-shadow-test 的实测失败形态：输出 2,2 而非 2,1）。 */
static const char *declareVarName(const char *src) {
    return addVarName(src);
}

/* 清空**所有**变量名/类型映射，但**不**重置 varSeq/tmpSeq/labelSeq。
 *
 * 函数级与块级的隔离现在由 enterScope/leaveScope 完成（每个函数体一层、每个
 * 嵌套块再一层，不同函数/不同块的同名变量自然各用各的 `vN`）；本函数只在
 * translateResetNames（单测复用进程）与整体重置时清空全部条目。
 * 计数器**不**清：编号继续单调增长，唯一命名程序的 varNameOf 调用顺序未变，
 * IR 逐字节不变。
 *
 * 表里 xstrdup 出来的字符串一并 free。释放是安全的：调用方（opVar）拿到名字时
 * 会**自己再复制一份**放进 IR，产物里的操作数不指向这里的字符串。 */
static void clearFunctionVarMaps(void) {
    int i;
    for (i = 0; i < varMapN; i++) {
        free((void *)varMap[i].src);
        free((void *)varMap[i].ir);
    }
    varMapN = 0;
    /* 类型登记表一并清空（同 varMap）。**只 free 名字**；Type 对象是 type.c 用
     * xmalloc 建的一次性结构，本文件不拥有、也不在别处释放 —— 与整个编译器
     * "一次进程、退出即回收"的既有口径一致，别在这里顺手加一套递归 free。 */
    for (i = 0; i < varTypesN; i++) free((void *)varTypes[i].name);
    varTypesN = 0;
}

/* 重置三个计数器与变量映射表。**只给单测用**（见 translate.h 的说明）：同一进程里
 * 连着跑多个用例时不清零的话，编号会接着上个用例往下走，期望值算不出来、也没法读。
 * 变量映射的清理复用 clearFunctionVarMaps；函数名保留表也要清 —— 名字是 xstrdup
 * 的，不 free 会泄漏并把上个用例的函数名留在表里（会让新用例的标号无谓地跳过
 * 那些名字）。 */
void translateResetNames(void) {
    int i;
    clearFunctionVarMaps();
    curScope = 0;
    for (i = 0; i < reservedFuncNamesN; i++) free(reservedFuncNames[i]);
    tmpSeq = labelSeq = varSeq = 0;
    reservedFuncNamesN = 0;
}

/* 登记一个保留的用户函数名（溢出响亮 abort）。 */
static void reserveFuncName(const char *name) {
    if (reservedFuncNamesN >= (int)(sizeof reservedFuncNames / sizeof reservedFuncNames[0])) {
        fprintf(stderr, "internal error: 函数名保留表溢出（>1024 个函数）\n");
        abort();
    }
    reservedFuncNames[reservedFuncNamesN++] = xstrdup(name);
}

/* 遍历 ExtDefList（右递归：ExtDef ExtDefList | ε），登记每个函数定义的
 * `FunDec.child[0]->lexeme`。只认 `Specifier FunDec CompSt` 这一种 ExtDef 形态，
 * 与 translateExtDef 的判别式一致 —— 全局变量/函数声明/结构体定义的名字不是
 * FUNCTION 行，不应占用标号命名空间。
 *
 * 这里对畸形形状**不** abort：translateExtDefList 随后会以 shapeError 响亮失败。
 * 预扫只负责收集，不抢它的判据。 */
static void reserveFunctionNamesInList(Node *l) {
    Node *e, *fd;
    if (l == NULL || l->nchild == 0) return;          /* ε */
    if (l->nchild != 2) return;
    e = l->child[0];
    if (e != NULL && e->nchild == 3 &&
        e->child[1] != NULL && e->child[1]->type == NT_FUNDEC &&
        e->child[2] != NULL && e->child[2]->type == NT_COMPST) {
        fd = e->child[1];
        if (fd->child[0] != NULL && fd->child[0]->kind == NODE_TOKEN &&
            fd->child[0]->lexeme != NULL)
            reserveFuncName(fd->child[0]->lexeme);
    }
    reserveFunctionNamesInList(l->child[1]);
}

/* 从 Program 根开始预扫函数名。translateProgram 在 translateResetNames 之后、
 * 真正翻译之前调用它。 */
static void reserveFunctionNames(Node *root) {
    if (root != NULL && root->type == NT_PROGRAM && root->nchild == 1)
        reserveFunctionNamesInList(root->child[0]);
}

/* 登记一个源码名及其类型。**同作用域**重复 / 溢出都**响亮 abort**（理由见
 * varTypes 的说明）；不同 depth 的同名允许（遮蔽）。 */
static void registerVarType(const char *name, Type t, int isArrayParam) {
    int i;
    for (i = 0; i < varTypesN; i++) {
        if (varTypes[i].depth == curScope && strcmp(varTypes[i].name, name) == 0) {
            fprintf(stderr, "internal error: 变量 \"%s\" 的类型在同一作用域内被登记了两次"
                            "（语义闸是否漏了重复定义？）\n", name);
            abort();
        }
    }
    if (varTypesN >= (int)(sizeof varTypes / sizeof varTypes[0])) {
        fprintf(stderr, "internal error: varTypes 溢出（>1024 个变量名）\n");
        abort();
    }
    varTypes[varTypesN].name = xstrdup(name);
    varTypes[varTypesN].type = t;
    varTypes[varTypesN].isArrayParam = isArrayParam;
    varTypes[varTypesN].depth = curScope;
    varTypesN++;
}

/* 从表尾向前找 = 取**最内层可见**的同名条目（遮蔽）。
 * 查不到返回 NULL；调用方（arrayElemAddr）必须把 NULL 当**响亮失败**处理，
 * 不得退化成"按 4 字节一步"猜一种布局。 */
static VarTypeEntry *lookupVarType(const char *name) {
    int i;
    for (i = varTypesN - 1; i >= 0; i--)
        if (varTypes[i].depth <= curScope && strcmp(varTypes[i].name, name) == 0)
            return &varTypes[i];
    return NULL;
}

/* ══ 数组整体值守卫（最终整分支审查 Important 1）══════════════════════════
 *
 * 语义层对数组**长度不参与判等**，故 `a = b;` / `int a[2] = b;` 会被放行；
 * 翻译层若照常按 `EX_ID` 发 `v1 := v2`，参考机只改 `a[0]` —— 一份**静默算错**
 * 的 IR，端到端看起来"跑完了、有输出"，只是结果不对。数组只能通过下标访问
 * （`a[i]`）或作为实参传递（`f(a)`，由 translateArgs 单独处理），**绝不能**
 * 作为整体标量值使用。
 *
 * 这是翻译层的最后防线，不改语义层。`asLvalue` 只用于把诊断措辞分成
 * "整体值使用"与"整体赋值目标"两种；判据与处理完全相同（诊断 + abort）。
 * 查不到类型的名字不拦 —— 只有登记为 TY_ARRAY 的名字才确定是数组。 */
static void rejectArrayWholeValue(Node *e, int asLvalue) {
    VarTypeEntry *ent;
    const char *name;
    if (e == NULL || classifyExp(e) != EX_ID) return;
    name = e->child[0]->lexeme;
    ent = lookupVarType(name);
    if (ent == NULL || ent->type == NULL || ent->type->kind != TY_ARRAY) return;
    fprintf(stderr,
            "internal error: 数组 \"%s\" 不能作为整体%s"
            "（数组只能通过下标访问或作为实参传递）at line %d\n",
            name, asLvalue ? "赋值目标" : "值使用", nodeLine(e));
    abort();
}

/* ══ 条件翻译（Task 9）══════════════════════════════════════════════════════
 *
 * ★ **为什么不做回填**：指导书 4.2.6 把 label_true/label_false 举成"回填"的典型用途，
 * 那是针对把它们当**综合属性**处理的写法 —— 生成跳转时还不知道目标，于是留下一串
 * "待填链"（跳转指令上挂着未定目标），等目标标号生成后再回头改写那些指令。
 * 本设计反过来：两个目标作为**继承属性**从调用方传进来（就是函数参数），
 * 条件表达式内部任何一处要往外跳，目标此刻**已经在手上**，直接写上即可 ——
 * 不需要待填链，也不需要第二遍回扫。代价是调用方必须先备好这两个标号
 * （语句翻译里就是 `l_true`/`l_false` 那一对，见 Task 10）。
 *
 * ★ **短路是真短路，不是"先把两侧都翻译出来挑一个"**：`Exp1 AND Exp2` 产出的形状是
 *     Cond(Exp1, l1, label_false) / [LABEL l1] / Cond(Exp2, label_true, label_false)
 * —— Exp2 的整段代码排在 `LABEL l1` **之后**，而 Exp1 为假时控制流在
 * `GOTO label_false` 处就出局了，永远落不到 l1，于是 **Exp2 的代码一次都不执行**。
 * OR 对称（Exp1 为真直接跳 label_true，落不到 l1）。
 * 这不是"顺手少几条指令"：Exp2 里可能有除零、函数调用、读输入，
 * 执行与否是**可观察**的语义差别。
 * Test/lab3/basic/logic.cmm 用 `z && (b / z)`（z == 0）守着这条：若短路退化成
 * "两侧都翻译"，除零会让参考机抛 ZeroDivisionError、运行台当场报红。
 * Test/unit/test_cond.c 用例⑦在单测层逐字节钉住同一件事（除法那条 IR 必须排在
 * `LABEL l3` 之后）。
 *
 * 关系运算符的文本取自 AST 里 `RELOP` 结点的**专用字段 `opText`** —— 源码原文
 * （`< <= > >= == !=` 之一）由 `newRelop`（tree.h/tree.c）填在那里；**词法器
 * （src/lexical.l 的 emitRelop）与单测（Test/unit/test_cond.c）共用它**，
 * 故单测构造的 RELOP 结点与真产物**逐字段同形**。
 * ★ 它**不在 `lexeme`**：newRelop 把 RELOP 结点的 lexeme 恒发成 NULL，好让语法树
 *   里就是一行裸 `RELOP`（打语法树的既有回归逐字节照此钉死）；翻译层要文本，
 *   故另有专用字段承载它（见 tree.h 里 opText 的注释与下方 relopTextOf）。
 * `irRelopCode` 对表外的文本**响亮 abort**，
 * 故这里不做任何归一化、也不猜；拼错的名字会在第一次真跑到时当场暴露，
 * 而不是静默变成一个错的关系符。
 *
 * ★ **`NOT` 的翻译是"目标互换"**（真跳假、假跳真），见下面那一行递归调用。
 * 这是本函数最容易写错的地方 —— 照抄目标的顺序（不互换）产出的 IR 仍然"看着挺像
 * 那么回事"、甚至行数一模一样，只有跳转目标反了。Test/unit/test_cond.c 用例④
 * 逐字节钉住 `!(n > 0)` 必须产出 `GOTO label2`（假目标）在前。
 *
 * RELOP / AND / OR 这三条分支都**不产生临时变量**，除非某一侧的表达形状认不出
 * （既不是立即数也不是简单变量）—— 那就是 `operandForCond` 返回 NULL 的情形，
 * 此时才先把它求值到一个临时变量。`if (n > 0)` 因此只有**一条** IF：
 * 这正是设计文档 §6.1 表里"省 1 条指令"那一行、也是要求书 4.1.6 样例 1
 * "优化后版本"的同一形态。
 */

/* 追加一条 `GOTO label`。 */
static InterCodeList *appendGoto(InterCodeList *tail, const char *label) {
    InterCode *c = newCode(IR_GOTO);
    c->u.one.x = opLabel(label);
    return irAppend(tail, c);
}

/* 追加一条 `LABEL label :`。 */
static InterCodeList *appendLabel(InterCodeList *tail, const char *label) {
    InterCode *c = newCode(IR_LABEL);
    c->u.one.x = opLabel(label);
    return irAppend(tail, c);
}

/* 条件的一侧能不能**直接**当 `IF` 的操作数用。
 * 认得出的两种是立即数（`IF v1 > #0`）与简单变量；认不出的返回 NULL，
 * 调用方据 `operandIsSimple` 决定"先求值到临时变量"。
 * ★ 这里就是"`if (n > 0)` 只用一条 IF 指令"的**唯一**来处（设计文档 §6.1）。 */
static Operand operandForCond(Node *e) {
    if (classifyExp(e) == EX_INT) return opConst(atoi(e->child[0]->lexeme));
    if (classifyExp(e) == EX_ID)  {
        /* `a > b` 里的数组同样不能当标量：这条路径不经过 translateExp，
         * 故守卫必须在这里单独落一次（Brief Important 1 的"任何整体值使用"）。 */
        rejectArrayWholeValue(e, 0);
        return opVar(varNameOf(e->child[0]->lexeme));
    }
    return NULL;
}

/* NULL（= operandForCond 认不出）不算"简单操作数"。
 *
 * ★ 那个 `kind` 判断**当前走不到"假"**：`operandForCond` 只返回 NULL（已被前面的 `o &&`
 * 拦下）或 `opConst`/`opVar`（OP_CONST/OP_VAR），故这里只会见到 NULL 或这两个 kind。
 * **不要当成死代码删掉** —— 它是 `operandForCond` **契约**的一部分：日后那个函数若添了
 * 新的"简单形状"（新的 kind），这里就是**安全网** —— 忘了同步的话，一个不认识的 kind
 * 会被误当"简单操作数"**直接内联进 IF**，产出一份看着挺像那么回事的错 IR（本仓库最怕的
 * 那种失败）。保留 + 此处说明"当前契约下走不到，是为契约变更时的安全网"。 */
static int operandIsSimple(Operand o) {
    return o && (o->kind == OP_VAR || o->kind == OP_CONST);
}

/* syntax.y 里**全部**二元产生式的运算符 —— 恰好**八个**（src/syntax.y 的
 *   `Exp ASSIGNOP Exp` / `Exp AND Exp` / `Exp OR Exp` / `Exp RELOP Exp` /
 *   `Exp PLUS Exp` / `Exp MINUS Exp` / `Exp STAR Exp` / `Exp DIV Exp`
 * 这八条）。classifyExp 把它们都判成 EX_BINOP（`Exp DOT ID`、`Exp LB Exp RB` 由更前面的
 * DOT/LB 检查分别判成 EX_FIELD/EX_INDEX，走不到这里）。
 *
 * 本函数是 translateCond 的"其它"兜底与 translateExp/translateBinArith 之间的**闸门**：
 * 兜底会把不认得的条件交给 translateExp，而那些运算里只有这八个是下游认得的。
 * 见下方兜底分支前的用法与理由。 */
static int isKnownBinOp(const char *op) {
    return strcmp(op, "ASSIGNOP") == 0 || strcmp(op, "AND")   == 0 ||
           strcmp(op, "OR")       == 0 || strcmp(op, "RELOP") == 0 ||
           strcmp(op, "PLUS")     == 0 || strcmp(op, "MINUS") == 0 ||
           strcmp(op, "STAR")     == 0 || strcmp(op, "DIV")   == 0;
}

/* RELOP 结点的**运算符文本**（`< <= > >= == !=` 之一）—— **只看专用字段 `opText`**。
 *
 * 文本**刻意不在 `lexeme`**：`lexeme` 非 NULL 会让 printNode 从裸 `RELOP` 变成
 * `RELOP: >`，打语法树的既有回归（Test/sample + Test/err，逐字节 diff）当场全红。
 * 故 RELOP 的文本另有 `opText` 承载（见 tree.h），而 `lexeme` 对 RELOP **恒为 NULL**。
 *
 * ★ **为什么这里不再"opText 为空就回退读 lexeme"**：那个回退是给 Test/unit/test_cond.c
 *   里**手工** `newToken("RELOP", 1, ">")`（文本塞在 lexeme 上）留的兼容路 ——
 *   而**真词法器从来不那么干**。于是那批单测一直在**测一条真实产物不会走的路**
 *   （判据的输入与真产物**不同形**），一条 Critical 因此躲过了单测、审查、回归三张网。
 *   ⇒ 两边**共用 `newRelop`**（tree.h/tree.c）构造结点、逐字段同形，兼容路再无存在
 *   的理由，故删掉 —— 保留它只会继续把这类"假绿"留在判据里。
 *
 * ★ `opText` 为空 => **响亮失败**，绝不静默兜一个关系符：那会产出一份看着挺像那么回事、
 *   却用了错关系符的 IR（本仓库最怕的那种失败；与 irRelopCode 的表外文本、classifyExp
 *   的 bad 标签同一套风格）。诊断里点名"关系运算符文本缺失"，让看到日志的人立刻知道
 *   该去查构造 RELOP 结点的地方（`newRelop` / `src/lexical.l` 的关系运算符规则）。 */
static const char *relopTextOf(const Node *relop) {
    if (!relop->opText) {
        fprintf(stderr, "internal error: 关系运算符文本缺失（RELOP 结点的 opText 为空）at line %d"
                        "（RELOP 结点应经 newRelop 构造、由它把源码文本填进 opText；"
                        "见 src/lexical.l 的关系运算符规则）\n",
                nodeLine(relop));
        abort();
    }
    return relop->opText;
}

InterCodeList *translateCond(InterCodeList *tail, Node *e,
                             const char *lt, const char *lf) {
    ExpKind k = classifyExp(e);

    if (k == EX_BINOP && strcmp(e->child[1]->name, "RELOP") == 0) {
        Operand a = operandForCond(e->child[0]);
        Operand b = operandForCond(e->child[2]);
        InterCode *c;
        if (!operandIsSimple(a)) { Operand t = opVar(newTempName());
                                   tail = translateExp(tail, e->child[0], t); a = t; }
        if (!operandIsSimple(b)) { Operand t = opVar(newTempName());
                                   tail = translateExp(tail, e->child[2], t); b = t; }
        c = newCode(IR_IF);
        c->u.iff.x = a; c->u.iff.y = b; c->u.iff.relop = irRelopCode(relopTextOf(e->child[1]));
        c->u.iff.z = opLabel(lt);
        tail = irAppend(tail, c);
        return appendGoto(tail, lf);
    }
    if (k == EX_UNARY && isTok(e->child[0], "NOT"))
        return translateCond(tail, e->child[1], lf, lt);        /* ★ 目标互换 */
    if (k == EX_BINOP && strcmp(e->child[1]->name, "AND") == 0) {
        char *l1 = newLabelName();
        tail = translateCond(tail, e->child[0], l1, lf);
        tail = appendLabel(tail, l1);                            /* Exp2 的代码落在它之后 */
        return translateCond(tail, e->child[2], lt, lf);
    }
    if (k == EX_BINOP && strcmp(e->child[1]->name, "OR") == 0) {
        char *l1 = newLabelName();
        tail = translateCond(tail, e->child[0], lt, l1);
        tail = appendLabel(tail, l1);                            /* Exp2 的代码落在它之后 */
        return translateCond(tail, e->child[2], lt, lf);
    }
    /* (other cases)：求值到 t1 再与 #0 比较。
     * 走到这里的是"不是关系/与/或/非"的一切 —— 裸变量、字面量、括号、算术式子、
     * 函数调用……它们的真假就是"值 != 0"，故先求值再比较。
     * 括号也在这一支（`(a > b)` 会多走一次求值与比较）：这是指导书表 4"其它"那行的
     * 形态，本任务照搬 —— 把括号剥掉属于优化 pass（Task 16 起）的事，不在这里做。
     *
     * ★ **先挡住未知二元运算符**：兜底把 e 交给 translateExp，而 translateExp ->
     * translateBinArith 对非 `+ - * /` 的二元运算符会**退回本函数**（translateCondAsValue）
     * —— 若那个运算符连 RELOP/AND/OR 都不是，就会再次落到**本兜底**，形成
     *   translateCond -> translateExp -> translateBinArith -> translateCondAsValue
     *   -> translateCond -> … 的**无限自递归**（爆栈），而**不是**响亮失败。
     * 今天经 syntax.y **到不了**这里（它的二元产生式恰好就是 isKnownBinOp 认的那八个，
     * 见该函数注释），但"低概率"不是"可以静默"的理由 —— 将来 syntax.y 添了第九个二元
     * 运算符、或别处构造出这种结点，这里必须当场诊断 + abort，与本文件其余位置
     * （classifyExp 的 bad 标签、shapeError、irRelopCode 的表外文本）的失败形态一致。
     * 注意这里**不是**补一个 `default:` 就完事：那是一句"没见过的形状"，说不清为什么会走到，
     * 故诊断里点名"未知二元运算符 + 会无限自递归"，让看到日志的人立刻知道该去查哪里。 */
    if (k == EX_BINOP && !isKnownBinOp(e->child[1]->name)) {
        fprintf(stderr, "internal error: translateCond 遇到未知二元运算符 \"%s\" at line %d"
                        "（继续下去会无限自递归，故此处响亮失败）\n",
                e->child[1]->name ? e->child[1]->name : "(none)", nodeLine(e));
        abort();
    }
    {
        Operand t1 = opVar(newTempName());
        InterCode *c;
        tail = translateExp(tail, e, t1);
        c = newCode(IR_IF);
        c->u.iff.x = t1; c->u.iff.y = opConst(0);
        c->u.iff.relop = irRelopCode("!="); c->u.iff.z = opLabel(lt);
        tail = irAppend(tail, c);
        return appendGoto(tail, lf);
    }
}

/* ══ 统一的"形状不符 ⇒ 响亮失败"（本层各处共用）══════════════════════════════
 *
 * 形状判错时生成的 IR 往往"看着挺像那么回事"，端到端（parser + 真机 + .exp）测不出来；
 * 而越界读 / 空指针解引用是**安静的**崩溃。故凡按已知形状取孩子之前，先过一道闸。
 * 它与 classifyExp/classifyStmt 的 bad 标签、irRelopCode 的表外文本同一套失败形态。
 * Task 11 之前它定义在文件后半（语句翻译一节）；现上提到调用翻译之前，供
 * translateArgs 一并使用 —— 定义只有这一处，避免出现两份"形状错误"的措辞。 */
static void shapeError(const char *what, Node *n) {
    fprintf(stderr, "internal error: %s 的形状与语法树不符（nchild=%d）at line %d\n",
            what, n ? n->nchild : -1, n ? nodeLine(n) : 0);
    abort();
}

/* 关系运算 / 与 / 或**作为值**：按设计文档 §6.2 的
 *   `[place := #0]` + translate_Cond(...) + `[LABEL l_true]` `[place := #1]` `[LABEL l_false]`
 * 生成 —— 条件成立的地方把 place 置 1，不成立的地方（l_false）保留先置的 0。
 * place 为 NULL（值不被需要）时那两条 `place := …` 都不产，**但条件代码照常生成**：
 * 跳转是这段表达式的全部语义，且两侧可能含函数调用这类副作用。
 *
 * ★ **`place` 与条件里读到的操作数同名时的交互**（详见 spec §11 P8）：本函数在条件
 * **之前**先产 `place := #0`，而条件里的 `EX_ID` 操作数是在 `translateCond` 里**惰性**
 * 就地读的（不像算术两侧那样先求值进临时变量）。于是当 place 本身又作为条件操作数出现
 * 时（`x = !x;`、`x = x > 0;`、`x = x && y;` 这类"赋值目标变量也在条件里被读"），
 * 生成的顺序是**先把自己清成 0、随后才去读它** —— 读到的是刚清掉的那个 0，而不是
 * 读之前的旧值。这与 C 的求值语义**不同**（C 里 `x = !x` 读的是旧值）。
 * 但它**逐字符合指导书表 2 的模板**：表 2 里 RELOP/NOT/AND/OR 那一行就是
 * `code0 = [place := #0]` 排在最前，本文件与设计文档 §6.2 都照抄了它；课程那份参考
 * 实现若按同一模板写，会有**完全相同**的行为。
 * ⇒ **这是刻意跟随指导书模板的行为，不是本实现的独立选择。改动前必须先确认判分口径**
 *   —— 判分按"中间代码在虚拟机上运行的结果"给，单方面改成 C 语义（先算进临时变量、
 *   或做别名门控）**可能反而与判分机期望的输出不一致**，故本任务只记录、不改行为。
 * 当前行为由 Test/unit/test_cond.c 的 `x = !x;` 用例逐字节钉住：日后若有人"顺手修成
 * C 语义"，那条用例会**当场变红**，而不是让行为悄悄改变。 */
static InterCodeList *translateCondAsValue(InterCodeList *tail, Node *e, Operand place) {
    char *lt = newLabelName(), *lf = newLabelName();
    InterCode *c;
    if (place) { c = newCode(IR_ASSIGN); c->u.assign.x = place; c->u.assign.y = opConst(0);
                 tail = irAppend(tail, c); }
    tail = translateCond(tail, e, lt, lf);
    tail = appendLabel(tail, lt);
    if (place) { c = newCode(IR_ASSIGN); c->u.assign.x = place; c->u.assign.y = opConst(1);
                 tail = irAppend(tail, c); }
    return appendLabel(tail, lf);
}

/* 逻辑非**作为值**：与上面逐字同形 —— `!Exp` 的全部特别之处都收在
 * translateCond 的"目标互换"那一行里（`translateCond` 认得 EX_UNARY/NOT），
 * 这里不再重复一遍那个判断，免得两处实现有分歧。 */
static InterCodeList *translateNotAsValue(InterCodeList *tail, Node *e, Operand place) {
    return translateCondAsValue(tail, e, place);
}

/* ══ 调用 / read · write（Task 11）════════════════════════════════════════════
 *
 * ★ `read` / `write` **不是**普通函数：语义层把它们当内建（要求书 2.1.4），故这里按
 *   **函数名特判**，翻译成专门的 IR_READ / IR_WRITE，而不是 `CALL read`。
 *
 * ★★ **实参顺序（本任务最易写反、也最难查的一处）**：虚拟机 `ARG` **压栈**、
 *   `PARAM` **弹栈**（参考机 §3.4；scripts/irsim_ref.py 的 `_step`：ARG 是
 *   `arg_stack.append`、PARAM 是 `arg_stack.pop`）。故形参 `(a,b,c)` 的实参 `(x,y,z)`
 *   必须发成 `ARG z / ARG y / ARG x` —— **与源码书写顺序相反**。
 *   ⇒ `translateArgs` 把每一项追加到（已递归出来的）右侧结果**之后**，于是
 *     argList = [最右, …, 最左]，`translateCallN` 再按此顺序发 ARG。
 *   ★ 求值顺序则**从左到右**（先翻 child[0]、再递归 child[2]），IR 因此按源码顺序生成，
 *     只有"入列顺序"相反 —— 两者别混为一谈。
 *   守护用例：Test/unit/lab3_call_in.cmm（`f(1,2,3)` ⇒ 123；顺序写反 ⇒ 321）。 */

/* 单次调用的实参上限。256 只是**防御上限**（定长栈数组，256 个 Operand 可接受）；
 * 要求书对本语言的实参个数**没有**限制，故不能把 Task 11 的 16 当成语言规则。 */
#define MAX_CALL_ARGS 256

/* 实参的**类型**（Task A-3d）。只认三种形状，其余返回 NULL：
 *   · `EX_PAREN` → 递归括号里的表达式（括号不改变类型）；
 *   · `EX_ID`    → varTypes 里登记的类型（翻译层自己的表，见该表处说明）；
 *   · `EX_INDEX` → 从 child[0] 的类型逐层 `u.array.elem`；下标层数超过维数返回 NULL。
 * 返回 NULL 的含义是"不是（能识别的）数组实参"，调用方据此走原有的标量路径。
 *
 * 为什么不直接用语义层的类型：`semanticAnalyze` 返回前已把函数作用域弹出，
 * 翻译时 `symLookup` 看不到局部变量/形参 —— 与 `arrayElemAddr` 必须自带
 * varTypes 是同一个原因。 */
static Type typeOfArgument(Node *e) {
    Type t;
    if (e == NULL) return NULL;
    switch (classifyExp(e)) {
    case EX_PAREN:
        return typeOfArgument(e->child[1]);
    case EX_ID: {
        VarTypeEntry *ent = lookupVarType(e->child[0]->lexeme);
        return ent ? ent->type : NULL;
    }
    case EX_INDEX:
        t = typeOfArgument(e->child[0]);
        if (t == NULL || t->kind != TY_ARRAY) return NULL;  /* 层数超过维数 / 基础非数组 */
        return t->u.array.elem;
    default:
        return NULL;
    }
}

/* 定义在下标翻译一节；translateArgs 的部分下标实参路径要提前调用。 */
static Operand arrayElemAddr(InterCodeList **p, Node *e, int allowPartial);

/* Args : Exp | Exp COMMA Args —— 右递归（见 src/syntax.y）。
 *
 * 入列顺序 = **源码顺序的逆序**：先递归右侧（更靠后的实参先入列），本项追加到末尾。
 * 为什么必须逆序：见本节的 ARG/PARAM 说明。
 *
 * ★ **实参能直接入列就不物化**（Task 11b 审查修正）。要求书明说"ARG 语句的参数可以是
 *   变量、以 `#` 开头的常数或以 `&` 开头的某个变量的地址" ⇒ `EX_INT` 直接入 `opConst`、
 *   `EX_ID` 直接入 `opVar`：`f(1,2,3)` 于是只出 **3 条** ARG，而不是"3 条搬运 + 3 条 ARG"
 *   的 6 条（要求书 4.1.6 样例 1 的 `WRITE v2` 也是这个形态 —— 原先 `write(n)` 会多出
 *   一条 `t := v4`）。其余形状（数组元素、算术式子、调用…）不是 ARG 认得的操作数形态，
 *   仍旧**先求值到一个临时变量**再入列。
 *   ⚠ 顺序仍是**逆序**（`argList[0]` 是最靠右的那个实参）—— 别把这次改动与顺序搞混。
 *   ⚠ 直接入列的变量**不再是"求值时刻的快照"**：真机 `ARG x` 是 `get_value(x)`，
 *     即 ARG 执行时**现读**。物化写法读的是各项求值那一刻的值。本语言里这个差别
 *     **观察不到** —— 假设 4 无全局变量；数组实参传的是**地址**（局部数组 `ARG &v`
 *     是常量地址；数组形参 `ARG b` 的 b 是形参地址，函数体内没有任何语句能改 b
 *     本身），故没有任何实参表达式能改到调用方看得见的标量变量；C 对 `f(x, g())`
 *     的求值次序本身也是未指定的。故按要求书的形态直接入列。
 *
 * ★ **数组实参（Task 14，选做 3.2c）**：`typeOfArgument` 判为 `TY_ARRAY` 时，
 *   入列的操作数是**地址**而不是值：
 *     · 局部数组   → `opAddr(名字)`，渲染 `ARG &vN`；
 *     · 数组形参   → `opVar(名字)`，形参的运行时值**已经就是基地址**，**绝不再加 `&`**
 *                    （否则会传出"地址的地址"，体内按 `v + i*4` 取到的是垃圾）。
 *     · **部分下标**（A-3d）→ `arrayElemAddr(..., allowPartial=1)` 算出行/子数组的
 *                    基地址临时量，直接入列（`ARG t`；t 的值就是基地址）。`r[0]`
 *                    仍是数组，语义层对数组只比维数/基类型、不比长度，故
 *                    `sum(r[0])` 合法；地址路径只在**实参**这一处放宽。
 *   括号不改变数组身份，故 `f((op))`、`f((r[0]))` 与不套括号同形。`argList` 是
 *   `Operand[]`，`opAddr`/地址临时量都直接放得下 —— 这就是 Task 11 把它从名字数组
 *   改成操作数数组的用处。非数组实参照旧走下面的标量路径（ID → `opVar`，其余形状
 *   → 物化）。**数组值整体**（例如把整个 `r` 赋给某处）仍不支持：形状不是
 *   ID/部分下标时响亮 abort；并且 `translateExp` 对部分下标作**标量值**的失败路径
 *   未被放宽（`x = r[0]`、`write(r[0])` 仍由语义层按类型不匹配拦下）。
 *   结构体实参属选做 3.1，由语义闸（Task 15）在翻译前拦下；且 `registerDeclType`
 *   只登记 int 标量/数组，故这里没有（也不需要）结构体分支。
 *
 * `argList` 是调用方给的定长数组（容量 MAX_CALL_ARGS），元素是**操作数**（原先存名字）；
 * 超出即**响亮 abort** —— 与 varMap 的容量守卫同一纪律：定长表越界会**静默踩内存**，
 * 那正是本仓库最怕的失效。 */
static InterCodeList *translateArgs(InterCodeList *tail, Node *a,
                                    Operand *argList, int *n) {
    Operand t;
    Node *arg, *id;
    Type argType;
    if (a == NULL || (a->nchild != 1 && a->nchild != 3)) shapeError("Args", a);
    /* 本项（源码里靠左）：认得的形状直接入列，不认得才物化到临时变量。 */
    arg = a->child[0];

    /* ★ 数组实参（Task 14 / A-3d）：括号不改变数组身份，先剥到最内层。 */
    id = arg;
    while (id != NULL && classifyExp(id) == EX_PAREN) id = id->child[1];
    argType = typeOfArgument(arg);
    if (argType != NULL && argType->kind == TY_ARRAY) {
        if (id != NULL && classifyExp(id) == EX_ID) {
            /* 局部数组传地址（&arr）；数组形参的运行时值已是基地址（不再加 &）。 */
            VarTypeEntry *ent = lookupVarType(id->child[0]->lexeme);
            const char *vn;
            if (ent == NULL || ent->type == NULL || ent->type->kind != TY_ARRAY) {
                fprintf(stderr, "internal error: 数组实参 \"%s\" 的类型登记缺失"
                                "（语义闸应已拦下）at line %d\n",
                        id->child[0]->lexeme, nodeLine(id));
                abort();
            }
            vn = varNameOf(id->child[0]->lexeme);
            t = ent->isArrayParam ? opVar(vn) : opAddr(vn);
        }
        else if (id != NULL && classifyExp(id) == EX_INDEX) {
            /* 部分下标（`r[0]`）作为数组实参：算出行/子数组的基地址。
             * ★ 只有这里允许 partial —— 右值/左值路径仍然要求取到标量
             *   （见 arrayElemAddr 的 allowPartial 参数）。 */
            t = arrayElemAddr(&tail, id, 1);
        }
        else {
            /* 本实验不支持"数组值"实参（形状既不是数组名也不是部分下标）。 */
            fprintf(stderr, "internal error: 不支持的数组实参形状（只支持数组名或"
                            "部分下标）at line %d\n", nodeLine(arg));
            abort();
        }
    }
    else if (classifyExp(arg) == EX_INT) t = opConst(atoi(arg->child[0]->lexeme));
    else if (classifyExp(arg) == EX_ID)  t = opVar(varNameOf(arg->child[0]->lexeme));
    else { t = opVar(newTempName());
           tail = translateExp(tail, arg, t); }
    if (a->nchild == 3)
        tail = translateArgs(tail, a->child[2], argList, n);    /* 更靠后的实参先入列 */
    if (*n >= MAX_CALL_ARGS) {
        fprintf(stderr, "internal error: 实参个数超过 %d（translateArgs 定长缓冲）\n",
                MAX_CALL_ARGS);
        abort();
    }
    argList[(*n)++] = t;                                        /* ★ 本项排在它们之后 */
    return tail;
}

/* `f()` / `read()`（`Exp : ID LP RP`）。 */
static InterCodeList *translateCall0(InterCodeList *tail, Node *e, Operand place) {
    const char *fname = e->child[0]->lexeme;
    if (strcmp(fname, "read") == 0) {
        /* read()：值落到 place。
         * ★★ **值不被需要时也必须发 READ**（Task 11b 审查修正）。原先这里写的是
         *   `if (place) …; return tail;`，注释还把它称作"**无副作用的**丢弃读" ——
         *   那是**假的**：`READ` 的"消费一次输入"本身就是**可观察副作用**，
         *   丢掉它会让**后续**的 read() 读到错的那个数。
         *   实测（Test/unit/lab3_call_in.cmm 的裸 `read();` 那一段）：
         *   `read(); n = read(); write(n);` 输入 `5 7` ⇒ 丢掉时输出 **5**、
         *   C 语义（scripts/c_oracle.py，独立第二条链）输出 **7**。
         *   这也与**紧接在本函数之后的** translateCallN 对普通调用的既有做法自相矛盾
         *   （那里写着"值不要也得调用：被调函数可能有副作用"）—— 同一件事，两种处理。
         *   要求书只规定 read 无参、返回读入值、可直接对应 `READ` 操作，
         *   **没有**"值没人要就不翻译"的许可。
         * ⇒ 值没人要时新造一个临时变量当目标，照常发 READ（丢弃的是**值**，不是那次读）。 */
        Operand dst = place ? place : opVar(newTempName());
        InterCode *c = newCode(IR_READ);
        c->u.one.x = dst;
        return irAppend(tail, c);
    }
    /* ★ `write()`（0 参）与 `read(x)` 是同一类形状错误的两半（Task 11b 审查修正）：
     * 要求书定义 write **恰 1 个**实参。若放任它走普通调用路径，产物是 `CALL write`，
     * 参考机**装载期**才报 `UndefinedLabelError: write` —— 同样是"看着挺像那么回事的
     * 错 IR"。故与 translateCallN 的实参个数闸对称：这里也当场诊断 + abort。 */
    if (strcmp(fname, "write") == 0) {
        fprintf(stderr, "internal error: write 恰好接受 1 个实参（要求书定义），实测 0 个"
                        " at line %d\n", nodeLine(e));
        abort();
    }
    if (place) { InterCode *c = newCode(IR_CALL);
                 c->u.call.x = place; c->u.call.f = opFunc(fname);
                 return irAppend(tail, c); }
    /* 值不要也得调用：被调函数可能有副作用（write/read/改全局状态）。 */
    { Operand t = opVar(newTempName());
      InterCode *c = newCode(IR_CALL);
      c->u.call.x = t; c->u.call.f = opFunc(fname);
      return irAppend(tail, c); }
}

/* `f(Args)` / `write(Exp)`（`Exp : ID LP Args RP`）。 */
static InterCodeList *translateCallN(InterCodeList *tail, Node *e, Operand place) {
    const char *fname = e->child[0]->lexeme;
    Operand argList[MAX_CALL_ARGS];
    int n = 0, i;

    /* ★ 形状闸（Task 11b 审查修正）：要求书定义 `read` 为**0 参**内建。
     * `read(x)` 不匹配 EX_CALL0（它多一个实参），会走到这里；原先会按普通调用路径
     * 产出 `t := CALL read`，而参考机装载期就 `UndefinedLabelError: read`（没有叫
     * read 的函数）。本仓库的纪律是"**不认识的形状响亮失败，绝不产出看着挺像那么回事
     * 的错 IR**"，故这里在翻译实参**之前**当场诊断 + abort。 */
    if (strcmp(fname, "read") == 0) {
        fprintf(stderr, "internal error: read 不接受实参（要求书定义 read 为 0 参内建）"
                        " at line %d\n", nodeLine(e));
        abort();
    }

    tail = translateArgs(tail, e->child[2], argList, &n);

    if (strcmp(fname, "write") == 0) {
        /* write(e)：`WRITE x`。**place 为 NULL 时不补 `place := #0`**
         * （那是 P2 才要的"write 表达式的值"；`write(e);` 作语句时值不被需要）。
         * ★ 实参个数闸（Task 11b 审查修正）：要求书定义 write **恰 1 个**实参。
         * 原先这里直接取 `argList[0]`，而 argList 是**逆序**的 ⇒ `write(a, b)` 会**静默**
         * 取到源码里**最后一个**实参 b（无任何诊断）；`write()` 0 参更会读到 argList 里
         * 未初始化的栈内存。两种都是"看着挺像那么回事的错 IR"，故一律响亮失败。 */
        InterCode *w;
        if (n != 1) {
            fprintf(stderr, "internal error: write 恰好接受 1 个实参（要求书定义），"
                            "实测 %d 个 at line %d\n", n, nodeLine(e));
            abort();
        }
        w = newCode(IR_WRITE);
        w->u.one.x = argList[0];
        tail = irAppend(tail, w);
        if (place) { InterCode *z = newCode(IR_ASSIGN);
                     z->u.assign.x = place; z->u.assign.y = opConst(0);
                     tail = irAppend(tail, z); }
        return tail;
    }
    /* 普通调用：按 argList 顺序发 ARG（= 源码逆序，见本节说明）。 */
    for (i = 0; i < n; i++) {
        InterCode *c = newCode(IR_ARG);
        c->u.one.x = argList[i];
        tail = irAppend(tail, c);
    }
    if (place) { InterCode *c = newCode(IR_CALL);
                 c->u.call.x = place; c->u.call.f = opFunc(fname);
                 return irAppend(tail, c); }
    { Operand t = opVar(newTempName());
      InterCode *c = newCode(IR_CALL);
      c->u.call.x = t; c->u.call.f = opFunc(fname);
      return irAppend(tail, c); }
}

/* ══ 数组下标（Task 11 一维；Task 13 高维 + 地址一次线性化）════════════════════
 *
 * `a[i][j]…` 的**元素字节地址** = `&a + Σ(k_d * stride_d)`，其中 `stride_d` 是
 * "走过第 d 个下标后剩下的元素类型"的字节数。本实现**从不物化行地址**：
 * 逐维只发一条 `MUL`（变量下标）并把常量下标累加进 `constOff`，最后只做一次
 * `addr := &a + total`。与"先把 `&a[i]` 算出来、再在它上面加 `j*4`"的链式写法相比，
 * 后者每多一维就多一条 ADD，且在常量下标处还会留下可折叠的中间量。
 *
 * 例（int r[1][2] 的 r[0][i]）：
 *     idx := i                 （变量下标才发）
 *     off := idx * #4
 *     addr := &r + off         （0*8 已折进 constOff==0，整条消失）
 *
 * 步长由**类型链**给出：`typeSizeBytes(cur->u.array.elem)`。类型从翻译层自己的
 * 名字 -> 类型登记表取（见 varTypes 处说明）——语义层的局部作用域此时已弹出，
 * `symLookup` 用不了。
 *
 * 失败形态（都与本文件其余位置一致：响亮 abort，绝不猜一种布局）：
 *   · 基础表达式不是 ID（`f()[i]` 等）—— 不在本任务范围；
 *   · 基础名没有类型登记 / 登记的不是数组 —— 语义闸（错误 10）应已拦下；
 *   · 下标层数超过数组维数 / 走完下标仍不是标量（**部分下标得到数组**）——
 *     后者语义上合法（`r[0]` 是行）。**只有实参路径**（`allowPartial=1`，A-3d）
 *     允许结果仍是数组并返回该部分下标的地址；右值/左值路径（`allowPartial=0`）
 *     仍要求取到标量，部分下标当值用照旧响亮失败（`x = r[0]` 等由语义闸先拦）。
 *
 * ★ **数组形参的基地址（Task 14）**：形参在运行时的值**就是**基地址，故
 *   `base = opVar(名字)`；局部数组才 `base = opAddr(名字)`（渲染 `&a`）。
 *   两者的区别只由 varTypes 的 `isArrayParam` 标记决定（见该表处说明）。
 *
 * `&a` 是 OP_ADDR（渲染成 `&a`），只用在"自己就带 &"的位置（ARITH 的操作数），
 * **不是**喂给 IR_ADDR 的那个（见 ir.h 顶部的"两个 & 的分工"）。 */

/* 下标链的定长缓冲容量。与 Dims.dims 的 8 一致；超出即响亮 abort（绝不静默越界）。 */
#define MAX_INDEX_DIMS 8

/* 一个类型的字节数。本实验只有 int/float（都 4 字节）与数组；结构体是选做 3.1，
 * 由 Task 15 的语义闸拦住 —— 走到这里说明闸漏了，故响亮失败而不是猜 4。 */
static int typeSizeBytes(Type t) {
    if (t == NULL) {
        fprintf(stderr, "internal error: typeSizeBytes 收到 NULL 类型\n");
        abort();
    }
    switch (t->kind) {
    case TY_BASIC:
        return 4;
    case TY_ARRAY:
        return t->u.array.size * typeSizeBytes(t->u.array.elem);
    case TY_STRUCTURE:
    case TY_UNKNOWN:
    default:
        fprintf(stderr, "internal error: typeSizeBytes 遇到不支持的类型 kind=%d"
                        "（结构体属选做 3.1，应由语义闸拦下）\n", (int)t->kind);
        abort();
    }
    return 0;   /* 不可达；只为让"所有路径都有返回值"显式 */
}

/* 把 `E0[E1]…[Ek]` 的整条下标链按**源码从左到右**收进 indices，返回剥掉括号的
 * 基础表达式。`a[i][j]` 的 AST 是 `(a[i])[j]` —— 先递归 child[0]（更靠左的下标），
 * 再记本层，故 indices = [i, j]。括号不产生 IR，链上每一层都剥掉。 */
static Node *collectIndexChain(Node *e, Node **indices, int *n) {
    while (e != NULL && classifyExp(e) == EX_PAREN) e = e->child[1];
    if (e == NULL) shapeError("Exp[Exp]", e);
    if (classifyExp(e) == EX_INDEX) {
        Node *base;
        if (e->nchild != 4 || e->child[0] == NULL || e->child[2] == NULL)
            shapeError("Exp[Exp]", e);
        base = collectIndexChain(e->child[0], indices, n);
        if (*n >= MAX_INDEX_DIMS) {
            fprintf(stderr, "internal error: 下标维数超过 %d（arrayElemAddr 定长缓冲）\n",
                    MAX_INDEX_DIMS);
            abort();
        }
        indices[(*n)++] = e->child[2];
        return base;
    }
    return e;
}

/* 计算 `Exp1[Exp2]…` 的元素地址到一个**新临时变量**并返回该操作数；新增 IR 挂在
 * `*p` 后（`*p` 就地前进到新尾）。
 *
 * `allowPartial == 0`：走完下标必须落到 `TY_BASIC`（右值/左值路径的既有契约）。
 * `allowPartial != 0`：允许结果仍是 `TY_ARRAY`（部分下标），返回其基地址操作数；
 * 供 `translateArgs` 的数组实参路径使用。结构体等其它非标量仍然响亮失败。 */
static Operand arrayElemAddr(InterCodeList **p, Node *e, int allowPartial) {
    Node *indices[MAX_INDEX_DIMS];
    Operand terms[MAX_INDEX_DIMS];
    int n = 0, nterms = 0, constOff = 0, i;
    Node *b;
    VarTypeEntry *ent;
    Type cur;
    Operand base, addr;
    InterCode *c;

    if (e == NULL || classifyExp(e) != EX_INDEX) shapeError("Exp[Exp]", e);
    b = collectIndexChain(e, indices, &n);

    if (b == NULL || classifyExp(b) != EX_ID) {
        fprintf(stderr, "internal error: 数组下标的基础表达式只能是数组名"
                        "（其它基础表达式不在本任务）at line %d\n",
                nodeLine(e));
        abort();
    }
    ent = lookupVarType(b->child[0]->lexeme);
    if (ent == NULL) {
        fprintf(stderr, "internal error: 变量 \"%s\" 没有类型登记"
                        "（语义闸应已拦下；见 varTypes 的说明）at line %d\n",
                b->child[0]->lexeme, nodeLine(e));
        abort();
    }
    if (ent->type == NULL || ent->type->kind != TY_ARRAY) {
        fprintf(stderr, "internal error: 对非数组变量 \"%s\" 取下标"
                        "（语义闸的错误 10 应已拦下）at line %d\n",
                ent->name, nodeLine(e));
        abort();
    }

    cur = ent->type;
    for (i = 0; i < n; i++) {
        Node *idxExp = indices[i];
        int stride;
        if (cur->kind != TY_ARRAY) {
            fprintf(stderr, "internal error: 下标层数超过数组 \"%s\" 的维数"
                            "（语义闸应已拦下）at line %d\n", ent->name, nodeLine(e));
            abort();
        }
        cur = cur->u.array.elem;                               /* 进一层元素类型 */
        stride = typeSizeBytes(cur);                           /* 本维步长 */
        if (classifyExp(idxExp) == EX_INT) {                       /* ★ 常量下标折叠 */
            /* 整条 idx/off 都不发，只累加常数偏移。 */
            constOff += atoi(idxExp->child[0]->lexeme) * stride;
        } else {
            Operand idx = opVar(newTempName());
            Operand off = opVar(newTempName());
            *p = translateExp(*p, idxExp, idx);                /* idx := <下标表达式> */
            c = newCode(IR_MUL);                               /* off := idx * #stride */
            c->u.binop.x = off; c->u.binop.y = idx; c->u.binop.z = opConst(stride);
            *p = irAppend(*p, c);
            terms[nterms++] = off;
        }
    }
    if (cur->kind != TY_BASIC) {
        /* 部分下标得到数组。**实参路径**允许（A-3d）：返回的地址就是行/子数组的
         * 基地址。其它路径（以及结构体等非数组非标量）仍响亮失败 —— 绝不能把它
         * 当标量算出个"像那么回事"的值。 */
        if (!(allowPartial && cur->kind == TY_ARRAY)) {
            fprintf(stderr, "internal error: 下标没有取到标量元素（部分下标得到数组）——"
                            "数组/部分下标不能当标量值使用 at line %d\n", nodeLine(e));
            abort();
        }
    }

    /* ★ 数组形参的运行时值已是基地址（opVar）；局部数组才取 &（opAddr）。 */
    base = ent->isArrayParam ? opVar(varNameOf(b->child[0]->lexeme))
                             : opAddr(varNameOf(b->child[0]->lexeme));

    /* ── 地址一次线性化：所有变量项先求和（含常数项），最后只加一次 &base ── */
    if (nterms == 0) {
        addr = opVar(newTempName());
        if (constOff == 0) {                                   /* `addr := &a`（省一条 ADD） */
            c = newCode(IR_ASSIGN);
            c->u.assign.x = addr; c->u.assign.y = base;
        } else {                                               /* `addr := &a + #constOff` */
            c = newCode(IR_ADD);
            c->u.binop.x = addr; c->u.binop.y = base; c->u.binop.z = opConst(constOff);
        }
        *p = irAppend(*p, c);
    } else {
        Operand total = terms[0];
        for (i = 1; i < nterms; i++) {
            Operand t = opVar(newTempName());
            c = newCode(IR_ADD);
            c->u.binop.x = t; c->u.binop.y = total; c->u.binop.z = terms[i];
            *p = irAppend(*p, c);
            total = t;
        }
        if (constOff != 0) {
            Operand t = opVar(newTempName());
            c = newCode(IR_ADD);
            c->u.binop.x = t; c->u.binop.y = total; c->u.binop.z = opConst(constOff);
            *p = irAppend(*p, c);
            total = t;
        }
        addr = opVar(newTempName());
        c = newCode(IR_ADD);                                   /* addr := &a + total */
        c->u.binop.x = addr; c->u.binop.y = base; c->u.binop.z = total;
        *p = irAppend(*p, c);
    }
    return addr;
}

/* `Exp1[Exp2]` 作**右值**：算出元素地址后 `place := *addr`（IR_ASSIGN_DEREF）。
 * ★ 左值路径（translateAssignToLValue）**不经过本函数**：它自己调 arrayElemAddr
 *   再发 IR_DEREF_ASSIGN（见那一节）。原先这里有个 `placeIsAddr` 出参，本意是告诉
 *   左值路径"本结点是个地址" —— 但**全仓库没有任何一处读它**（Task 11b 审查核实），
 *   等于文档承诺了一条并不存在的契约，故连同 translate.h 的说明一并删掉。 */
static InterCodeList *translateIndex(InterCodeList *tail, Node *e, Operand place) {
    Operand addr = arrayElemAddr(&tail, e, 0);
    if (place) {
        InterCode *c = newCode(IR_ASSIGN_DEREF);               /* place := *addr */
        c->u.deref.x = place; c->u.deref.y = addr;
        tail = irAppend(tail, c);
    }
    return tail;
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

/* ══ 非 ID 左值的赋值（`a[i] = <expr>`）（Task 11）════════════════════════════
 *
 * 先算出元素**地址**，再把右值算进一个**普通临时变量**，最后 `*addr := t`
 * （IR_DEREF_ASSIGN）。
 *
 * ★★ **绝不能生成 `*t := a op b`**（spec §3.7-2）：真机 `ARITH` 的写回目标是
 *    `mem[symTable[x][0] // 4]`，只做裸下标 —— `tableInsert` 早把 `*` 剥掉，表里没有
 *    `*t` 这个键 ⇒ **KeyError 崩**（参考机逐字复现，见 Test/lab3/unit_exec/test_exec.py
 *    的 deref_arith 用例）。故右值一定先落到**普通**临时变量，再走 IR_DEREF_ASSIGN
 *    （它渲染 `*addr := t`，真机 `MOV` 的 `store_var` **支持** `*` 目标）——这正是本
 *    功能绕开那个真机缺口的写法。 */
static InterCodeList *translateAssignToLValue(InterCodeList *tail, Node *e, Operand place) {
    Node *lhs = e->child[0];
    if (classifyExp(lhs) == EX_INDEX) {                        /* a[i] = <expr> */
        Operand addr = arrayElemAddr(&tail, lhs, 0);
        Operand val = opVar(newTempName());
        InterCode *c;
        tail = translateExp(tail, e->child[2], val);           /* t := <右值> */
        c = newCode(IR_DEREF_ASSIGN);                          /* *addr := t */
        c->u.deref.x = addr; c->u.deref.y = val;
        tail = irAppend(tail, c);
        if (place) {                                           /* 赋值表达式的值还要用 */
            c = newCode(IR_ASSIGN);
            c->u.assign.x = place; c->u.assign.y = val;
            tail = irAppend(tail, c);
        }
        return tail;
    }
    /* 结构体域左值（`s.x = ...`）属选做 3.1，本实验不实现 —— 响亮失败。 */
    fprintf(stderr, "internal error: 非 ID / 非数组下标的左值赋值不支持"
                    "（结构体域属选做 3.1）at line %d\n", nodeLine(e));
    abort();
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
    tail = translateExp(tail, e->child[0], t1);
    t2 = opVar(newTempName());
    tail = translateExp(tail, e->child[2], t2);
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
    /* ★ 数组名作赋值目标同样必须拒绝：`a = b;` 语义层可能放行，而按 `v1 := v2`
     *   翻译只改 `a[0]`（参考机语义），是一份静默算错的 IR。守卫放在把 dst
     *   传给右子表达式**之前** —— 先翻译右侧再 abort 会白白产生一批 IR。 */
    rejectArrayWholeValue(lhs, 1);
    dst = opVar(varNameOf(lhs->child[0]->lexeme));
    tail = translateExp(tail, e->child[2], dst);         /* ★ 直接算进目标变量 */
    if (place) {                                         /* 赋值表达式的值还要用 */
        c = newCode(IR_ASSIGN);
        c->u.assign.x = place; c->u.assign.y = dst;
        tail = irAppend(tail, c);
    }
    return tail;
}

InterCodeList *translateExp(InterCodeList *tail, Node *e, Operand place) {
    switch (classifyExp(e)) {
    case EX_INT:
        if (place) { InterCode *c = newCode(IR_ASSIGN);
                     c->u.assign.x = place;
                     c->u.assign.y = opConst(atoi(e->child[0]->lexeme));
                     tail = irAppend(tail, c); }
        return tail;

    case EX_ID:
        /* 数组整体值不能当标量：见 rejectArrayWholeValue 的说明。 */
        rejectArrayWholeValue(e, 0);
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
            tail = translateExp(tail, e->child[1], t1);
            c = newCode(IR_SUB);
            c->u.binop.x = place; c->u.binop.y = opConst(0); c->u.binop.z = t1;
            tail = irAppend(tail, c);
        } else {
            tail = translateExp(tail, e->child[1], NULL);
        }
        return tail;

    case EX_PAREN:
        /* 括号不产生任何 IR：把 place 一路传下去（`(a[i])` 与 `a[i]` 因此同形）。 */
        return translateExp(tail, e->child[1], place);

    case EX_CALL0:
        return translateCall0(tail, e, place);                  /* Task 11 */

    case EX_CALLN:
        return translateCallN(tail, e, place);                  /* Task 11 */

    case EX_INDEX:
        return translateIndex(tail, e, place);                  /* Task 11 */

    case EX_FIELD:
        return translateField(tail, e, place);                  /* 选做 3.1，不实现 */
    }
    return tail;
}

/* ══ 语句 / 复合语句 / 函数定义（Task 10）════════════════════════════════════
 *
 * 指导书表 3 的六条语句模板逐条落在 translateStmt 的 switch 里。`label_true` /
 * `label_false` 由**这里**当场备好（`newLabelName()`）再作继承属性传给 translateCond ——
 * 那是 Task 9"不做回填"在调用方一侧的对应物：标号在生成跳转之前就已在手上，
 * 故不需要待填链、也不需要第二遍回扫。标号的**摆放**（`[LABEL l1]` 摆在哪一段之前）
 * 是这几条模板的全部内容：摆错位置生成的 IR 照样"看着挺像那么回事"，只是跑不对。
 *
 * ── ★ 本任务唯一的一处实质偏离：列表是**右递归**的，不能"遍历全部孩子" ──────
 * brief 里 `translateStmtList` / `translateDefList` / `translateDecList` /
 * `translateProgram` 的内嵌代码都写成"遍历该结点的**全部**孩子"，而 src/syntax.y 里
 * 这四个列表**全部右递归**：
 *     StmtList : Stmt StmtList | ε        DefList : Def DefList | ε
 *     DecList  : Dec COMMA DecList | Dec  ExtDefList : ExtDef ExtDefList | ε
 * ⇒ `child[nchild-1]`（DecList 是 `child[2]`）是**同类的子列表结点**，不是本层元素。
 * 照抄那个循环会得到（逐条实测，见 .superpowers/sdd/lab3/task-10-report.md §5）：
 *   · translateStmtList 把子列表送进 translateStmt ⇒ classifyStmt 见 nchild==2 且
 *     child[0] 是语法结点 ⇒ 判成 ST_EXP ⇒ classifyExp 落 bad ⇒ **abort**
 *     —— **任何非空函数体**的第一条语句之后当场死；
 *   · translateDefList 对子列表取 `child[1]`，而空 DefList 的 `child` 是 **NULL**
 *     ⇒ **空指针解引用**（段错误：安静、难查，且与本仓库"响亮失败"的纪律正相反）；
 *   · translateProgram 那个循环里 translateExtDef 对子列表走"nchild != 3 ⇒ 跳过"
 *     ⇒ **只有第一个函数**被翻译出来（多函数输入悄悄少一半）。
 * 故本文件按**右递归**逐层下降：`child[0]` 是本层元素、`child[1]`（DecList 的 `child[2]`）
 * 是余下的列表。**每条产出的 IR 与 brief 逐字一致** —— 偏离只在"怎么走到列表的下一个元素"。
 *
 * 取值前一律先判形状：本层判错的后果是**越界读 / 空指针解引用**（安静地错），
 * 而不是本仓库要求的响亮 abort，故每个取值点前都有 shapeError 这道闸。
 */

/* VarDec 的**最内层**名字：`VarDec : ID` 或 `VarDec : VarDec LB INT RB`。
 * brief 的说法是"沿 child[0] 下行到只有一个孩子的结点"——这里显式写成"前进到不再是
 * 四孩子的 VarDec 为止"，语义相同而少一次越界机会。形状不对时响亮失败：
 * 返回 NULL 会让调用方把 NULL 当名字用（xstrdup(NULL) 当场段错误，安静地崩）。 */
static const char *leafIdName(Node *vd) {
    while (vd != NULL && vd->nchild == 4) {
        if (vd->child[0] == NULL || vd->child[0]->type != NT_VARDEC) break;
        vd = vd->child[0];
    }
    if (vd == NULL || vd->nchild != 1 || vd->child[0] == NULL ||
        vd->child[0]->kind != NODE_TOKEN)
        shapeError("VarDec", vd);
    return vd->child[0]->lexeme;
}

/* ── 数组声明的维数（Task 11；Task 13 起必须给出**顺序正确**的维度链）──────────
 *
 * `VarDec : ID | VarDec LB INT RB`。语法是**左递归**的，故树的最外层结点带的是
 * **源码里最后一个方括号**：`int a[10][2]` 的 AST 是
 * `VarDec[VarDec[VarDec[a],LB,10,RB],LB,2,RB]`。C 语义要求第一个方括号是最外维，
 * 而 `typeArrayFromDims` 的左折叠要求 dims **最内层在前** ⇒ 对 `a[10][2]` 必须是
 * dims=[2,10]（设计文档 §5.1）。
 * 所以这里**先记本层、再沿 child[0] 下行**（前序），得到的正是 [2,10]。
 *
 * ★ 这是 Task 13 修掉的一个**潜伏错误**：原先写成"先递归再记本层"（后序），
 *   对 `a[10][2]` 得到 [10,2] —— 乘积不变，而 Task 11 只用总字节数，故当时没有任何
 *   症状；但用它构造 Type 会得到 `array(2, array(10,int))`，每一维的步长全反，
 *   高维下标的地址会静默算错。`src/semantic.c` 的 analyzeVarDec 用的就是前序
 *   （先记本层），本函数现在与它、与 type.h 的约定一致。
 *
 * Dims.dims 定长：写入前查容量（同 varMap 的纪律）—— 溢出会**静默越界写**，
 * 那不是本仓库的失败形态。
 *
 * ★ 取值前先过形状闸（Task 11b 审查补上）：语法保证下这里不可达，但本层的纪律是
 *   "取值前一律先判形状"：判错的后果是**越界读 / 空指针解引用**（安静地错），
 *   而不是本仓库要求的响亮 abort。 */
typedef struct { int dims[8]; int n; } Dims;

static void collectDims(Node *vd, Dims *d) {
    if (vd == NULL) shapeError("VarDec", vd);
    if (vd->nchild == 1) {                       /* VarDec : ID —— 到最内层 */
        if (vd->child[0] == NULL || vd->child[0]->kind != NODE_TOKEN)
            shapeError("VarDec", vd);
        return;
    }
    if (vd->nchild != 4) shapeError("VarDec", vd);               /* VarDec : VarDec LB INT RB */
    if (vd->child[0] == NULL || vd->child[0]->type != NT_VARDEC)
        shapeError("VarDec", vd);
    if (vd->child[2] == NULL || vd->child[2]->kind != NODE_TOKEN)
        shapeError("VarDec", vd);                                /* LB 与 RB 之间是 INT 终结符 */
    if (d->n >= (int)(sizeof d->dims / sizeof d->dims[0])) {
        fprintf(stderr, "internal error: 数组维数超过 %d（Dims.dims 定长）\n",
                (int)(sizeof d->dims / sizeof d->dims[0]));
        abort();
    }
    d->dims[d->n++] = atoi(vd->child[2]->lexeme);/* 先记本层（源码最后一个方括号 = 最内维） */
    collectDims(vd->child[0], d);                /* 再到更外层 */
}

/* 收集 VarDec 的维数并把"名字 -> Type"登记进 varTypes；返回维数（0 = 标量）。
 * 局部变量（translateVarDec）与形参（translateVarListAsParams）共用这一处
 * （Task 13 审查 Minor 5 去重）：两处的"标量登记 int / 数组按 dims 构造 Type"
 * 必须逐字一致，否则同一份声明在两条路径上会得到不同的 Type，下标步长随之不同。
 * `isArrayParam` 只对数组有意义（标量形参恒记 0，见 varTypes 处说明）。
 * outDims 非 NULL 时把收集到的 dims 交给调用方（translateVarDec 还要算 DEC 字节数）。 */
static int registerDeclType(const char *name, Node *vd, int isArrayParam, Dims *outDims) {
    Dims d;
    d.n = 0;
    collectDims(vd, &d);
    if (d.n == 0)
        registerVarType(name, typeBasic(BT_INT), 0);
    else
        registerVarType(name, typeArrayFromDims(typeBasic(BT_INT), d.dims, d.n),
                        isArrayParam);
    if (outDims) {                               /* 只复制已用到的维（别搬未初始化的槽位） */
        int i;
        outDims->n = d.n;
        for (i = 0; i < d.n; i++) outDims->dims[i] = d.dims[i];
    }
    return d.n;
}

/* VarDec 的**声明**部分。
 * 登记名字（`varNameOf`）：登记顺序 = 书写顺序，这正是 IR 里的编号与源码顺序一致的
 * 原因，也是要求书 4.1.6 样例 2 里 `fact` 的形参拿到 v1、main 的 `int m, result;`
 * 拿到 v2/v3 的原因（v 号不是"用到时候"才发的）。
 * ★ **类型登记**（Task 13）：标量登记 typeBasic(BT_INT)，数组按正确顺序的 dims 构造
 *   Type。高维下标求步长要用它（语义层的局部作用域在翻译时已弹出）。
 * ★ **数组**（含多维）另发一条 `DEC v <总字节>`（Task 11）：它是虚拟机为该数组分配
 *   地址的依据，漏了它则 `&a` 在真机上查不到、数组整个不可用。非数组不发 DEC。 */
static InterCodeList *translateVarDec(InterCodeList *tail, Node *vd) {
    const char *srcName = leafIdName(vd);        /* ★ 类型表按**源码名**索引 */
    const char *irName = declareVarName(srcName);/* ★ 声明：当前作用域新发一个 v 号 */
    Dims d;
    if (registerDeclType(srcName, vd, 0, &d) > 0) {  /* 数组：dims 已是最内层在前 */
        int i, total = 1;
        InterCode *c;
        for (i = 0; i < d.n; i++) total *= d.dims[i];
        c = newCode(IR_DEC);
        c->u.dec.x = opVar(irName);
        c->u.dec.size = total * 4;               /* 本实验只有 int，宽度 4 */
        tail = irAppend(tail, c);
    }
    return tail;
}

/* DecList : Dec | Dec COMMA DecList —— 右递归（余下的列表在 child[2]，隔着一个 COMMA）。 */
static InterCodeList *translateDecList(InterCodeList *tail, Node *dl) {
    Node *dec;
    if (dl == NULL || (dl->nchild != 1 && dl->nchild != 3)) shapeError("DecList", dl);
    dec = dl->child[0];                          /* Dec : VarDec | VarDec ASSIGNOP Exp */
    if (dec == NULL || dec->type != NT_DEC ||
        (dec->nchild != 1 && dec->nchild != 3)) shapeError("Dec", dec);
    tail = translateVarDec(tail, dec->child[0]);
    if (dec->nchild == 3)
        /* 初值**直接算进被声明的变量**（place 作继承属性）：`int x = 5;` 只出一条
         * `v := #5`，不是"先落临时变量再搬过来"两条。 */
        tail = translateExp(tail, dec->child[2],
                            opVar(varNameOf(leafIdName(dec->child[0]))));
    if (dl->nchild == 3) tail = translateDecList(tail, dl->child[2]);
    return tail;
}

/* DefList : Def DefList | ε —— 右递归。 */
InterCodeList *translateDefList(InterCodeList *tail, Node *l) {
    Node *d;
    if (l == NULL) shapeError("DefList", l);
    if (l->nchild == 0) return tail;             /* ε */
    if (l->nchild != 2) shapeError("DefList", l);
    d = l->child[0];                             /* Def : Specifier DecList SEMI */
    /* `Def : error` 的 child 数为 1，且 $1 是 error 伪符号（语义值可能为 NULL）。
     * 假设 7（无语法错误）保证不出现；真出现了就响亮失败，绝不静默翻过去。 */
    if (d == NULL || d->type != NT_DEF || d->nchild != 3) shapeError("Def", d);
    tail = translateDecList(tail, d->child[1]);
    return translateDefList(tail, l->child[1]);
}

/* StmtList : Stmt StmtList | ε —— 右递归。 */
InterCodeList *translateStmtList(InterCodeList *tail, Node *l) {
    if (l == NULL) shapeError("StmtList", l);
    if (l->nchild == 0) return tail;             /* ε */
    if (l->nchild != 2) shapeError("StmtList", l);
    tail = translateStmt(tail, l->child[0]);
    return translateStmtList(tail, l->child[1]);
}

/* 指导书表 3 的六条语句模板。形状判别由 classifyStmt 负责（本层唯一"读懂结构"的地方），
 * 这里不再自行猜孩子位置 —— 两个子任务各判一半的话，判错时没人说得清错在哪一层。
 * `Stmt : CompSt`、`IF`、`IF-ELSE`、`WHILE` 的孩子位置见 src/syntax.y。 */
InterCodeList *translateStmt(InterCodeList *tail, Node *s) {
    switch (classifyStmt(s)) {
    case ST_EXP:
        /* 表达式语句：值不被需要（place = NULL），但**照常翻译** —— 里面有副作用。 */
        return translateExp(tail, s->child[0], NULL);

    case ST_COMPST:
        /* 嵌套块：压一层作用域，块内的同名声明遮蔽外层；离开时把该层登记全部弹掉。
         * 函数体自身的 CompSt 不经过这里 —— 它由 translateExtDef 压过一层。 */
        enterScope();
        tail = translateCompSt(tail, s->child[0]);
        leaveScope();
        return tail;

    case ST_RETURN: {
        /* 先把返回值算进一个临时变量，再 `RETURN 它`。
         * （要求书 4.1.6 样例 2 里 `return 0;` 对应的 `RETURN #0` 是这条的**优化形态**；
         *   把常量直接内联进 RETURN 属 Task 16 起的优化 pass，本任务不做。） */
        Operand t = opVar(newTempName());
        InterCode *c;
        tail = translateExp(tail, s->child[1], t);
        c = newCode(IR_RETURN); c->u.one.x = t;
        return irAppend(tail, c);
    }

    case ST_IF: {
        /* 条件假时跳 l2；真时**落进** l1 ⇒ `LABEL l1` 摆在语句体之前。 */
        char *l1 = newLabelName(), *l2 = newLabelName();
        tail = translateCond(tail, s->child[2], l1, l2);
        tail = appendLabel(tail, l1);
        tail = translateStmt(tail, s->child[4]);
        tail = appendLabel(tail, l2);
        return tail;
    }

    case ST_IFELSE: {
        /* l3 是 then 分支末尾跳过 else 的出口：**没有它两个分支会连成一串都执行**。 */
        char *l1 = newLabelName(), *l2 = newLabelName(), *l3 = newLabelName();
        tail = translateCond(tail, s->child[2], l1, l2);
        tail = appendLabel(tail, l1);
        tail = translateStmt(tail, s->child[4]);
        tail = appendGoto(tail, l3);
        tail = appendLabel(tail, l2);
        tail = translateStmt(tail, s->child[6]);
        tail = appendLabel(tail, l3);
        return tail;
    }

    case ST_WHILE: {
        /* `LABEL l1`（回跳目标）在**条件之前**：每条循环回边都从条件重新判起。
         * 删掉末尾那条 `GOTO l1` 循环就只跑一遍 —— Test/unit/test_translate_prog.c
         * 的判别力自证拿的正是这一条（见 task-10-report.md §6）。 */
        char *l1 = newLabelName(), *l2 = newLabelName(), *l3 = newLabelName();
        tail = appendLabel(tail, l1);
        tail = translateCond(tail, s->child[2], l2, l3);
        tail = appendLabel(tail, l2);
        tail = translateStmt(tail, s->child[4]);
        tail = appendGoto(tail, l1);
        tail = appendLabel(tail, l3);
        return tail;
    }
    }
    /* 六个 kind 都已 return；这一行只为让"所有路径都有返回值"对读者与编译器都显式。 */
    return tail;
}

/* CompSt : LC DefList StmtList RC —— child[0]=LC, 1=DefList, 2=StmtList, 3=RC。
 * 局部定义先于语句翻译：登记顺序（v 号）因此与书写顺序一致。 */
InterCodeList *translateCompSt(InterCodeList *tail, Node *c) {
    if (c == NULL || c->nchild != 4) shapeError("CompSt", c);
    tail = translateDefList(tail, c->child[1]);
    return translateStmtList(tail, c->child[2]);
}

/* VarList : ParamDec COMMA VarList | ParamDec（都是右递归）。
 * ★ **按形参声明顺序**发 PARAM。逆序的是**实参**（ARG），那是 Task 11 的事：
 * 虚拟机 ARG 压栈、PARAM 弹栈（spec §3.4），故 ARG 的顺序必须与 PARAM 相反。
 * 要求书 4.1.6 样例 2 的 `fact(n-1)` 只有一个实参、看不出顺序，**别把这条当"无所谓"**：
 * 一旦这里也逆序，两个错误会互相抵消一部分，症状只在多形参函数上出现。 */
static InterCodeList *translateVarListAsParams(InterCodeList *tail, Node *vl) {
    Node *pd, *vd;                               /* ParamDec : Specifier VarDec */
    const char *name;
    InterCode *c;
    if (vl == NULL || (vl->nchild != 1 && vl->nchild != 3)) shapeError("VarList", vl);
    pd = vl->child[0];
    if (pd == NULL || pd->type != NT_PARAMDEC || pd->nchild != 2) shapeError("ParamDec", pd);
    vd = pd->child[1];
    name = leafIdName(vd);
    /* ★ 形参也要登记类型（Task 13）：body 里的 `x[i]` 要靠它求步长。标量按 int；
     *   数组形参也照实登记维数，并打上 isArrayParam 标记 —— Task 14 起该标记表示
     *   "形参的运行时值本身就是基地址"：arrayElemAddr 用 opVar 不再取 &，
     *   translateArgs 把它再作实参传递时也直接 opVar。 */
    registerDeclType(name, vd, /*isArrayParam=*/1, NULL);
    c = newCode(IR_PARAM);
    c->u.one.x = opVar(declareVarName(name));
    tail = irAppend(tail, c);
    if (vl->nchild == 3) tail = translateVarListAsParams(tail, vl->child[2]);
    return tail;
}

/* ExtDefList : ExtDef ExtDefList | ε —— 右递归（**多函数输入全靠这条递归**：
 * 只翻第一个的话，第二个函数在 IR 里根本不存在，端到端只会看到"结果不对"）。 */
static InterCodeList *translateExtDefList(InterCodeList *tail, Node *l) {
    if (l == NULL) shapeError("ExtDefList", l);
    if (l->nchild == 0) return tail;             /* ε */
    if (l->nchild != 2) shapeError("ExtDefList", l);
    tail = translateExtDef(tail, l->child[0]);
    return translateExtDefList(tail, l->child[1]);
}

/* ExtDef 的**五种**形态靠孩子结构区分（依据 src/syntax.y 的 5 条 ExtDef 产生式）：
 *   [Specifier, FunDec, CompSt]    函数定义 —— ★ 本任务唯一处理的形态
 *   [Specifier, ExtDecList, SEMI]  全局变量 —— 假设 4（无全局变量）保证不出现 ⇒ 跳过
 *   [Specifier, FunDec, SEMI]      函数声明 —— 假设 6（无函数声明）保证不出现 ⇒ 跳过
 *   [Specifier, SEMI]              结构体定义 —— 本实验不涉及 ⇒ 跳过
 *   [error, SEMI]                  错误恢复 —— 假设 7（无语法错误）保证不出现 ⇒ 跳过
 * ★ 这四种"跳过"的形态都是**认得出来的**（不是"未知形状"），故不响亮失败：要求书
 * 假设 4（无全局变量）与假设 6（无函数声明）已把它们排除在本实验的输入之外。
 * **真出现时翻译层不保证语义**（例如全局变量不会被登记、也不会分配存储），属超范围；
 * 这里只保证不把它们误当函数定义翻下去，不声称参考机一定会响亮报错。
 * ★ 但**绝不能**"兜底当函数定义"：那会把 `e->child[2]`（SEMI 终结符或别的结点）当
 * CompSt 翻下去，产出一份看着挺像那么回事的错 IR。故三条判别式一个都不省。
 * 形状上要留神：`Specifier FunDec SEMI` 与 `Specifier FunDec CompSt` **孩子数相同**，
 * `Specifier SEMI` 与 `Specifier ExtDecList SEMI` 前两个孩子同形 ——
 * 只有 `child[1]->type == NT_FUNDEC && child[2]->type == NT_COMPST` 这对合取能定案。 */
InterCodeList *translateExtDef(InterCodeList *tail, Node *e) {
    Node *fd;
    const char *fname;
    if (e == NULL) shapeError("ExtDef", e);
    if (e->nchild != 3) return tail;                     /* [Specifier,SEMI] / [error,SEMI] */
    if (e->child[1] == NULL || e->child[1]->type != NT_FUNDEC) return tail;   /* 全局变量 */
    if (e->child[2] == NULL || e->child[2]->type != NT_COMPST) return tail;   /* 函数声明 */

    fd = e->child[1];                                    /* FunDec : ID LP [VarList] RP */
    if (fd->child[0] == NULL || fd->child[0]->kind != NODE_TOKEN) shapeError("FunDec", fd);
    fname = fd->child[0]->lexeme;

    { InterCode *c = newCode(IR_FUNCTION); c->u.one.x = opFunc(fname);
      tail = irAppend(tail, c); }

    /* ★ 每个函数独立的局部变量映射：语义层按函数作用域放行不同函数中的同名
     *   局部变量。这里压一层作用域（形参与函数体同层，嵌套块再各自压栈），
     *   函数结束时 leaveScope 把本函数的登记全部弹掉。**不清计数器**：vN 继续
     *   全局单调，唯一命名程序的 IR 编号不受影响。 */
    enterScope();

    /* 形参：按声明顺序发 PARAM。`FunDec : ID LP RP`（无参）时 child[2] 是 RP，
     * 故只在 nchild == 4 时走进去，且进去前确认它真是 VarList。 */
    if (fd->nchild == 4) {
        if (fd->child[2] == NULL || fd->child[2]->type != NT_VARLIST) shapeError("FunDec", fd);
        tail = translateVarListAsParams(tail, fd->child[2]);
    } else if (fd->nchild != 3) {
        shapeError("FunDec", fd);
    }

    tail = translateCompSt(tail, e->child[2]);
    leaveScope();
    return tail;
}

/* 翻译层的入口：遍历 Program -> ExtDefList 生成整个程序的 IR。
 * 返回**带哨兵的循环链表**（空程序也是 irList() 而不是 NULL —— 下游一律按循环链表处理，
 * NULL 会让 irAppend/printIR 在多处需要特判）。
 * `translateResetNames()` 放这里（brief 如此）：编号从 1 起，与要求书样例的编号一致；
 * 真实编译流程只调一次，表是空的，循环不做事。
 * ★ **本轮仍无调用方** —— src/main.c 要到 Task 12 才调它。故本任务的判据是
 * Test/unit/test_translate_prog.c（`make prog-test`：真 lexer/parser + 逐字节比对），
 * 不是端到端（端到端此刻只会看到"parser 不产出 IR"）。 */
#if !LAB3_ENABLE_32
/* ── 仅必做构建（LAB3_ENABLE_32=0）的 AST 预扫 ──────────────────────────────
 * 方括号层数：VarDec 是左递归链，沿 child[0] 每下一层对应源码里一个 `[...]`。
 * 不递归进 child[1..] —— 那些是 `LB INT RB` 终结符，不是 VarDec。 */
static int varDecBracketDepth(const Node *n) {
    int d = 0;
    while (n && n->type == NT_VARDEC && n->nchild == 4) {
        d++;
        n = n->child[0];
    }
    return d;
}

/* 通用 AST 预扫。"当前是否在形参里"由**递归参数**传递，不靠全局状态：
 *   · 任意 VarDec 层数 ≥ 2 ⇒ 高维数组变量；
 *   · ParamDec 下的 VarDec 层数 ≥ 1 ⇒ 数组形参。
 * 必须在翻译**之前**判定并整体返回空 IR —— 边翻译边 abort 会在输出文件里
 * 留下半份中间代码。 */
static int programUsesUnsupported32(const Node *n, int inParams) {
    int i;
    if (!n) return 0;
    if (n->type == NT_PARAMDEC) {                 /* ParamDec : Specifier VarDec */
        for (i = 0; i < n->nchild; i++)
            if (programUsesUnsupported32(n->child[i], 1)) return 1;
        return 0;
    }
    if (n->type == NT_VARDEC) {
        int d = varDecBracketDepth(n);
        if (inParams ? (d >= 1) : (d >= 2)) return 1;
    }
    for (i = 0; i < n->nchild; i++)
        if (programUsesUnsupported32(n->child[i], inParams)) return 1;
    return 0;
}
#endif

InterCodeList *translateProgram(Node *root) {
    InterCodeList *t;
    if (root == NULL || root->type != NT_PROGRAM || root->nchild != 1)
        shapeError("Program", root);
#if !LAB3_ENABLE_32
    if (programUsesUnsupported32(root, /*inParams=*/0)) {
        /* 要求书 4.1.7 样例 2 的"不完成 3.2"提示。走 stdout（诊断口径）。 */
        fprintf(stdout, "Cannot translate: Code contains variables of multi-dimensional array type or parameters of array type.\n");
        return irList();
    }
#endif
    t = irList();
    translateResetNames();
    /* ★ 先预扫用户函数名再翻译：参考机的 LABEL/FUNCTION 共用 labelTable，
     *   用户函数 `label1` 与生成标号 `label1` 同名会在装载期被拒。 */
    reserveFunctionNames(root);
    t = translateExtDefList(t, root->child[0]);          /* Program : ExtDefList */
    return t;
}
