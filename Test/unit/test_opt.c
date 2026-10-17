/* 优化框架单测（Task 16）。
 *
 * 被测对象只有“框架”本身，不测任何实际优化（O1–O9 在 Task 17–19 才实现）：
 *   1) irIsTemp 的名字判定（t1/t12 真；t/temp/t1x/label1/v1 假）；
 *   2) irCountOperandUses 的“只数读、不数定义”口径与同行多次出现计数；
 *   3) 全桩时的 no-op：optimizeIR 前后 IR 逐字节不变；
 *   4) 临时变量单赋值断言“会响”：--double-assign / --use-before-def 必须 abort。
 *
 * ★ 独立可执行 + 子模式，而不是把 abort 用例并进普通用例：abort() 会结束整个进程，
 *   并进主流程会让后面的用例一条都不跑（与 test_ir_abort 的理由相同）。
 *
 * ★ 输入用**字符串形式的 IR**（按 printIR 的格式），由下面这个极小解析器建成链表。
 *   这样 Task 17–19 加用例时只添几行字符串，不必再学一遍构造 API；解析器覆盖
 *   ir.h 里全部 19 种指令形状，遇到不认识的形状直接 abort（不静默兜底）。
 *
 * 通过时 stdout/stderr 都不写（PASS 由 Makefile 打印）；失败诊断走 stderr、返回 1。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ir.h"
#include "optimize.h"

#define MAX_TOKENS 16
#define TOKEN_MAX  64

static int failures = 0;

static void check_int(const char *name, int got, int want) {
    if (got != want) {
        fprintf(stderr, "FAIL %s: got %d, want %d\n", name, got, want);
        failures++;
    }
}

/* ── 迷你 IR 解析器 ─────────────────────────────────────────────────────── */

static void die(const char *msg, const char *detail) {
    fprintf(stderr, "test_opt: %s: %s\n", msg, detail ? detail : "");
    abort();
}

static int splitTokens(const char *line, char tok[MAX_TOKENS][TOKEN_MAX]) {
    char buf[512];
    char *p;
    int n = 0;

    if (strlen(line) >= sizeof buf) die("line too long", line);
    strcpy(buf, line);
    for (p = strtok(buf, " \t\r\n"); p; p = strtok(NULL, " \t\r\n")) {
        if (n >= MAX_TOKENS) die("too many tokens", line);
        if (strlen(p) >= TOKEN_MAX) die("token too long", line);
        strcpy(tok[n++], p);
    }
    return n;
}

/* `#n` → 常量；`&x` → 地址；`label…` → 标号；其余 → 变量（含 tN）。 */
static Operand parseOperand(const char *tok) {
    if (tok[0] == '#') return opConst(atoi(tok + 1));
    if (tok[0] == '&') return opAddr(tok + 1);
    if (strncmp(tok, "label", 5) == 0) return opLabel(tok);
    return opVar(tok);
}

static int binopKind(const char *op, const char *line) {
    if (strcmp(op, "+") == 0) return IR_ADD;
    if (strcmp(op, "-") == 0) return IR_SUB;
    if (strcmp(op, "*") == 0) return IR_MUL;
    if (strcmp(op, "/") == 0) return IR_DIV;
    die("unknown binop", line);
    return 0;   /* 不可达；仅为消除“可能无返回值”的警告 */
}

