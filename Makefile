CC     = gcc
FLEX   = flex
BISON  = bison

# 选做 3.2（高维数组/数组形参）的编译期开关。默认 1 = 实现完整选做；
# `make LAB3_32=0` 得到仅必做构建（相关输入只打提示、不产 IR）。
# ⚠️ 切换取值之前必须先 `make clean`：LAB3_32 只进 CFLAGS，**不进任何目标的
#    依赖表**，否则 make 会复用按旧值编出的 .o，得到一个静默的混合配置。
LAB3_32 ?= 1
CFLAGS = -std=c99 -Wall -g -Isrc -DLAB3_ENABLE_32=$(LAB3_32)

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

# 优化框架单测（Task 16）：只测框架与不变量断言，不测 O1–O9 的实际优化。
# 输入是**字符串形式的 IR**（见 test_opt.c 顶部），Task 17–19 加用例时只添字符串。
# 链接 src/optimize.c + src/ir.c（+ util.c：ir.c 的 xmalloc/xstrdup 在 util.c）。
Test/unit/test_opt: Test/unit/test_opt.c src/optimize.c src/optimize.h \
                     src/ir.c src/ir.h src/util.c src/util.h
	$(CC) $(CFLAGS) -o $@ Test/unit/test_opt.c src/optimize.c src/ir.c src/util.c

# 判据是退出码；成功时本程序不写 stdout/stderr，PASS 由这里打印。
opt-test: Test/unit/test_opt
	@bash -o pipefail -c './Test/unit/test_opt' && echo "PASS opt-test"

# 「响亮失败」用例：单赋值 / 先定义后使用被违反时必须 abort。
# abort 带 SIGABRT 结束整个进程，故与普通用例分成**独立调用**（同 ir-abort-test 的理由）。
# 通过判据两条都要满足：退出码**恰为 134**（128+SIGABRT）**且** stderr 含诊断 ——
# 只看“非 0”会让段错误/其它信号蒙混过关，只看文字则可能漏掉“其实没崩”。
# 8 个子模式各自跑一次（Task 16b：条件取值例外补 6 个反例）；`mktemp` 避免留临时件。
opt-abort-test: Test/unit/test_opt
	@bash -c 'exec 2>/dev/null; rc=0; for m in --double-assign --use-before-def --pair-no-label \
	    --pair-cross-func --pair-third --pair-wrong-const --pair-no-prev0 --self-use; do \
	    err=$$(mktemp); ./Test/unit/test_opt $$m >/dev/null 2>$$err; st=$$?; \
	    if [ $$st -ne 134 ]; then echo "FAIL opt-abort $$m: 退出码 $$st（期望 134）"; rc=1; \
	    elif ! grep -qE "赋值|使用" $$err; then echo "FAIL opt-abort $$m: 诊断缺失"; cat $$err; rc=1; fi; \
	    rm -f $$err; done; \
	    if [ $$rc -eq 0 ]; then echo "PASS opt-abort（8 个模式均 134）"; fi; exit $$rc'

# ── 优化开/关对拍（Task 18）─────────────────────────────────────────────────
# LAB3_OPT 是**编译期宏**，不是环境变量：`LAB3_OPT=0 ./parser` 不会关掉优化。
# 故这里单独构建一个 `-DLAB3_OPT=0` 的 parser，再用 run_lab3.sh 的 LAB3_PARSER
# 指向它，跑同一批 *.cmm。两条链都以 .exp（参考机行为）为裁判，因此“优化未改
# 行为”由 oracle 独立证明，而不是自己比自己。
build/parser-noopt: $(SRCS) syntax.tab.c lex.yy.c
	mkdir -p build
	$(CC) $(CFLAGS) -DLAB3_OPT=0 -o $@ $(SRCS) syntax.tab.c lex.yy.c -lfl

# ★ 只跑五个**真实用例组**：Test/lab3/selftest/ 下的夹具是运行台的负例
#   （缺 .in / 缺 .exp / 故意 mismatch），递归扫 Test/lab3 必然红，不能当回归门。
opt-off-test: build/parser-noopt
	@set -e; for d in basic arr1d arrnd func req; do \
	    echo "=== opt-off: Test/lab3/$$d ==="; \
	    LAB3_PARSER="$(CURDIR)/build/parser-noopt" bash scripts/run_lab3.sh "Test/lab3/$$d"; \
	done

