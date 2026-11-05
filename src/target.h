#ifndef TARGET_H
#define TARGET_H

#include <stdio.h>
#include "ir.h"

/* 实验四：把中间代码翻译为 MIPS32 汇编（允许伪指令），写入 out。
 * 只读 IR 链表，不修改它；不依赖前端的数据结构。
 * 详见 docs/superpowers/specs/2026-11-05-cmm-lab4-design.md。 */
void generateMIPS(FILE *out, InterCodeList *ir);

#endif
