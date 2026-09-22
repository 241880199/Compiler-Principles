#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symtab.h"
#include "util.h"

/* 指导书给的 P.J. Weinberger 散列函数 */
static unsigned int hashPJW(const char *name) {
    unsigned int val = 0, i;
    for (; *name; ++name) {
        val = (val << 2) + (unsigned char)*name;
        if ((i = val & ~0x3fffu)) val = (val ^ (i >> 12)) & 0x3fffu;
    }
    return val;
}

/* hashPJW 的值域恰好是 0..0x3fff（16384 个值），故按它的范围定表长 ——
 * 写成 1024 会让 hashPJW 返回 1024..16383 时越界写内存。 */
#define HASH_SIZE 16384
#define MAX_DEPTH 64

static SymEntry *buckets[HASH_SIZE];
static SymEntry *scopeHead[MAX_DEPTH];
static int curDepth = 0;

void symInit(void) {
    int i;
    for (i = 0; i < HASH_SIZE; i++) buckets[i] = NULL;
    for (i = 0; i < MAX_DEPTH; i++) scopeHead[i] = NULL;
    curDepth = 0;
}

void scopeEnter(void) {
    curDepth++;
    if (curDepth >= MAX_DEPTH) {
        fprintf(stderr, "internal error: scope nesting exceeds %d\n", MAX_DEPTH);
        abort();
    }
    scopeHead[curDepth] = NULL;
}

void scopeExit(void) {
    SymEntry *e, *next;
    if (curDepth == 0) return;
    /* 顺本层链表把每个条目从它所在的散列槽里摘掉 */
    for (e = scopeHead[curDepth]; e; e = next) {
        unsigned int h = hashPJW(e->name);
        SymEntry **p = &buckets[h];
        while (*p && *p != e) p = &(*p)->hashNext;
        if (*p) *p = e->hashNext;
        next = e->scopeNext;
    }
    scopeHead[curDepth] = NULL;
    curDepth--;
}

int scopeDepth(void) { return curDepth; }

/* 冲突码规则（设计文档 §8 第 1 条，**这是一个开关点**）：
 * 单一名字空间；按**后定义者的种类**报码 —— 变量→3、结构体→16、函数→4。
 * 要求书的三条错误（3/4/16）互相覆盖不到全部组合（例如变量与函数同名），
 * 这里取这个自洽的读法。改这一条时只改这个函数。 */
int symConflictCode(SymKind newKind) {
    switch (newKind) {
    case SYM_VAR:    return 3;    /* 变量重复定义，或与已有结构体重名 */
    case SYM_STRUCT: return 16;   /* 结构体与已有结构体或变量重名 */
    case SYM_FUNC:   return 4;    /* 函数出现重复定义 */
    }
    return 0;
}

SymEntry *symEnter(const char *name, SymKind kind, Type type, int line, int *errCode) {
    SymEntry *e;
    unsigned int h;

    if (errCode) *errCode = 0;

    /* 只在**当前作用域**内判重：2.2 之下不同作用域同名是允许的（遮蔽）。 */
    for (e = scopeHead[curDepth]; e; e = e->scopeNext) {
        if (strcmp(e->name, name) == 0) {
            /* 设计文档 §8 第 7 条：保留最早那个 —— 不覆盖，返回已存在的条目。 */
            if (errCode) *errCode = symConflictCode(kind);
            return e;
        }
    }

    e = xmalloc(sizeof *e);
    e->name      = xstrdup(name);
    e->kind      = kind;
    e->type      = type;
    e->params    = NULL;
    e->nparams   = 0;
    e->defined   = 0;
    e->declLine  = line;
    e->depth     = curDepth;
    e->scopeNext = scopeHead[curDepth];
    scopeHead[curDepth] = e;

    h = hashPJW(name);
    e->hashNext = buckets[h];
    buckets[h] = e;

    return e;
}

SymEntry *symLookup(const char *name) {
    unsigned int h = hashPJW(name);
    SymEntry *e;
    /* 槽内链表按插入顺序，最新插入的（最深、最内层）在链头。
     * 因此第一个 depth <= curDepth 的同名条目就是最内层的定义。
     * depth 字段用于把这条不变式显式化 —— 指导书也要求记录深度信息。 */
    for (e = buckets[h]; e; e = e->hashNext)
        if (e->depth <= curDepth && strcmp(e->name, name) == 0) return e;
    return NULL;
}

SymEntry *symLookupLocal(const char *name) {
    SymEntry *e;
    for (e = scopeHead[curDepth]; e; e = e->scopeNext)
        if (strcmp(e->name, name) == 0) return e;
    return NULL;
}

SymEntry *symGlobalList(void) { return scopeHead[0]; }