# AST 形状判别表单测：全项目唯一一个直接构造 Node 来钉"语法树长什么样"的用例。
# 判别表一旦判错，错的是整棵树的语义，而错误的 IR 往往"看着挺像那么回事"，
# 端到端（parser + 真机）测不出来，故必须有单测。
# 链接翻译层本体（src/translate.c）而不是它的副本 —— 测的必须是真正要跑的那份
# classifyExp/classifyStmt。translate.c 的 translateProgram 返回 irList()，
# 故 src/ir.c 必须一起链（ir.c 依赖 util.c，已在链上）。
# ★ Task 13 起 translate.c 还要在翻译期重建变量类型（typeBasic/typeArrayFromDims，
#   供高维下标的步长用），故 src/type.c 也必须一起链。
Test/unit/test_expkind: Test/unit/test_expkind.c src/translate.c src/translate.h \
                        src/tree.c src/tree.h src/util.c src/util.h src/ir.c src/ir.h \
                        src/type.c src/type.h
	$(CC) $(CFLAGS) -o $@ Test/unit/test_expkind.c src/translate.c src/tree.c src/util.c src/ir.c src/type.c

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
                          src/tree.c src/tree.h src/util.c src/util.h src/ir.c src/ir.h \
                          src/type.c src/type.h
	$(CC) $(CFLAGS) -o $@ Test/unit/test_translate.c src/translate.c src/tree.c src/util.c src/ir.c src/type.c

translate-test: Test/unit/test_translate
	@bash -o pipefail -c './Test/unit/test_translate | diff -u Test/unit/translate_exp_expected.txt -' \
	    && echo "PASS translate-exp"

# 条件翻译单测（Task 9）：stdout 逐字节钉住 `if (n > 0) write(1);` 的产物 ——
# 条件两侧都是"简单操作数"（ID 与 INT）时**直接内联进 IF**，只有**一条** IF 指令，
# 而不是先落 `t1 := #0` 再 `IF v1 > t1 GOTO …` 两条。这正是设计文档 §6.1 表里
# `if (n > 0) …` 那行"省 1 条指令"的落地（要求书 4.1.6 样例 1 的"优化后版本"同形态）。
# 两种写法跑出来的程序结果完全相同，端到端一个字都察觉不到 —— 只能靠逐字节比对钉。
# 其余用例（六个 relop 的映射、NOT 的目标互换、AND/OR 短路、条件作值、place 为空）
# 写 stderr、以退出码为判据（`-o pipefail` 让非 0 退出码同样判红），**不往 stdout
# 写一个字节**。链接行与 translate-test 一致；Task 13 起 translate.c 还调用
# typeBasic/typeArrayFromDims，故 src/type.c 也要一起链（symtab.c 仍不需要）。
Test/unit/test_cond: Test/unit/test_cond.c src/translate.c src/translate.h \
                     src/tree.c src/tree.h src/util.c src/util.h src/ir.c src/ir.h \
                     src/type.c src/type.h
	$(CC) $(CFLAGS) -o $@ Test/unit/test_cond.c src/translate.c src/tree.c src/util.c src/ir.c src/type.c

cond-test: Test/unit/test_cond
	@bash -o pipefail -c './Test/unit/test_cond | diff -u Test/unit/cond_expected.txt -' \
	    && echo "PASS cond"

