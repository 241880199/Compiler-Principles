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

/* 本任务的 Program 动作只置 NULL —— newNode() 要到 Task 2 才实现。
 * Task 2 Step 7 会把它改成真正构造结点。 */
Program
  : /* empty */   { root = NULL; }
  ;

%%

void yyerror(const char *msg) {
    reportError('B', curTokenLine, msg);
}
