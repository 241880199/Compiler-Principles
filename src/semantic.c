#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "semantic.h"
#include "type.h"
#include "symtab.h"
#include "report.h"
#include "util.h"

static int errorCount = 0;
static Type curFuncReturn = NULL;   /* 当前函数的返回类型（错误 8 用） */
static int  anonCounter = 0;        /* 匿名结构体的隐藏名计数 */

/* ── 报错：统一在这里累加条数 ────────────────────────────── */
static void semErr(int code, int line, const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (reportSemanticError(code, line, buf)) errorCount++;
}

/* 判某个结点是不是名为 name 的**词法单元**。表达式的分派全靠它：18 种产生式里
 * 有 5 种彼此只差"某个子结点是终结符还是表达式"（`LP Exp RP`/`ID LP RP`/`Exp DOT ID`
 * 都是 3 个子结点，`ID LP Args RP`/`Exp LB Exp RB` 都是 4 个），故这个判定必须
 * **只有一处**、名字必须写对。非词法单元（表达式等非终结符结点）一律返回 0 ——
 * 于是它同时承担"child[i] 是不是终结符"这个判据，比各处手写 kind==NODE_TOKEN 不易漏。
 *
 * 注：它由本任务（Task 8）补上 —— Task 7 里没有调用者，定义出来会撞
 * `-Wunused-function` 而违反"零 gcc 警告"（见提交 68035cb 的回填）。 */
static int isTok(const Node *n, const char *name) {
    return n && n->kind == NODE_TOKEN && n->name && strcmp(n->name, name) == 0;
}

/* ── 前置声明 ──────────────────────────────────────────────
 * 下面几个函数互相递归（analyzeSpecifier ↔ analyzeStructSpecifier ↔ analyzeFieldList、
 * analyzeLocalDefList → analyzeDef → analyzeDecList → analyzeDec），而被调者定义在后。
 * **C99 不再允许隐式声明**：漏掉它们不是多几条警告，而是**编译不过**
 * （实测 `error: static declaration of 'analyzeDec' follows non-static declaration`，
 * 且返回指针的函数会被当成返回 int、截断成野指针）。故集中在此声明一次。 */
static Type      analyzeStructSpecifier(const Node *n);
static FieldList analyzeFieldList(const Node *n);
static FieldList analyzeFieldDef(const Node *n, FieldList head);
static void      analyzeDef(const Node *n);
static void      analyzeDecList(const Node *n, Type base);
static void      analyzeDec(const Node *n, Type base);
/* 语句遍历（Task 8 的最小结构下潜，见 analyzeStmt 上方那段 ⚠️）与 CompSt 互相
 * 递归：analyzeCompSt 要遍历函数体的语句、`Stmt : CompSt` 又要回到 analyzeCompSt。 */
static void      analyzeCompSt(const Node *n, int freshScope);

/* ── ExtDef 的四种形态（设计文档 §3.4）─────────────────────
 * 注意 `ExtDef : error SEMI` 的 child[0] 是 bison 的 error 符号，语义值未定义，
 * 可能是垃圾指针，故一律先判空。该分支在本程序里**不可达** —— 有语法错误时
 * hasError() 为真，main 根本不会调用语义分析。防御而已。 */
typedef enum { ED_GLOBAL_VAR, ED_STRUCT_ONLY, ED_FUNC, ED_PROTO, ED_ERROR } ExtDefKind;

static ExtDefKind extdefKind(const Node *n) {
    Node *c0 = n->nchild > 0 ? n->child[0] : NULL;
    Node *c1 = n->nchild > 1 ? n->child[1] : NULL;
    if (n->nchild == 2)
        return (c0 && c0->type == NT_SPECIFIER) ? ED_STRUCT_ONLY : ED_ERROR;
    if (!c1) return ED_ERROR;
    if (c1->type != NT_FUNDEC) return ED_GLOBAL_VAR;
    return (n->nchild > 2 && n->child[2] && n->child[2]->type == NT_COMPST)
             ? ED_FUNC : ED_PROTO;
}

/* ══ 表达式分析（错误 1/2/5/6/7/9/10/11/12/13/14）════════════
 *
 * Exp 有 18 种产生式，而结点上只带非终结符名（§3.4 的取舍），故**只能**靠
 * "子结点个数 + 子结点里词法单元的名字"分派。全项目只此一处做这种分派，
 * 集中在 analyzeExp 里，不散进各分支。
 *
 * 贯穿全文的三条约定：
 *   · 错误码与行号取法见设计文档 §7 的十九条规则表；
 *   · **UNKNOWN 毒化**（决策 8）：名字查不到 ⇒ 该名字的类型记为 UNKNOWN，
 *     `typeEqual(UNKNOWN, 任何)` 为真；各检查点遇到 UNKNOWN 操作数一律静默 ——
 *     这是"只报较本质那个错误"的实现机制；
 *   · 同一处多条候选按遍历顺序**留最先检出的那条**（决策 9），且同行只留一条。
 */

static int checkable(Type t) { return !typeIsUnknown(t); }   /* 毒化开关（决策 8）*/
static int isIntT(Type t)    { return t->kind == TY_BASIC && t->u.basic == BT_INT; }
static int isArith(Type t)   { return t->kind == TY_BASIC; } /* 假设 2：只有 int/float 能算术 */

/* 消息里用的短文本。**消息内容不参与评分**（要求书明说"说明文字的内容没有具体要求"），
 * 但注意 FLOAT/INT 的 lexeme 是**十进制展开值**而非源文本（实验一 lexical.l 的
 * `%f` 展开），所以样例 12 的 `1.5` 会被渲染成 `1.500000`。这不影响判分，
 * 也是设计文档 §11.3 要给官方样例另配 `.pairs` 的原因 —— **不要为了让它显示成
 * `1.5` 去改 lexer**，那会动到实验一已验证的 lex.yy.c 行为。 */
static const char *expText(const Node *e) {
    static char buf[64];
    if (e->nchild == 1 && e->child[0]->kind == NODE_TOKEN) {
        const Node *t = e->child[0];
        if (t->lexeme) return t->lexeme;
        snprintf(buf, sizeof buf, "%s", t->name);
        return buf;
    }
    return "expression";
}