/* 解析一行 IR（格式严格照 printIR）；未知形状 → abort。 */
static InterCode *parseLine(const char *line) {
    char tk[MAX_TOKENS][TOKEN_MAX];
    int n = splitTokens(line, tk);
    InterCode *c;

    if (n == 0) die("empty line", line);

    if (strcmp(tk[0], "LABEL") == 0 || strcmp(tk[0], "FUNCTION") == 0) {
        if (n != 3 || strcmp(tk[2], ":") != 0) die("bad LABEL/FUNCTION", line);
        c = newCode(tk[0][0] == 'L' ? IR_LABEL : IR_FUNCTION);
        c->u.one.x = (c->kind == IR_LABEL) ? opLabel(tk[1]) : opFunc(tk[1]);
        return c;
    }
    if (strcmp(tk[0], "GOTO") == 0) {
        if (n != 2) die("bad GOTO", line);
        c = newCode(IR_GOTO);
        c->u.one.x = opLabel(tk[1]);
        return c;
    }
    if (strcmp(tk[0], "RETURN") == 0 || strcmp(tk[0], "ARG") == 0 ||
        strcmp(tk[0], "PARAM") == 0 || strcmp(tk[0], "READ") == 0 ||
        strcmp(tk[0], "WRITE") == 0) {
        int kind;
        if (n != 2) die("bad unary statement", line);
        if (strcmp(tk[0], "RETURN") == 0) kind = IR_RETURN;
        else if (strcmp(tk[0], "ARG") == 0) kind = IR_ARG;
        else if (strcmp(tk[0], "PARAM") == 0) kind = IR_PARAM;
        else if (strcmp(tk[0], "READ") == 0) kind = IR_READ;
        else kind = IR_WRITE;
        c = newCode(kind);
        c->u.one.x = parseOperand(tk[1]);
        return c;
    }
    if (strcmp(tk[0], "DEC") == 0) {
        if (n != 3) die("bad DEC", line);
        c = newCode(IR_DEC);
        c->u.dec.x = parseOperand(tk[1]);
        c->u.dec.size = atoi(tk[2]);
        return c;
    }
    if (strcmp(tk[0], "IF") == 0) {
        if (n != 6 || strcmp(tk[4], "GOTO") != 0) die("bad IF", line);
        c = newCode(IR_IF);
        c->u.iff.x = parseOperand(tk[1]);
        c->u.iff.relop = irRelopCode(tk[2]);
        c->u.iff.y = parseOperand(tk[3]);
        c->u.iff.z = opLabel(tk[5]);
        return c;
    }

    /* 其余都以 `:=` 出现在第 2 个词的位置（含 `*x := y`）。 */
    if (n >= 3 && strcmp(tk[1], ":=") == 0) {
        if (tk[0][0] == '*') {                       /* *x := y */
            if (n != 3) die("bad DEREF_ASSIGN", line);
            c = newCode(IR_DEREF_ASSIGN);
            c->u.deref.x = parseOperand(tk[0] + 1);
            c->u.deref.y = parseOperand(tk[2]);
            return c;
        }
        if (n == 3) {
            if (tk[2][0] == '&') {                   /* x := &y（y 必须是裸名字） */
                c = newCode(IR_ADDR);
                c->u.assign.x = parseOperand(tk[0]);
                c->u.assign.y = opVar(tk[2] + 1);
            } else if (tk[2][0] == '*') {            /* x := *y */
                c = newCode(IR_ASSIGN_DEREF);
                c->u.deref.x = parseOperand(tk[0]);
                c->u.deref.y = parseOperand(tk[2] + 1);
            } else {                                 /* x := y */
                c = newCode(IR_ASSIGN);
                c->u.assign.x = parseOperand(tk[0]);
                c->u.assign.y = parseOperand(tk[2]);
            }
            return c;
        }
        if (n == 4 && strcmp(tk[2], "CALL") == 0) {  /* x := CALL f */
            c = newCode(IR_CALL);
            c->u.call.x = parseOperand(tk[0]);
            c->u.call.f = opFunc(tk[3]);
            return c;
        }
        if (n == 5) {                                /* x := y op z */
            c = newCode(binopKind(tk[3], line));
            c->u.binop.x = parseOperand(tk[0]);
            c->u.binop.y = parseOperand(tk[2]);
            c->u.binop.z = parseOperand(tk[4]);
            return c;
        }
    }
    die("unknown IR shape", line);
    return NULL;   /* 不可达 */
}

static InterCodeList *buildIR(const char *const *lines) {
    InterCodeList *t = irList();
    int i;
    for (i = 0; lines[i]; i++)
        t = irAppend(t, parseLine(lines[i]));
    return t;
}

/* ── 比对 helper ───────────────────────────────────────────────────────── */

static char *renderIR(InterCodeList *t) {
    FILE *f = tmpfile();
    long sz;
    char *buf;

    if (!f) die("tmpfile failed", NULL);
    printIR(f, t);
    if (fseek(f, 0, SEEK_END) != 0) die("fseek failed", NULL);
    sz = ftell(f);
    if (sz < 0) die("ftell failed", NULL);
    rewind(f);
    buf = (char *)malloc((size_t)sz + 1);
    if (!buf) die("malloc failed", NULL);
    if (sz > 0 && fread(buf, 1, (size_t)sz, f) != (size_t)sz) die("fread failed", NULL);
    buf[sz] = '\0';
    fclose(f);
    return buf;
}

static char *joinLines(const char *const *lines) {
    size_t total = 0;
    char *buf, *p;
    int i;

    for (i = 0; lines[i]; i++) total += strlen(lines[i]) + 1;   /* 每行一个 \n */
    buf = (char *)malloc(total + 1);
    if (!buf) die("malloc failed", NULL);
    p = buf;
    for (i = 0; lines[i]; i++) {
        size_t len = strlen(lines[i]);
        memcpy(p, lines[i], len);
        p += len;
        *p++ = '\n';
    }
    *p = '\0';
    return buf;
}

