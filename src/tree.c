#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "tree.h"
#include "util.h"

/* 名字 → NodeType。**这是唯一需要与 syntax.y 同步的地方**：
 * syntax.y 里新增了非终结符而忘了在这里登记，newNode 会 abort（大声失败，不静默）。 */
static const struct { const char *name; NodeType t; } nodeTypeTable[] = {
    {"Program", NT_PROGRAM},         {"ExtDefList", NT_EXTDEFLIST},
    {"ExtDef", NT_EXTDEF},           {"ExtDecList", NT_EXTDECLIST},
    {"Specifier", NT_SPECIFIER},     {"StructSpecifier", NT_STRUCTSPECIFIER},
    {"OptTag", NT_OPTTAG},           {"Tag", NT_TAG},
    {"VarDec", NT_VARDEC},           {"DecList", NT_DECLIST},
    {"Dec", NT_DEC},                 {"DefList", NT_DEFLIST},
    {"Def", NT_DEF},                 {"FunDec", NT_FUNDEC},
    {"VarList", NT_VARLIST},         {"ParamDec", NT_PARAMDEC},
    {"CompSt", NT_COMPST},           {"StmtList", NT_STMTLIST},
    {"Stmt", NT_STMT},               {"Exp", NT_EXP},
    {"Args", NT_ARGS},
};

static NodeType lookupNodeType(const char *name) {
    size_t i;
    for (i = 0; i < sizeof nodeTypeTable / sizeof nodeTypeTable[0]; i++)
        if (strcmp(name, nodeTypeTable[i].name) == 0) return nodeTypeTable[i].t;
    fprintf(stderr, "internal error: unknown nonterminal \"%s\" in newNode()\n", name);
    fprintf(stderr, "              (若刚在 syntax.y 里加了新非终结符，请同步 nodeTypeTable)\n");
    abort();
}

Node *newToken(const char *name, int line, const char *lexeme) {
    Node *n = xmalloc(sizeof(Node));
    n->kind   = NODE_TOKEN;
    n->type   = NT_NONE;
    n->name   = xstrdup(name);
    n->line   = line;
    n->lexeme = xstrdup(lexeme);
    n->opText = NULL;       /* 只有 newRelop 会填它（见 tree.h） */
    n->nchild = 0;
    n->child  = NULL;
    return n;
}

/* RELOP 词法单元 —— 词法器与单测**共用的唯一**构造路径（见 tree.h 的说明）。
 * 先把"非 RELOP 词法单元"那一份交给 newToken 建好（name="RELOP"、lexeme=NULL、行号），
 * 再补上专有字段 opText —— 于是它与真词法器发出来的结点**逐字段同形**（kind/type/
 * name/line/lexeme/opText/nchild/child 一个不差）。
 * ★ opText 走 xstrdup：与 newToken 对 lexeme 的处理一致（调用方传进来的可能是 flex
 *   的复用缓冲区 yytext，也可能是栈上/字面量字符串；复制一份后调用方不必保活）。
 * ★ **这一行就是"运算符文本"的全部来源** —— 删掉它，RELOP 结点的 opText 恒为 NULL，
 *   翻译层会当场响亮 abort。控制它的是 Test/unit/test_cond.c 那 9 条用例与 `make prog-test`
 *   （两者都必须变红）—— 那正是"单测与真产物同形"的证明。 */
Node *newRelop(const char *opText, int line) {
    Node *n = newToken("RELOP", line, NULL);
    n->opText = xstrdup(opText);
    return n;
}

Node *newNode(const char *name, int nchild, ...) {
    Node *n = xmalloc(sizeof(Node));
    va_list ap;
    int i;

    n->kind   = NODE_GRAMMAR;
    n->type   = lookupNodeType(name);
    n->name   = xstrdup(name);
    n->line   = 0;          /* 行号一律由 nodeLine() 推导，不在此处指定 */
    n->lexeme = NULL;
    n->opText = NULL;       /* 只对词法单元有意义（见 tree.h） */
    n->nchild = nchild;
    n->child  = nchild ? xmalloc(sizeof(Node *) * (size_t)nchild) : NULL;

    va_start(ap, nchild);
    for (i = 0; i < nchild; i++) n->child[i] = va_arg(ap, Node *);
    va_end(ap);

    return n;
}

/* 子树中第一个词法单元的行号；子树不含词法单元时返回 0。
 * 返回 0 同时就是"该结点产生了 ε"的判定 —— 见设计文档 §4.1。 */
int nodeLine(const Node *n) {
    int i;
    if (n->kind == NODE_TOKEN) return n->line;
    for (i = 0; i < n->nchild; i++) {
        int l = nodeLine(n->child[i]);
        if (l > 0) return l;
    }
    return 0;
}

static void printNode(const Node *n, int depth) {
    int i, line;

    if (n->kind == NODE_GRAMMAR) {
        line = nodeLine(n);
        if (line == 0) return;          /* 产生了 ε，不打印、不占位 */
    } else {
        line = n->line;
    }

    for (i = 0; i < depth; i++) printf("  ");

    if (n->kind == NODE_TOKEN) {
        if (n->lexeme) printf("%s: %s\n", n->name, n->lexeme);
        else           printf("%s\n", n->name);
    } else {
        printf("%s (%d)\n", n->name, line);
    }

    for (i = 0; i < n->nchild; i++) printNode(n->child[i], depth + 1);
}

void printTree(const Node *root) { printNode(root, 0); }
