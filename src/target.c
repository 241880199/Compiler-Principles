#include "target.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 *  实验四：IR -> MIPS32 目标代码生成
 *
 *  设计要点（详见 docs/superpowers/specs/2026-11-05-cmm-lab4-design.md）：
 *
 *  1. 采用指导书 5.2.4 的**朴素寄存器分配**：所有变量/临时变量都驻栈，
 *     每条 IR 先把操作数装进 $t0/$t1，算完把结果写回栈槽。
 *     寄存器只做指令间的临时周转，因此不需要寄存器描述符、不需要溢出决策。
 *
 *  2. 调用约定（实验允许不遵循 MIPS ABI）：调用者把**全部实参压栈**，
 *     被调用者从 0($fp)、4($fp)… 取形参；返回值放 $v0。
 *     所有局部访问一律经 $fp，因此函数调用/递归不会改变局部变量的偏移。
 *
 *  3. 活动记录：
 *
 *         0($fp)   第 1 个实参（调用者压入）
 *         4($fp)   第 2 个实参 …
 *        -4($fp)   旧 $ra
 *        -8($fp)   旧 $fp
 *        -12($fp)  起：局部变量 / 临时变量 / DEC 数组（向低地址增长）
 *
 *  4. 除法按**课程虚拟机语义**（向下取整）实现，与实验三的 .exp 一致；
 *     MIPS 的 div 是向零截断，故在余数非零且两操作数异号时把商减 1。
 *
 *  本文件只读 InterCodeList，不修改 IR，也不直接依赖前端任何数据结构。
 * ============================================================================ */

#define MAX_SLOTS  8192
#define MAX_PARAMS 1024
#define MAX_SYMBOL 512

typedef struct {
    const char *name;
    int offset;          /* 相对 $fp 的字节偏移（负数） */
    int size;            /* 字节数；标量 4，数组由 DEC 给出 */
} Slot;

typedef struct {
    const char *name;    /* 源码里的函数名 */
    char symbol[MAX_SYMBOL]; /* 汇编里的函数标签；见 funcSymbol */
    Slot slots[MAX_SLOTS];
    int  nslots;
    const char *params[MAX_PARAMS];
    int  nparams;
    int  frameSize;
} FuncInfo;

static void internalError(const char *msg) {
    fprintf(stderr, "internal error (target): %s\n", msg);
    abort();
}

/* 源码函数名不能直接当汇编标签：函数名 `add` 会被 SPIM 当成指令助记符，
 * 报 "syntax error on line ... add:"。故除入口 main 外一律加前缀，
 * 前缀本身不可能与 C-- 标识符或 MIPS 助记符相撞。 */
static const char *funcSymbol(const char *name, char *buf, size_t n) {
    int len = snprintf(buf, n, "%s%s", strcmp(name, "main") == 0 ? "" : "__cmm_", name);
    if (len < 0 || (size_t)len >= n)
        internalError("函数名过长，无法生成汇编标签");
    return buf;
}

/* ── 槽位表 ──────────────────────────────────────────────────────────────── */

static Slot *findSlot(FuncInfo *fi, const char *name) {
    int i;
    for (i = 0; i < fi->nslots; i++)
        if (strcmp(fi->slots[i].name, name) == 0)
            return &fi->slots[i];
    return NULL;
}

static int slotOffset(FuncInfo *fi, const char *name) {
    Slot *s = findSlot(fi, name);
    if (!s) {
        fprintf(stderr, "internal error (target): 变量 \"%s\" 没有分配栈槽\n", name);
        abort();
    }
    return s->offset;
}

/* 登记一个变量名。已存在时只把 size 取较大值（DEC 可能晚于首次使用）；
 * 偏移在全部扫描完成后统一分配。 */
static void addSlot(FuncInfo *fi, const char *name, int size) {
    Slot *s;
    if (!name) internalError("addSlot: NULL name");
    s = findSlot(fi, name);
    if (s) {
        if (size > s->size) s->size = size;
        return;
    }
    if (fi->nslots >= MAX_SLOTS) {
        fprintf(stderr, "internal error (target): 变量数超过 %d\n", MAX_SLOTS);
        abort();
    }
    fi->slots[fi->nslots].name = name;
    fi->slots[fi->nslots].offset = 0;
    fi->slots[fi->nslots].size = size;
    fi->nslots++;
}

