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

/* 错误恢复同步点之三（顶层）：原本顶层没有出口，Bison 只能弹空栈并**中止**分析，
   于是 `int a = 1;` 换行 `int b = 2` 这种输入只报出第一处（要求书 2.1.3 要求报出
   全部错误）。写法与 `Stmt : error SEMI` 完全一致（含 newNode 元数约定）。

   **实测（bison 3.8.2 与 3.0.4 两版）：冲突 state 数都仍是 1、内容都仍是那处悬空
   else**，只有编号从 `State 111` 变成 `State 113`（**这两个编号是当时实测的**，
   当时基线冲突数为 1）—— 那是多了一条产生式、多出两个 new state 造成的，
   **不是多了一个冲突**。所以这不是"修了就要加冲突"的取舍，冲突数哨兵
   （scripts/run_tests.sh）不受影响。守护它的用例是
   Test/err/toplevel_resync.cmm（删掉本产生式后立刻变红）。

   ⚠️ 编号与**文法**相关、与 Bison 版本无关。本式之上后来又加过产生式
   （`Def : error`、`ExtDef : Specifier FunDec SEMI`），故上句的 111/113 **不是当前值**：
   当前实测值是下方 `Def` 一节的 `State 27 / 32 / 114`（总 state 数 118）。两处并排
   不矛盾 —— 它们是同一件事在两代文法上的取值。 */
ExtDef
  : Specifier ExtDecList SEMI   { $$ = newNode("ExtDef", 3, $1, $2, $3); }
  | Specifier SEMI              { $$ = newNode("ExtDef", 2, $1, $2); }
  | Specifier FunDec CompSt     { $$ = newNode("ExtDef", 3, $1, $2, $3); }
/* **本版本不做选做 2.1：不支持函数声明。** 这条产生式的作用因此不是"支持声明"，
   而是**让前端能认出这种写法**，好在语义层逐条拒绝它（见 semantic.c 的 `ED_PROTO`）。

   **为什么不干脆把它删掉**（实测选出来的，不是想当然）：删掉之后 `int f(int a);`
   由 Bison 的错误恢复报错，而恢复会把**相邻的声明成对吃掉** —— 实测
   `int f(int a);` / `int g(int b);` / `int h(int c);` 三行相邻只报出第 1、3 行；
   中间夹一个空行的两条只报出第 1 条。而要求书选做样例 2 给出的"不需要完成要求2.1"
   期望输出是**每条声明各一条** `Error type B`（那里是第 6、8 行两条）。保留产生式、
   逐条拒绝，给出的正是要求书那个形状；守护它的用例是 Test/lab2/lab2_declreject.cmm
   （三行相邻 ⇒ 必须三条，删掉本式立刻变红）。

   **也不在这里的动作里报错**：那样 `hasError()` 为真，main 会整段跳过语义分析
   （见 src/main.c），于是"声明 + 别处一条语义错"的输入只会剩下一串 B —— 与要求书
   "需要将它们全部检查出来"相悖。改在语义层报，文件其余部分照常分析。

   代价：本式只新增 1 个 state、**不新增任何冲突**（冲突数哨兵仍是 3，见下方 `Def`
   一节的编号说明），所以"保留它"在文法是零代价的。 */
  | Specifier FunDec SEMI       { $$ = newNode("ExtDef", 3, $1, $2, $3); }
  | error SEMI                  { $$ = newNode("ExtDef", 2, $1, $2); }
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

/* 错误恢复同步点之四（定义层）：**不加同步符**（对照上面 `ExtDef`、下面 `Stmt`/`CompSt`
   三处都带同步符）。

   要修的问题：`CompSt : LC • DefList StmtList RC` 状态的闭包里**没有** `error` 出口
   （`StmtList` 排在 `DefList` 之后，不在其闭包中），于是块内局部定义缺分号时，Bison
   一路弹到 `CompSt : error RC`，在函数末尾的 `}` 处同步 —— **同一块内后续错误全被丢弃**。

   为什么**不能**带同步符：带同步符时 Bison 先**丢弃**词法单元直到同步符出现，才按本式
   归约；而不带同步符时移进 `error` 后**立即归约**，不丢弃任何前瞻。实测（同一份输入
   `int main(){ int a } int f(){ int b }`，整份文件里**一个 `;` 都没有**）：

     `Def : error SEMI` → 丢弃到 EOF，**只报 1 条**（少报，本要修的问题原样保留）；
     `Def : error`（本式）→ 立即归约，后续 `}` 与下一个函数照常解析，**报 2 条**。
   `Def : error RC` 同理（甚至更糟：`}` 被同步符吃掉，块内定义后续内容整段消失）。

   代价（实测）：新增 2 个冲突 state —— `DefList` 可空，于是"看 `error` 时移进"与
   "按 `DefList → ε` 归约"不可兼得。**这两处的确切归属（粘自 syntax.output）**：

     State 27  `CompSt: LC • DefList StmtList RC`
     State 32  `DefList: Def • DefList`          ← 递归式，前瞻集与 State 27 相同
   （编号为加 `ExtDef : Specifier FunDec SEMI` 之后的实测值；该产生式只新增 1 个 state
   且落在第 26 位，故 ≥26 的编号统一 +1，原先的 26/31 即 27/32。冲突个数不变，
   与 scripts/run_tests.sh 的「冲突数断言」块一致。**本版本保留那条产生式**（它只为
   逐条拒绝函数声明，见上方 `ExtDef` 一节），所以编号取这一支。）

   注意**不是** `StructSpecifier : STRUCT OptTag LC • DefList RC`（State 20）——
   结构体成员表后面只能跟 `RC`，`error` 不进它的前瞻集，那里 `error` 是个无歧义的
   移进动作，**不冲突**。两者都落在 `CompSt` 一路，因为只有那里 `error` 能跟在
   `DefList` 之后（`StmtList` 可以 `error` 开头）。
   连悬空 else（State 114）在内，冲突 state 总数由 1 变 3。
   这与 `Def : error SEMI` 引入的冲突**完全相同**（两者的冲突都由 `error` 这个符号
   本身引起，与后面跟不跟同步符无关）—— 即同步符的取舍是**纯收益**，不额外付冲突代价。
   冲突数哨兵的期望值已随之改为 3（scripts/run_tests.sh），守护本式的用例是
   Test/err/def_resync.cmm（删掉本式后立刻变红）。 */
Def
  : Specifier DecList SEMI      { $$ = newNode("Def", 3, $1, $2, $3); }
  | error                       { $$ = newNode("Def", 1, $1); }
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
/* 块层出口**不带同步符**：带同步符时 Bison 会先丢弃词法单元直到 `}` 出现才归约，
   把块内其后的定义整段吃掉，同一份文件因此少报错误（实测：`if (1 < 2` 缺右括号
   之后紧跟的缺分号再也报不出来）。裸 `error` 移进后立即归约、不丢任何前瞻。
   改动只加 1 条产生式，冲突 state 数仍是 3。 */
  | error                       { $$ = newNode("CompSt", 1, $1); }
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
   看 `error` 时既要移进又要按 DefList→ε 归约）。**冲突数必须保持当前基线 3**
   不变（哨兵见 scripts/run_tests.sh 的「冲突数断言」块）；上面那两个"新增 N 处"
   是更早、基线为 1 时实测的增量，不是与 3 相比的值。 */
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
