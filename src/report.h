#ifndef REPORT_H
#define REPORT_H

/* 最近一次由词法分析器返回的词法单元所在行号。
 * yyerror() 用它而非 yylineno —— 后者可能已被多行词法单元推进到结束行。 */
extern int curTokenLine;

/* 报告并记录一条错误。type 为 'A'（词法）或 'B'（语法）。
 * 输出：Error type [A|B] at Line [n]: [msg].   （句末句点由本函数追加） */
void reportError(char type, int line, const char *msg);

/* 是否出现过任何错误 */
int hasError(void);

#endif