/* 操作数里出现的变量名登记为 4 字节；常量/标号/函数名不占栈槽。 */
static void noteOperand(FuncInfo *fi, Operand o) {
    if (!o) internalError("noteOperand: NULL operand");
    if (o->kind == OP_VAR || o->kind == OP_ADDR)
        addSlot(fi, o->name, 4);
}

/* 一条指令中所有需要栈槽的操作数。IR_LABEL/IR_FUNCTION 的名字不是变量。 */
static void scanCode(FuncInfo *fi, InterCode *c) {
    switch (c->kind) {
    case IR_LABEL:
    case IR_FUNCTION:
        break;
    case IR_ASSIGN:
    case IR_ADDR:
        noteOperand(fi, c->u.assign.x);
        noteOperand(fi, c->u.assign.y);
        break;
    case IR_ASSIGN_DEREF:
    case IR_DEREF_ASSIGN:
        noteOperand(fi, c->u.deref.x);
        noteOperand(fi, c->u.deref.y);
        break;
    case IR_ADD:
    case IR_SUB:
    case IR_MUL:
    case IR_DIV:
        noteOperand(fi, c->u.binop.x);
        noteOperand(fi, c->u.binop.y);
        noteOperand(fi, c->u.binop.z);
        break;
    case IR_GOTO:
    case IR_RETURN:
    case IR_ARG:
    case IR_READ:
    case IR_WRITE:
        noteOperand(fi, c->u.one.x);
        break;
    case IR_IF:
        noteOperand(fi, c->u.iff.x);
        noteOperand(fi, c->u.iff.y);
        /* z 是标号，不占栈槽 */
        break;
    case IR_DEC:
        noteOperand(fi, c->u.dec.x);
        addSlot(fi, c->u.dec.x->name, c->u.dec.size);   /* 数组总字节数 */
        break;
    case IR_CALL:
        noteOperand(fi, c->u.call.x);                   /* f 是函数名 */
        break;
    case IR_PARAM:
        noteOperand(fi, c->u.one.x);
        break;
    }
}

/* 函数体扫描：params 按 PARAM 出现顺序记录，槽位按首次出现顺序记录。 */
static void buildFuncInfo(FuncInfo *fi, const char *name,
                          InterCodeList *start, InterCodeList *stop) {
    InterCodeList *p;
    int i, cur;

    fi->name = name;
    funcSymbol(name, fi->symbol, sizeof fi->symbol);
    fi->nslots = 0;
    fi->nparams = 0;

    for (p = start; p != stop; p = p->next) {
        InterCode *c = p->code;
        if (c->kind == IR_PARAM) {
            Operand o = c->u.one.x;
            if (o->kind != OP_VAR) internalError("PARAM 的操作数不是 OP_VAR");
            if (fi->nparams >= MAX_PARAMS) {
                fprintf(stderr, "internal error (target): 形参个数超过 %d\n", MAX_PARAMS);
                abort();
            }
            fi->params[fi->nparams++] = o->name;
        }
        scanCode(fi, c);
    }

    /* 从 -12 起向下分配；先加 size 再取负，槽区为 [offset, offset+size)。 */
    cur = 8;                               /* 旧 $ra + 旧 $fp */
    for (i = 0; i < fi->nslots; i++) {
        cur += fi->slots[i].size;
        fi->slots[i].offset = -cur;
    }
    fi->frameSize = (cur + 7) & ~7;        /* 8 字节对齐 */
}

/* ── 操作数装载/写回 ─────────────────────────────────────────────────────── */

static void loadOperand(FILE *out, FuncInfo *fi, Operand o, const char *reg) {
    switch (o->kind) {
    case OP_CONST:
        fprintf(out, "  li %s, %d\n", reg, o->value);
        break;
    case OP_VAR:
        fprintf(out, "  lw %s, %d($fp)\n", reg, slotOffset(fi, o->name));
        break;
    case OP_ADDR:
        /* `&x`：局部数组/变量的栈地址。 */
        fprintf(out, "  addiu %s, $fp, %d\n", reg, slotOffset(fi, o->name));
        break;
    default:
        internalError("loadOperand: 操作数不是变量/常量/地址");
    }
}

