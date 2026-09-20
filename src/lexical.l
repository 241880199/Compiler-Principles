%option yylineno
%option noyywrap
%option noinput nounput never-interactive
%{
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "tree.h"
#include "report.h"
#include "syntax.tab.h"

/* 数字词素的十进制展开缓冲区。**不能小**：`%f` 把 `0.5e103`（附录 A 列出的合法
 * 形式）展开为 103 位整数 + 小数点 + 6 位小数 = 110 字符，64 字节的缓冲区会把值
 * 静默截断成错值（`%f` 不产生 UB，只是结果错）。512 对 double 的 `%f` 全量展开绰绰有余。 */
#define NUMBUF 512

static int commentStart = 0;   /* 未闭合注释的起始行 */

/* 返回一个词法单元：记录行号、构造结点、交给 Bison */
static int emit(int tok, const char *name, const char *lexeme) {
    curTokenLine = yylineno;
    yylval = newToken(name, yylineno, lexeme);
    return tok;
}

/* 报一条词法错误（type A） */
static void errA(int line, const char *fmt, const char *arg) {
    char buf[256];
    snprintf(buf, sizeof buf, fmt, arg);
    reportError('A', line, buf);
}
%}

DIGIT   [0-9]
ALNUM   [0-9a-zA-Z_]
FBASE   ({DIGIT}+\.{DIGIT}*|\.{DIGIT}+)

%x COMMENT

%%
    /* ── 注释 ──────────────────────────────────────────── */
"/*"                    { commentStart = yylineno; BEGIN(COMMENT); }
<COMMENT>"*/"           { BEGIN(INITIAL); }
<COMMENT>\n             { }
<COMMENT>.              { }
<COMMENT><<EOF>>        { BEGIN(INITIAL);
                         /* 先记下 EOF 所在行再返回 0：否则 curTokenLine 仍停在最后一个
                          * 真实词法单元上，语法分析器随后报出的 type B 会**倒指回一行
                          * 合法代码**（要求书 2.1.3：错误类型和行号是唯一评分标准）。 */
                         curTokenLine = yylineno;
                         reportError('A', commentStart, "Unterminated comment");
                         return 0; }

"//"[^\n]*              { }

    /* ── 数字：非法者在此直接报 type A ────────────────── */
0[xX]{ALNUM}+           {
        const char *p; int ok = 1;
        for (p = yytext + 2; *p; p++) if (!isxdigit((unsigned char)*p)) ok = 0;
        if (ok) {
            char buf[NUMBUF];
            snprintf(buf, sizeof buf, "%ld", strtol(yytext, NULL, 0));
            return emit(INT, "INT", buf);
        }
        errA(yylineno, "Illegal hexadecimal number '%s'", yytext);
    }

0{DIGIT}+               {
        const char *p; int ok = 1;
        for (p = yytext + 1; *p; p++) if (*p > '7') ok = 0;
        if (ok) {
            char buf[NUMBUF];
            snprintf(buf, sizeof buf, "%ld", strtol(yytext, NULL, 8));
            return emit(INT, "INT", buf);
        }
        errA(yylineno, "Illegal octal number '%s'", yytext);
    }

{FBASE}[eE][+-]?{DIGIT}+    {
        char buf[NUMBUF];
        snprintf(buf, sizeof buf, "%f", atof(yytext));
        return emit(FLOAT, "FLOAT", buf);
    }
{FBASE}[eE][+-]?            {
        errA(yylineno, "Illegal floating point number \"%s\"", yytext);
    }

{DIGIT}+\.{DIGIT}+      {
        char buf[NUMBUF];
        snprintf(buf, sizeof buf, "%f", atof(yytext));
        return emit(FLOAT, "FLOAT", buf);
    }

0|[1-9]{DIGIT}*         {
        char buf[NUMBUF];
        snprintf(buf, sizeof buf, "%ld", strtol(yytext, NULL, 0));
        return emit(INT, "INT", buf);
    }

    /* ── 关键字（必须先于标识符） ─────────────────────── */
"int"                   { return emit(TYPE,   "TYPE",   "int");   }
"float"                 { return emit(TYPE,   "TYPE",   "float"); }
"struct"                { return emit(STRUCT, "STRUCT", NULL);    }
"return"                { return emit(RETURN, "RETURN", NULL);    }
"if"                    { return emit(IF,     "IF",     NULL);    }
"else"                  { return emit(ELSE,   "ELSE",   NULL);    }
"while"                 { return emit(WHILE,  "WHILE",  NULL);    }

    /* ── 标识符 ───────────────────────────────────────── */
[a-zA-Z_]{ALNUM}*       { return emit(ID, "ID", yytext); }

    /* ── 运算符与定界符（长符号必须先于短符号） ───────── */
"&&"                    { return emit(AND,      "AND",      NULL); }
"||"                    { return emit(OR,       "OR",       NULL); }
"=="                    { return emit(RELOP,    "RELOP",    NULL); }
"!="                    { return emit(RELOP,    "RELOP",    NULL); }
"<="                    { return emit(RELOP,    "RELOP",    NULL); }
">="                    { return emit(RELOP,    "RELOP",    NULL); }
"<"                     { return emit(RELOP,    "RELOP",    NULL); }
">"                     { return emit(RELOP,    "RELOP",    NULL); }
"="                     { return emit(ASSIGNOP, "ASSIGNOP", NULL); }
"+"                     { return emit(PLUS,     "PLUS",     NULL); }
"-"                     { return emit(MINUS,    "MINUS",    NULL); }
"*"                     { return emit(STAR,     "STAR",     NULL); }
"/"                     { return emit(DIV,      "DIV",      NULL); }
"!"                     { return emit(NOT,      "NOT",      NULL); }
"."                     { return emit(DOT,      "DOT",      NULL); }
";"                     { return emit(SEMI,     "SEMI",     NULL); }
","                     { return emit(COMMA,    "COMMA",    NULL); }
"("                     { return emit(LP,       "LP",       NULL); }
")"                     { return emit(RP,       "RP",       NULL); }
"["                     { return emit(LB,       "LB",       NULL); }
"]"                     { return emit(RB,       "RB",       NULL); }
"{"                     { return emit(LC,       "LC",       NULL); }
"}"                     { return emit(RC,       "RC",       NULL); }

    /* ── 空白与兜底 ───────────────────────────────────── */
[ \t\r]+                { }
\n                      { }
.                       { errA(yylineno, "Mysterious character \"%s\"", yytext); }
%%