/* 左值判据 —— 实验二指导书给的**语法层**简化（设计文档 §1.4）：
 * 赋值号左边只能是 ID、Exp LB Exp RB、Exp DOT ID。故 `(a) = 1` 也报错误 6。 */
static int isLvalueForm(const Node *e) {
    if (e->kind != NODE_GRAMMAR) return 0;
    if (e->nchild == 1 && isTok(e->child[0], "ID"))  return 1;
    if (e->nchild == 3 && isTok(e->child[1], "DOT")) return 1;
    if (e->nchild == 4 && isTok(e->child[1], "LB"))  return 1;
    return 0;
}

static Type analyzeCall(const Node *n, int hasArgs);
static Type analyzeSubscript(const Node *n);
static Type analyzeFieldAccess(const Node *n);
static Type analyzeAssign(const Node *n);
static Type analyzeBinary(const Node *n);

/* ── 分派兜底守卫（默认关）────────────────────────────────
 * 下面三处"其余情况"的兜底分支在当前文法下**不可达**：没有 `Exp : error` 产生式，
 * 且 main 只在无语法错误时才调语义分析（ExtDef 的 ED_ERROR 分支同理，见上方注释）。
 * 但将来若加入新的 3 子结点 / 4 子结点产生式而忘了在这里加显式分支，兜底会**静默**
 * 把新形态当成二元运算或下标处理 —— 这类缺陷既不改输出也无警告。故留一个调试开关：
 * `-DCMM_DEBUG_DISPATCH` 把"落入兜底"变成响亮失败。**默认必须是关的** —— 判分要求
 * stderr 上不能有任何输出（见 scripts/run_tests.sh 的 stderr 断言）。
 * 这里刻意**不**用 C 标准 `assert`：它在未定义 NDEBUG 时是默认生效的，一旦我对文法的
 * 穷举有疏漏，就会在判分现场 abort 掉整份输出 —— 代价远大于收益。
 *
 * ⚠️ 守卫的**判据必须是"形态不属于已知的那几种"**，不能只写"走到了兜底分支" ——
 * 兜底分支本身是**合法路径**（`Exp PLUS Exp` 就该走 `analyzeBinary`）。实测教训：
 * 第一版只调 `dispatchBug("nchild==3 fallthrough")`，`make CFLAGS=…-DCMM_DEBUG_DISPATCH`
 * 跑 dec08_poison 立刻 abort 在 `1 + j` 上。故下面用 `isBinaryOpTok` / `isTok(c1,"LB")`
 * 把"已知形态"逐一排除。 */
#ifdef CMM_DEBUG_DISPATCH
static void dispatchBug(const char *what) {
    fprintf(stderr, "internal: unexpected Exp shape in analyzeExp: %s\n", what);
    abort();
}
#else
static void dispatchBug(const char *what) { (void)what; }
#endif

/* nchild==3 的二元运算，child[1] 必是这七个运算符终结符之一（词法单元名取自 lexical.l）。
 * 它只服务于上面的调试守卫，故单独一个函数而不是塞进 `if` 的一长串 `&&`。 */
static int isBinaryOpTok(const Node *n) {
    static const char *ops[] = { "AND", "OR", "RELOP", "PLUS", "MINUS", "STAR", "DIV" };
    int i;
    for (i = 0; i < (int)(sizeof ops / sizeof ops[0]); i++)
        if (isTok(n, ops[i])) return 1;
    return 0;
}

/* 分派：先按**子结点个数**，再按子结点里词法单元的名字。
 *   nchild==1  Exp : ID | INT | FLOAT
 *   nchild==2  Exp : MINUS Exp | NOT Exp
 *   nchild==3  Exp : LP Exp RP | ID LP RP | Exp DOT ID | Exp ASSIGNOP Exp
 *                    | Exp {AND,OR,RELOP,PLUS,MINUS,STAR,DIV} Exp
 *   nchild==4  Exp : ID LP Args RP | Exp LB Exp RB               */
static Type analyzeExp(Node *n) {
    const Node *c0 = n->nchild > 0 ? n->child[0] : NULL;
    const Node *c1 = n->nchild > 1 ? n->child[1] : NULL;

    switch (n->nchild) {
    case 1:                                        /* Exp : ID | INT | FLOAT */
        if (isTok(c0, "INT"))   return typeBasic(BT_INT);
        if (isTok(c0, "FLOAT")) return typeBasic(BT_FLOAT);
        {
            SymEntry *e = symLookup(c0->lexeme);   /* Exp : ID */
            if (!e || e->kind == SYM_FUNC) {
                /* 函数名出现在变量位置：要求书的 17 种里没有对应码，
                 * 与"变量在使用时未定义"归为同类，报错误 1。 */
                semErr(1, nodeLine(c0), "Undefined variable \"%s\"", c0->lexeme);
                return typeUnknown();
            }
            return e->type;
        }
    case 2:                                        /* MINUS Exp | NOT Exp */
        if (isTok(c0, "NOT")) {                    /* 假设 2：逻辑非只有 int 能做 */
            Type t = analyzeExp(n->child[1]);
            if (!checkable(t)) return typeUnknown();
            if (!isIntT(t))
                semErr(7, nodeLine(n), "Type mismatched for operands");
            return typeBasic(BT_INT);
        }
        {                                          /* MINUS Exp（取负，属四则运算）*/
            Type t = analyzeExp(n->child[1]);
            if (!checkable(t)) return typeUnknown();
            if (!isArith(t))
                semErr(7, nodeLine(n), "Type mismatched for operands");
            return t;
        }
    case 3:
        if (isTok(c0, "LP"))                       /* LP Exp RP */
            return analyzeExp(n->child[1]);
        if (c0->kind == NODE_TOKEN)                /* ID LP RP */
            return analyzeCall(n, /*hasArgs=*/0);
        if (isTok(c1, "DOT"))                      /* Exp DOT ID */
            return analyzeFieldAccess(n);
        if (isTok(c1, "ASSIGNOP"))                 /* Exp ASSIGNOP Exp */
            return analyzeAssign(n);
        /* 兜底：3 子结点里除掉上面四种形态，只剩二元运算（child[1] 必是运算符终结符）。
         * **新增 3 子结点产生式时必须在这里加显式分支**，否则会静默按算术处理。 */
        if (!isBinaryOpTok(c1))
            dispatchBug("nchild==3 fallthrough to analyzeBinary: child[1] is not an operator");
        return analyzeBinary(n);                   /* 其余二元运算 */
    case 4:
        if (c0->kind == NODE_TOKEN)                /* ID LP Args RP */
            return analyzeCall(n, /*hasArgs=*/1);
        /* 兜底：c0 不是终结符 ⇒ 只剩 `Exp LB Exp RB`（child[1] 必是 `[`）。新增 4 子结点
         * 产生式时同上。 */
        if (!isTok(c1, "LB"))
            dispatchBug("nchild==4 fallthrough to analyzeSubscript: child[1] is not LB");
        return analyzeSubscript(n);                /* Exp LB Exp RB */
    }
    /* 兜底：Exp 只有 1..4 子结点这 18 种产生式（无 `Exp : error`，且 main 只在
     * 无语法错误时才调用本函数），故这一行不可达。留着只是为了 `-Wswitch` 与
     * 防御性返回；真要走到这里，说明分派的前提被破坏了。 */
    dispatchBug("Exp with neither 1, 2, 3 nor 4 children");
    return typeUnknown();
}