/* optimizeIR 后 printIR 到 tmpfile，与 want_lines 逐字节比较。 */
static void check_ir(const char *name, const char *const *in, const char *const *want) {
    InterCodeList *t = buildIR(in);
    char *got, *exp;

    optimizeIR(t);
    got = renderIR(t);
    exp = joinLines(want);
    if (strcmp(got, exp) != 0) {
        fprintf(stderr, "FAIL %s\n--- got ---\n%s--- want ---\n%s", name, got, exp);
        failures++;
    }
    free(got);
    free(exp);
}

/* ── 用例 ───────────────────────────────────────────────────────────────── */

static void test_is_temp(void) {
    check_int("t1 是临时变量",       irIsTemp(opVar("t1")), 1);
    check_int("t12 是临时变量",      irIsTemp(opVar("t12")), 1);
    check_int("t0 是临时变量",       irIsTemp(opVar("t0")), 1);
    check_int("t 不是临时变量",      irIsTemp(opVar("t")), 0);
    check_int("temp 不是临时变量",   irIsTemp(opVar("temp")), 0);
    check_int("t1x 不是临时变量",    irIsTemp(opVar("t1x")), 0);
    check_int("t12a 不是临时变量",   irIsTemp(opVar("t12a")), 0);
    check_int("label1 不是临时变量", irIsTemp(opVar("label1")), 0);
    check_int("v1 不是临时变量",     irIsTemp(opVar("v1")), 0);
    check_int("常量不是临时变量",    irIsTemp(opConst(1)), 0);
    check_int("标号 t1 不是临时变量", irIsTemp(opLabel("t1")), 0);
    check_int("地址 t1 不是临时变量", irIsTemp(opAddr("t1")), 0);
}

static void test_count_uses(void) {
    /* brief Step 5 用例 2：t1 读 1 次、v1 读 1 次。 */
    static const char *const prog[] = {
        "t1 := #5",
        "v1 := t1",
        "RETURN v1",
        NULL
    };
    /* 加一行 `v1 := v1`：右侧 v1 是读，左侧 v1 是定义，不数。 */
    static const char *const prog_self[] = {
        "t1 := #5",
        "v1 := t1",
        "v1 := v1",
        "RETURN v1",
        NULL
    };
    /* 读操作数口径表：覆盖每种指令的“数 / 不数”。 */
    static const char *const prog_table[] = {
        "FUNCTION f :",
        "DEC d1 8",
        "t1 := #5",
        "v1 := &av",
        "w1 := *t1",
        "*w2 := v2",
        "s1 := s2 + s2",
        "IF a1 < a2 GOTO label1",
        "x1 := CALL g",
        "READ r1",
        "ARG &av",
        "PARAM p1",
        "WRITE v1",
        "GOTO label2",
        "LABEL label3 :",
        "RETURN v1",
        NULL
    };
    InterCodeList *t;
    InterCodeList *tt;

    t = buildIR(prog);
    check_int("t1 读 1 次", irCountOperandUses(t, opVar("t1")), 1);
    check_int("v1 读 1 次", irCountOperandUses(t, opVar("v1")), 1);
    check_int("未出现的 v2 读 0 次", irCountOperandUses(t, opVar("v2")), 0);

    tt = buildIR(prog_self);
    check_int("v1 赋给自己：读 2 次（右侧 + RETURN，左侧不数）",
              irCountOperandUses(tt, opVar("v1")), 2);
    check_int("加自赋值后 t1 仍读 1 次", irCountOperandUses(tt, opVar("t1")), 1);

    t = buildIR(prog_table);
    check_int("DEC 的 x 不算读",            irCountOperandUses(t, opVar("d1")), 0);
    check_int("ADDR 读 y（裸名字）",        irCountOperandUses(t, opVar("av")), 1);
    /* `&av` 是 OP_ADDR，与 IR_ADDR 的裸名字 OP_VAR 是不同操作数：只数 ARG 那次。 */
    check_int("ARG &av 读地址操作数",       irCountOperandUses(t, opAddr("av")), 1);
    check_int("赋值目标不算读",             irCountOperandUses(t, opVar("v1")), 2); /* WRITE + RETURN */
    check_int("ASSIGN_DEREF 读地址",        irCountOperandUses(t, opVar("t1")), 1);
    check_int("ASSIGN_DEREF 目标不算读",    irCountOperandUses(t, opVar("w1")), 0);
    check_int("DEREF_ASSIGN 读地址 x",      irCountOperandUses(t, opVar("w2")), 1);
    check_int("DEREF_ASSIGN 读值 y",        irCountOperandUses(t, opVar("v2")), 1);
    check_int("同一行出现两次就数两次",     irCountOperandUses(t, opVar("s2")), 2);
    check_int("ARITH 目标不算读",           irCountOperandUses(t, opVar("s1")), 0);
    check_int("IF 读 x",                    irCountOperandUses(t, opVar("a1")), 1);
    check_int("IF 读 y",                    irCountOperandUses(t, opVar("a2")), 1);
    check_int("IF 读标号 z",                irCountOperandUses(t, opLabel("label1")), 1);
    check_int("CALL 目标不算读",            irCountOperandUses(t, opVar("x1")), 0);
    check_int("CALL 函数名不算读",          irCountOperandUses(t, opFunc("g")), 0);
    check_int("READ 的 x 算读",             irCountOperandUses(t, opVar("r1")), 1);
    check_int("PARAM 读 x",                 irCountOperandUses(t, opVar("p1")), 1);
    check_int("GOTO 读标号",                irCountOperandUses(t, opLabel("label2")), 1);
    check_int("LABEL 名字不算读",           irCountOperandUses(t, opLabel("label3")), 0);
    check_int("FUNCTION 名字不算读",        irCountOperandUses(t, opFunc("f")), 0);
    check_int("空表返回 0",                 irCountOperandUses(irList(), opVar("t1")), 0);
}