# 程序级翻译单测（Task 10）：**用仓库里真正的 lexer/parser** 解析一段 .cmm 拿到 AST，
# 调 translateProgram，再把 printIR 的输出与实测钉住的期望逐字节比对。
#
# 它存在的理由（协调者加的要求，计划里没有）：Task 12 之前 `translateProgram` 没有调用方
# ⇒ Task 10 写下的语句 / 复合语句 / 函数翻译在端到端跑通之前**没有任何判据**，
# 而 "判据对但没人在跑" 正是本项目反复栽过的失效（Task 8 审查刚抓到一次）。
# brief 里本任务的验收（用 c_oracle.py 生成 fact.cmm 的 .exp）**不经过我们的编译器**，
# 故它一个字都证明不了本单测的被测对象。
#
# 链接行照 test_lexer（真 lexer/parser）+ translate.c/ir.c（被测模块与它的唯一依赖）；
# main 由测试文件自己提供。
# 输入 Test/unit/lab3_prog_in.cmm **刻意避开本任务尚未实现的能力**：
# 函数调用 / read / write / 数组下标都是 Task 11 的桩，走到就 abort。
#
# ★ 该夹具**含关系运算**（`if (a > b)` 单字符、`while (t <= b)` 双字符，顺带钉住
# lexer 的最长匹配）。历史：RELOP 的操作符文本原先没进 AST（src/lexical.l 发 RELOP
# 时 lexeme 与 opText 都是 NULL），`if (a > b)` 会让 translateCond 响亮 abort
# （`unknown relop "(null)"`，退出码 134），故本条夹具当时**刻意避开**关系条件
# （见 .superpowers/sdd/lab3/task-10-report.md §7）。
# Task 10b 修法：给 `Node` 加专用字段 `opText`（lexeme 保持 NULL，语法树输出逐字节
# 不变），关系运算符文本填在那里。Task 10c 又把构造收进 `newRelop`
# （tree.h/tree.c）——**词法器与单测共用同一条构造路径**，两边逐字段同形。
# ⇒ 关系条件已在判据射程内（本夹具逐字节钉住它们产出的 `IF v1 > v2 …` /
#   `IF v3 <= v2 …`），"修好后加回关系条件"这件事**已完成**，无需再动本夹具。
# 兜底（裸变量求值到临时变量再与 #0 比）仍由 main 里的 `!x` 覆盖（NOT 会递归进兜底），
# 故关系条件加回后**没有**丢掉原来那两条被测分支。
Test/unit/test_translate_prog: Test/unit/test_translate_prog.c lex.yy.c syntax.tab.c \
                               src/tree.c src/report.c src/util.c src/translate.c src/ir.c \
                               src/type.c src/tree.h src/report.h src/util.h src/translate.h \
                               src/ir.h src/type.h
	$(CC) $(CFLAGS) -I. -o $@ Test/unit/test_translate_prog.c lex.yy.c syntax.tab.c \
	      src/tree.c src/report.c src/util.c src/translate.c src/ir.c src/type.c -lfl

# 三条防线（照 scripts/run_tests.sh 的 check_one 写法，不另创一套）：
#   ① stderr **单独**捕获、非空即判红 —— "正常流程不写 stderr"这条约束此前**没有任何门**
#      （只有一份末入库的脚本查过 stderr_bytes=0）。要求书要求错误提示走 stdout，
#      故通过时 stderr 必须为空；万一哪天有诊断漏进 stderr 而 stdout 恰好还对，
#      这条会当场变红 —— 而不是让约束悄悄失守。
#   ② 退出码：yyparse 失败返回 2、内部 abort 是 134，非 0 一律判红。不查状态码，
#      段错误留下的**空产物**会与"正确地什么都不打印"混在一起、只在 diff 处含糊地红。
#   ③ stdout 与期望逐字节 diff。
# stdout/stderr 与退出码都在 **bash -c 脚本内部**取（$$ 转义后交给 shell），
# 绝不把 $? 塞进跨层引号（本项目已三次栽在这上面，其中一次是 make 失败却打印 rc=0）。
prog-test: Test/unit/test_translate_prog
	@bash -c 'out=$$(mktemp); err=$$(mktemp); \
	    ./Test/unit/test_translate_prog < Test/unit/lab3_prog_in.cmm >$$out 2>$$err; rc=$$?; \
	    if [ $$rc -ne 0 ]; then echo "FAIL translate-prog: 退出码 $$rc（yyparse 失败或内部 abort）"; rc=1; fi; \
	    if [ -s $$err ]; then echo "FAIL translate-prog: stderr 非空（正常流程不得写 stderr）"; sed "s/^/      /" $$err | head -10; rc=1; fi; \
	    if ! diff -u Test/unit/translate_prog_expected.txt $$out; then echo "FAIL translate-prog: stdout 与期望不符"; rc=1; fi; \
	    rm -f $$out $$err; \
	    if [ $$rc -eq 0 ]; then echo "PASS translate-prog"; fi; exit $$rc'

