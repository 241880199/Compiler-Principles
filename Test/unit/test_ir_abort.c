/* 隔离的「响亮失败」用例（Task 6 审查修正第 2 条）。
 *
 * 断言：给 IR_ADDR 传一个**已经带 `&`** 的 OP_ADDR 操作数时，printIR 必须**大声失败**
 * （stderr 出一句诊断 + abort），而**不是**静默打出 `&&x`。
 * `&&x` 在真机上是语法错、在本项目参考机上会被装载器拒掉，表现为"莫名装载失败" ——
 * 一行的坏指令没有任何信号，正是最该防的失效。
 *
 * 为什么**必须**独立成一个可执行（而不是并进 test_ir.c）：
 *   abort() 会带 SIGABRT 结束**整个进程**。若把它塞进那批"打印到 stdout 再整体 diff"
 *   的用例，进程会在打到这条时崩掉、后面的用例一条都不跑，且 `| diff` 只会看到一段
 *   被截断的输出 —— 既分不清是谁崩的，也会把"没跑到"伪装成"输出不符"。
 *   独立可执行则让"崩溃"本身成为可通过/不通过判定的目标：本用例的**通过**判据是
 *   「退出码非 0 **且** stderr 含诊断」，由 Makefile 的 ir-abort-test 目标执行。
 *
 * 正常路径（合法输入）由 Test/unit/test_ir 负责；本文件只走这一条失败路径。 */

#include <stdio.h>
#include "ir.h"

int main(void) {
    InterCodeList *t = irList();
    InterCode *c = newCode(IR_ADDR);
    c->u.assign.x = opVar("v1");
    c->u.assign.y = opAddr("v2");   /* ← 错用：y 已经带 `&`，printIR 会加第二个 `&` */
    irAppend(t, c);

    printIR(stdout, t);             /* 必须在此 abort（stderr 有诊断），到不了下一行 */
    return 0;
}
