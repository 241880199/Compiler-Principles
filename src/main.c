#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tree.h"
#include "report.h"
#include "semantic.h"
#include "ir.h"
#include "translate.h"
#include "optimize.h"

extern FILE *yyin;
extern int   yyparse(void);
extern int   yylex(void);
extern Node *root;

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <input-file> [<output-file>]\n", argv[0]);
        return 1;
    }
    if (!(yyin = fopen(argv[1], "r"))) {
        perror(argv[1]);
        return 1;
    }

    {
        FILE *out = stdout;
        if (argc >= 3) {
            out = fopen(argv[2], "w");
            if (!out) { perror(argv[2]); return 1; }
        }
        yyparse();

        /* 词法错误与语法分析是否走完**无关**，不该因为语法分析中止而丢失。
         * （实验一已修的问题，原样保留。） */
        if (hasError()) {
            while (yylex() != 0) { }
        } else {
            /* 语义错误由 semanticAnalyze 打到 stdout（实验二口径）。**翻译以语义错误
             * 数为闸**：有错就不翻译 —— 语义非法的输入（lab2 的 13 个语义错误用例、
             * 畸形内建调用等）不得再走进翻译层，否则翻译层的响亮失败（如
             * MAX_CALL_ARGS 的 abort）会盖掉语义层的诊断。 */
            int semErrs = semanticAnalyze(root);
            /* CMM_SEMANTIC_ONLY：只跑实验二口径的语义检查，不做翻译。
             * 由 scripts/parser_stdout_only.sh 设置，仅供 scripts/run_tests.sh 的
             * lab2 回归（其中 lab2_wideargs_ok 有 60 个实参，超过翻译层
             * MAX_CALL_ARGS=16 的**故意**响亮失败上限）。不影响对外契约。
             * 严格只认 "1"：置成 "0" 或空串都**不得**关闭翻译。 */
            const char *semOnly = getenv("CMM_SEMANTIC_ONLY");
            int semanticOnly = semOnly && strcmp(semOnly, "1") == 0;
            if (semErrs == 0 && !semanticOnly) {
                InterCodeList *ir = translateProgram(root);
                optimizeIR(ir);      /* Task 6 落的 no-op；Task 16 起才有真实现 */
                printIR(out, ir);
            }
        }
        /* 错误路径也必须关掉已打开的输出文件（stdout 不关）。 */
        if (out != stdout) fclose(out);
    }
    return 0;
}