static void storeOperand(FILE *out, FuncInfo *fi, Operand o, const char *reg) {
    if (o->kind != OP_VAR)
        internalError("storeOperand: 目标不是 OP_VAR");
    fprintf(out, "  sw %s, %d($fp)\n", reg, slotOffset(fi, o->name));
}

/* ── 指令选择 ────────────────────────────────────────────────────────────── */

static int divLabelSeq;

static void emitDiv(FILE *out) {
    int id = ++divLabelSeq;
    /* $t0 = 被除数，$t1 = 除数；结果放 $t2（向下取整）。 */
    fprintf(out, "  div $t0, $t1\n");
    fprintf(out, "  mflo $t2\n");
    fprintf(out, "  mfhi $t3\n");
    fprintf(out, "  beq $t3, $zero, .Ldiv_%d\n", id);
    fprintf(out, "  xor $t3, $t0, $t1\n");
    fprintf(out, "  bgez $t3, .Ldiv_%d\n", id);
    fprintf(out, "  addi $t2, $t2, -1\n");
    fprintf(out, ".Ldiv_%d:\n", id);
}

static void emitCode(FILE *out, FuncInfo *fi, InterCode *c, int *pendingArgs) {
    switch (c->kind) {
    case IR_LABEL:
        fprintf(out, "%s:\n", c->u.one.x->name);
        break;

    case IR_FUNCTION:
        internalError("emitCode 收到 FUNCTION（应由 generateMIPS 处理）");
        break;

    case IR_ASSIGN:
        loadOperand(out, fi, c->u.assign.y, "$t0");
        storeOperand(out, fi, c->u.assign.x, "$t0");
        break;

    case IR_ADDR:
        fprintf(out, "  addiu $t0, $fp, %d\n",
                slotOffset(fi, c->u.assign.y->name));
        storeOperand(out, fi, c->u.assign.x, "$t0");
        break;

    case IR_ASSIGN_DEREF:                       /* x := *y */
        loadOperand(out, fi, c->u.deref.y, "$t0");
        fputs("  lw $t0, 0($t0)\n", out);
        storeOperand(out, fi, c->u.deref.x, "$t0");
        break;

    case IR_DEREF_ASSIGN:                       /* *x := y */
        loadOperand(out, fi, c->u.deref.x, "$t0");
        loadOperand(out, fi, c->u.deref.y, "$t1");
        fputs("  sw $t1, 0($t0)\n", out);
        break;

    case IR_ADD:
    case IR_SUB:
        loadOperand(out, fi, c->u.binop.y, "$t0");
        if (c->u.binop.z->kind == OP_CONST &&
            c->u.binop.z->value >= -32768 && c->u.binop.z->value <= 32767) {
            int k = c->u.binop.z->value;
            if (c->kind == IR_SUB) {
                if (k == -32768) {              /* -k 超出 addi 立即数范围 */
                    loadOperand(out, fi, c->u.binop.z, "$t1");
                    fputs("  sub $t0, $t0, $t1\n", out);
                    goto binStore;
                }
                k = -k;
            }
            fprintf(out, "  addi $t0, $t0, %d\n", k);
        } else {
            loadOperand(out, fi, c->u.binop.z, "$t1");
            fprintf(out, "  %s $t0, $t0, $t1\n",
                    c->kind == IR_ADD ? "add" : "sub");
        }
    binStore:
        storeOperand(out, fi, c->u.binop.x, "$t0");
        break;

    case IR_MUL:
        loadOperand(out, fi, c->u.binop.y, "$t0");
        loadOperand(out, fi, c->u.binop.z, "$t1");
        fputs("  mult $t0, $t1\n  mflo $t2\n", out);
        storeOperand(out, fi, c->u.binop.x, "$t2");
        break;

    case IR_DIV:
        loadOperand(out, fi, c->u.binop.y, "$t0");
        loadOperand(out, fi, c->u.binop.z, "$t1");
        emitDiv(out);
        storeOperand(out, fi, c->u.binop.x, "$t2");
        break;

    case IR_GOTO:
        fprintf(out, "  j %s\n", c->u.one.x->name);
        break;

    case IR_IF: {
        static const char *const br[6] = {
            "blt", "ble", "bgt", "bge", "beq", "bne"
        };
        loadOperand(out, fi, c->u.iff.x, "$t0");
        loadOperand(out, fi, c->u.iff.y, "$t1");
        fprintf(out, "  %s $t0, $t1, %s\n",
                br[c->u.iff.relop], c->u.iff.z->name);
        break;
    }

    case IR_RETURN:
        loadOperand(out, fi, c->u.one.x, "$v0");
        fprintf(out, "  j .Lret_%s\n", fi->symbol);
        break;

    case IR_DEC:
        /* 存储已在预扫描中分配，无指令。 */
        break;

    case IR_ARG:
        loadOperand(out, fi, c->u.one.x, "$t0");
        fputs("  addiu $sp, $sp, -4\n  sw $t0, 0($sp)\n", out);
        (*pendingArgs)++;
        break;

    case IR_CALL: {
        char sym[MAX_SYMBOL];
        fprintf(out, "  jal %s\n", funcSymbol(c->u.call.f->name, sym, sizeof sym));
        if (*pendingArgs > 0) {
            fprintf(out, "  addiu $sp, $sp, %d\n", 4 * (*pendingArgs));
            *pendingArgs = 0;
        }
        storeOperand(out, fi, c->u.call.x, "$v0");
        break;
    }

    case IR_PARAM:
        /* prologue 已从 0($fp)、4($fp)… 拷入形参槽。 */
        break;

    case IR_READ:
        fputs("  li $v0, 5\n  syscall\n", out);
        storeOperand(out, fi, c->u.one.x, "$v0");
        break;

    case IR_WRITE:
        loadOperand(out, fi, c->u.one.x, "$a0");
        /* print_int，然后 print_char('\n')；与实验三虚拟机的 WRITE 一致。 */
        fputs("  li $v0, 1\n  syscall\n"
              "  li $v0, 11\n  li $a0, 10\n  syscall\n", out);
        break;
    }
}

