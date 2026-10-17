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
 * 本轮尚未接上驱动：src/main.c 要到 Task 12 才调用它 —— 故它的判据是
 * Test/unit/test_translate_prog.c（`make prog-test`），而不是端到端。 */
InterCodeList *translateProgram(Node *root);

/* ── 语句 / 复合语句 / 函数定义（Task 10）────────────────────────────────────
 * 全部沿用 translateExp/translateCond 的协议：返回"新增 IR 挂在传入 tail 之后的新尾"。
 * 形状判别交给 classifyStmt（本层唯一"读懂结构"的地方），故这里的入口都只接
 * "已知形状"的结点 —— 调用方（translateProgram 与彼此）负责给出对的那个孩子。
 *
 * ⚠️ `syntax.y` 里的列表是**右递归**的（`StmtList : Stmt StmtList`、`DefList : Def DefList`、
 * `DecList : Dec COMMA DecList`、`ExtDefList : ExtDef ExtDefList`），故这些函数的
 * `child[0]` 是本层元素、`child[1]`/`child[2]` 是**余下的同类列表**，必须递归下去。 */
InterCodeList *translateStmt(InterCodeList *tail, Node *s);
InterCodeList *translateCompSt(InterCodeList *tail, Node *c);
InterCodeList *translateStmtList(InterCodeList *tail, Node *l);
InterCodeList *translateDefList(InterCodeList *tail, Node *l);
InterCodeList *translateExtDef(InterCodeList *tail, Node *e);

/* ── 表达式翻译（Task 8）──────────────────────────────────────────────────
 * `place` 是**继承属性**：调用方告诉本函数"把值算到哪个操作数里"，而不是让它
 * 自造临时变量再由调用方搬运。传 NULL 表示"值不被需要"（表达式语句），此时两侧
 * 仍必须照常翻译 —— 它们可能有副作用（函数调用）。
 *
 * 本设计最大的效率来源就在这条协议上：`x = <expr>;` 时不产生
 * `t := <expr>; x := t` 两条，而是**把 x 自己当 place 传给右子表达式**，只产生一条。
 * 要求书 4.1.6 样例 1 自己给出的"优化后版本"正是这个形态。
 * Test/unit/test_translate.c 逐字节钉住它（朴素写法跑出来的程序结果完全相同，
 * 端到端一个字都察觉不到 —— 只有行数能分辨）。
 *
 * 返回为本表达式新增的 IR（挂在传入的 tail 之后）的**新尾**。
 *
 * ★ 原先还有一个 `placeIsAddr` 出参（`Exp1[Exp2]` 置 1、其余形状置 0）。Task 11b
 *   审查发现它**只写不读** —— 全仓库没有任何一处读它，左值路径自己在
 *   `translateAssignToLValue` 里重算地址。留着一个没人读的出参，等于"文档承诺了
 *   一条并不存在的契约"，故删掉。
 *   Task 13（高维）与 Task 14（数组形参）落地后仍未复用：两条路径都**就地分派** ——
 *   `arrayElemAddr` 按 isArrayParam 选 opAddr/opVar，`translateArgs` 按 varTypes
 *   的 TY_ARRAY 选 &arr/形参地址，故不需要一个"本结点是地址"的出参。 */
InterCodeList *translateExp(InterCodeList *tail, Node *e, Operand place);

/* ── 条件翻译（Task 9）────────────────────────────────────────────────────
 * 按指导书表 4 的**短路**形式生成代码：条件成立跳 labelTrue，否则跳 labelFalse。
 * labelTrue / labelFalse 是**继承属性**（函数参数），**不做回填** —— 见 §6.3 与
 * translate.c 里 translateCond 顶部那段理由。
 *
 * 调用方负责在合适的时机摆好两个目标标号（语句翻译见 Task 10）；本函数只保证
 * "条件为真时控制流落在 labelTrue 上、为假时落在 labelFalse 上"，不生成标号本身。
 * 返回新增 IR 挂在传入 tail 之后的新尾。 */
InterCodeList *translateCond(InterCodeList *tail, Node *e,
                             const char *labelTrue, const char *labelFalse);

/* ── 名字发生器 ───────────────────────────────────────────────────────────
 * 变量与临时变量**共用编号空间**，避免临时变量与源码变量同名。
 * 声明落在 Task 7、实现落在 Task 8。 */
char *newTempName(void);                /* "t1", "t2" … */
char *newLabelName(void);               /* "label1" … */
/* 源码名 -> "v1" …，首次调用时登记。返回**指向表内字符串的借用指针**（const，
 * 调用方不得改动或释放；opVar 拿到后会自己再复制一份）。 */
const char *varNameOf(const char *srcName);

/* 把三个计数器与变量映射表一起清零。**只给单测用**：同一进程里连着跑多个用例时，
 * 不清零的话第二个用例的编号接着第一个往下走（期望值算不出来、也没法读）。
 * 正常编译流程只在进程开头调用一次（或根本不调），故它不影响产物。 */
void translateResetNames(void);

#endif
