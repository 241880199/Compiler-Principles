#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "tree.h"

/* 遍历 AST 做语义分析与类型检查。
 * 返回：报告的错误条数（>0 表示有错）。 */
int semanticAnalyze(Node *root);

#endif