# ── 调用 / read · write / 一维数组下标（Task 11）──────────────────────────────
# 与 prog-test **同一套三重断言**（退出码 / stderr 空 / stdout 逐字节），只是换了输入与期望；
# 输入仍由**真 lexer/parser** 解析（同一个 `test_translate_prog` 二进制，读 stdin）。
# 为什么另建夹具而不追加进 lab3_prog_in.cmm：那里的期望逐字节钉住 Task 10 的语句/函数翻译，
# 夹具与期望一一对应；混进 Task 11 的新形状只会让"某条红线属于哪个任务"含糊。
#
# 两类夹具**分开成两个目标**，正是为了让两类变异各自**恰好**打红一条（见
# .superpowers/sdd/lab3/task-11-report.md 的变异自证）：
#   · 把 translateArgs 的逆序改成正序 ⇒ 只有 call-test 红（ARG 顺序变了，值也随之变错）；
#   · 去掉常量下标折叠               ⇒ 只有 arr-test 红（`&a + #8` 变成 `idx := i*4` 再相加）。
#
# ★ 中间件（IR 落盘处）**必须用 `mktemp`，不许写 `build/xxx.ir`**（Task 11b 审查修正）：
#   `build/` 在 .gitignore 里，而本 Makefile **没有** `mkdir -p build`，
#   ⇒ **fresh clone 上 `make call-test` 必红**（`cp -f … build/t11_call.ir` 因目录不存在
#   失败）。好在那是**响亮**的红，不是静默绿；但同一 recipe 里另外三个临时件
#   （out/err/got）本来就是 `mktemp`，照它们改成本最低、也不必再引入一个目录依赖。
#
# 每个目标第二段把生成的 IR **丢进参考机（scripts/irsim_ref.py）执行**，比对 WRITE 输出 ——
# 证明逐字节钉住的那份 IR 不只是"我们的输出"，而是**语义正确**的。
# 判据链上没有一处是我们的手抄：输入是真源码、IR 是真 parser+翻译层的产物、
# 期望 IR 与期望输出都是**实测钉住**的（对 call 夹具，参考机把 f(1,2,3) 算成 123；
# 若 ARG 顺序写反，同一份 IR 会跑出 321 并当场打红第二段）。
call-test: Test/unit/test_translate_prog
	@bash -c 'set -u; rc=0; \
	    PY="$$(command -v python3 || command -v python)"; \
	    out=$$(mktemp); err=$$(mktemp); got=$$(mktemp); ir=$$(mktemp); \
	    ./Test/unit/test_translate_prog < Test/unit/lab3_call_in.cmm >$$out 2>$$err; prc=$$?; \
	    if [ $$prc -ne 0 ]; then echo "FAIL call-test: 退出码 $$prc（yyparse 失败或内部 abort）"; rc=1; fi; \
	    if [ -s $$err ]; then echo "FAIL call-test: stderr 非空（正常流程不得写 stderr）"; sed "s/^/      /" $$err | head -10; rc=1; fi; \
	    if ! diff -u Test/unit/translate_call_expected.txt $$out; then echo "FAIL call-test: stdout 与期望不符"; rc=1; fi; \
	    cp -f $$out $$ir; \
	    if ! "$$PY" -I scripts/irsim_ref.py --run $$ir --in Test/unit/lab3_call.in >$$got 2>$$err; then \
	        echo "FAIL call-test: 参考机执行失败"; sed "s/^/      /" $$err | head -5; rc=1; fi; \
	    tail -n +2 $$got > $$got.v; mv $$got.v $$got; \
	    if ! diff -u Test/unit/lab3_call_out.txt $$got; then echo "FAIL call-test: 参考机 WRITE 输出与期望不符"; rc=1; fi; \
	    rm -f $$out $$err $$got $$ir; \
	    if [ $$rc -eq 0 ]; then echo "PASS call-test"; fi; exit $$rc'

arr-test: Test/unit/test_translate_prog
	@bash -c 'set -u; rc=0; \
	    PY="$$(command -v python3 || command -v python)"; \
	    out=$$(mktemp); err=$$(mktemp); got=$$(mktemp); ir=$$(mktemp); \
	    ./Test/unit/test_translate_prog < Test/unit/lab3_arr_in.cmm >$$out 2>$$err; prc=$$?; \
	    if [ $$prc -ne 0 ]; then echo "FAIL arr-test: 退出码 $$prc（yyparse 失败或内部 abort）"; rc=1; fi; \
	    if [ -s $$err ]; then echo "FAIL arr-test: stderr 非空（正常流程不得写 stderr）"; sed "s/^/      /" $$err | head -10; rc=1; fi; \
	    if ! diff -u Test/unit/translate_arr_expected.txt $$out; then echo "FAIL arr-test: stdout 与期望不符"; rc=1; fi; \
	    cp -f $$out $$ir; \
	    if ! "$$PY" -I scripts/irsim_ref.py --run $$ir >$$got 2>$$err; then \
	        echo "FAIL arr-test: 参考机执行失败"; sed "s/^/      /" $$err | head -5; rc=1; fi; \
	    tail -n +2 $$got > $$got.v; mv $$got.v $$got; \
	    if ! diff -u Test/unit/lab3_arr_out.txt $$got; then echo "FAIL arr-test: 参考机 WRITE 输出与期望不符"; rc=1; fi; \
	    rm -f $$out $$err $$got $$ir; \
	    if [ $$rc -eq 0 ]; then echo "PASS arr-test"; fi; exit $$rc'

