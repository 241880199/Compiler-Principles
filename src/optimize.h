#ifndef OPTIMIZE_H
#define OPTIMIZE_H

#include "ir.h"

/* 优化框架（Task 16 起）。
 *
 * `optimizeIR` 就地改链表，反复调用各子 pass 直到一轮下来改动条数为 0（不动点）；
 * 子 pass 的实现分属 Task 17–19，Task 16 里它们都是返回 0 的桩。
 *
 * 正确性的地基是「临时变量单赋值」：每个 tN 全程序只被赋值一次、且只在赋值后使用。
 * 该不变量由 newTempName() 的单调编号保证；optimizeIR 在**前后各断言一次**，
 * 任何未来的违反都会以 stderr 诊断 + abort 响亮失败，而不是让优化器静默改错程序。 */
void optimizeIR(InterCodeList *head);

/* 操作数 o 在全程序中作为**读**（source/use）出现的次数；定义（destination）
 * 不计入。具体口径见 optimize.c 中 codeReadOperands 的注释表。
 * `head` 是哨兵（irHead 语义），空表返回 0。 */
int irCountOperandUses(InterCodeList *head, Operand o);

/* 名字形如 t<至少一位十进制数字> 的 OP_VAR 才是临时变量：
 * t1/t12 为真；t/temp/t1x/label1/v1 为假；非 OP_VAR 一律为假。 */
int irIsTemp(Operand o);

#endif