/* ── 赋值（错误 6 先于 5，设计文档 §7.1）──────────────────
 * ⚠️ **"报 6 在分析子结点之前"必须显式钉住**：Test/lab2/dec09b_nonmono.cmm 的
 * 判别力依赖这个完整顺序（先 6、再左子结点、再右操作数、最后 5 —— 三段可以报在
 * L → M → L 三个非单调行号上）。若重构成"先分析子结点再判左值性"，那条端到端
 * 用例会悄悄退化成新旧实现都绿。 */
static Type analyzeAssign(const Node *n) {
    Type l, r;
    if (!isLvalueForm(n->child[0]))
        semErr(6, nodeLine(n), "The left-hand side of an assignment must be a variable");
    l = analyzeExp(n->child[0]);
    r = analyzeExp(n->child[2]);
    if (!typeEqual(l, r))
        semErr(5, nodeLine(n), "Type mismatched for assignment");
    return l;                                      /* UNKNOWN 会继续向上传播 */
}

/* ── 二元运算 ─────────────────────────────────────────────
 * 三条规则都来自假设 1/2 与附录 A：
 *   逻辑与/或（AND/OR）：两侧必须 int（假设 2 的"逻辑运算"）
 *   关系（RELOP）：附录 A 把"关系表达式"与"逻辑与/逻辑或"**并列**，故它不是逻辑运算；
 *                  受假设 1 约束 —— 两侧同为 int 或同为 float，不混。结果是 int
 *                  （否则 `if (a<b)` 的条件就不是 int 了）。
 *   四则（PLUS/MINUS/STAR/DIV）：两侧必须同为 int 或同为 float（假设 1、2）。 */
static Type analyzeBinary(const Node *n) {
    const char *o = n->child[1]->name;
    Type l = analyzeExp(n->child[0]);
    Type r = analyzeExp(n->child[2]);
    int logical, relop;

    if (!checkable(l) || !checkable(r)) return typeUnknown();   /* 毒化 */

    logical = strcmp(o, "AND") == 0 || strcmp(o, "OR") == 0;
    relop   = strcmp(o, "RELOP") == 0;

    if (logical) {
        if (!isIntT(l) || !isIntT(r))
            semErr(7, nodeLine(n), "Type mismatched for operands");
        return typeBasic(BT_INT);
    }
    if (relop) {
        if (!isArith(l) || !isArith(r) || l->u.basic != r->u.basic)
            semErr(7, nodeLine(n), "Type mismatched for operands");
        return typeBasic(BT_INT);
    }
    /* 四则 */
    if (!isArith(l) || !isArith(r) || l->u.basic != r->u.basic)
        semErr(7, nodeLine(n), "Type mismatched for operands");
    return isArith(l) ? l : typeUnknown();
}

/* ── 下标（错误 10 先于 12 且短路，设计文档 §7.1）────────── */
static Type analyzeSubscript(const Node *n) {
    Type base = analyzeExp(n->child[0]);
    Type idx;

    if (!checkable(base)) return typeUnknown();

    if (base->kind != TY_ARRAY) {
        semErr(10, nodeLine(n->child[0]), "\"%s\" is not an array", expText(n->child[0]));
        return typeUnknown();                      /* 短路：不再分析下标、不报 12 */
    }
    idx = analyzeExp(n->child[2]);
    if (!checkable(idx)) return typeUnknown();
    /* **决策 6 的开关点**：下标必须是 int。放数组（如 a[b]、b 是数组）也报 12，
     * 因为错误 12 是下标的专项规则、错误 7 是通则，专项优先。 */
    if (!isIntT(idx)) {
        semErr(12, nodeLine(n->child[2]), "\"%s\" is not an integer", expText(n->child[2]));
        return typeUnknown();
    }
    return base->u.array.elem;
}

/* ── 结构体访域（错误 13 先于 14）───────────────────────── */
static Type analyzeFieldAccess(const Node *n) {
    Type base = analyzeExp(n->child[0]);
    const Node *idNode = n->child[2];
    FieldList f;

    if (!checkable(base)) return typeUnknown();
    if (base->kind != TY_STRUCTURE) {
        semErr(13, nodeLine(n), "Illegal use of \".\"");
        return typeUnknown();
    }
    f = typeField(base, idNode->lexeme);
    if (!f) {
        semErr(14, nodeLine(n), "Non-existent field \"%s\"", idNode->lexeme);
        return typeUnknown();
    }
    return f->type;
}

/* ── 函数调用（错误 11 先于 2 先于 9，设计文档 §7.1）──────
 * 收集实参类型。Args : Exp | Exp COMMA Args —— 左深链表，最左的实参在最外层。 */
