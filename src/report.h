#ifndef REPORT_H
#define REPORT_H

/* 最近一次由词法分析器返回的词法单元所在行号。
 * yyerror() 用它而非 yylineno —— 后者可能已被多行词法单元推进到结束行。 */
extern int curTokenLine;

/* 报告并记录一条错误。type 为 'A'（词法）或 'B'（语法）。
 * 输出：Error type [A|B] at Line [n]: [msg].   （句末句点由本函数追加） */
void reportError(char type, int line, const char *msg);

/* 报告一条语义错误（实验二）。code 为 1-19 的错误类型码。
 * 同一行只保留**最先检出**的那一条（设计文档 §8 第 9 条 + 要求书"每行最多一个错误"）。
 * 输出：Error type [code] at Line [line]: [msg].   到 stdout，句末句点由本函数追加。
 * 返回：1 = 已打印；0 = 被同行去重抑制（调用方据此计数，才能与 stdout 一致）。 */
int reportSemanticError(int code, int line, const char *msg);

/* 是否出现过任何错误 */
int hasError(void);

#endif
