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

/* 注：`isTok(n, name)`（判某个终结符的名字）在 Task 7 的定义侧**没有调用者**，
 * 而 `-Wall` 的 `-Wunused-function` 会把"定义了但没人用"的 static 函数报成警告
 * （"零 gcc 警告"是验收项），故这里不定义它 —— Task 8 加表达式分析时按需加上。
 * （brief 的代码块里带了它，但它不在 brief 自己列的 Produces 清单里。） */

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

/* ── 声明（Task 8/9 填充）────────────────────────────────── */
static Type analyzeExp(Node *n) { (void)n; return typeUnknown(); }
static void analyzeStmtList(Node *n) { (void)n; }

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