static Type expType(const Node *e) { return analyzeExp((Node *)e); }

static Type *collectArgTypes(const Node *args, int *nOut) {
    int cap = 4, n = 0;
    Type *arr = xmalloc(sizeof(Type) * (size_t)cap);
    while (args) {                                 /* Args : Exp | Exp COMMA Args */
        if (n == cap) {
            Type *bigger = xmalloc(sizeof(Type) * (size_t)cap * 2);
            memcpy(bigger, arr, sizeof(Type) * (size_t)n);
            free(arr);
            arr = bigger;
            cap *= 2;
        }
        arr[n++] = expType(args->child[0]);
        if (args->nchild == 1) break;
        args = args->child[2];
    }
    *nOut = n;
    return arr;
}

/* ── 累加式拼接（消息构造专用）────────────────────────────
 * `snprintf` 的返回值是**本该写多长**，不是实际写进去多少。所以累加量 off 一旦
 * 越过 n（实参/形参很多、或某个类型名很长时），后面那句 `(size_t)(n - off)` 就把
 * 负的 int 转成了巨大的无符号数，`buf + off` 于是写到栈上别处 —— 缓冲区越界写。
 * 两处消息构造（形参侧 sigString、实参侧 analyzeCall）都走这里，越界只在**这一处**收口。
 *
 * 收口规则：off 一旦达到 `n - 1`（此时 buf+off 只剩 1 字节，任何 snprintf 都只能
 * 写一个 NUL），后续拼接直接跳过。正常大小的输入 off 恒小于 n-1，故**消息逐字节不变**；
 * 边界输入下得到的是"截断且 NUL 收尾"的串，不会越界。 */
static int appendf(char *buf, int n, int off, const char *fmt, ...) {
    va_list ap;
    int r;
    if (off >= n - 1) return n - 1;
    va_start(ap, fmt);
    r = vsnprintf(buf + off, (size_t)(n - off), fmt, ap);
    va_end(ap);
    off += r;
    return off > n - 1 ? n - 1 : off;
}

/* "func(int, float)" 形式的签名文本（形参侧）。 */
static void sigString(const SymEntry *e, const char *name, char *buf, int n) {
    char one[128];
    int i, off = 0;
    off = appendf(buf, n, off, "%s(", name);
    for (i = 0; i < e->nparams; i++) {
        typeToString(e->params[i], one, sizeof one);
        off = appendf(buf, n, off, "%s%s", i ? ", " : "", one);
    }
    appendf(buf, n, off, ")");
}

static Type analyzeCall(const Node *n, int hasArgs) {
    const Node *idNode = n->child[0];
    const char *name = idNode->lexeme;
    SymEntry *e = symLookup(name);
    Type *args = NULL;
    int nargs = 0, i, bad = 0;

    /* ⚠️ **两条名字检查必须在收集实参之前**（设计文档 §7.1 的调用行：11 → 2 → 9，命中即短路）。
     * 放在后面会报**较不本质**的那个错：`inc(j)`（`inc` 与 `j` 都未定义）会先掉进实参
     * 收集、报出 `j` 的错误 1，而错误 2 因为同一行去重被抑制 —— 于是这个"两者都未定义"
     * 的输入只剩一条错误 1，与"只报较本质那个错误"（§5.3）相反。顺序对调后：
     * 名字不存在就报 2 并短路，**根本不去碰实参**（这同时也省掉了那次分配）。
     * 错误 9 的路径不受影响 —— 它是最后一个检查，两者都用得着实参表。 */
    if (!e) {
        semErr(2, nodeLine(idNode), "Undefined function \"%s\"", name);
        return typeUnknown();
    }
    if (e->kind != SYM_FUNC) {
        /* 决策：名字存在但不是函数 ⇒ 错误 11（要求书样例 11 的 `i(10)` 只该出 11）。
         * 在查"是否定义"之前 —— 普通变量本来就不需要"定义"，先判 2 会报错码。 */
        semErr(11, nodeLine(idNode), "\"%s\" is not a function", name);
        return typeUnknown();
    }

    if (hasArgs) args = collectArgTypes(n->child[2], &nargs);

    if (nargs != e->nparams) {
        bad = 1;
    } else {
        for (i = 0; i < nargs; i++) {
            /* 毒化（决策 8）：实参类型 UNKNOWN ⇒ 无法判定，静默 —— 但**返回 UNKNOWN**。
             * 返回 `e->type` 会让调用表达式"看起来"有一个确定的类型，UNKNOWN 就此
             * 止步，下游检查不再静默：`float x; x =` 换行 `f(k);`（`k` 未定义）会在
             * 错误 1 之外**多报一条错误 5**（§5.3："含 UNKNOWN 的表达式，其类型继续
             * 传播 UNKNOWN"）。
             * ⚠️ 别顺手把下面 `if (bad)` 里那个 `return e->type` 也改掉 —— 那个是**故意**
             * 的（§5.3 的例外，`dec09b_nonmono.cmm` 靠它同时报 9 与 5）。 */
            if (!checkable(args[i])) return typeUnknown();
            if (!typeEqual(args[i], e->params[i])) bad = 1;
        }
    }

    if (bad) {
        /* ⚠️ **返回的是 e->type（函数返回类型），不是 UNKNOWN** —— 设计文档明确依赖
         * 这一点：它让"实参不匹配 + 赋值类型也不匹配"能同时报出两条
         * （`dec09b_nonmono.cmm` 那个非单调行号用例正是靠它）。别改成 UNKNOWN。 */
        char s1[256], s2[256];
        int off = 0;
        sigString(e, name, s1, sizeof s1);
        off = appendf(s2, sizeof s2, off, "(");
        for (i = 0; i < nargs; i++) {
            char one[128];
            typeToString(args[i], one, sizeof one);
            off = appendf(s2, sizeof s2, off, "%s%s", i ? ", " : "", one);
        }
        appendf(s2, sizeof s2, off, ")");
        semErr(9, nodeLine(idNode),
               "Function \"%s\" is not applicable for arguments \"%s\"", s1, s2);
    }
    return e->type;
}

