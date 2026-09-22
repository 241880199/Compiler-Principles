#ifndef TREE_H
#define TREE_H

typedef enum { NODE_GRAMMAR, NODE_TOKEN } NodeKind;

typedef struct Node {
    NodeKind      kind;
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