# ── read/write 内建 + 语义闸（Task 12b）──────────────────────────────────────
# 与上面两类的区别：这里测的是 **main.c 的端到端行为**（是否打印假错误、语义错误时
# 是否还翻译），不是翻译产物本身。故跑的是真 `./parser`（带 main），而不是
# test_translate_prog 那条共享二进制。四条断言的说明见 Test/unit/test_builtins.sh。
builtin-test: parser
	@bash Test/unit/test_builtins.sh

# 结构体输入的"不可翻译"提示（选做 3.1 的样例 1 / 3.2 的数组形参样例都要求：
# 不实现时只打提示、输出文件为空）。判据是 **main.c 的端到端行为**，故跑真 parser。
# 夹具**不放** Test/lab3/**：那个运行台会递归扫描并要求每个 .cmm 都有 .exp 且产出 IR，
# 而结构体样例按设计不产 IR。
struct-test: parser
	@bash Test/unit/test_struct.sh

# ── 数组整体值守卫（最终整分支审查 Important 1）──────────────────────────────
# 语义层对数组长度不参与判等，故 `a = b;` / `int a[2] = b;` 会被放行。翻译层
# 必须把"数组名当整体标量值"变为**响亮失败**：否则产出 `v1 := v2`，参考机只改
# `a[0]` —— 静默算错。判据 = 两个夹具分别 rc=134、stderr 含 `数组`、stdout 为空、
# 输出 IR 为空（或未创建）；任一不满足即红。
# 变异自证：去掉 translate.c 的 TY_ARRAY 守卫后两个用例都会变成 rc=0/有 IR ⇒ 本目标红。
array-guard-test: parser
	@bash -c 'rc=0; for c in array_value array_init_value; do \
	    ir=$$(mktemp); out=$$(mktemp); err=$$(mktemp); \
	    ./parser Test/unit/$$c.cmm $$ir >$$out 2>$$err; st=$$?; \
	    if [ $$st -ne 134 ]; then echo "FAIL array-guard $$c: 退出码 $$st（期望 134）"; rc=1; fi; \
	    if ! grep -qF "数组" $$err; then echo "FAIL array-guard $$c: stderr 未含「数组」诊断"; rc=1; fi; \
	    if [ -s $$out ]; then echo "FAIL array-guard $$c: stdout 非空"; sed "s/^/      /" $$out | head -5; rc=1; fi; \
	    if [ -s $$ir ]; then echo "FAIL array-guard $$c: 输出 IR 非空"; sed "s/^/      /" $$ir | head -5; rc=1; fi; \
	    rm -f $$ir $$out $$err; done; \
	    if [ $$rc -eq 0 ]; then echo "PASS array-guard（2 个用例均 134，stderr 含「数组」，stdout/IR 为空）"; fi; exit $$rc'

