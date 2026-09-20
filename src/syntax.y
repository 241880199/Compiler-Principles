%{
#include <stdio.h>
#include "tree.h"
#include "report.h"

int  yylex(void);
void yyerror(const char *msg);

Node *root = NULL;
%}

%define api.value.type {Node *}

%token INT FLOAT ID SEMI COMMA ASSIGNOP RELOP PLUS MINUS STAR DIV AND OR DOT NOT
%token TYPE LP RP LB RB LC RC STRUCT RETURN IF ELSE WHILE

%start Program
%%

/* Program 结点无子结点：nodeLine() 返回 0 → 判为 ε → 不打印，
 * 所以空文件依然静默。 */
Program
  : /* empty */   { root = newNode("Program", 0); }
  ;

%%

void yyerror(const char *msg) {
    reportError('B', curTokenLine, msg);
}
