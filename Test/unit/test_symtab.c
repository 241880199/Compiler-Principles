#include <stdio.h>
#include "symtab.h"

static int failed = 0;

static void check(int cond, const char *what) {
    if (!cond) { printf("FAIL %s\n", what); failed = 1; }
    else       { printf("ok   %s\n", what); }
}

int main(void) {
    Type intT = typeBasic(BT_INT);
    SymEntry *e;
    int code;

    symInit();

    /* ── 全局填入与查找 ── */
    e = symEnter("g", SYM_VAR, intT, 1, &code);
    check(code == 0 && e && e->kind == SYM_VAR, "全局变量 g 填入无冲突");
    check(symLookup("g") == e, "查得到 g");
    check(symLookup("nope") == NULL, "查不到 nope");

    /* ── 全局作用域内重复定义 → 错误 3 ── */
    symEnter("g", SYM_VAR, intT, 2, &code);
    check(code == 3, "全局重复定义变量 → 错误 3");
    check(symLookup("g") == e, "重复定义后保留最早那个");

    /* ── 冲突码按后定义者的种类（设计文档 §8 第 1 条）── */
    symEnter("S", SYM_STRUCT, typeStruct("S", NULL), 3, &code);
    check(code == 0, "填入结构体 S");
    symEnter("S", SYM_VAR, intT, 4, &code);
    check(code == 3, "变量与已有结构体同名 → 错误 3");
    symEnter("S", SYM_STRUCT, typeStruct("S2", NULL), 5, &code);
    check(code == 16, "结构体与已有结构体同名 → 错误 16");
    symEnter("S", SYM_FUNC, intT, 6, &code);
    check(code == 4, "函数与已有名字冲突 → 错误 4");

    /* ── 嵌套作用域：遮蔽 ── */
    scopeEnter();                                   /* 深度 1，模拟函数作用域 */
    check(scopeDepth() == 1, "scopeEnter 后深度为 1");
    e = symEnter("g", SYM_VAR, intT, 10, &code);
    check(code == 0, "内层重定义外层同名变量不冲突（选做 2.2）");
    check(symLookup("g") == e, "内层遮蔽：查到内层的 g");
    check(symLookupLocal("g") == e, "symLookupLocal 查到内层的 g");

    /* ── 同一作用域内重复定义仍报 3 ── */
    symEnter("g", SYM_VAR, intT, 11, &code);
    check(code == 3, "同一作用域内重复定义 → 错误 3");

    /* ── 不同函数体的局部变量可重名 ── */
    scopeExit();
    check(scopeDepth() == 0, "scopeExit 后深度为 0");
    check(symLookup("g") != e, "退层后内层的 g 已消亡");
    scopeEnter();
    e = symEnter("g", SYM_VAR, intT, 20, &code);
    check(code == 0, "另一个函数体内同名局部变量不冲突");

    /* ── 退层后重进同深度作用域：旧局部的名字不得复活 ──
     * 上面那条"退层后内层的 g 已消亡"是在 curDepth==0 时查的，那时 curDepth 为 0，
     * symLookup 的 depth 守卫**自己**就能把 depth==1 的残留挡掉 —— 于是
     * "scopeExit 忘了把条目从散列槽摘除"这个变异能安然通过（实测：变异存活）。
     * 这一组专门堵它：退层后**再进一层**（curDepth 又回到 1），此时若残留还在槽里，
     * 守卫放行，查到的就是上一轮那个已经死掉的局部。 */
    scopeExit();                                     /* 回到全局 */
    scopeEnter();                                    /* 深度 1，模拟新函数体 */
    e = symEnter("only_inner", SYM_VAR, intT, 30, &code);
    check(code == 0 && e != NULL, "新函数体内填入 only_inner");
    scopeExit();
    scopeEnter();                                    /* 又一层新作用域，深度同为 1 */
    check(symLookup("only_inner") == NULL, "退层后重进同深度作用域：旧局部不复活");
    check(symLookupLocal("only_inner") == NULL, "symLookupLocal 也查不到旧局部");
    scopeExit();

    /* ── 全局列表 ── */
    check(symGlobalList() != NULL, "symGlobalList 非空");

    printf(failed ? "\nFAIL symtab\n" : "\nPASS symtab\n");
    return failed;
}
