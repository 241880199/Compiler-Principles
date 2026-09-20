#include <stdio.h>
#include "tree.h"
#include "syntax.tab.h"

extern int   yylex(void);
extern Node *yylval;

static const char *tokName(int t) {
    switch (t) {
    case INT:      return "INT";
    case FLOAT:    return "FLOAT";
    case ID:       return "ID";
    case SEMI:     return "SEMI";
    case COMMA:    return "COMMA";
    case ASSIGNOP: return "ASSIGNOP";
    case RELOP:    return "RELOP";
    case PLUS:     return "PLUS";
    case MINUS:    return "MINUS";
    case STAR:     return "STAR";
    case DIV:      return "DIV";
    case AND:      return "AND";
    case OR:       return "OR";
    case DOT:      return "DOT";
    case NOT:      return "NOT";
    case TYPE:     return "TYPE";
    case LP:       return "LP";
    case RP:       return "RP";
    case LB:       return "LB";
    case RB:       return "RB";
    case LC:       return "LC";
    case RC:       return "RC";
    case STRUCT:   return "STRUCT";
    case RETURN:   return "RETURN";
    case IF:       return "IF";
    case ELSE:     return "ELSE";
    case WHILE:    return "WHILE";
    default:       return "?";
    }
}

int main(void) {
    int t;
    while ((t = yylex()) != 0) {
        if (yylval && yylval->lexeme) printf("%s: %s\n", tokName(t), yylval->lexeme);
        else                          printf("%s\n", tokName(t));
    }
    return 0;
}
