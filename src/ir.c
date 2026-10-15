#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ir.h"
#include "util.h"

/* ============================================================================
 *  操作数构造函数
 *
 *  ★ 名字一律 xstrdup：调用方传进来的多是**会失效的缓冲区**
 *  （translate.c 用 snprintf 往栈上拼 "t1"/"v2"/"label3" 再交给 opVar/opLabel）。
 *  若只存指针，等那帧栈没了，IR 文本里就是垃圾 —— 而且**只有真跑到才知道**。
 * ============================================================================ */

static Operand newOperand(int kind, const char *name, int value) {
    Operand o = xmalloc(sizeof *o);
    o->kind  = kind;
    o->name  = xstrdup(name);   /* xstrdup(NULL) == NULL（opConst 用） */
    o->value = value;
    return o;
}

Operand opVar(const char *name)   { return newOperand(OP_VAR,   name, 0); }
Operand opConst(int v)            { return newOperand(OP_CONST, NULL, v); }
Operand opLabel(const char *name) { return newOperand(OP_LABEL, name, 0); }
/* ★ 函数名必须是**源码里的真实名字**：虚拟机的 `CALL f` 拿 f 去查 labelTable
 *   （键是 `FUNCTION name :` 那行登记的字面名），编号在这里毫无意义。
 *   同理入口函数必须逐字叫 main —— 真机靠 strs[1] == "main" 找入口。 */
Operand opFunc(const char *name)  { return newOperand(OP_FUNC,  name, 0); }
Operand opAddr(const char *name)  { return newOperand(OP_ADDR,  name, 0); }

int operandIsConst(Operand o)   { return o && o->kind == OP_CONST; }
int operandIsVarLike(Operand o) { return o && (o->kind == OP_VAR || o->kind == OP_ADDR); }
int operandIsLabel(Operand o)   { return o && o->kind == OP_LABEL; }

/* ============================================================================
 *  关系运算符：文本 <-> 编号
 * ============================================================================ */

static const char *const RELOP_TEXT[6] = { "<", "<=", ">", ">=", "==", "!=" };

int irRelopCode(const char *relopText) {
    int i;
    if (relopText) {
        for (i = 0; i < 6; i++)
            if (strcmp(relopText, RELOP_TEXT[i]) == 0) return i;
    }
    /* 内部错误：走到这里说明语法树里的 relop 文本没被处理全，属于程序缺陷。
     * 大声失败比"悄悄当成 <"安全（错的关系符会静默产生错误的跳转）。 */
    fprintf(stderr, "internal error: unknown relop \"%s\"\n",
            relopText ? relopText : "(null)");
    abort();
}

const char *irRelopText(int code) {
    if (code < 0 || code > 5) {
        fprintf(stderr, "internal error: bad relop code %d\n", code);
        abort();
    }
    return RELOP_TEXT[code];
}

/* ============================================================================
 *  指令与链表
 * ============================================================================ */

InterCode *newCode(int kind) {
    InterCode *c = xmalloc(sizeof *c);
    /* 零初始化整个 union：调用方只填自己那条指令用到的成员，其余（尤其
     * u.iff.relop）必须是 0 而不是垃圾 —— 否则打印出一条随机的 IF。 */
    memset(c, 0, sizeof *c);
    c->kind = kind;
    return c;
}

InterCodeList *irList(void) {
    InterCodeList *h = xmalloc(sizeof *h);
    h->code = NULL;              /* 哨兵：code == NULL 是它的唯一标识 */
    h->next = h;
    h->prev = h;
    return h;
}

InterCodeList *irHead(InterCodeList *any) {
    InterCodeList *p;
    if (!any) return NULL;
    for (p = any; p->code != NULL; p = p->next)
        ;
    return p;
}

/* 追加到表尾，返回新结点（= 新尾）。
 * 先归一化到哨兵、再插在 h->prev 之后 —— 这与"插在 tail 之后"在 tail 就是当前尾
 * 时完全一致，而 tail 传哨兵时（空表/首次追加）也不会退化成"插在表头"。 */
InterCodeList *irAppend(InterCodeList *tail, InterCode *c) {
    InterCodeList *h = irHead(tail);
    InterCodeList *n = xmalloc(sizeof *n);
    n->code = c;
    n->prev = h->prev;
    n->next = h;
    h->prev->next = n;
    h->prev = n;
    return n;
}

/* 摘链并释放**指令与结点**。
 * ★ 刻意**不 free 操作数**：Operand 会在指令间共享（translate.c 把同一个 `place`
 *   操作数塞进多条指令，如 READ x 与随后的 x := #0），逐条释放会留下悬垂指针。
 *   编译器是一次性进程，操作数内存交给进程退出即可。 */
void deleteCode(InterCodeList *c) {
    if (!c || c->code == NULL) return;   /* NULL 与哨兵都不删（哨兵没有 code） */
    c->prev->next = c->next;
    c->next->prev = c->prev;
    free(c->code);
    free(c);
}

/* ============================================================================
 *  打印 —— 本模块唯一的对外行为
 *
 *  格式就是虚拟机的输入格式：真机装载器按**空白分词**逐行 sanity_check，
 *  并按下标登记标号。因此每条指令恰好一行、行尾恰好一个 \n、**不产生空行**
 *  （空行会被整行跳过，却仍占 codes 的下标，行号语义随之错位）。
 * ============================================================================ */

