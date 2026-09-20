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

/* 优先级由低到高（Bison 中后声明者优先级高），依据附录 A 表 1。
 * 注意 RELOP 不分级：附录 A 脚注 5 已把六个关系运算符统一到同一优先级，
 * 且文法中本就是单一产生式 Exp : Exp RELOP Exp。 */
%right ASSIGNOP
%left  OR
%left  AND
%left  RELOP
%left  PLUS MINUS
%left  STAR DIV
%right NOT UMINUS

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
  | VarDec ASSIGNOP Exp         { $$ = newNode("Dec", 3, $1, $2, $3); }
  ;

/* ── DefList/Def 由 Task 4 落地：结构体成员定义 `float real, image;` 走的正是
      StructSpecifier → LC DefList RC，若 DefList 只有 ε 则结构体定义无法解析。 ── */

DefList
  : Def DefList                 { $$ = newNode("DefList", 2, $1, $2); }
  | /* empty */                 { $$ = newNode("DefList", 0); }
  ;

Def
  : Specifier DecList SEMI      { $$ = newNode("Def", 3, $1, $2, $3); }
  ;

FunDec
  : ID LP VarList RP            { $$ = newNode("FunDec", 4, $1, $2, $3, $4); }
  | ID LP RP                    { $$ = newNode("FunDec", 3, $1, $2, $3); }
  ;

VarList
  : ParamDec COMMA VarList      { $$ = newNode("VarList", 3, $1, $2, $3); }
  | ParamDec                    { $$ = newNode("VarList", 1, $1); }
  ;

ParamDec
  : Specifier VarDec            { $$ = newNode("ParamDec", 2, $1, $2); }
  ;

CompSt
  : LC DefList StmtList RC      { $$ = newNode("CompSt", 4, $1, $2, $3, $4); }
  ;

StmtList
  : Stmt StmtList               { $$ = newNode("StmtList", 2, $1, $2); }
  | /* empty */                 { $$ = newNode("StmtList", 0); }
  ;

Stmt
  : Exp SEMI                    { $$ = newNode("Stmt", 2, $1, $2); }
  | CompSt                      { $$ = newNode("Stmt", 1, $1); }
  | RETURN Exp SEMI             { $$ = newNode("Stmt", 3, $1, $2, $3); }
  | IF LP Exp RP Stmt           { $$ = newNode("Stmt", 5, $1, $2, $3, $4, $5); }
  | IF LP Exp RP Stmt ELSE Stmt { $$ = newNode("Stmt", 7, $1, $2, $3, $4, $5, $6, $7); }
  | WHILE LP Exp RP Stmt        { $$ = newNode("Stmt", 5, $1, $2, $3, $4, $5); }
  ;

Exp
  : Exp ASSIGNOP Exp            { $$ = newNode("Exp", 3, $1, $2, $3); }
  | Exp AND Exp                 { $$ = newNode("Exp", 3, $1, $2, $3); }
  | Exp OR Exp                  { $$ = newNode("Exp", 3, $1, $2, $3); }
  | Exp RELOP Exp               { $$ = newNode("Exp", 3, $1, $2, $3); }
  | Exp PLUS Exp                { $$ = newNode("Exp", 3, $1, $2, $3); }
  | Exp MINUS Exp               { $$ = newNode("Exp", 3, $1, $2, $3); }
  | Exp STAR Exp                { $$ = newNode("Exp", 3, $1, $2, $3); }
  | Exp DIV Exp                 { $$ = newNode("Exp", 3, $1, $2, $3); }
  | LP Exp RP                   { $$ = newNode("Exp", 3, $1, $2, $3); }
  | MINUS Exp %prec UMINUS      { $$ = newNode("Exp", 2, $1, $2); }
  | NOT Exp                     { $$ = newNode("Exp", 2, $1, $2); }
  | ID LP Args RP               { $$ = newNode("Exp", 4, $1, $2, $3, $4); }
  | ID LP RP                    { $$ = newNode("Exp", 3, $1, $2, $3); }
  | Exp LB Exp RB               { $$ = newNode("Exp", 4, $1, $2, $3, $4); }
  | Exp DOT ID                  { $$ = newNode("Exp", 3, $1, $2, $3); }
  | ID                          { $$ = newNode("Exp", 1, $1); }
  | INT                         { $$ = newNode("Exp", 1, $1); }
  | FLOAT                       { $$ = newNode("Exp", 1, $1); }
  ;

Args
  : Exp COMMA Args              { $$ = newNode("Args", 3, $1, $2, $3); }
  | Exp                         { $$ = newNode("Args", 1, $1); }
  ;

%%

void yyerror(const char *msg) {
    reportError('B', curTokenLine, msg);
}