/* ══ 语句遍历与**语句层检查**（结构遍历 Task 8 加；两处检查 Task 9 加）═══
 *
 * ⚠️ **这一节是 Task 8 对计划边界的一处调整，已上报（task-8-report.md）。**
 * 计划把 `analyzeStmtList` 整个留给 Task 9（"语句与作用域"），但 Task 8 要落地的
 * 11 个官方必做样例**每一条的错误都长在语句里的表达式上**（`j = i + 1;`、
 * `inc(i);`、`i[1.5] = 10;`、`i.x;`、`if (p.n == 3.7)`……），而表达式除了
 * 全局/局部定义里的初始化式之外**只能经语句到达**。实测证据：语句层留空壳时
 * `int main(){ int i = 0; j = i + 1; }` 一条输出都没有，Step 4 的 11 条样例会
 * 全部以"无期望输出"的形式静默失败。故 Task 8 先补上了一版**只做结构下潜、
 * 不做任何语句层检查**的遍历，让表达式可达。
 *
 * **Task 8 留的两个空缺已被 Task 9 补上**（就是下面 `checkCondition` 与 RETURN
 * 分支里的那两句）：
 *   · `RETURN Exp SEMI` → 错误 8（返回类型与函数返回类型不匹配）；
 *   · `IF`/`WHILE` 的条件 → 决策 2（条件必须是 `int`，报错误 7）。
 * 结构下潜本身（`Stmt : CompSt` 压栈、`Stmt : Exp SEMI` 只分析表达式）没有改动。 */

/* **决策 2 的开关点**：if/while 的条件必须是 int（假设 2）。
 * 17 种错误里没有"条件类型错"，取错误 7（操作数类型与操作符不匹配）——
 * 把 if/while 看作要求 int 操作数的运算。专属用例 `dec02_condnotint.cmm`。
 *
 * `checkable` 那半条不能省（决策 8 / §5.3 的 UNKNOWN 毒化）：`if (undefined_var)`
 * 只应报上游那条错误 1，不该在这里再报一条条件类型错。 */
static void checkCondition(const Node *exp) {
    Type t = analyzeExp((Node *)exp);
    if (checkable(t) && !isIntT(t))
        semErr(7, nodeLine(exp), "The condition of if/while must be an int");
}

/* 语句分派：只看 child[0] 是不是某个词法单元（再看子结点类型）。
 * 7 种 Stmt 产生式在此穷举：
 *   Stmt : CompSt | Exp SEMI | RETURN Exp SEMI | IF LP Exp RP Stmt
 *        | IF LP Exp RP Stmt ELSE Stmt | WHILE LP Exp RP Stmt | error SEMI
 * `Stmt : error SEMI` 的 child[0] 是 bison 的 error 符号，语义值未定义、可能是
 * 垃圾指针（与 extdefKind 同一处境），故先判空。该分支在本程序里**不可达** ——
 * 有语法错误时 hasError() 为真，main 根本不会调用语义分析（见 src/main.c）。
 * 防御而已。 */
static void analyzeStmt(const Node *n) {
    const Node *c0 = n->nchild > 0 ? n->child[0] : NULL;
    if (!c0) return;

    if (c0->kind == NODE_TOKEN) {
        const char *nm = c0->name;

        if (strcmp(nm, "RETURN") == 0) {                /* Stmt : RETURN Exp SEMI */
            /* 错误 8：行号取 RETURN（设计文档 §7 第 8 条）。
             * 两条守卫都不省：`curFuncReturn` 为 NULL 时（不可能出现在此处，
             * 但防御）不做比较；`checkable` 那半条是 UNKNOWN 毒化 ——
             * `float f(){ return undefined_var; }` 只该报错误 1。 */
            Type t = analyzeExp((Node *)n->child[1]);
            if (curFuncReturn && checkable(t) && !typeEqual(t, curFuncReturn))
                semErr(8, nodeLine(n), "Type mismatched for return");
            return;
        }
        if (strcmp(nm, "IF") == 0 || strcmp(nm, "WHILE") == 0) {
            /* Stmt : IF LP Exp RP Stmt [ELSE Stmt] | WHILE LP Exp RP Stmt */
            checkCondition(n->child[2]);
            analyzeStmt(n->child[4]);
            if (n->nchild == 7) analyzeStmt(n->child[6]);
            return;
        }
        return;                                         /* Stmt : error SEMI（不可达）*/
    }

    /* `Stmt : CompSt` —— **决策 3 的另一半，就是下面这个 `1`**：作为语句出现的
     * `{...}` 是**新作用域**（选做 2.2），而函数体的 `{...}` 不是（它的作用域已由
     * analyzeExtDef 压好、形参与之同层，故那边传 0）。两处 freshScope 就是全部开关，
     * 互换任一处都会让对应用例变红（见 plans 的 Task 11 变异表第 8 行）。 */
    if (c0->type == NT_COMPST) { analyzeCompSt(c0, /*freshScope=*/1); return; }
    if (c0->type == NT_EXP)    { (void)analyzeExp((Node *)c0); return; }   /* Stmt : Exp SEMI */
}

static void analyzeStmtList(const Node *n) {        /* StmtList : Stmt StmtList | ε */
    if (n->nchild == 0) return;
    analyzeStmt(n->child[0]);
    analyzeStmtList(n->child[1]);
}

/* ── VarDec ──────────────────────────────────────────────── */

/* 取变量名：沿 child[0] 下行到最左的 ID。 */
static const char *varDecName(const Node *v) {
    while (v->nchild == 4) v = v->child[0];   /* VarDec : VarDec LB INT RB */
    return v->child[0]->lexeme;               /* VarDec : ID */
}

/* 数组维度**最内层先收集**（沿 child[0] 下行），再交给 typeArrayFromDims 左折叠。
 * 顺序反了会让 int a[10][2] 变成 array(2, array(10, int))，而 17 种检查
 * 一条都发现不了 —— 见设计文档 §5.1 与 Task 2 的单测。 */
