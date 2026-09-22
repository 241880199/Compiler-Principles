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

    /* ── 名等价：不同名的结构体不等价 ── */
    structEquivalence = 0;
    t = typeStruct("A", NULL);
    u = typeStruct("B", NULL);
    check(!typeEqual(t, u), "名等价：struct A != struct B");
    check(typeEqual(t, t), "名等价：struct A == struct A（同一指针）");
    /* 匿名结构体各有隐藏名，两个匿名的不等价 */
    check(!typeEqual(typeStruct("#anon_1", NULL), typeStruct("#anon_2", NULL)),
          "名等价：两个匿名结构体不等价");
    structEquivalence = 1;

    /* ── 结构等价：忽略域名，按位置比类型 ── */
    {
        /* 要求书 2.3 的等价例子：struct a{int x; float y;} vs struct b{int y; float z;} */
        FieldList fa = xmalloc(sizeof *fa), fb = xmalloc(sizeof *fb);
        FieldList a2 = xmalloc(sizeof *a2), b2 = xmalloc(sizeof *b2);
        fa->name = "x"; fa->type = intT;   fa->node = NULL; fa->tail = a2;
        a2->name = "y"; a2->type = floatT; a2->node = NULL; a2->tail = NULL;
        fb->name = "y"; fb->type = intT;   fb->node = NULL; fb->tail = b2;
        b2->name = "z"; b2->type = floatT; b2->node = NULL; b2->tail = NULL;
        t = typeStruct("a", fa); u = typeStruct("b", fb);
        check(typeEqual(t, u), "结构等价：忽略域名、按位置比类型 → 等价");
    }

    /* ── 结构等价：不展开数组（要求书 2.3 的不等价例子）── */
    {
        /* A = { int a; struct { float f; int i; } b[10]; } */
        FieldList ffi = xmalloc(sizeof *ffi), fi = xmalloc(sizeof *fi);
        Type innerA;
        FieldList aFields = xmalloc(sizeof *aFields), a2 = xmalloc(sizeof *a2);
        Type A;
        int d10[1]; d10[0] = 10;

        /* 内层匿名结构体 { float f; int i; } */
        ffi->name = "f"; ffi->type = floatT; ffi->node = NULL; ffi->tail = fi;
        fi->name  = "i"; fi->type = intT;   fi->node = NULL; fi->tail = NULL;
        innerA = typeStruct("#anon_1", ffi);

        aFields->name = "a"; aFields->type = intT; aFields->node = NULL;
        aFields->tail = a2;
        a2->name = "b"; a2->type = typeArrayFromDims(innerA, d10, 1);
        a2->node = NULL; a2->tail = NULL;
        A = typeStruct("A", aFields);

        /* B = { struct { int i; float f; } b[10]; int b; } */
        {
            FieldList fii = xmalloc(sizeof *fii), fff = xmalloc(sizeof *fff);
            Type innerB;
            FieldList bFields = xmalloc(sizeof *bFields), b2 = xmalloc(sizeof *b2);
            Type B;
            fii->name = "i"; fii->type = intT;   fii->node = NULL; fii->tail = fff;
            fff->name = "f"; fff->type = floatT; fff->node = NULL; fff->tail = NULL;
            innerB = typeStruct("#anon_2", fii);

            bFields->name = "b"; bFields->type = typeArrayFromDims(innerB, d10, 1);
            bFields->node = NULL; bFields->tail = b2;
            b2->name = "b"; b2->type = intT; b2->node = NULL; b2->tail = NULL;
            B = typeStruct("B", bFields);

            check(!typeEqual(A, B),
                  "结构等价：不展开数组 → A 与 B 不等价（展开的话会误判为等价）");
        }
    }

    /* ── 域的数量不同不等价 ── */
    {
        FieldList f = xmalloc(sizeof *f);
        f->name = "x"; f->type = intT; f->node = NULL; f->tail = NULL;
        check(!typeEqual(typeStruct("P", f), typeStruct("Q", NULL)),
              "结构等价：域数量不同 → 不等价");
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
