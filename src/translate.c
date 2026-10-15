/* AST -> IR。本文件当前只含**形状判别表**与 translateProgram 的骨架；
 * 真正的翻译函数（Exp/Stmt/CompSt/…）在后续任务里逐条加上来。
 *
 * 判别表放在这里而不是散在各翻译函数里：它是整层唯一"读懂语法树结构"的地方，
 * 判错时错的是整棵树的语义，且生成的 IR 往往"看着挺像那么回事"——
 * 端到端（parser + 真机）测不出来。集中一处 + Test/unit/test_expkind.c 才钉得住。 */
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
        return EX_INT;                    /* FLOAT 由假设 1 排除；走到这里说明假设被破坏 */
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
    fprintf(stderr, "internal error: unclassified Exp (nchild=%d) at line %d\n",
            e->nchild, nodeLine(e));
    abort();
}

StmtKind classifyStmt(Node *s) {
    if (s->nchild == 1) {
        if (s->child[0]->type == NT_COMPST) return ST_COMPST;
        goto bad;
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

/* 骨架：翻译层的入口。修完后续任务后，这里会遍历 Program -> ExtDefList 生成 IR。
 * 现在返回空表（哨兵）而不是 NULL —— 下游一律按"带哨兵的循环链表"处理，
 * NULL 会让 irAppend/printIR 在多处需要特判。 */
InterCodeList *translateProgram(Node *root) {
    (void)root;
    return irList();
}
