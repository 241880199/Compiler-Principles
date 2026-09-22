#include <stdio.h>
#include <string.h>
#include "type.h"
#include "util.h"

int structEquivalence = 1;   /* 选做 2.3 —— 设计文档 §8 之外，但有明确原文依据 */

static Type newType(TypeKind k) {
    Type t = xmalloc(sizeof *t);
    t->kind = k;
    return t;
}

Type typeBasic(int kind) {
    Type t = newType(TY_BASIC);
    t->u.basic = kind;
    return t;
}

Type typeArray(Type elem, int size) {
    Type t = newType(TY_ARRAY);
    t->u.array.elem = elem;
    t->u.array.size = size;
    return t;
}

Type typeStruct(const char *name, FieldList fields) {
    Type t = newType(TY_STRUCTURE);
    t->u.structure.name = xstrdup(name);
    t->u.structure.fields = fields;
    return t;
}

/* UNKNOWN 用单例：既省内存，也让 typeIsUnknown 可以退化成指针比较。 */
static Type unknownSingleton = NULL;

Type typeUnknown(void) {
    if (!unknownSingleton) unknownSingleton = newType(TY_UNKNOWN);
    return unknownSingleton;
}

int typeIsUnknown(Type t) { return t != NULL && t->kind == TY_UNKNOWN; }

/* dims 从最内层到最外层 —— 从左往右折叠。
 * 写反（从最外层收集再左折叠）会让 int a[10][2] 变成 array(2, array(10, int))，
 * 而 17 种检查一条都发现不了它。见设计文档 §5.1。 */
Type typeArrayFromDims(Type base, const int *dims, int ndims) {
    Type t = base;
    int i;
    for (i = 0; i < ndims; i++) t = typeArray(t, dims[i]);
    return t;
}

/* 结构等价：忽略域名，按位置比域的类型；域数量必须相同。
 * 数组域走 typeEqual 的 TY_ARRAY 分支，即整体比较（基类型 + 维数），
 * **不展开** —— 展开会把要求书 2.3 那对不等价的例子误判为等价。
 * 不会成环：C-- 不允许结构体包含自身（那样的域会先报错误 17）。 */
static int fieldsEqual(FieldList a, FieldList b) {
    while (a && b) {
        if (!typeEqual(a->type, b->type)) return 0;
        a = a->tail; b = b->tail;
    }
    return a == NULL && b == NULL;
}

int typeEqual(Type a, Type b) {
    if (typeIsUnknown(a) || typeIsUnknown(b)) return 1;   /* 毒化 */
    if (a == b) return 1;
    if (a->kind != b->kind) return 0;

    switch (a->kind) {
    case TY_BASIC:
        return a->u.basic == b->u.basic;

    case TY_ARRAY: {
        /* 维数与基类型相同即等价，**长度不参与判等**（要求书 note 1）。 */
        Type x = a, y = b;
        while (x->kind == TY_ARRAY && y->kind == TY_ARRAY) {
            x = x->u.array.elem;
            y = y->u.array.elem;
        }
        if (x->kind == TY_ARRAY || y->kind == TY_ARRAY) return 0;  /* 维数不同 */
        return typeEqual(x, y);                                    /* 比基类型 */
    }

    case TY_STRUCTURE:
        if (!structEquivalence) return a == b;    /* 名等价：指针相等（上面已查过） */
        return fieldsEqual(a->u.structure.fields, b->u.structure.fields);

    default:
        return 0;
    }
}

FieldList typeField(Type structType, const char *name) {
    FieldList f;
    if (!structType || structType->kind != TY_STRUCTURE) return NULL;
    for (f = structType->u.structure.fields; f; f = f->tail)
        if (strcmp(f->name, name) == 0) return f;
    return NULL;
}

void typeToString(Type t, char *buf, int n) {
    char inner[256];
    if (!t)                    { snprintf(buf, n, "<null>"); return; }
    switch (t->kind) {
    case TY_BASIC:
        snprintf(buf, n, "%s", t->u.basic == BT_FLOAT ? "float" : "int");
        return;
    case TY_ARRAY:
        typeToString(t->u.array.elem, inner, sizeof inner);
        snprintf(buf, n, "%s[%d]", inner, t->u.array.size);
        return;
    case TY_STRUCTURE:
        snprintf(buf, n, "struct %s", t->u.structure.name);
        return;
    default:
        snprintf(buf, n, "?");
        return;
    }
}
