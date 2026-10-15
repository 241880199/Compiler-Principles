#ifndef TREE_H
#define TREE_H

typedef enum { NODE_GRAMMAR, NODE_TOKEN } NodeKind;

/* 非终结符种类。**只到非终结符级**，同一非终结符的不同产生式形态由子结点结构
 * 判别（见设计文档 §3.4）。终结符与未识别的结点为 NT_NONE。 */
typedef enum {
    NT_NONE = 0,
    NT_PROGRAM, NT_EXTDEFLIST, NT_EXTDEF, NT_EXTDECLIST,
    NT_SPECIFIER, NT_STRUCTSPECIFIER, NT_OPTTAG, NT_TAG,
    NT_VARDEC, NT_DECLIST, NT_DEC, NT_DEFLIST, NT_DEF,
    NT_FUNDEC, NT_VARLIST, NT_PARAMDEC,
    NT_COMPST, NT_STMTLIST, NT_STMT,
    NT_EXP, NT_ARGS,
    NT_COUNT                      /* 哨兵：表长 */
} NodeType;

typedef struct Node {
    NodeKind      kind;
    NodeType      type;    /* 实验二新增 */
    const char   *name;
    int           line;
    const char   *lexeme;
    /* RELOP 运算符的源码文本（`< <= > >= == !=` 之一）。**刻意不放进 `lexeme`** ——
     * 语法树输出要逐字节不变，而 `lexeme` 非 NULL 会让 printTree 多打 `": >"`：
     * RELOP 结点的 lexeme 由 newRelop 刻意发成 NULL，树里就是一行裸 `RELOP`，
     * 凡是打语法树的既有回归（Test/sample + Test/err，逐字节 diff）都照此钉死。
     * 翻译层要运算符文本（translateCond -> irRelopCode），故另开一个字段承载它；
     * printNode **不读这个字段**，故它对语法树输出零影响。
     * ★ 填它的**唯一**地方是下面的 newRelop —— 词法器（lexical.l 的 emitRelop）
     *   与单测（Test/unit/test_cond.c）都经它构造 RELOP 结点，故两边**同形**。
     *   正是这一点让"单测的输入与真实产物不同形"那类假绿不可能再出现。 */
    const char   *opText;
    int           nchild;
    struct Node **child;
} Node;

Node *newNode(const char *name, int nchild, ...);
Node *newToken(const char *name, int line, const char *lexeme);

/* 构造一个 RELOP 词法单元 —— **与真词法器的产物完全同形**：
 * kind=NODE_TOKEN / name="RELOP" / line=line / opText=<源码文本> / lexeme=NULL。
 *
 * ★ 它存在的唯一理由：让**词法器**（src/lexical.l 的 emitRelop）与**单测**
 *   （Test/unit/test_cond.c）走**同一条构造路径**。此前单测手工 `newToken("RELOP",1,">")`
 *   把文本塞在 lexeme 上，而真词法器把文本放在 opText、lexeme 留 NULL ——
 *   于是那批用例一直在**测一条真实产物不会走的路**（判据的输入与真实产物不同形），
 *   一条 Critical 因此躲过了单测、审查、回归三张网。
 *   共用本构造函数后，两边不可能再同形不同。
 *
 * 内存归属与 newToken 一致：name 与 opText 都各 xstrdup 一份（调用方不必保活）。
 * ★ 传 NULL 的 opText 会得到一个 opText 为 NULL 的 RELOP 结点 —— 那正是翻译层
 *   `relopTextOf` 要**响亮 abort** 的形态，不要用它当"占位"。 */
Node *newRelop(const char *opText, int line);

void  printTree(const Node *root);

/* 子树中第一个词法单元的行号；子树不含词法单元时返回 0。
 * 实验二用它作为**统一的报错行号**（设计文档 §7）。 */
int nodeLine(const Node *n);

#endif
