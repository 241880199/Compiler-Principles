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
 *   Exp 关系/与/或   -> translateCondAsValue：`[place := #0]` + 条件翻译 + `place := #1`
 *   !Exp             -> translateNotAsValue（同上；NOT 的特别之处在 translateCond 的
 *                       目标互换那一行，两条路径共用同一个实现）
 *   条件本体         -> translateCond(tail, e, label_true, label_false)：**短路**，
 *                       两个跳转目标是**继承属性**（函数参数），不做回填（Task 9）
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
 * 变量与临时变量**共用编号空间**（前缀 `t`/`v` 区分，计数器各自独立）：`varNameOf`
 * 产出 "v1","v2"…，`newTempName` 产出 "t1","t2"…。两个前缀不会撞；各自的计数器让
 * "哪个名字是第几个出现的"始终单调，便于人读产物、也便于单测写期望（编号与登记
 * 顺序一一对应）——注意**不是**共用同一个计数器：生成一个临时变量不会消耗一个变量号。
 * 与 src/translate.h 中那句说明对齐。
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

const char *varNameOf(const char *src) {
    int i;
    for (i = 0; i < varMapN; i++)
        if (strcmp(varMap[i].src, src) == 0) return varMap[i].ir;
    /* ★ 定长表，**写入前必须查容量**：溢出会**静默越界写** —— 没有诊断、没有 abort，
     * 还会踩到同 TU 里的其它静态变量（tmpSeq/varSeq 就在旁边），产出一份"看着挺像
     * 那么回事"的错 IR。这与本仓库"响亮失败"的纪律正好相反。课程用例远到不了 1024 个
     * 变量名，但低概率不是"可以静默"的理由，故超限即响亮 abort。 */
    if (varMapN >= (int)(sizeof varMap / sizeof varMap[0])) {
        fprintf(stderr, "internal error: varMap 溢出（>1024 个变量名）\n");
        abort();
    }
    { char buf[32]; snprintf(buf, sizeof buf, "v%d", ++varSeq);
      varMap[varMapN].src = xstrdup(src); varMap[varMapN].ir = xstrdup(buf); varMapN++; }
    return varMap[varMapN - 1].ir;
}

/* 重置三个计数器与变量映射表。**只给单测用**（见 translate.h 的说明）：同一进程里
 * 连着跑多个用例时不清零的话，编号会接着上个用例往下走，期望值算不出来、也没法读。
 *
 * 表里 xstrdup 出来的字符串在这里一并 free —— 不释放的话每个用例泄漏一批，且槽位里
 * 留着上个用例的内容，调试时看变量表会被误导。释放是安全的：调用方（opVar）拿到名字
 * 时会**自己再复制一份**放进 IR，产物里的操作数不指向这里的字符串；真实编译流程只在
 * 进程开头调它一次（或根本不调），那时表是空的，循环不做事。 */
void translateResetNames(void) {
    int i;
    for (i = 0; i < varMapN; i++) {
        free((void *)varMap[i].src);
        free((void *)varMap[i].ir);
    }
    tmpSeq = labelSeq = varSeq = varMapN = 0;
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
 * 关系运算符的文本取自 AST 里 `RELOP` 结点的 **lexeme** —— syntax.y 直接存源码原文
 * （`< <= > >= == !=` 之一）。`irRelopCode` 对表外的文本**响亮 abort**，
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
    if (classifyExp(e) == EX_ID)  return opVar(varNameOf(e->child[0]->lexeme));
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

InterCodeList *translateCond(InterCodeList *tail, Node *e,
                             const char *lt, const char *lf) {
    ExpKind k = classifyExp(e);

    if (k == EX_BINOP && strcmp(e->child[1]->name, "RELOP") == 0) {
        Operand a = operandForCond(e->child[0]);
        Operand b = operandForCond(e->child[2]);
        InterCode *c;
        if (!operandIsSimple(a)) { Operand t = opVar(newTempName());
                                   tail = translateExp(tail, e->child[0], t, NULL); a = t; }
        if (!operandIsSimple(b)) { Operand t = opVar(newTempName());
                                   tail = translateExp(tail, e->child[2], t, NULL); b = t; }
        c = newCode(IR_IF);
        c->u.iff.x = a; c->u.iff.y = b; c->u.iff.relop = irRelopCode(e->child[1]->lexeme);
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
     * （classifyExp 的 bad 标签、notImplemented、irRelopCode 的表外文本）的失败形态一致。
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
        tail = translateExp(tail, e, t1, NULL);
        c = newCode(IR_IF);
        c->u.iff.x = t1; c->u.iff.y = opConst(0);
        c->u.iff.relop = irRelopCode("!="); c->u.iff.z = opLabel(lt);
        tail = irAppend(tail, c);
        return appendGoto(tail, lf);
    }
}

/* ══ 属于后续任务的函数：本任务只落**响亮失败的桩** ══════════════════════════
 *
 * translateExp 的形状要求它调这些函数，但它们的实现分别在 Task 10/11。
 * 桩不能"随便兜一个实现"：那会产出一份看着挺像那么回事的错 IR，端到端测不出来。
 * 故一律**诊断 + abort**（与本文件 classifyExp/classifyStmt 的失败形态一致）。
 * 每个桩在 Task N 落地时，把它替换成真实现即可。
 * （Task 9 已经把自己那两个替换掉了：见上面的 translateCond，以及下面两个
 *   ——它们不再是桩，故不在本节的桩清单里。） */
static void notImplemented(const char *fn, int task) {
    fprintf(stderr, "internal error: %s 属于 Task %d，尚未实现\n", fn, task);
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