# ── 生成标号 vs 用户函数名冲突（最终整分支审查 Important 2）──────────────────
# 参考机 labelTable 同时登记 LABEL 与 FUNCTION：用户函数 `label1` + 一个 `if`
# 会同时得到 `FUNCTION label1` 与 `LABEL label1`，装载期 DuplicatedLabelError。
# 判据：rc=0、stdout/stderr 空、IR 含 `FUNCTION label1 :` 但**不含**
# `LABEL label1 :`，且参考机执行输出与 .exp 一致。
# 变异自证：删掉 newLabelName 的跳过循环 ⇒ 参考机 DuplicatedLabelError，本目标红。
label-test: parser
	@bash -c 'set -u; rc=0; \
	    PY="$$(command -v python3 || command -v python)"; \
	    out=$$(mktemp); err=$$(mktemp); got=$$(mktemp); ir=$$(mktemp); \
	    ./parser Test/unit/label_collision.cmm $$ir >$$out 2>$$err; prc=$$?; \
	    if [ $$prc -ne 0 ]; then echo "FAIL label-test: 退出码 $$prc（期望 0）"; rc=1; fi; \
	    if [ -s $$err ]; then echo "FAIL label-test: stderr 非空"; sed "s/^/      /" $$err | head -10; rc=1; fi; \
	    if [ -s $$out ]; then echo "FAIL label-test: stdout 非空"; sed "s/^/      /" $$out | head -10; rc=1; fi; \
	    if ! grep -q "^FUNCTION label1 :$$" $$ir; then echo "FAIL label-test: IR 缺 FUNCTION label1"; rc=1; fi; \
	    if grep -q "^LABEL label1 :$$" $$ir; then echo "FAIL label-test: IR 含 LABEL label1（与函数名冲突）"; rc=1; fi; \
	    if ! "$$PY" -I scripts/irsim_ref.py --run $$ir --in Test/unit/label_collision.in >$$got 2>$$err; then \
	        echo "FAIL label-test: 参考机执行失败"; sed "s/^/      /" $$err | head -5; rc=1; \
	    else \
	        tail -n +2 $$got > $$got.v; mv $$got.v $$got; \
	        if ! diff -u Test/unit/label_collision.exp $$got; then echo "FAIL label-test: 参考机 WRITE 输出与期望不符"; rc=1; fi; \
	    fi; \
	    rm -f $$out $$err $$got $$ir; \
	    if [ $$rc -eq 0 ]; then echo "PASS label-test"; fi; exit $$rc'

# ── 前向函数调用（实验三 4.1.1 未要求"先定义后使用"）────────────────────────
# main 写在前面、add 定义在后面；语义层必须放行，产出非空 IR，且参考机算出 7。
# 变异自证：删掉 analyzeCall 里的 isForwardFunc 分支 ⇒ parser 报 Error type 2、
# IR 为空 ⇒ run_lab3.sh 以"parser 未产出 IR"判红。
forward-test: parser
	@bash scripts/run_lab3.sh Test/unit/forward_call.cmm && echo "PASS forward-test"

# ── 驱动命令行兼容（判分器 O1）──────────────────────────────────────────────
# 判分器以 `./parser -O1 A.cmm out.ir` 调用；驱动必须把 -O 当选项而不是输入文件。
# 四种调用形式都要工作：-O 在输入前、-o 在输入前、-O 在输入后、标准位置参数。
# 每种都断言：rc=0、stderr 空、输出 IR 非空、参考机 WRITE 输出与 .exp 一致（7）。
# 变异自证：把 main.c 恢复成"只认 argv[1]/argv[2]" ⇒ 第 1 种形式 fopen("-O1")
# 失败、不产 IR ⇒ 本目标红。
cli-test: parser
	@bash -c 'set -u; rc=0; \
	    mkdir -p build; \
	    tmp=$$(mktemp -d build/cli-test.XXXXXX) || exit 1; \
	    PY="$$(command -v python3 || command -v python)"; \
	    if [ -z "$$PY" ]; then echo "FAIL cli-test: 找不到 python3 / python"; rm -rf "$$tmp"; exit 1; fi; \
	    for mode in a b c d; do \
	      ir=$$tmp/$$mode.ir; out=$$tmp/$$mode.out; err=$$tmp/$$mode.err; got=$$tmp/$$mode.got; \
	      case $$mode in \
	        a) ./parser -O1 Test/unit/forward_call.cmm "$$ir" >"$$out" 2>"$$err" ;; \
	        b) ./parser -o "$$ir" Test/unit/forward_call.cmm >"$$out" 2>"$$err" ;; \
	        c) ./parser Test/unit/forward_call.cmm -O1 "$$ir" >"$$out" 2>"$$err" ;; \
	        d) ./parser Test/unit/forward_call.cmm "$$ir" >"$$out" 2>"$$err" ;; \
	      esac; \
	      st=$$?; \
	      if [ $$st -ne 0 ]; then echo "FAIL cli-test[$$mode]: 退出码 $$st（期望 0）"; rc=1; fi; \
	      if [ -s "$$err" ]; then echo "FAIL cli-test[$$mode]: stderr 非空"; sed "s/^/      /" "$$err" | head -5; rc=1; fi; \
	      if [ ! -s "$$ir" ]; then echo "FAIL cli-test[$$mode]: 未产出非空 IR"; rc=1; fi; \
	      if ! "$$PY" -I scripts/irsim_ref.py --run "$$ir" --in Test/unit/forward_call.in >"$$got" 2>"$$err"; then \
	        echo "FAIL cli-test[$$mode]: 参考机执行失败"; sed "s/^/      /" "$$err" | head -5; rc=1; \
	      else \
	        tail -n +2 "$$got" > "$$got.vals"; mv "$$got.vals" "$$got"; \
	        if ! diff -u Test/unit/forward_call.exp "$$got"; then echo "FAIL cli-test[$$mode]: 参考机 WRITE 输出与 .exp 不符"; rc=1; fi; \
	      fi; \
	    done; \
	    rm -rf "$$tmp"; \
	    if [ $$rc -eq 0 ]; then echo "PASS cli-test（4 种参数形式，rc=0/stderr 空/IR 非空/输出 7）"; fi; exit $$rc'

