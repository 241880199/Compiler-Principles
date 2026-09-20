%option yylineno
%option noyywrap
%{
#include <stdio.h>
#include "tree.h"
#include "report.h"
#include "syntax.tab.h"
%}
%%
[ \t\r]+   { }
\n         { }
.          { }
%%
