#ifndef OPTIMIZE_H
#define OPTIMIZE_H

#include "ir.h"

/* 优化 pass。Task 12 的流水线会调用它；Task 16 起才有真实现。
 * 现在必须是**已定义的空函数**：main.c 里那句 optimizeIR(ir) 若无定义，整个程序
 * 会因 `undefined reference to 'optimizeIR'` 链接失败。 */
void optimizeIR(InterCodeList *head);

#endif
