CC     = gcc
FLEX   = flex
BISON  = bison
CFLAGS = -std=c99 -Wall -g -Isrc

SRCS   = src/main.c src/tree.c src/report.c src/util.c src/semantic.c src/type.c \
         src/symtab.c src/ir.c src/translate.c src/optimize.c
OBJS   = $(SRCS:.c=.o)

# 默认目标**只有** ./parser；`make` 必须能在"只有源码 + Makefile"的包里独立跑通。
all: parser

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

%.o: %.c src/tree.h src/report.h src/util.h src/semantic.h src/type.h src/symtab.h \
        src/ir.h src/translate.h src/optimize.h
	$(CC) $(CFLAGS) -c -o $@ $<

# PKG-CUT >>> 交付包 Makefile 到此为止（scripts/make_package.sh 取本节以上内容）
#
# 下面全是**开发期**规则（Test/unit 单测、Test/regress 回归驱动、以及若干只服务于
# 本仓库测试语料的注释），它们依赖的目录**不进交付包**，故不随包交付。
# 交付包的源码平铺在 Code/ 下，没有 src/ 前缀，因此打包脚本会把本节以上的 `src/`
# 一律去掉 —— 这是唯一一处派生变换，由 scripts/verify_package.sh 在**解包后的实际
# 产物**上端到端复核（干净目录 make + 与仓库构建逐字节对拍）。
# 这一行是哨兵：make_package.sh 靠 grep 它来定位切分点，改名会让打包**响亮失败**。

# ── 实验一回归驱动 ────────────────────────────────────────────
# 它**不含** src/main.o（自己带 main）、也**不含** src/semantic.o（实验一的 30 个
# .exp 装的是语法树与 A/B 错误，不该跑语义分析）。
#
# 刻意**不**挂进默认目标 `all`：驱动源码在 Test/regress/，是开发期目录，交付包里
# 没有它。一旦挂进去，凡是没有 Test/regress/driver.c 的包，`make` 会直接因
# "No rule to make target 'Test/regress/driver.o'" 失败 —— **整个程序都编译不出来**。
# 需要它的是 scripts/run_tests.sh，由它自己调 `make Test/regress/driver` 并在
# 构建失败时响亮退出（见该脚本）。
REGOBJS = src/tree.o src/report.o src/util.o syntax.tab.o lex.yy.o

Test/regress/driver: Test/regress/driver.o $(REGOBJS)
	$(CC) -o $@ $^ -lfl


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

# symtab 单测：钉住"冲突码规则只有一处"（§8 第 1 条）与"保留最早那个"（§8 第 7 条）。
# 它同时是 HASH_SIZE 的把关者 —— hashPJW 的值域恰好是 0..0x3fff（16384 个值），
# 表长写小了就会在"返回值落在 1024..16383"时越界写内存。
# 已做变异测试：把 HASH_SIZE 改成 1024，本单测 SIGSEGV（exit 139），不是静默出错。
Test/unit/test_symtab: Test/unit/test_symtab.c src/symtab.c src/type.c src/util.c \
                       src/symtab.h src/type.h src/tree.h src/util.h
	$(CC) $(CFLAGS) -o $@ Test/unit/test_symtab.c src/symtab.c src/type.c src/util.c

symtab-test: Test/unit/test_symtab
	@bash -o pipefail -c './Test/unit/test_symtab' && echo "PASS symtab"

# report 单测：钉住实验二的 reportSemanticError —— 数字错误类型 + 同行去重。
# 与实验一 reportError 的 A/B 路径**完全独立**（两条路径在评测中不会相遇，见设计文档 §10.2），
# 本单测不覆盖 A/B 路径；那条路径由 Test/regress 的 30 个样例把关。
# stdout 是逐字节比对的对象，故测试自己的 ok/FAIL 提示一律写 stderr。
Test/unit/test_report: Test/unit/test_report.c src/report.c src/report.h
	$(CC) $(CFLAGS) -o $@ Test/unit/test_report.c src/report.c

report-test: Test/unit/test_report
	@bash -o pipefail -c './Test/unit/test_report | diff -u Test/unit/report_expected.txt -' \
	    && echo "PASS report"

# ir 单测：只测**打印格式** —— 那是 ir 这一层唯一的对外行为。
# 打印格式不是"好看的"选择：真机装载器按空白分词、逐行 sanity_check，
# 行数不对（空行）会直接改变 codes 的下标，故这里逐字节钉住。
Test/unit/test_ir: Test/unit/test_ir.c src/ir.c src/ir.h src/util.c src/util.h
	$(CC) $(CFLAGS) -o $@ Test/unit/test_ir.c src/ir.c src/util.c

ir-test: Test/unit/test_ir
	@bash -o pipefail -c './Test/unit/test_ir | diff -u Test/unit/ir_expected.txt -' && echo "PASS ir"

