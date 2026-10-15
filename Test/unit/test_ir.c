#include <stdio.h>
#include <string.h>
#include "ir.h"

static void emit(InterCodeList **t, InterCode *c) { *t = irAppend(*t, c); }

int main(void) {
    InterCodeList *t = irList();
    InterCode *c;

    c = newCode(IR_FUNCTION); c->u.one.x = opFunc("f1");           emit(&t, c);
    c = newCode(IR_DEC);      c->u.dec.x = opVar("v1"); c->u.dec.size = 8; emit(&t, c);
    c = newCode(IR_ASSIGN);   c->u.assign.x = opVar("v1"); c->u.assign.y = opConst(5); emit(&t, c);
    c = newCode(IR_ADDR);     c->u.assign.x = opVar("v2"); c->u.assign.y = opVar("v1"); emit(&t, c);
    c = newCode(IR_ADD);      c->u.binop.x = opVar("v3"); c->u.binop.y = opVar("v2"); c->u.binop.z = opConst(8); emit(&t, c);
    c = newCode(IR_ASSIGN_DEREF); c->u.deref.x = opVar("v4"); c->u.deref.y = opVar("v3"); emit(&t, c);
    c = newCode(IR_DEREF_ASSIGN); c->u.deref.x = opVar("t4"); c->u.deref.y = opVar("v1"); emit(&t, c);
    c = newCode(IR_IF);       c->u.iff.x = opVar("v1"); c->u.iff.y = opConst(0);
                              c->u.iff.z = opLabel("label1"); c->u.iff.relop = irRelopCode(">"); emit(&t, c);
    c = newCode(IR_GOTO);     c->u.one.x = opLabel("label2");      emit(&t, c);
    c = newCode(IR_LABEL);    c->u.one.x = opLabel("label1");      emit(&t, c);
    c = newCode(IR_ARG);      c->u.one.x = opAddr("v1");           emit(&t, c);
    c = newCode(IR_CALL);     c->u.call.x = opVar("v5"); c->u.call.f = opFunc("f2"); emit(&t, c);
    c = newCode(IR_RETURN);   c->u.one.x = opConst(0);             emit(&t, c);

    printIR(stdout, t);
    return 0;
}