static Type analyzeVarDec(const Node *n, Type base) {
    int dims[64];
    int nd = 0;
    const Node *v = n;
    while (v->nchild == 4) {
        if (nd >= 64) { fprintf(stderr, "internal error: too many array dimensions\n"); abort(); }
        dims[nd++] = atoi(v->child[2]->lexeme);
        v = v->child[0];
    }
    return typeArrayFromDims(base, dims, nd);
}

/* ── Specifier / StructSpecifier ─────────────────────────── */

static Type analyzeSpecifier(const Node *n) {     /* Specifier : TYPE | StructSpecifier */
    const Node *c = n->child[0];
    if (c->kind == NODE_TOKEN)                    /* Specifier : TYPE */
        return typeBasic(strcmp(c->lexeme, "float") == 0 ? BT_FLOAT : BT_INT);
    return analyzeStructSpecifier(c);
}

static Type analyzeStructSpecifier(const Node *n) {
    /* StructSpecifier : STRUCT Tag —— 引用已有结构体 */
    if (n->nchild == 2) {
        const Node *tag = n->child[1]->child[0];      /* Tag : ID */
        SymEntry *e = symLookup(tag->lexeme);
        if (!e || e->kind != SYM_STRUCT) {
            /* 错误 17。**决策 5 的开关点**：`struct Undefined;`（没有变量）也走这里，
             * 因为"看到 STRUCT Tag 就查表、查不到就报"是自然写法。 */
            semErr(17, nodeLine(n->child[0]), "Undefined structure \"%s\"", tag->lexeme);
            return typeUnknown();
        }
        return e->type;
    }

    /* StructSpecifier : STRUCT OptTag LC DefList RC —— 定义 */
    {
        const Node *opt = n->child[1];                /* OptTag : ID | ε */
        char name[64];
        FieldList fields;
        Type t;

        if (opt->nchild == 1)
            snprintf(name, sizeof name, "%s", opt->child[0]->lexeme);
        else                                          /* 匿名：独有的隐藏名（note 3）*/
            snprintf(name, sizeof name, "#anon_%d", ++anonCounter);

        /* **先分析域、后登记结构体名** —— 于是 `struct A { struct A x; }` 里的
         * `struct A` 查不到（A 尚未入表），报错误 17，与 C 的不完整类型一致。 */
        fields = analyzeFieldList(n->child[3]);
        t = typeStruct(name, fields);

        if (opt->nchild == 1) {
            int code;
            /* **决策 4 的开关点**：注册在**当前作用域**，不开特例。 */
            symEnter(name, SYM_STRUCT, t, nodeLine(n->child[0]), &code);
            if (code)
                semErr(code, nodeLine(n->child[0]), "Duplicated name \"%s\"", name);
        }
        return t;
    }
}

/* ── 结构体成员表（与局部定义表是两套规则）────────────────── */

/* DefList : Def DefList | ε。按**从左到右**的顺序逐个 Def 收集（顺序即域序），
 * 并把**已收集的域表**一路带下去 —— 它是重名检查的唯一依据。 */
static FieldList analyzeFieldList(const Node *n) {
    FieldList head = NULL;
    while (n->nchild != 0) {
        head = analyzeFieldDef(n->child[0], head);
        n = n->child[1];
    }
    return head;
}

/* Def : Specifier DecList SEMI —— 把本 Def 的域接到已收集的 `head` 之后并返回表头。
 *
 * ⚠️ **重名检查必须针对整个结构体已收集的域**（`head` 含前面所有 Def 的域，而不只是
 * 本 Def 的）。要求书必做样例 15 的 `float x, y;` 与 `int x;` 分属**两个** Def：
 * 只看本 Def 就一条错误都报不出来（brief 的原始写法如此，实测样例 15 完全无输出）。 */
static FieldList analyzeFieldDef(const Node *n, FieldList head) {
    Type base = analyzeSpecifier(n->child[0]);
    FieldList tail = head;
    const Node *dl = n->child[1];

    while (tail && tail->tail) tail = tail->tail;     /* 追加点 */

    for (;;) {                                       /* DecList : Dec | Dec COMMA DecList */
        const Node *dec = dl->child[0];
        Type t = analyzeVarDec(dec->child[0], base);
        const char *name = varDecName(dec->child[0]);
        FieldList p;
        int dup = 0;

        if (dec->nchild == 3)                        /* Dec : VarDec ASSIGNOP Exp */
            semErr(15, nodeLine(dec), "Initialization of field \"%s\"", name);

        /* 域重名只在**本结构体已收集的域**里查 —— 假设 7 保证不跨结构体重名 */
        for (p = head; p; p = p->tail)
            if (strcmp(p->name, name) == 0) dup = 1;

        if (dup) {
            semErr(15, nodeLine(dec), "Redefined field \"%s\"", name);
        } else {
            FieldList f = xmalloc(sizeof *f);
            f->name = xstrdup(name);
            f->type = t;
            f->node = (Node *)dec->child[0];
            f->tail = NULL;
            if (tail) tail->tail = f; else head = f;
            tail = f;
        }

        if (dl->nchild == 1) break;
        dl = dl->child[2];
    }
    return head;
}

/* ── 局部定义表 ──────────────────────────────────────────── */

static void analyzeLocalDefList(const Node *n) {     /* DefList */
    if (n->nchild == 0) return;
    analyzeDef(n->child[0]);
    analyzeLocalDefList(n->child[1]);
}

static void analyzeDef(const Node *n) {              /* Def : Specifier DecList SEMI */
    Type base = analyzeSpecifier(n->child[0]);
    analyzeDecList(n->child[1], base);
}

static void analyzeDecList(const Node *n, Type base) {   /* DecList : Dec | Dec COMMA DecList */
    analyzeDec(n->child[0], base);
    if (n->nchild == 3) analyzeDecList(n->child[2], base);
}

