#include <stdio.h>
#include <string.h>
#include "type.h"
#include "util.h"

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
        /* **名等价**（假设 5 的原文口径）：结构体只按名字认类型。同一名字在符号表里
         * 只有**一个** Type 对象，故"名字相同"就等于"指针相同"—— 上面那句 `a == b`
         * 已经判过，走到这里就是两个不同名字的结构体 ⇒ 不等价。
         * 匿名结构体各有一个独有的隐藏名（要求书第二条注记），因此它们彼此也不等价。
         * 本版本不做选做 2.3，故不按域逐个比较。 */
        return 0;

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
