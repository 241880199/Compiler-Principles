#ifndef TRANSLATE_H
#define TRANSLATE_H

#include "tree.h"
#include "ir.h"

/* AST -> IR。见 docs/plan.md Task 7。
 *
 * 本层的一条纪律：**"读懂语法树结构"这件事只在这里做一次**（设计文档 §3.4）。
 * 下面两个判别函数是整层唯一按 nchild / 孩子名字分支的地方；下游每个翻译函数
 * 都先 switch 它们的返回值，再按已知形状直接取 child[i]，不再各自重新猜测。
 * 理由：产生式形状的知识散落在十几处时，任何一处判错都会生成"看着像模像样"的
 * 错误 IR —— 端到端测不出来。集中一处 + 单测（Test/unit/test_expkind.c）才钉得住。 */

/* ── Exp 的形状 ───────────────────────────────────────────────────────────
 * 判据来自 main 分支 syntax.y 的 18 条 Exp 产生式（见 task-7-brief.md 的表）。 */
typedef enum { EX_INT, EX_ID, EX_BINOP, EX_UNARY, EX_PAREN,
               EX_CALL0, EX_CALLN, EX_INDEX, EX_FIELD } ExpKind;
ExpKind classifyExp(Node *e);

/* ── Stmt 的形状 ──────────────────────────────────────────────────────────
 * ST_EXP 是 `Exp SEMI`；`error SEMI` 与它同形但 child[0] 不是语法结点，
 * 分类器对此**响亮失败**（要求书假设 7 保证输入无语法错误，故不该遇到）。
 * 注意 ST_IF 与 ST_WHILE 的孩子数相同（都是 5），只能靠 child[0] 区分。 */
typedef enum { ST_EXP, ST_COMPST, ST_RETURN, ST_IF, ST_IFELSE, ST_WHILE } StmtKind;
StmtKind classifyStmt(Node *s);

/* 翻译整棵语法树，返回 IR 链表（带哨兵的循环链表，见 ir.h）。
 * 本轮尚未接上驱动：src/main.c 要到 Task 12 才调用它。 */
InterCodeList *translateProgram(Node *root);

/* ── 名字发生器 ───────────────────────────────────────────────────────────
 * 变量与临时变量**共用编号空间**，避免临时变量与源码变量同名。
 * 声明落在 Task 7、实现落在 Task 8：本轮**不得有任何调用方**
 * （只声明不实现时，一旦有调用方就会链接失败）。 */
char *newTempName(void);                /* "t1", "t2" … */
char *newLabelName(void);               /* "label1" … */
char *varNameOf(const char *srcName);   /* 源码名 -> "v1" …，首次调用时登记 */

#endif
