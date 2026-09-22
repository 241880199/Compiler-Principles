#ifndef TYPE_H
#define TYPE_H

#include "tree.h"

typedef struct Type_ *Type;
typedef struct FieldList_ *FieldList;

/* 基本类型种类 */
enum { BT_INT, BT_FLOAT };

typedef enum { TY_BASIC, TY_ARRAY, TY_STRUCTURE, TY_UNKNOWN } TypeKind;

struct Type_ {
    TypeKind kind;
    union {
        int basic;                                          /* TY_BASIC */
        struct { Type elem; int size; } array;              /* TY_ARRAY：size 个 elem */
        struct { char *name; FieldList fields; } structure; /* TY_STRUCTURE */
    } u;
};

struct FieldList_ {
    char     *name;
    Type      type;
    Node     *node;   /* 该域定义处的 AST 结点：报错误 15 的行号要用 */
    FieldList tail;
};

Type typeBasic(int kind);
Type typeArray(Type elem, int size);
Type typeStruct(const char *name, FieldList fields);
Type typeUnknown(void);
int  typeIsUnknown(Type t);

/* dims 的顺序是**从最内层到最外层**（即按 VarDec 沿 child[0] 下行收集到的顺序）。
 * 见设计文档 §5.1 —— 顺序反了会让 int a[10][2] 变成 array(2, array(10, int))。 */
Type typeArrayFromDims(Type base, const int *dims, int ndims);

/* 返回 1 表示等价。UNKNOWN 与任何类型等价（毒化，设计文档 §5.3）。 */
int typeEqual(Type a, Type b);

FieldList typeField(Type structType, const char *name);

void typeToString(Type t, char *buf, int n);

/* 选做 2.3：结构等价。默认 1。置 0 退回名等价（设计文档 §11.5 变异测试 1）。 */
extern int structEquivalence;

#endif