# 「响亮失败」用例（Task 6 审查修正第 2 条）：给 IR_ADDR 传 OP_ADDR 时必须 abort。
# abort 带 SIGABRT 结束**整个进程**，故**必须**与上面那批「打印到 stdout 再整体 diff」
# 的用例分开成一个独立可执行 —— 否则进程会中途崩掉，后面的用例一条都不跑，
# 而 `| diff` 只看到被截断的输出，分不清是谁崩的。理由详见 test_ir_abort.c 顶部。
# 通过判据 = 退出码非 0 **且** stderr 含诊断 —— 两条都要断言：只看退出码会让
# "被别的信号杀掉"也蒙混过关，只看 stderr 文字则可能漏掉"其实没崩"。
Test/unit/test_ir_abort: Test/unit/test_ir_abort.c src/ir.c src/ir.h src/util.c src/util.h
	$(CC) $(CFLAGS) -o $@ Test/unit/test_ir_abort.c src/ir.c src/util.c

ir-abort-test: Test/unit/test_ir_abort
	@exec 2>/dev/null; err=$$(./$< 2>&1 >/dev/null); st=$$?; \
	if [ $$st -eq 0 ]; then \
	    echo "FAIL ir-abort: 期望进程失败（abort），实测退出码 0"; exit 1; fi; \
	if ! printf '%s' "$$err" | grep -qF 'would print "&&x"'; then \
	    echo "FAIL ir-abort: stderr 未含预期诊断：$$err"; exit 1; fi; \
	echo "PASS ir-abort（退出码 $$st，诊断已出）"

# AST 形状判别表单测：全项目唯一一个直接构造 Node 来钉"语法树长什么样"的用例。
# 判别表一旦判错，错的是整棵树的语义，而错误的 IR 往往"看着挺像那么回事"，
# 端到端（parser + 真机）测不出来，故必须有单测。
# 链接翻译层本体（src/translate.c）而不是它的副本 —— 测的必须是真正要跑的那份
# classifyExp/classifyStmt。translate.c 的 translateProgram 返回 irList()，
# 故 src/ir.c 必须一起链（ir.c 依赖 util.c，已在链上）。
Test/unit/test_expkind: Test/unit/test_expkind.c src/translate.c src/translate.h \
                        src/tree.c src/tree.h src/util.c src/util.h src/ir.c src/ir.h
	$(CC) $(CFLAGS) -o $@ Test/unit/test_expkind.c src/translate.c src/tree.c src/util.c src/ir.c

# 判据是**退出码**：本单测自己会统计 failures 并 return failures ? 1 : 0，
# 提示写 stderr（故不能用上面那批「打印到 stdout 再整体 diff」的写法）。
expkind-test: Test/unit/test_expkind
	@bash -o pipefail -c './Test/unit/test_expkind' && echo "PASS expkind"

# 表达式翻译单测（Task 8）：stdout 逐字节钉住**「place 作为继承属性」**的产物 ——
# `x = 5;` 必须只有一行 `v1 := #5`，而不是 `t1 := #5` + `v1 := t1` 两行。
#
# 这条比对是本单测存在的**唯一**理由：朴素写法与当前写法跑出来的程序结果完全相同，
# 端到端（parser + 参考机 + .exp）一个字都察觉不到，差别只在 IR 行数上 ——
# 那是本任务最大的效率来源（报告亮点 1 的论据），只能逐字节钉。
# 其余用例（二元/取负/链式赋值/括号/值不被需要）写 stderr、以退出码为判据
# （`-o pipefail` 让非 0 退出码同样能判红），**不往 stdout 写一个字节**。
Test/unit/test_translate: Test/unit/test_translate.c src/translate.c src/translate.h \
                          src/tree.c src/tree.h src/util.c src/util.h src/ir.c src/ir.h
	$(CC) $(CFLAGS) -o $@ Test/unit/test_translate.c src/translate.c src/tree.c src/util.c src/ir.c

translate-test: Test/unit/test_translate
	@bash -o pipefail -c './Test/unit/test_translate | diff -u Test/unit/translate_exp_expected.txt -' \
	    && echo "PASS translate-exp"

# 注：这里**没有** `test` 目标。评分流程是 `make` + `./parser <文件>`，不需要它；
# 而让它挂着一个开发期脚本（scripts/run_tests.sh）会把"构建可用性"和"开发期工具
# 是否随提交一起打包"绑在一起 —— 打包时漏掉 scripts/ 就会让 `make test` 直接坏掉。
# `make` 本身必须能独立跑通，故把测试入口交给文档说明，不做成 make 目标。
#
# 同一条原则也适用于 `Test/regress/driver`：它是**开发期**的回归驱动，源码在
# Test/regress/ 下，故同样**不进 `all`**（见文件开头的说明）。脚本要它时自己会建。
# 自检请直接：
#     bash scripts/run_tests.sh      # 需先 make；脚本自己会检查 ./parser 并构建回归驱动

.PHONY: all clean unit-test lexer-test type-test nodetype-test symtab-test report-test ir-test \
        ir-abort-test expkind-test translate-test
clean:
	rm -f parser cc *.o src/*.o lex.yy.c syntax.tab.c syntax.tab.h syntax.output \
	      Test/unit/test_tree Test/unit/test_lexer Test/unit/test_type \
	      Test/unit/test_nodetype Test/unit/test_symtab Test/unit/test_report \
	      Test/unit/test_ir Test/unit/test_ir_abort Test/unit/test_expkind \
	      Test/unit/test_translate \
	      Test/regress/driver Test/regress/*.o
