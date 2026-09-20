%option yylineno
%option noyywrap
%option noinput nounput never-interactive
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
