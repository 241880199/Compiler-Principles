#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "tree.h"

/* 遍历 AST 做语义分析与类型检查。
 * 返回：报告的错误条数（>0 表示有错）。 */
int semanticAnalyze(Node *root);

/* 本次分析中是否声明过结构体类型的变量或形参（选做 3.1 的未实现提示用）。
 * 只反映最近一次 semanticAnalyze 的结果；数组元素递归含结构体也算。 */
int semanticHasStruct(void);

#endif
