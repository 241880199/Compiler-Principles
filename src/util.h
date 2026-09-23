#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

/* 分配失败即打印到 stderr 并 exit(1)。
 * 三个新模块（type/symtab/semantic）都要分配，集中在这里避免三份重复。 */
void *xmalloc(size_t n);

/* 复制字符串；入参为 NULL 时返回 NULL（调用方依赖这个语义，见 tree.c）。 */
char *xstrdup(const char *s);

#endif