# ── 不同函数中的同名局部变量（A-3 第二项）────────────────────────────────────
# 语义层按函数作用域放行 `int x` 出现在多个函数里；翻译层每个函数必须清空
# "源码名 -> vN/Type"的扁平映射（计数器不清），否则 registerVarType 响亮 abort、
# 无 IR。变异自证：去掉 translateExtDef 里的 clearFunctionVarMaps() ⇒
# run_lab3.sh 以"parser 未产出 IR"判红。
duplicate-locals-test: parser
	@bash scripts/run_lab3.sh Test/unit/duplicate_locals.cmm && echo "PASS duplicate-locals-test"

# ── 嵌套块同名遮蔽（A-3 第三项）─────────────────────────────────────────────
# 语义层按作用域允许内层块 `int x` 遮蔽外层 `int x`；翻译层的映射必须按 depth
# 分层，查找取最内层可见条目。变异自证：去掉 ST_COMPST 的 enter/leaveScope
# ⇒ 内层声明与外层同 depth，registerVarType 响亮 abort、无 IR ⇒ 本目标红。
nested-shadow-test: parser
	@bash scripts/run_lab3.sh Test/unit/nested_shadow.cmm && echo "PASS nested-shadow-test"

# ── 单次调用 20 个实参（A-3 第三项）─────────────────────────────────────────
# 要求书对实参个数没有上限；MAX_CALL_ARGS 只是防御上限（256）。
# 变异自证：把 MAX_CALL_ARGS 改回 16 ⇒ translateArgs 响亮 abort、无 IR ⇒ 本目标红。
manyargs-test: parser
	@bash scripts/run_lab3.sh Test/unit/manyargs.cmm && echo "PASS manyargs-test"

# ── 部分下标作为数组实参（A-3d 第四项）──────────────────────────────────────
# `int r[1][2]` 的 `r[0]` 仍是数组；`sum(int a[2])` 按数组只比维数/基类型接受它。
# 翻译层必须把行地址传给被调函数（`ARG t`，t 是 `&r + 0` 类地址），而不是在
# arrayElemAddr 的"走完下标仍非标量"处 abort。变异自证：让实参路径的
# allowPartial 继续 abort（或删掉 translateArgs 的数组分支）⇒ 无 IR ⇒ 本目标红。
rowarg-test: parser
	@bash scripts/run_lab3.sh Test/unit/rowarg.cmm && echo "PASS rowarg-test"

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
        ir-abort-test opt-test opt-abort-test opt-off-test expkind-test translate-test \
        cond-test prog-test call-test arr-test builtin-test struct-test \
        array-guard-test label-test forward-test cli-test duplicate-locals-test \
        nested-shadow-test manyargs-test rowarg-test
clean:
	rm -f parser cc *.o src/*.o lex.yy.c syntax.tab.c syntax.tab.h syntax.output \
	      Test/unit/test_tree Test/unit/test_lexer Test/unit/test_type \
	      Test/unit/test_nodetype Test/unit/test_symtab Test/unit/test_report \
	      Test/unit/test_ir Test/unit/test_ir_abort Test/unit/test_expkind \
	      Test/unit/test_opt Test/unit/test_translate Test/unit/test_cond \
	      Test/unit/test_translate_prog \
	      Test/regress/driver Test/regress/*.o build/parser-noopt