/* ── 函数输出 ────────────────────────────────────────────────────────────── */

static void emitFunction(FILE *out, FuncInfo *fi,
                         InterCodeList *start, InterCodeList *stop) {
    InterCodeList *p;
    int pendingArgs = 0;
    int i;

    fprintf(out, "%s:\n", fi->symbol);
    fprintf(out, "  addiu $sp, $sp, -%d\n", fi->frameSize);
    fprintf(out, "  sw $ra, %d($sp)\n", fi->frameSize - 4);
    fprintf(out, "  sw $fp, %d($sp)\n", fi->frameSize - 8);
    fprintf(out, "  addiu $fp, $sp, %d\n", fi->frameSize);

    /* 形参：第 i 个实参在 4*i($fp)（调用者按源码逆序压栈，栈顶是第 1 个）。 */
    for (i = 0; i < fi->nparams; i++) {
        fprintf(out, "  lw $t0, %d($fp)\n", 4 * i);
        fprintf(out, "  sw $t0, %d($fp)\n", slotOffset(fi, fi->params[i]));
    }

    for (p = start; p != stop; p = p->next)
        emitCode(out, fi, p->code, &pendingArgs);

    /* 统一出口：多个 RETURN 都跳到这里。 */
    fprintf(out, ".Lret_%s:\n", fi->symbol);
    fputs("  lw $ra, -4($fp)\n"
          "  move $sp, $fp\n"
          "  lw $fp, -8($sp)\n"
          "  jr $ra\n", out);
}

/* start 起（含）到下一个 FUNCTION 之前（不含）的边界。 */
static InterCodeList *findNextFunction(InterCodeList *start, InterCodeList *head) {
    InterCodeList *p;
    for (p = start; p != head; p = p->next)
        if (p->code->kind == IR_FUNCTION)
            return p;
    return head;
}

void generateMIPS(FILE *out, InterCodeList *ir) {
    static FuncInfo fi;
    InterCodeList *head = irHead(ir), *p;

    if (!head) return;

    fputs(".data\n.text\n.globl main\n", out);

    p = head->next;
    while (p != head) {
        InterCodeList *stop;
        if (p->code->kind != IR_FUNCTION) {
            p = p->next;
            continue;
        }
        stop = findNextFunction(p->next, head);
        buildFuncInfo(&fi, p->code->u.one.x->name, p->next, stop);
        emitFunction(out, &fi, p->next, stop);
        p = stop;
    }
}