static void analyzeDec(const Node *n, Type base) {   /* Dec : VarDec | VarDec ASSIGNOP Exp */
    Type t = analyzeVarDec(n->child[0], base);
    const char *name = varDecName(n->child[0]);
    int code;

    symEnter(name, SYM_VAR, t, nodeLine(n->child[0]), &code);
    if (code)
        semErr(code, nodeLine(n->child[0]), "Redefined variable \"%s\"", name);

    if (n->nchild == 3) {
        Type rhs = analyzeExp(n->child[2]);
        /* 名字冲突时不比类型：此时 symEnter 返回的是**旧**变量，
         * 拿新声明的类型去比会报出一条指向错误对象的错。 */
        if (!code && !typeEqual(t, rhs))
            semErr(5, nodeLine(n), "Type mismatched for assignment");
    }
}

/* ── 函数头 ──────────────────────────────────────────────── */

typedef struct { const char *name; Type type; Node *decl; } ParamInfo;

/* 收集形参名与类型。**必须一次收集完、不要在登记时再分析一遍** ——
 * analyzeSpecifier 有副作用（结构体定义会入表），分析两遍会让
 * `int f(struct A {int x;} p)` 把 struct A 登记两次、多报一条错误 16。 */
static ParamInfo *collectParams(const Node *varList, int *nOut) {
    int cap = 4, n = 0;
    ParamInfo *arr = xmalloc(sizeof(ParamInfo) * (size_t)cap);
    while (varList) {                                /* VarList : ParamDec | ParamDec COMMA VarList */
        const Node *pd = varList->child[0];          /* ParamDec : Specifier VarDec */
        Type base = analyzeSpecifier(pd->child[0]);
        if (n == cap) {
            ParamInfo *bigger = xmalloc(sizeof(ParamInfo) * (size_t)cap * 2);
            memcpy(bigger, arr, sizeof(ParamInfo) * (size_t)n);
            free(arr);
            arr = bigger;
            cap *= 2;
        }
        arr[n].name = varDecName(pd->child[1]);
        arr[n].type = analyzeVarDec(pd->child[1], base);
        arr[n].decl = (Node *)pd->child[1];
        n++;
        if (varList->nchild == 1) break;
        varList = varList->child[2];
    }
    *nOut = n;
    return arr;
}

static void insertParams(const ParamInfo *ps, int n) {
    int i;
    for (i = 0; i < n; i++) {                        /* ps == NULL / n == 0 时是空操作 */
        int code;
        symEnter(ps[i].name, SYM_VAR, ps[i].type, nodeLine(ps[i].decl), &code);
        if (code)
            semErr(code, nodeLine(ps[i].decl), "Redefined variable \"%s\"", ps[i].name);
    }
}

static int sameSignature(const SymEntry *e, Type retType, const ParamInfo *ps, int n) {
    int i;
    if (!typeEqual(e->type, retType)) return 0;
    if (e->nparams != n) return 0;
    for (i = 0; i < n; i++)
        if (!typeEqual(e->params[i], ps[i].type)) return 0;
    return 1;
}

/* 登记函数名与签名，并**返回该在函数作用域里登记的形参表**（含被拒的定义，
 * 见下方 ⚠️：*nOut 恒为 nparams、返回值恒为 ps）。形参为什么不在本函数里登记、
 * 而要交给调用方，见 analyzeCompSt 上方那段 ⚠️。
 * 调用前提：curDepth 必须是**全局层**（函数名属于全局作用域，假设 6）。 */
static ParamInfo *analyzeFunDec(const Node *n, Type retType, int isDefinition, int *nOut) {
    const Node *idNode = n->child[0];
    const char *name = idNode->lexeme;
    int nparams = 0;
    ParamInfo *ps = NULL;
    SymEntry *e;
    int i;

    if (n->nchild == 4) ps = collectParams(n->child[2], &nparams);
    *nOut = nparams;

    /* ⚠️ **失败路径也必须原样返回 ps 与 nparams —— 不能改成返回 NULL / 置 0。**
     * 因为调用方（ED_FUNC 分支）**无论登记成功与否都会继续分析函数体**：
     * 所以被拒的那种定义（重复定义、签名冲突）的函数体照样要跑，
     * 而它的形参必须已经在作用域里。否则形参不在作用域 →
     * 从 Task 8 起，函数体里每个形参的使用都会多报一条错误 1。
     * 这个 bug 在 Task 7 里**几乎看不出来**（`analyzeExp` 是空壳，且必做样例 4 的
     * 第二次定义恰好没有形参），会一路潜伏到 Task 8/9 才炸 —— 已由评审指出。
     * Task 7 里唯一能钉住它的是 `Test/lab2/dec14_rejecteddef_params.cmm`：
     * 被拒定义的函数体里有一条 `int i;` 与形参 `i` 重名 —— 形参不在作用域时
     * 那句 `symEnter` 不会冲突，于是错误 3（第 8 行）整条消失。 */

    /* **这里先预检、只在 !e 时才调 symEnter** —— 这个顺序是有意的：`symEnter`
     * 对 SYM_FUNC 冲突本身就会返回 4（Task 4 的单测断言了这一点），若绕过预检
     * 直接调它，会多报一条错误。 */
    e = symLookupLocal(name);

    /* 名字被变量或结构体占了：按**后定义者的种类**报码（决策 1，规则在 symtab.c）*/
    if (e && e->kind != SYM_FUNC) {
        semErr(symConflictCode(SYM_FUNC), nodeLine(idNode),
               "Redefined function \"%s\"", name);
        return ps;
    }

    if (!e) {                                 /* 首次见到这个名字 */
        int code;
        e = symEnter(name, SYM_FUNC, retType, nodeLine(idNode), &code);
        e->params   = xmalloc(sizeof(Type) * (size_t)(nparams ? nparams : 1));
        for (i = 0; i < nparams; i++) e->params[i] = ps[i].type;
        e->nparams  = nparams;
        e->declLine = nodeLine(idNode);
        if (isDefinition) e->defined = 1;
        return ps;
    }

    /* **两次定义 → 错误 4。这一判必须在签名比较之前**：要求书必做样例 4 的两次
     * 定义签名就不一致（`int func(int i)` 与 `int func()`），而官方期望输出是
     * "Error type 4 at Line 6"、**不是 19**；设计文档 §7 的 4 号一行也写明
     * "与声明无关，专指两次定义"。反过来的顺序（先比签名）会让样例 4 报出 19。 */
    if (isDefinition && e->defined) {
        semErr(4, nodeLine(idNode), "Redefined function \"%s\"", name);
        return ps;
    }

    /* 其余不一致 = 声明与声明、或声明与定义冲突 → 错误 19（要求书 2.1(b)）*/
    if (!sameSignature(e, retType, ps, nparams)) {
        semErr(19, nodeLine(idNode), "Inconsistent declaration of function \"%s\"", name);
        return ps;
    }
    if (isDefinition) e->defined = 1;         /* 声明在前、定义在后且一致 */
    return ps;                                /* 一致地重复声明 → 合法，不报错（选做 2.1）*/
}

