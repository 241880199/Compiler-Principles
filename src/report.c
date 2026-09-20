#include <stdio.h>
#include "report.h"

int curTokenLine = 1;

static int errorFlag = 0;

void reportError(char type, int line, const char *msg) {
    errorFlag = 1;
    printf("Error type %c at Line %d: %s.\n", type, line, msg);
}

int hasError(void) { return errorFlag; }
