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
%left  LB DOT

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

/* 错误恢复同步点之一（块层兜底）：`{` 之后出现无法归入 Def/Stmt 的内容时，
   弹栈到此并在 `}` 处同步，避免一路吞到文件尾。 */
CompSt
  : LC DefList StmtList RC      { $$ = newNode("CompSt", 4, $1, $2, $3, $4); }
  | error RC                    { $$ = newNode("CompSt", 2, $1, $2); }
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
/* 错误恢复同步点之二（语句层）：语句内部出错时弹到最近的 Stmt 出口，
   丢弃出错的一段后在 `;` 同步，使同一函数体后续语句仍能被检查。 */
  | error SEMI                  { $$ = newNode("Stmt", 2, $1, $2); }
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
/* 这里**不放** `| error RB` —— 实测（见 task-7-report.md）它会破坏下标内的恢复：
   `Exp LB . Exp RB` 的闭包含 `Exp -> . error RB`，故 Bison 在下标内部就停下了弹栈，
   把 `]` 当作 error 规则的同步符消耗掉，外层 `[` 反而永远等不到 `]`，
   于是 `a[5,3] = 1.5;` 在行内多报一条错误（要求书 2.1.3 保证同一行不出多个错误）。
   去掉它后，下标内的错误由 Stmt 层出口接管，丢弃整条语句后在 `;` 同步。
   实测同样否决的写法：裸 `| error`（Exp 层再开一个出口，与 Stmt/CompSt 层重复，
   且新增 1 处移进/归约冲突）、`Def : error SEMI`（新增 2 处冲突：DefList 可空，
   看 `error` 时既要移进又要按 DefList→ε 归约）。冲突数必须保持 1。 */
  ;

Args
  : Exp COMMA Args              { $$ = newNode("Args", 3, $1, $2, $3); }
  | Exp                         { $$ = newNode("Args", 1, $1); }
  ;

%%

/* 说明文字统一为 "Syntax error"：要求书 2.1.3 说明文字内容不限，只要错误类型与
 * 行号正确；Bison 默认给的 msg 是 "syntax error"（小写 s），这里统一成规范写法。 */
void yyerror(const char *msg) {
    (void)msg;
    reportError('B', curTokenLine, "Syntax error");
}