static void test_noop(void) {
    /* brief Step 5 用例 3：所有 pass 是桩 ⇒ 逐字节不变。 */
    static const char *const plain[] = {
        "v1 := #5",
        "RETURN v1",
        NULL
    };
    /* 含临时变量：顺带证明合法 IR 能通过单赋值断言。 */
    static const char *const with_temp[] = {
        "t1 := #5",
        "v1 := t1",
        "RETURN v1",
        NULL
    };
    /* 普通变量允许被多次赋值：断言只射临时变量，不能误伤 vN。 */
    static const char *const var_redef[] = {
        "FUNCTION f :",
        "v1 := #5",
        "v1 := #6",
        "RETURN v1",
        NULL
    };
    /* 指导书表 2 的“条件表达式作为值”模板：同一个 tN 先 #0 后 #1（跨 LABEL）。
     * 这是单赋值断言**唯一**允许的例外（见 optimize.c 的 isCondValuePair）。 */
    static const char *const cond_value[] = {
        "t1 := #0",
        "IF v1 != #0 GOTO label1",
        "GOTO label2",
        "LABEL label1 :",
        "t1 := #1",
        "LABEL label2 :",
        "IF t1 != #0 GOTO label3",
        "GOTO label4",
        "LABEL label3 :",
        "WRITE #1",
        "GOTO label5",
        "LABEL label4 :",
        "WRITE #0",
        "LABEL label5 :",
        "RETURN #0",
        NULL
    };

    check_ir("no-op（普通变量）", plain, plain);
    check_ir("no-op（临时变量，断言通过）", with_temp, with_temp);
    check_ir("no-op（普通变量重复赋值不误报）", var_redef, var_redef);
    check_ir("no-op（条件取值模板 #0/#1 不误报）", cond_value, cond_value);
}

/* 失败模式：必须 abort。若 optimizeIR 正常返回，本函数以 1 退出（Makefile 判红）。 */
static void run_abort_case(int use_before_def) {
    InterCodeList *t = irList();
    InterCode *c;

    if (!use_before_def) {
        c = newCode(IR_ASSIGN);
        c->u.assign.x = opVar("t1"); c->u.assign.y = opConst(5);
        irAppend(t, c);
        c = newCode(IR_ASSIGN);
        c->u.assign.x = opVar("t1"); c->u.assign.y = opConst(6);
        irAppend(t, c);
        c = newCode(IR_RETURN);
        c->u.one.x = opVar("t1");
        irAppend(t, c);
    } else {
        c = newCode(IR_RETURN);
        c->u.one.x = opVar("t1");
        irAppend(t, c);
        c = newCode(IR_ASSIGN);
        c->u.assign.x = opVar("t1"); c->u.assign.y = opConst(5);
        irAppend(t, c);
    }

    optimizeIR(t);   /* 必须在此 abort（stderr 有诊断） */

    fprintf(stderr, "FAIL %s: optimizeIR 未 abort\n",
            use_before_def ? "--use-before-def" : "--double-assign");
    exit(1);
}

int main(int argc, char **argv) {
    if (argc > 1) {
        if (strcmp(argv[1], "--double-assign") == 0) {
            run_abort_case(0);
        } else if (strcmp(argv[1], "--use-before-def") == 0) {
            run_abort_case(1);
        } else {
            fprintf(stderr, "usage: %s [--double-assign|--use-before-def]\n", argv[0]);
            return 2;
        }
        return 1;   /* 不可达：run_abort_case 要么 abort 要么 exit(1) */
    }

    test_is_temp();
    test_count_uses();
    test_noop();

    if (failures) {
        fprintf(stderr, "FAIL opt: %d 项失败\n", failures);
        return 1;
    }
    return 0;
}
