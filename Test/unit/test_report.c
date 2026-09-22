/* report.c 的同行去重（设计文档 §8 第 9 条，要求书"每行最多只有一个错误"）。
 * 输出与 report_expected.txt 逐字节比对；返回值同时要正确 ——
 * semantic.c 的错误计数依赖它，数错了会让"实际打印条数"与 stdout 对不上。 */
#include <stdio.h>
#include "report.h"

static int failed = 0;

/* **一律写 stderr** —— stdout 要留给 reportSemanticError 的输出，
 * 它才是与 report_expected.txt 逐字节比对的对象。混在一起 diff 必然失败。 */
static void expect(int got, int want, const char *what) {
    if (got != want) { fprintf(stderr, "FAIL %s: got %d want %d\n", what, got, want); failed = 1; }
    else             { fprintf(stderr, "ok   %s\n", what); }
}

int main(void) {
    expect(reportSemanticError(1, 4, "Undefined variable \"j\""), 1, "第 4 行第一条打印");
    expect(reportSemanticError(7, 4, "Type mismatched for operands"), 0, "同行第二条被抑制");
    expect(reportSemanticError(3, 9, "Redefined variable \"i\""), 1, "第 9 行第一条打印");
    expect(reportSemanticError(5, 9, "Type mismatched for assignment"), 0, "同行第二条被抑制");
    /* **非连续**的同行错误（2026-09-22 评审修正）：第 9 行打印过之后**回到**第 4 行。
     * 判据是无条件的（"每行最多只有一个错误"+§8 第 9 条），所以这条必须被抑制。
     * 旧实现只记"上一次打印的行号"，此时它是 9，`4 != 9` → 会**再打印一条第 4 行**并返回 1：
     * 这条断言是唯一能把"已打印行号集合"和"连续近似"区分开的判据（前两条不行 —— 第二次调用
     * 紧挨着第一次，两种实现在那里行为相同）。 */
    expect(reportSemanticError(6, 4, "The left-hand side of an assignment must be a variable"), 0,
           "非连续的同行第二条也被抑制");
    /* 被抑制的调用**仍然**置 errorFlag（与 reportError 同一约定）。
     * 注意 hasError() 对本进程是**latch** 的，所以只能"1"不能"0"；放在此处而不是开头，
     * 是因为开头尚未有过任何调用。 */
    expect(hasError(), 1, "被抑制的错误仍置 errorFlag");
    expect(reportSemanticError(2, 12, "Undefined function \"inc\""), 1, "第 12 行打印");
    return failed;
}