/* ── CompSt / ExtDef / 顶层 ──────────────────────────────── */

/* freshScope=0 用于函数体：作用域已由 analyzeExtDef 压好（**决策 3 的开关点** ——
 * 形参与函数体同作用域，故 `int f(int i){int i;}` 报错误 3，与 C 一致）。
 *
 * ⚠️ **难点是"压函数作用域"这一步的位置**（ED_FUNC 分支）：它必须在
 * `analyzeFunDec`（登记函数名 → 全局作用域，假设 6）**之后**、`insertParams`
 * （登记形参 → 与函数体同层，决策 3）**之前**。
 * 若照"压了作用域再登记函数名"来写，函数名会落在**它自己那层**上，
 * `scopeExit()` 一执行就被摘掉 —— 后果不是少报一条：错误 4 变成**死代码**
 * （实测"同一个函数原样定义两次"一条错误都不报），且 Task 8 起"调用先前定义过的
 * 函数"会因查不到而误报错误 2。故本函数返回"该登记的形参表"，由调用方在压好
 * 作用域后插入。 */
static void analyzeCompSt(const Node *n, int freshScope) {
    if (freshScope) scopeEnter();
    analyzeLocalDefList(n->child[1]);
    analyzeStmtList(n->child[2]);
    if (freshScope) scopeExit();
}

static void analyzeGlobalDecList(const Node *n, Type base) {   /* ExtDecList */
    const Node *v = n->child[0];
    Type t = analyzeVarDec(v, base);
    const char *name = varDecName(v);
    int code;
    symEnter(name, SYM_VAR, t, nodeLine(v), &code);
    if (code) semErr(code, nodeLine(v), "Redefined variable \"%s\"", name);
    if (n->nchild == 3) analyzeGlobalDecList(n->child[2], base);
}

static void analyzeExtDef(const Node *n) {
    Type base, ret;
    switch (extdefKind(n)) {
    case ED_GLOBAL_VAR:                      /* Specifier ExtDecList SEMI */
        base = analyzeSpecifier(n->child[0]);
        analyzeGlobalDecList(n->child[1], base);
        break;
    case ED_STRUCT_ONLY:                     /* Specifier SEMI —— `int;` 也走这里，不报错 */
        (void)analyzeSpecifier(n->child[0]);
        break;
    case ED_FUNC: {                          /* Specifier FunDec CompSt */
        int nparams = 0;
        ParamInfo *ps;
        ret = analyzeSpecifier(n->child[0]);
        /* ① 函数名与签名 → **全局作用域**（假设 6），故此时**还没**压函数作用域，
         *    登记完之后才压。② 形参在压好作用域后才登记（决策 3，见 analyzeCompSt
         *    上方那段 ⚠️：顺序反了会让错误 4 变成死代码）。 */
        ps = analyzeFunDec(n->child[1], ret, /*isDefinition=*/1, &nparams);
        scopeEnter();                        /* 函数作用域：形参与函数体同层 */
        curFuncReturn = ret;
        insertParams(ps, nparams);
        analyzeCompSt(n->child[2], /*freshScope=*/0);
        curFuncReturn = NULL;
        scopeExit();
        break;
    }
    case ED_PROTO: {                         /* Specifier FunDec SEMI —— 选做 2.1 */
        int nparams = 0;
        ret = analyzeSpecifier(n->child[0]);
        /* 声明**没有**函数体，故不开作用域、也不登记形参名（只比签名） */
        analyzeFunDec(n->child[1], ret, /*isDefinition=*/0, &nparams);
        break;
    }
    case ED_ERROR:
        break;
    }
}

static void analyzeExtDefList(const Node *n) {   /* ExtDefList : ExtDef ExtDefList | ε */
    if (n->nchild == 0) return;
    analyzeExtDef(n->child[0]);
    analyzeExtDefList(n->child[1]);
}

/* 错误 18：声明了但没定义。行号取**首次声明/定义**所在行（要求书选做样例 2 → 行 6）。 */
static void analyzeDeclaredNotDefined(void) {
    SymEntry *e;
    /* **这是全项目唯一沿 `scopeNext` 遍历的地方**（Task 4 评审指出）：设计文档
     * 说符号表的接口与数据结构无关，对 scopeEnter/scopeExit/symEnter/symLookup
     * 四个函数确实成立（换"每层一条链表 + 线性查找"时调用方不改），但这个循环
     * 是直接**贴在同层链表上**写的，换结构就得改这里。若将来真换了，加一个
     * `symNextGlobal(SymEntry *)` 之类的访问器即可恢复"调用方不改"。
     * 另：`symGlobalList` 的"按插入逆序"是被本循环依赖的性质（错误 18 的输出顺序
     * 由此确定），而它目前只被一条 `!= NULL` 的弱断言覆盖 —— 见账本里的 Minor。 */
    for (e = symGlobalList(); e; e = e->scopeNext)
        if (e->kind == SYM_FUNC && !e->defined)
            semErr(18, e->declLine, "Undefined function \"%s\"", e->name);
}

int semanticAnalyze(Node *root) {
    errorCount = 0;
    curFuncReturn = NULL;
    anonCounter = 0;
    symInit();
    if (!root) return 0;
    analyzeExtDefList(root->child[0]);       /* Program : ExtDefList */
    analyzeDeclaredNotDefined();
    return errorCount;
}
