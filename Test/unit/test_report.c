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
    expect(reportSemanticError(2, 12, "Undefined function \"inc\""), 1, "第 12 行打印");
    return failed;
}