static void printOperand(FILE *out, Operand o) {
    /* ★ NULL 守卫：newCode 零初始化 ⇒ translate.c 若漏填某个操作数，这里拿到的是 NULL。
     * 刻意**响亮失败**而非打占位串：一行"非空但错"的指令（如 `? := #0`）正是本项目
     * 最怕的失效 —— 它能骗过整套打印比对、在真机上又是可执行但语义错的行。诊断 + abort
     * 才能把"漏填"当场暴露成本模块的缺陷，而不是一个难查的下游症状。
     * 合法输入的 o 恒非 NULL，故本守卫**不改变任何合法输出的字节**。 */
    if (!o) {
        fprintf(stderr, "internal error: printOperand got a NULL operand "
                        "(a code generator forgot to fill an operand)\n");
        abort();
    }
    switch (o->kind) {
    case OP_VAR:   fputs(o->name, out); break;
    case OP_CONST: fprintf(out, "#%d", o->value); break;
    case OP_LABEL: fputs(o->name, out); break;
    case OP_FUNC:  fputs(o->name, out); break;      /* 真实函数名，虚拟机靠它查 labelTable */
    case OP_ADDR:  fprintf(out, "&%s", o->name); break;
    }
}

void printIR(FILE *out, InterCodeList *any) {
    InterCodeList *h = irHead(any), *p;
    if (!h) return;
    for (p = h->next; p != h; p = p->next) {
        InterCode *c = p->code;
        switch (c->kind) {
        case IR_LABEL:    fprintf(out, "LABEL "); printOperand(out, c->u.one.x); fprintf(out, " :\n"); break;
        case IR_FUNCTION: fprintf(out, "FUNCTION "); printOperand(out, c->u.one.x); fprintf(out, " :\n"); break;
        case IR_ASSIGN:
            printOperand(out, c->u.assign.x); fprintf(out, " := ");
            printOperand(out, c->u.assign.y); fprintf(out, "\n"); break;
        case IR_ADDR:
            /* ★ 堵 `&&x`：本条指令的 `&` 由**本函数**打印，故 y 必须是**裸名字**的
             * OP_VAR。若 y 已是 OP_ADDR（它本身就渲染成 `&name`），会打出 `&&x` ——
             * 真机语法错、参考机装载器也会拒（表现为"莫名装载失败"）。
             * 响亮失败优于静默产出一行"非空但语义错"的指令（见 printOperand 的守卫说明）。 */
            if (c->u.assign.y && c->u.assign.y->kind == OP_ADDR) {
                fprintf(stderr, "internal error: IR_ADDR operand must be OP_VAR, not OP_ADDR "
                                "(would print \"&&x\")\n");
                abort();
            }
            printOperand(out, c->u.assign.x); fprintf(out, " := &");
            printOperand(out, c->u.assign.y); fprintf(out, "\n"); break;
        case IR_ASSIGN_DEREF:
            printOperand(out, c->u.deref.x); fprintf(out, " := *");
            printOperand(out, c->u.deref.y); fprintf(out, "\n"); break;
        case IR_DEREF_ASSIGN:
            fprintf(out, "*"); printOperand(out, c->u.deref.x); fprintf(out, " := ");
            printOperand(out, c->u.deref.y); fprintf(out, "\n"); break;
        case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV: {
            static const char *nm[4] = { "+", "-", "*", "/" };
            printOperand(out, c->u.binop.x); fprintf(out, " := ");
            printOperand(out, c->u.binop.y); fprintf(out, " %s ", nm[c->kind - IR_ADD]);
            printOperand(out, c->u.binop.z); fprintf(out, "\n"); break;
        }
        case IR_GOTO:   fprintf(out, "GOTO "); printOperand(out, c->u.one.x); fprintf(out, "\n"); break;
        case IR_IF:
            fprintf(out, "IF "); printOperand(out, c->u.iff.x);
            fprintf(out, " %s ", irRelopText(c->u.iff.relop));
            printOperand(out, c->u.iff.y); fprintf(out, " GOTO ");
            printOperand(out, c->u.iff.z); fprintf(out, "\n"); break;
        case IR_RETURN: fprintf(out, "RETURN "); printOperand(out, c->u.one.x); fprintf(out, "\n"); break;
        case IR_DEC:    fprintf(out, "DEC "); printOperand(out, c->u.dec.x);
                        fprintf(out, " %d\n", c->u.dec.size); break;
        case IR_ARG:    fprintf(out, "ARG "); printOperand(out, c->u.one.x); fprintf(out, "\n"); break;
        case IR_CALL:   printOperand(out, c->u.call.x); fprintf(out, " := CALL ");
                        printOperand(out, c->u.call.f); fprintf(out, "\n"); break;
        case IR_PARAM:  fprintf(out, "PARAM "); printOperand(out, c->u.one.x); fprintf(out, "\n"); break;
        case IR_READ:   fprintf(out, "READ "); printOperand(out, c->u.one.x); fprintf(out, "\n"); break;
        case IR_WRITE:  fprintf(out, "WRITE "); printOperand(out, c->u.one.x); fprintf(out, "\n"); break;
        }
    }
}
