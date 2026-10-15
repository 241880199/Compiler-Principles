/* AST 形状判别表的单测（Task 7）。
 *
 * 判别表是整个翻译层里**唯一需要"读懂语法树结构"**的地方（设计文档 §3.4），
 * 下游所有翻译函数都按它的分派结果走。它一旦判错，错的是**整棵树的语义**，
 * 而且错误会以"生成的 IR 看着挺像那么回事"的形式表现出来 —— 端到端测不出来。
 * 故这里直接构造 Node 逐条钉住每个判别分支。
 *
 * 三个最容易错的点，各自对应下面一条断言：
 *   1) `Exp DOT ID` 与二元运算**形状完全相同**（nchild==3、child[1] 是 token），
 *      只能靠 DOT 区分 —— "Exp:Exp DOT ID"；
 *   2) `Stmt IF` 与 `Stmt WHILE` **孩子数相同**（都是 5），只能靠 child[0] 的
 *      token 名字区分 —— "Stmt:IF" / "Stmt:WHILE"；
 *   3) `Stmt : error SEMI` 与 `Stmt : Exp SEMI` 同形，靠 child[0] 是不是语法结点
 *      区分（无语法错误的输入里不该出现前者）。
 *
 * 断言提示一律写 stderr：本单测在 Makefile 里以**退出码**为判据，stdout 不参与比对。
 */
#include <stdio.h>
#include <string.h>
#include "tree.h"
#include "translate.h"

static int failures = 0;
static void expect(int got, int want, const char *what) {
    if (got != want) { fprintf(stderr, "FAIL %s: got %d want %d\n", what, got, want); failures++; }
    else fprintf(stderr, "ok   %s\n", what);
}

int main(void) {
    /* Exp : INT */
    Node *i = newNode("Exp", 1, newToken("INT", 1, "42"));
    expect(classifyExp(i), EX_INT, "Exp:INT");

    /* Exp : ID */
    Node *v = newNode("Exp", 1, newToken("ID", 1, "a"));
    expect(classifyExp(v), EX_ID, "Exp:ID");

    /* Exp : Exp PLUS Exp */
    Node *b = newNode("Exp", 3, v, newToken("PLUS", 1, "+"), i);
    expect(classifyExp(b), EX_BINOP, "Exp:Exp PLUS Exp");

    /* Exp : MINUS Exp */
    Node *u = newNode("Exp", 2, newToken("MINUS", 1, "-"), v);
    expect(classifyExp(u), EX_UNARY, "Exp:MINUS Exp");

    /* Exp : NOT Exp —— 一元运算的另一个产生式，与 MINUS 同形（nchild==2） */
    Node *un = newNode("Exp", 2, newToken("NOT", 1, "!"), v);
    expect(classifyExp(un), EX_UNARY, "Exp:NOT Exp");

    /* Exp : LP Exp RP */
    Node *p = newNode("Exp", 3, newToken("LP", 1, "("), v, newToken("RP", 1, ")"));
    expect(classifyExp(p), EX_PAREN, "Exp:LP Exp RP");

    /* Exp : ID LP RP */
    Node *c0 = newNode("Exp", 3, newToken("ID", 1, "f"), newToken("LP", 1, "("), newToken("RP", 1, ")"));
    expect(classifyExp(c0), EX_CALL0, "Exp:ID LP RP");

    /* Exp : Exp DOT ID —— 与二元运算同形，必须靠 DOT 区分 */
    Node *fld = newNode("Exp", 3, v, newToken("DOT", 1, "."), newToken("ID", 1, "x"));
    expect(classifyExp(fld), EX_FIELD, "Exp:Exp DOT ID");

    /* Exp : Exp LB Exp RB */
    Node *idx = newNode("Exp", 4, v, newToken("LB", 1, "["), i, newToken("RB", 1, "]"));
    expect(classifyExp(idx), EX_INDEX, "Exp:Exp LB Exp RB");

    /* Exp : ID LP Args RP */
    Node *args = newNode("Args", 1, i);
    Node *cn = newNode("Exp", 4, newToken("ID", 1, "f"), newToken("LP", 1, "("), args, newToken("RP", 1, ")"));
    expect(classifyExp(cn), EX_CALLN, "Exp:ID LP Args RP");

    /* ── 以下三条是 brief 给的样例里**没有**的分支，补上以覆盖全部判别分支。 ──
       （它们排在 IF/IF-ELSE/WHILE 三条之前，是为了让"删掉 WHILE 那行"这个变异
       **恰好**落在最后一条断言上 —— classifyStmt 判不出来时会 abort 整个进程，
       落在中间的话，后面的断言根本没机会跑，"恰好"就观测不到了。） */
    /* Stmt : CompSt —— 孩子数 1，且唯一的（语法）孩子是 CompSt */
    Node *scomp = newNode("Stmt", 1, newNode("CompSt", 4, newToken("LC",4,"{"),
                                             newNode("DefList",0), newNode("StmtList",0),
                                             newToken("RC",4,"}")));
    expect(classifyStmt(scomp), ST_COMPST, "Stmt:CompSt");

    /* Stmt : Exp SEMI —— 与 `error SEMI` 同形，靠 child[0] 是语法结点区分 */
    Node *sexp = newNode("Stmt", 2, v, newToken("SEMI", 5, ";"));
    expect(classifyStmt(sexp), ST_EXP, "Stmt:Exp SEMI");

    /* Stmt : RETURN Exp SEMI —— 孩子数 3，靠 child[0] 的 RETURN 区分 */
    Node *sret = newNode("Stmt", 3, newToken("RETURN", 6, "return"), v, newToken("SEMI", 6, ";"));
    expect(classifyStmt(sret), ST_RETURN, "Stmt:RETURN Exp SEMI");

    /* Stmt : IF LP Exp RP Stmt */
    /* 注：brief 里这一句少一个右括号（实测 `(` 11 个、`)` 10 个），逐字抄下来编译不过；
       这里补上末尾那个 `)`，构出的仍是同一个结点（形状由 syntax.y 唯一确定）。 */
    Node *sif = newNode("Stmt", 5, newToken("IF", 2, "if"), newToken("LP", 2, "("),
                        v, newToken("RP", 2, ")"), newNode("Stmt", 1, newNode("CompSt", 4,
                        newToken("LC",2,"{"), newNode("DefList",0), newNode("StmtList",0), newToken("RC",2,"}"))));
    expect(classifyStmt(sif), ST_IF, "Stmt:IF");

    /* Stmt : IF LP Exp RP Stmt ELSE Stmt */
    Node *sife = newNode("Stmt", 7, newToken("IF",2,"if"), newToken("LP",2,"("), v, newToken("RP",2,")"),
                         sif, newToken("ELSE",2,"else"), sif);
    expect(classifyStmt(sife), ST_IFELSE, "Stmt:IF-ELSE");

    /* Stmt : WHILE LP Exp RP Stmt —— 孩子数与 IF 相同，必须靠 WHILE 区分 */
    Node *sw = newNode("Stmt", 5, newToken("WHILE",3,"while"), newToken("LP",3,"("), v, newToken("RP",3,")"), sif);
    expect(classifyStmt(sw), ST_WHILE, "Stmt:WHILE");

    fprintf(stderr, failures ? "FAILURES=%d\n" : "ALL PASS\n", failures);
    return failures ? 1 : 0;
}
