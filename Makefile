CC     = gcc
FLEX   = flex
BISON  = bison
CFLAGS = -std=c99 -Wall -g -Isrc

SRCS   = src/main.c src/tree.c src/report.c
OBJS   = $(SRCS:.c=.o)

parser: $(OBJS) syntax.tab.o lex.yy.o
	$(CC) -o $@ $^ -lfl
	cp -f parser cc

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

%.o: %.c src/tree.h src/report.h
	$(CC) $(CFLAGS) -c -o $@ $<

Test/unit/test_tree: Test/unit/test_tree.c src/tree.c src/tree.h
	$(CC) $(CFLAGS) -o $@ Test/unit/test_tree.c src/tree.c

unit-test: Test/unit/test_tree
	@bash -o pipefail -c './Test/unit/test_tree | diff -u Test/unit/tree_expected.txt -' && echo "PASS tree"

Test/unit/test_lexer: Test/unit/test_lexer.c lex.yy.c syntax.tab.c src/tree.c src/report.c \
                      src/tree.h src/report.h
	$(CC) $(CFLAGS) -I. -o $@ Test/unit/test_lexer.c lex.yy.c syntax.tab.c src/tree.c src/report.c -lfl

lexer-test: Test/unit/test_lexer
	@bash -o pipefail -c './Test/unit/test_lexer < Test/unit/lexer_in.cmm | diff -u Test/unit/lexer_expected.txt -' && echo "PASS lexer"

test: parser
	@bash scripts/run_tests.sh

.PHONY: clean unit-test lexer-test test
clean:
	rm -f parser cc *.o src/*.o lex.yy.c syntax.tab.c syntax.tab.h syntax.output \
	      Test/unit/test_tree Test/unit/test_lexer
