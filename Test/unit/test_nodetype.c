/* 验证 newNode 能按名字自动填出 NodeType，且 21 个非终结符一个不漏。 */
#include <stdio.h>
#include <string.h>
#include "tree.h"

static int failed = 0;

static void check(NodeType got, NodeType want, const char *name) {
    if (got != want) { printf("FAIL %s: got %d want %d\n", name, got, want); failed = 1; }
    else             { printf("ok   %s\n", name); }
}

int main(void) {
    struct { const char *name; NodeType t; } cases[] = {
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
    size_t i;
    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        Node *n = newNode(cases[i].name, 0);
        check(n->type, cases[i].t, cases[i].name);
    }

    /* 终结符不带非终结符种类 */
    {
        Node *tok = newToken("ID", 3, "abc");
        if (tok->type != NT_NONE) { printf("FAIL 终结符的 type 应为 NT_NONE\n"); failed = 1; }
        else                      { printf("ok   终结符的 type 为 NT_NONE\n"); }
    }

    printf(failed ? "\nFAIL nodetype\n" : "\nPASS nodetype\n");
    return failed;
}
