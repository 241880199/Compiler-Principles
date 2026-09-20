#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "tree.h"

static void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) { fprintf(stderr, "out of memory\n"); exit(1); }
    return p;
}

static char *xstrdup(const char *s) {
    if (!s) return NULL;
    char *p = xmalloc(strlen(s) + 1);
    strcpy(p, s);
    return p;
}

Node *newToken(const char *name, int line, const char *lexeme) {
    Node *n = xmalloc(sizeof(Node));
    n->kind   = NODE_TOKEN;
    n->name   = xstrdup(name);
    n->line   = line;
    n->lexeme = xstrdup(lexeme);
    n->nchild = 0;
    n->child  = NULL;
    return n;
}

Node *newNode(const char *name, int nchild, ...) {
    Node *n = xmalloc(sizeof(Node));
    va_list ap;
    int i;

    n->kind   = NODE_GRAMMAR;
    n->name   = xstrdup(name);
    n->line   = 0;          /* 行号一律由 nodeLine() 推导，不在此处指定 */
    n->lexeme = NULL;
    n->nchild = nchild;
    n->child  = nchild ? xmalloc(sizeof(Node *) * (size_t)nchild) : NULL;

    va_start(ap, nchild);
    for (i = 0; i < nchild; i++) n->child[i] = va_arg(ap, Node *);
    va_end(ap);

    return n;
}

/* 子树中第一个词法单元的行号；子树不含词法单元时返回 0。
 * 返回 0 同时就是"该结点产生了 ε"的判定 —— 见设计文档 §4.1。 */
static int nodeLine(const Node *n) {
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
