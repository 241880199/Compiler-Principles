#ifndef SYMTAB_H
#define SYMTAB_H

#include "type.h"

typedef enum { SYM_VAR, SYM_FUNC, SYM_STRUCT } SymKind;

typedef struct SymEntry {
    char    *name;
    SymKind  kind;
    Type     type;        /* 变量：变量类型；函数：返回类型；结构体：结构体类型 */
    /* 函数专用 */
    Type    *params;      /* 形参类型数组 */
    int      nparams;
    int      declLine;    /* 这个名字进入符号表时所处的行 */
    /* 十字链表 */
    int      depth;       /* 所在作用域深度 */
    struct SymEntry *scopeNext;   /* 同一作用域 */
    struct SymEntry *hashNext;    /* 同一散列槽 */
} SymEntry;

void symInit(void);

void scopeEnter(void);
void scopeExit(void);
int  scopeDepth(void);

/* 填入一个名字。
 * 返回值：**应当代表这个名字**的条目。冲突时不覆盖已有条目，返回已存在的那个
 *         （设计文档 §8 第 7 条：保留最早那个）。
 * *errCode：0 表示无冲突；否则为应报的错误码（3/4/16）。
 *         规则收敛在本文件的一张表里 —— 设计文档 §8 第 1 条。 */
SymEntry *symEnter(const char *name, SymKind kind, Type type, int line, int *errCode);

/* 沿作用域链由内向外查找，返回最内层可见的定义；找不到返回 NULL。 */
SymEntry *symLookup(const char *name);

/* 只查当前作用域（判重复定义用）。 */
SymEntry *symLookupLocal(const char *name);

/* 冲突码规则（设计文档 §8 第 1 条，**这是一个开关点**）：
 * 单一名字空间；按**后定义者的种类**报码 —— 变量→3、结构体→16、函数→4。
 * 与 symEnter 共用，保证规则只有一处。 */
int symConflictCode(SymKind newKind);

#endif
