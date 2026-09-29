/* type 模块单测。**必须查具体的 size 值**，不能只查维数 ——
 * 设计文档 §5.1：维度顺序写反了，17 种语义检查一条都发现不了。 */
#include <stdio.h>
#include <string.h>
#include "type.h"
#include "util.h"      /* xmalloc：本测试要手工搭 FieldList */

static int failed = 0;

static void check(int cond, const char *what) {
    if (!cond) { printf("FAIL %s\n", what); failed = 1; }
    else       { printf("ok   %s\n", what); }
}

int main(void) {
    Type intT = typeBasic(BT_INT), floatT = typeBasic(BT_FLOAT);
    int dims[4];
    Type t, u;
    char buf[256];

    /* ── 基本类型 ── */
    check(typeEqual(intT, intT), "int == int");
    check(!typeEqual(intT, floatT), "int != float");

    /* ── 数组维度顺序（本文件存在的首要理由）── */
    /* int a[10][2] 沿 child[0] 下行收集到的最内层在前是 [2, 10] */
    dims[0] = 2; dims[1] = 10;
    t = typeArrayFromDims(intT, dims, 2);
    check(t->kind == TY_ARRAY && t->u.array.size == 10,
          "a[10][2] 外层 size 必须是 10");
    check(t->u.array.elem->kind == TY_ARRAY && t->u.array.elem->u.array.size == 2,
          "a[10][2] 内层 size 必须是 2");
    check(t->u.array.elem->u.array.elem == intT, "a[10][2] 基类型是 int");

    /* ── 数组等价忽略长度 ── */
    dims[0] = 3; dims[1] = 5;
    u = typeArrayFromDims(intT, dims, 2);
    check(typeEqual(t, u), "int[10][2] 等价于 int[5][3]（长度不参与判等）");

    /* ── 维数不同不等价 ── */
    dims[0] = 10;
    u = typeArrayFromDims(intT, dims, 1);
    check(!typeEqual(t, u), "int[10][2] 不等价于 int[10]（维数不同）");

    /* ── 基类型不同不等价 ── */
    dims[0] = 2; dims[1] = 10;
    u = typeArrayFromDims(floatT, dims, 2);
    check(!typeEqual(t, u), "int[10][2] 不等价于 float[10][2]");

    /* ── 名等价（假设 5 的原文口径）：结构体只按名字认类型 ──
     * 依据：要求书第二条注记"每个匿名的结构体类型我们认为均具有一个独有的隐藏名字，
     * 以此进行**名等价**判定"。本版本不做选做 2.3，故不按域逐个比较。 */
    t = typeStruct("A", NULL);
    u = typeStruct("B", NULL);
    check(!typeEqual(t, u), "名等价：struct A != struct B");
    check(typeEqual(t, t), "名等价：struct A == struct A");
    /* 匿名结构体各有独有隐藏名，两个匿名的不等价 */
    check(!typeEqual(typeStruct("#anon_1", NULL), typeStruct("#anon_2", NULL)),
          "名等价：两个匿名结构体不等价");

    /* ── 判的是**对象同一性**，不是名字字符串 ──
     * 符号表对每个名字只建一个 Type 对象，故在无错输入上"同名"与"同一对象"是同一件事
     * （这条同时说明：把 typeEqual 改成 strcmp(名字) 在合法输入上不可观测，
     * 因此不要"顺手优化"成字符串比较）。 */
    check(!typeEqual(typeStruct("A", NULL), typeStruct("A", NULL)),
          "名等价：两个新建的同名 Type 对象不等价（判同一性）");

    /* ── 域逐个相同、但**结构体名字不同** ⇒ 不等价（这是本版本与选做 2.3 的分界）── */
    {
        FieldList fa = xmalloc(sizeof *fa), fb = xmalloc(sizeof *fb);
        fa->name = "x"; fa->type = intT; fa->node = NULL; fa->tail = NULL;
        fb->name = "x"; fb->type = intT; fb->node = NULL; fb->tail = NULL;
        check(!typeEqual(typeStruct("a", fa), typeStruct("b", fb)),
              "名等价：域逐个相同但名字不同 ⇒ 不等价（结构等价是**不做**的选做 2.3）");
    }

    /* ── UNKNOWN 与任何类型等价（毒化）── */
    check(typeEqual(typeUnknown(), intT), "UNKNOWN 等价于 int");
    check(typeEqual(intT, typeUnknown()), "int 等价于 UNKNOWN");
    check(typeEqual(typeUnknown(), typeStruct("A", NULL)), "UNKNOWN 等价于结构体");

    /* ── 查域 ── */
    {
        FieldList f = xmalloc(sizeof *f);
        Type s;
        f->name = "real"; f->type = floatT; f->node = NULL; f->tail = NULL;
        s = typeStruct("Complex", f);
        check(typeField(s, "real") != NULL, "typeField 找得到 real");
        check(typeField(s, "im") == NULL, "typeField 找不到 im");
    }

    /* ── 类型转字符串（错误 9 的消息要用）── */
    t = typeArrayFromDims(intT, (dims[0] = 10, dims), 1);
    typeToString(t, buf, sizeof buf);
    check(strcmp(buf, "int[10]") == 0, "typeToString(int[10])");
    typeToString(intT, buf, sizeof buf);
    check(strcmp(buf, "int") == 0, "typeToString(int)");
    typeToString(typeUnknown(), buf, sizeof buf);
    check(strcmp(buf, "?") == 0, "typeToString(UNKNOWN)");

    printf(failed ? "\nFAIL type\n" : "\nPASS type\n");
    return failed;
}
