CC     = gcc
FLEX   = flex
BISON  = bison
CFLAGS = -std=c99 -Wall -g -Isrc

SRCS   = src/main.c src/tree.c src/report.c src/util.c src/semantic.c src/type.c
OBJS   = $(SRCS:.c=.o)

# 默认目标**只有** ./parser。
#
# 这里刻意不带上 Test/regress/driver：那个驱动的源码在 `Test/regress/`，是**开发期目录**，
# 交付给评分方的包里只有 src/ + Makefile（外加 Test/sample、Test/err 作样例）。
# 一旦把 driver 挂进默认目标，凡是没有 Test/regress/driver.c 的包，`make` 会直接
# 因 "No rule to make target 'Test/regress/driver.o'" 失败 —— **整个程序都编译不出来**。
# 这与下方"没有 test 目标"的理由是同一条原则：`make` 必须能独立跑通。
#
# 需要回归驱动的是 scripts/run_tests.sh，它**自己**调 `make Test/regress/driver`
# 并在构建失败时响亮退出（见该脚本），把这份依赖留在开发期工具里。
all: parser

parser: $(OBJS) syntax.tab.o lex.yy.o
	$(CC) -o $@ $^ -lfl
	cp -f parser cc

# 实验一回归驱动：它**不含** src/main.o（自己带 main）、也**不含** src/semantic.o
# （实验一的 30 个 .exp 装的是语法树与 A/B 错误，不该跑语义分析）。
REGOBJS = src/tree.o src/report.o src/util.o syntax.tab.o lex.yy.o

Test/regress/driver: Test/regress/driver.o $(REGOBJS)
	$(CC) -o $@ $^ -lfl

# 两个产物同一条规则生成（`bison -d` 一次写出 .c 与 .h）。
# 只写 `syntax.tab.c: src/syntax.y` 是不够的：`syntax.tab.h` 被手删后 make 并不知道
# 该重新生成它，于是 lex.yy.o 的编译直接 `fatal error: syntax.tab.h: No such file`。
#
# ⚠️ 因此**本项目不要用 `make -j`**：上面这种"一个 recipe、两个目标"的多目标规则，
# 在 `make -j` 下会被当成两条独立规则**并发跑两次 bison**，两次都写同一个
# syntax.tab.c。正确的分组写法是 GNU make ≥ 4.3 的 `syntax.tab.c syntax.tab.h &:`，
# 而评分镜像是 Ubuntu 20.04 的 make 4.2（不支持 `&:`），故这里只作说明、不改写法；
# 本项目规模很小，串行构建的耗时可以忽略。构建请用 `make`，不要用 `make -j`。
syntax.tab.c syntax.tab.h: src/syntax.y
	$(BISON) -o syntax.tab.c -d -v src/syntax.y

# flex 需要 syntax.tab.h，故依赖 syntax.tab.c（bison -d 会一并生成该头文件）
lex.yy.c: src/lexical.l syntax.tab.c
	$(FLEX) -o lex.yy.c src/lexical.l

syntax.tab.o: syntax.tab.c src/tree.h src/report.h
	$(CC) $(CFLAGS) -c -o $@ syntax.tab.c

# syntax.tab.h 必须显式列出：lex.yy.c 里 `#include "syntax.tab.h"`，
# 只在语法头文件被删/被改而 .c 未重新生成时，缺这条依赖会得到编译失败。
lex.yy.o: lex.yy.c syntax.tab.h src/tree.h src/report.h
	$(CC) $(CFLAGS) -c -o $@ lex.yy.c

%.o: %.c src/tree.h src/report.h src/util.h src/semantic.h src/type.h
	$(CC) $(CFLAGS) -c -o $@ $<

# src/util.c 必须一起链：tree.c 里的 xmalloc/xstrdup 自实验二起搬到了 util.c
Test/unit/test_tree: Test/unit/test_tree.c src/tree.c src/tree.h src/util.c src/util.h
	$(CC) $(CFLAGS) -o $@ Test/unit/test_tree.c src/tree.c src/util.c

unit-test: Test/unit/test_tree
	@bash -o pipefail -c './Test/unit/test_tree | diff -u Test/unit/tree_expected.txt -' && echo "PASS tree"

Test/unit/test_lexer: Test/unit/test_lexer.c lex.yy.c syntax.tab.c src/tree.c src/report.c \
                      src/tree.h src/report.h src/util.c src/util.h
	$(CC) $(CFLAGS) -I. -o $@ Test/unit/test_lexer.c lex.yy.c syntax.tab.c src/tree.c src/report.c \
	      src/util.c -lfl

lexer-test: Test/unit/test_lexer
	@bash -o pipefail -c './Test/unit/test_lexer < Test/unit/lexer_in.cmm | diff -u Test/unit/lexer_expected.txt -' && echo "PASS lexer"

# type 模块单测：全项目唯一能完全脱离词法/语法/符号表独立测试的模块。
# 它存在的首要理由是钉住数组维度顺序（设计文档 §5.1）—— 那个错误端到端测不出来
# （长度不参与判等、维数不变、错误 12 只看下标类型），故必须查具体的 size 值。
Test/unit/test_type: Test/unit/test_type.c src/type.c src/type.h src/util.c src/util.h src/tree.h
	$(CC) $(CFLAGS) -o $@ Test/unit/test_type.c src/type.c src/util.c

type-test: Test/unit/test_type
	@bash -o pipefail -c './Test/unit/test_type' && echo "PASS type"

# NodeType 单测：钉住"名字 → 枚举"的映射表与 syntax.y 的 21 个非终结符一致。
# 查表集中在 tree.c，测试只需证明每个名字都能查到自己的枚举值。
Test/unit/test_nodetype: Test/unit/test_nodetype.c src/tree.c src/tree.h src/util.c src/util.h
	$(CC) $(CFLAGS) -o $@ Test/unit/test_nodetype.c src/tree.c src/util.c

nodetype-test: Test/unit/test_nodetype
	@bash -o pipefail -c './Test/unit/test_nodetype' && echo "PASS nodetype"

# 注：这里**没有** `test` 目标。评分流程是 `make` + `./parser <文件>`，不需要它；
# 而让它挂着一个开发期脚本（scripts/run_tests.sh）会把"构建可用性"和"开发期工具
# 是否随提交一起打包"绑在一起 —— 打包时漏掉 scripts/ 就会让 `make test` 直接坏掉。
# `make` 本身必须能独立跑通，故把测试入口交给文档说明，不做成 make 目标。
#
# 同一条原则也适用于 `Test/regress/driver`：它是**开发期**的回归驱动，源码在
# Test/regress/ 下，故同样**不进 `all`**（见文件开头的说明）。脚本要它时自己会建。
# 自检请直接：
#     bash scripts/run_tests.sh      # 需先 make；脚本自己会检查 ./parser 并构建回归驱动

.PHONY: all clean unit-test lexer-test type-test nodetype-test
clean:
	rm -f parser cc *.o src/*.o lex.yy.c syntax.tab.c syntax.tab.h syntax.output \
	      Test/unit/test_tree Test/unit/test_lexer Test/unit/test_type \
	      Test/unit/test_nodetype \
	      Test/regress/driver Test/regress/*.o
