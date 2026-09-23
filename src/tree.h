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
    int           nchild;
    struct Node **child;
} Node;

Node *newNode(const char *name, int nchild, ...);
Node *newToken(const char *name, int line, const char *lexeme);
void  printTree(const Node *root);

/* 子树中第一个词法单元的行号；子树不含词法单元时返回 0。
 * 实验二用它作为**统一的报错行号**（设计文档 §7）。 */
int nodeLine(const Node *n);

#endif
