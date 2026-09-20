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

Program
  : ExtDefList                  { root = newNode("Program", 1, $1); }
  ;

ExtDefList
  : ExtDef ExtDefList           { $$ = newNode("ExtDefList", 2, $1, $2); }
  | /* empty */                 { $$ = newNode("ExtDefList", 0); }
  ;

ExtDef
  : Specifier ExtDecList SEMI   { $$ = newNode("ExtDef", 3, $1, $2, $3); }
  | Specifier SEMI              { $$ = newNode("ExtDef", 2, $1, $2); }
  | Specifier FunDec CompSt     { $$ = newNode("ExtDef", 3, $1, $2, $3); }
  ;

ExtDecList
  : VarDec                      { $$ = newNode("ExtDecList", 1, $1); }
  | VarDec COMMA ExtDecList     { $$ = newNode("ExtDecList", 3, $1, $2, $3); }
  ;

Specifier
  : TYPE                        { $$ = newNode("Specifier", 1, $1); }
  | StructSpecifier             { $$ = newNode("Specifier", 1, $1); }
  ;

StructSpecifier
  : STRUCT OptTag LC DefList RC { $$ = newNode("StructSpecifier", 5, $1, $2, $3, $4, $5); }
  | STRUCT Tag                  { $$ = newNode("StructSpecifier", 2, $1, $2); }
  ;

OptTag
  : ID                          { $$ = newNode("OptTag", 1, $1); }
  | /* empty */                 { $$ = newNode("OptTag", 0); }
  ;

Tag
  : ID                          { $$ = newNode("Tag", 1, $1); }
  ;

VarDec
  : ID                          { $$ = newNode("VarDec", 1, $1); }
  | VarDec LB INT RB            { $$ = newNode("VarDec", 4, $1, $2, $3, $4); }
  ;

DecList
  : Dec                         { $$ = newNode("DecList", 1, $1); }
  | Dec COMMA DecList           { $$ = newNode("DecList", 3, $1, $2, $3); }
  ;

Dec
  : VarDec                      { $$ = newNode("Dec", 1, $1); }
  ;

/* ── 本任务必须含 DefList/Def：结构体成员定义 `float real, image;` 走的正是
      StructSpecifier → LC DefList RC，若 DefList 只有 ε 则结构体定义无法解析。
      FunDec / CompSt 仍留占位，Task 5 补全。 ── */

DefList
  : Def DefList                 { $$ = newNode("DefList", 2, $1, $2); }
  | /* empty */                 { $$ = newNode("DefList", 0); }
  ;

Def
  : Specifier DecList SEMI      { $$ = newNode("Def", 3, $1, $2, $3); }
  ;

FunDec
  : ID LP RP                    { $$ = newNode("FunDec", 3, $1, $2, $3); }
  ;

CompSt
  : LC RC                       { $$ = newNode("CompSt", 2, $1, $2); }
  ;

%%

void yyerror(const char *msg) {
    reportError('B', curTokenLine, msg);
}
