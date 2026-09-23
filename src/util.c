#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"

void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) { fprintf(stderr, "out of memory\n"); exit(1); }
    return p;
}

char *xstrdup(const char *s) {
    char *p;
    if (!s) return NULL;
    p = xmalloc(strlen(s) + 1);
    strcpy(p, s);
    return p;
}
