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

#endif
