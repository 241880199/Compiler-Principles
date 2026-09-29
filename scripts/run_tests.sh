#!/usr/bin/env bash
# 遍历 Test/sample 与 Test/err 下全部 .cmm，与同名 .exp 逐字节比对。
# Test/unit/ 不在范围内（由 unit-test / lexer-test 目标各自负责）。
set -u
cd "$(dirname "$0")/.."

if [ ! -x ./parser ]; then
    echo "错误：未找到 ./parser，请先运行 make" >&2
    exit 1
fi

# 实验一的回归驱动**不在** `make` 的默认目标里（理由见 Makefile：`make` 必须能在
# 只有 src/ + Makefile 的包里独立跑通，而 Test/regress/ 是开发期目录）。故由本脚本
# 自己把它建出来 —— 缺失或过期都会重新生成，`make` 对已最新的目标什么都不做。
# 构建失败必须**响亮退出**：静默跳过会让实验一那 30 个用例无声消失，
# 而脚本仍打印"通过 N 个，失败 0 个"。
if ! make Test/regress/driver; then
    echo "错误：无法构建 Test/regress/driver —— 实验一的回归用例无法运行" >&2
    echo "      （它需要 Test/regress/driver.c 及其依赖；请确认仓库完整）" >&2
    exit 1
fi
# 构建成功仍要复核一次可执行位：上面的 `make` 有可能判定"已是最新"而什么都没做。
if [ ! -x Test/regress/driver ]; then
    echo "错误：未找到 Test/regress/driver —— 实验一的回归用例无法运行" >&2
    exit 1
fi

pass=0
fail=0
lab2_ran=0
tmp=$(mktemp)
err=$(mktemp)
# check_pairs 用：存放从实测输出里抽出来的 "(错误类型, 行号)" 序列。
tmp2=$(mktemp)
trap 'rm -f "$tmp" "$err" "$tmp2"' EXIT

# 单个用例的完整判定。抽成函数是因为要跑两个目录、两个程序（见下方两个循环），
# 而这三条防线一条都不能少：
#   ① stderr **单独**捕获，不并进比对的 stdout。若写成 `> "$tmp" 2>&1`，把 reportError
#      改成 fprintf(stderr, ...) 仍然全绿 —— 而要求书 2.1.3 要求错误提示信息输出到
#      **标准输出**，判分脚本读的是 stdout。合并流会让这条规范悄悄失效。
#   ② 退出码检查：程序恒返回 0（要求书未规定退出码），非 0 一定是异常。段错误会留下
#      空输出，不查状态码就会与"正确地什么都不打印"混淆。
#   ③ .exp 缺失判 FAIL：否则用例被删掉 .exp 后会静默变成"无期望输出"。
check_one() {
    cmm="$1"; runner="$2"
    exp="${cmm%.cmm}.exp"
    if [ ! -f "$exp" ]; then
        echo "FAIL  $cmm  （缺同名 .exp）"
        fail=$((fail + 1))
        return
    fi
    "$runner" "$cmm" > "$tmp" 2> "$err"
    status=$?
    if [ "$status" -ne 0 ]; then
        echo "FAIL  $cmm  （退出码 $status）"
        fail=$((fail + 1))
        return
    fi
    # 非空即判 FAIL：标准错误上不该有任何输出。
    if [ -s "$err" ]; then
        echo "FAIL  $cmm  （stderr 非空 —— 要求书要求输出到标准输出）"
        sed 's/^/      /' "$err" | head -10
        fail=$((fail + 1))
        return
    fi
    if diff -q "$exp" "$tmp" > /dev/null; then
        pass=$((pass + 1))
        check_pairs "$cmm" "$tmp"
    else
        fail=$((fail + 1))
        echo "FAIL  $cmm  （.exp 不符 —— 实测输出与期望逐字节不一致）"
        diff -u "$exp" "$tmp" | sed 's/^/      /' | head -30
    fi
}

# 官方样例的**评分字段**校验：只比"错误类型 + 行号"序列，不比说明文字。
# 要求书明说"说明文字的内容没有具体要求，但是错误类型和出错的行号一定要正确，
# 因为这是判断输出的错误提示信息是否正确的唯一标准"（3.1.3 节）。
# 故这里与 `.exp` 分成两层：`.exp` 钉**我们的完整文本**（防回归），`.pairs` 钉
# **官方的 (类型,行号) 序列**（防判分失分）。合并成一条会让"改了说明文字"与
# "错误类型/行号搞错了"无法区分 —— 实验一的教训。
# 只在 `.exp` 已通过时调用（见 check_one 的成功分支）：输出本身是错的时候，
# `.pairs` 的 diff 只是噪声，会掩盖真正的回归。
check_pairs() {
    cmm="$1"; out="$2"
    pairs="${cmm%.cmm}.pairs"
    [ -f "$pairs" ] || return 0
    sed -nE 's/^Error type ([0-9]+) at Line ([0-9]+):.*/\1 \2/p' "$out" > "$tmp2"
    if ! diff -q "$pairs" "$tmp2" > /dev/null; then
        echo "FAIL  $cmm  （判分字段不符：期望的 (类型,行号) 序列与实测不一致）"
        diff -u "$pairs" "$tmp2" | sed 's/^/      /' | head -20
        fail=$((fail + 1))
    fi
}

# 实验一的用例（Test/sample、Test/err）期望的是**语法树或 A/B 错误**，由回归驱动跑；
# 实验二的用例（Test/lab2）期望的是**语义错误**，由 ./parser 跑。
for cmm in Test/sample/*.cmm Test/err/*.cmm; do
    [ -e "$cmm" ] || continue
    runner=./Test/regress/driver
    check_one "$cmm" "$runner"
done

# Test/lab2 由后续任务建立；不存在时整段跳过，实验一的 30 个用例仍照跑。
# `lab2_ran` 数的是**本循环真正跑到**的用例数（不是 pass）：见下方与 `pass -eq 0`
# 并列的断言 —— 目录在而一个用例都没跑成，说明这半边哨兵已失效。
if [ -d Test/lab2 ]; then
    for cmm in Test/lab2/*.cmm; do
        [ -e "$cmm" ] || continue
        lab2_ran=$((lab2_ran + 1))
        runner=./parser
        check_one "$cmm" "$runner"
    done
fi

# ── 决策用例必须存在 ────────────────────────────────────────
# 设计文档 §8 的十条决策各有一个专属用例。缺一个就判 FAIL ——
# 否则"改错一条决策"将没有任何用例能发现（实验一 crlf.cmm 那次同类：
# 防线静默消失、全部用例照过）。
#
# **名单已是全量 18 个**（Task 11 Step 5 由 5 个扩到 16 个；终审 Task 12 再加
# dec16/dec17 两个"漏报修复"的守卫）。历史：Task 10 落地时只列已存在的 5 个
# （dec02/dec03/dec08/dec10/dec14）—— 那个"只列已存在的"状态是当时的**权宜**，
# 理由记载在 task-10-report.md；Task 11 建齐其余 11 个后即扩到全量。
# 名单比文件多一个就会报红（这正是这条断言的作用），所以扩名单必须与建用例同一次落地。
#
# 已知的两个"既有、但不在计划 Task 11 创建清单里"的用例（**不要重复创建**）：
#   dec08_poison            Task 8 修缺陷时补的
#   dec14_rejecteddef_params Task 7 修缺陷时补的
# Task 12 新增的两个（终审发现"修了但没有用例守护"）：
#   dec16_structname_as_exp 结构体名出现在变量位置 ⇒ 必须报错误 1（修前一条都不报）
#   dec17_callorder         调用名检查先于收集实参 ⇒ `inc(j)` 两者都未定义时出 2@3（还原顺序则出 1@3）
#
# 用例名 → 决策编号：#1 名字空间冲突 #2 条件非 int #3 形参作用域 #4 局部结构体
#                   #5 只有未定义结构体 #6 下标是数组 #7 保留最早 #8 UNKNOWN 毒化
#                   #9 同行去重（两个用例：adjacent 顺序 + nonmono 非单调行号）
#                   #10 定义先于使用
# dec13/dec14 不是新增决策，而是**已有规则在边界上的钉**（故不加进设计文档 §8 的十条表）：
#   dec13 互相包含的结构体          → 17（全项目唯一"顺序不可倒退"之处，顺序反了就栈溢出）
#   dec14 被拒定义的形参作用域      → 3（决策 3 扩展到被拒定义上；Task 7 评审缺陷的守卫）
# 历史上还有 dec11/dec12 两个**纯选做 2.1** 的用例（"声明 + 签名冲突的定义" → 19 与 18、
# "定义在前 + 冲突声明" → 只有 19）。2026-09-29 随选做 2.1 一并移除：本文法里没有"声明"
# 这一形态，错误 18/19 两个码不存在，那两个用例的期望输出已无对应行为。
# 注意 dec09b_nonmono 是**端到端**的判别用例（非单调行号 L→M→L），
# 它比单测强：走的是真实遍历。不要因为它名字带 b 就以为可以省。
for d in dec01_namekind dec02_condnotint dec03_paramscope dec04_localstruct \
         dec05_undefonly dec06_arrindex dec07_keepfirst dec08_poison \
         dec09_sameline dec09b_nonmono dec10_useorder dec13_mutualstruct \
         dec14_rejecteddef_params dec15_iforder dec16_structname_as_exp \
         dec17_callorder; do
    if [ ! -f "Test/lab2/$d.cmm" ]; then
        # 措辞刻意**不**指向设计文档 §8：名单里 dec11..dec15 本就不在 §8 的十条表里
        # （它们是"已有规则在边界上的钉"，见上）。把排查者引到错误文档比不指路更坏。
        echo "FAIL  决策/边界用例缺失：Test/lab2/$d.cmm（这条钉没了守护 —— 改坏对应行为不会有任何用例变红）"
        fail=$((fail + 1))
    fi
    if [ ! -f "Test/lab2/$d.exp" ]; then
        echo "FAIL  决策用例缺期望文件：Test/lab2/$d.exp"
        fail=$((fail + 1))
    fi
done

# ── 官方样例的 .pairs 必须存在 ────────────────────────────────
# `check_pairs` 开头是 `[ -f "$pairs" ] || return 0` —— 那是为了让**非官方**的用例
# （决策用例、边界用例）不必各配一个 .pairs（判分字段只有官方样例才有）。
# 但它同时意味着：**删掉任何一条官方样例的 .pairs 都会静默关掉那一层防线** ——
# 脚本照样打印"通过 N 个，失败 0 个"、退出 0。Task 10 存在的理由就是保护评分字段
# （错误类型 + 行号），而它自己的保护此前**没有被保护** —— 与上面那条决策用例断言、
# 以及实验一 crlf.cmm 的运行时断言，防的是同一类失效：防线静默消失、无任何信号。
#
# 这份清单**同时充当"哪些是官方样例"的文档**（17 个必做 + 选做 2.2 的 2 个）。
# 历史：要求书的 6 个选做样例里，2 个属选做 2.1（函数声明）、2 个属选做 2.3（结构等价），
# 2026-09-29 随那两个选做一并移除 —— 本版本不做它们，端到端的期望输出已无对应行为。
# 新增官方样例时必须在**同一提交**里加进这里，否则新样例的 .pairs 被删依旧是静默的。
for p in lab2_undef_exp lab2_undef_func lab2_dupvar lab2_dupfunc \
         lab2_assign_mismatch lab2_lvalue lab2_operand lab2_return \
         lab2_argcount lab2_notarray lab2_notfunc lab2_subscript2 \
         lab2_notstruct lab2_nofield lab2_dupfield lab2_dupstruct \
         lab2_undefstruct \
         lab2_funcreuse lab2_shadow; do
    # 三样都要判：只有 `.pairs` 那一层被断言过，而 `.cmm` **不见了**同样是防线失效。
    # 实测（Task 12）：把 lab2_dupvar.cmm 移走、留下 .exp 与 .pairs，循环不再迭代它、
    # 脚本照旧打印「通过 72 个，失败 0 个」并退出 0 —— 官方样例静默少了一个，
    # 而判分字段那层防护看似完好。与本文件其它"防线静默消失"断言防的是同一类失效。
    if [ ! -f "Test/lab2/$p.cmm" ]; then
        echo "FAIL  官方样例缺 .cmm：Test/lab2/$p.cmm（该样例已整体消失，判分字段那一层防线随之失效）"
        fail=$((fail + 1))
    fi
    if [ ! -f "Test/lab2/$p.pairs" ]; then
        echo "FAIL  官方样例缺 .pairs：Test/lab2/$p.pairs（判分字段那一层防线已静默失效）"
        fail=$((fail + 1))
    fi
done

# ── 冲突数断言 ────────────────────────────────────────────────
# **期望恰好 3 个冲突 state，且每个都是「1 shift/reduce」。** 归属（实测，粘自
# syntax.output；bison 3.8.2（本机）、3.5.1（focal，判分版）与 3.0.4（bionic）
# 三版**完全一致** —— state 编号、状态项、冲突双方、动作全部逐字相同，
# 只有 • 的写法不同（3.5.1/3.0.4 印 `.`，3.8.2 印 `•`）：
#
#   ① State 27  `CompSt: LC • DefList StmtList RC`
#        error  shift, and go to state 29
#        error  [reduce using rule 25 (DefList)]      ← 冲突
#   ② State 32  `DefList: Def • DefList`
#        error  shift, and go to state 29
#        error  [reduce using rule 25 (DefList)]      ← 冲突
#   ③ State 114 `Stmt: IF LP Exp RP Stmt •` / `| IF LP Exp RP Stmt • ELSE Stmt`
#        ELSE  shift, and go to state 116
#        ELSE      [reduce using rule 40 (Stmt)]      ← 冲突
#        （悬空 else，Bison 默认移进即 C 语义）
#
# 编号的由来（Task 6 加选做 2.1 的 `ExtDef : Specifier FunDec SEMI` 之后重测）：
# 这条新产生式只**新增 1 个 state** —— State 26 恰好就是它的完成项
# `ExtDef: Specifier FunDec SEMI •`；该 state 落在 Bison 状态构造序的第 26 位，
# 于是 **≥26 的所有 state 编号统一 +1**（26→27、31→32、113→114），
# 总 state 数 117→118，**而冲突个数不变（仍是 3）**。没有新冲突是因为新产生式
# 不引入任何新的取舍：`Specifier` 之后看到 ID 时，状态里同时有 `VarDec: ID •`
# 与 `FunDec: ID • LP …`；前瞻 SEMI/COMMA/LB 归约为 VarDec，前瞻 LP 移进，
# FunDec 的两条产生式都以 `ID LP` 开头，1 个前瞻符号够用（LALR(1) 足够）。
# 在 26 之前的 state 编号**未变** —— 下文的 `StructSpecifier` 仍是 State 20。
# （本版本虽然**不做**选做 2.1，但那条产生式**仍然保留**：它只为在动作里逐条拒绝
#  函数声明，见 src/syntax.y。所以编号取"带它"的这一支。）
#
# ①② 都是 `Def : error`（错误恢复同步点之四，见 src/syntax.y）引起的同一件事：
# **`DefList` 可空**，于是"看 `error` 时移进"与"按 `DefList → ε` 归约"不可兼得。
# 它们只出现在 `CompSt` 一路（`error` 能跟在 `DefList` 之后，因为 `StmtList` 可
# 以 `error` 开头）；`StructSpecifier : STRUCT OptTag LC • DefList RC`（State 20）
# **不冲突** —— 结构体成员表后面只能跟 `RC`，`error` 根本不进它的前瞻集，
# 那里 `error` 是个无歧义的移进动作。故"两处"指的是 State 27/32，**不是**
# `StructSpecifier`。带同步符的 `Def : error SEMI` 引入的是**同样这 2 个**冲突
# （冲突由 `error` 符号本身引起，与后面跟不跟同步符无关），所以"不带同步符"是
# 纯收益，未额外付冲突代价。
#
# ⚠ 编号的坑（Task 6 实测澄清）：本块与 src/syntax.y 里原先写的 `State 114`
# 是**按 `Def : error SEMI` 变体测的**（那一版冲突落在 26/31/114）。改成裸
# `error` 后少了一个 state，悬空 else 实际退到 113，但当时只重推了"个数"
# 没重测"编号"，于是 114 这个错号一直留在注释里（**断言只数个数，抓不到错号**）。
# 裸 `error` + Task 6 新产生式后实测回到 114，属巧合对上，不是原注释本来正确。
# 2026-09-29 又把新产生式去掉试过一次（编号退到 26/31/113），随后出于行为原因
# 又恢复了它（见 src/syntax.y 里"为什么保留产生式"）—— 两次都保持冲突数 3 不变。
#
# 这条断言不可省：实测有无 %left LB DOT 的 LR 动作表**完全相同**，
# 所有 .exp 比对都察觉不到它被误删，只有这个计数能发现。
# 哨兵的**语义不变**（冲突数一变就报警，不区分"变好"还是"变坏"），
# 只是期望值随 `Def : error` 的采纳由 1 改为 3。任何变化 —— 多一个 state、
# 某个 state 变成 reduce/reduce、或悬空 else 消失 —— 都会红。
# ── `crlf.cmm` 必须真的含 CR ──────────────────────────────────
# `.gitattributes` 的 `-text` 只保护**入索引的那一刻**。若有人就地把它
# 重写成 LF，索引 blob 回到 29 字节、全部用例照过、防线再次静默消失 ——
# 与当初 `eol=lf` 把它规范化掉是同一类失效，且同样没有信号。
# 属性本身也扛不住文件改名（`-text` 是字面路径而非 glob），故运行时再断言一次。
if [ -f Test/err/crlf.cmm ]; then
    if [ "$(tr -d '\r' < Test/err/crlf.cmm | wc -c)" = "$(wc -c < Test/err/crlf.cmm)" ]; then
        echo "FAIL  Test/err/crlf.cmm 不含 CR —— 该用例的判别力已失效"
        fail=$((fail + 1))
    fi
fi

# ── 至少跑了一个用例 ──────────────────────────────────────────
# 否则 Test/ 被移动或清空时会打印 "通过 0 个，失败 0 个" 并退出 0。
if [ "$pass" -eq 0 ]; then
    echo "FAIL  没有任何用例通过（0 个）—— 检查 Test/ 目录是否被移动或清空"
    fail=$((fail + 1))
fi

# ── Test/lab2 存在时至少跑了一个用例 ──────────────────────────
# 上面那条只管 `pass`。**实验二这半边需要单独一条**：`if [ -d Test/lab2 ]` 只保证
# 目录在，目录**空着**（或只有非 .cmm 文件）时循环仍会迭代那个字面 glob
# `Test/lab2/*.cmm`、每次都在 `[ -e "$cmm" ] || continue` 处跳出 —— 一个用例都没跑，
# 却照样打印"通过 30 个，失败 0 个"并退出 0。实验一的 30 个用例照过，
# 掩盖了实验二哨兵**整体失效**这件事。故这里数的是"跑到几个"，不是"过了几个"。
if [ -d Test/lab2 ] && [ "$lab2_ran" -eq 0 ]; then
    echo "FAIL  存在 Test/lab2 但一个用例都没跑成 —— 这半边哨兵已失效"
    fail=$((fail + 1))
fi

# ── 显式声明未纳入范围的 .cmm ─────────────────────────────────
# Test/unit/ 下的 .cmm 由 unit-test / lexer-test 各自负责。显式列出，
# 避免"漏掉某个目录"变成静默行为。
other=$(find Test -name '*.cmm' -not -path 'Test/sample/*' -not -path 'Test/err/*' \
        -not -path 'Test/lab2/*' 2>/dev/null)
if [ -n "$other" ]; then
    echo "NOTE  以下 .cmm 不在本脚本范围内（由各自的单测目标负责）："
    echo "$other" | sed 's/^/      /'
fi

if [ -f syntax.output ]; then
    # 用锚定模式而非子串匹配：`grep -c "conflicts:"` 对
    # "State N conflicts: 1 shift/reduce, 1 reduce/reduce" 也会放行。
    # 锚定同时消除了本断言唯一依赖 bison 版本的地方
    # （state 编号大小写用 [Ss] 兼容 3.0.4）。
    n=$(grep -cE '^[Ss]tate [0-9]+ conflicts:' syntax.output)
    if [ "$n" -ne 3 ]; then
        # ⚠ **消息里不写 state 编号**：这条哨兵只数个数、**不校验编号**，所以编号
        # 一旦漂移（改文法、换 bison 版本），`n -ne 3` 仍然可能不触发、而消息会把排查者
        # 指向一个**错的** state —— `113/114` 那个错号当年就是这么潜伏至今的。
        # 具体归属见本块上方注释（那里的编号由 Task 6 实测校正过），消息只做指路。
        echo "FAIL  期望恰好 3 个冲突 state（归属见本块上方注释：Def : error 引起的两处，加悬空 else），实测 $n 个"
        grep -nE '^[Ss]tate [0-9]+ conflicts:' syntax.output | sed 's/^/      /'
        fail=$((fail + 1))
    elif [ "$(grep -cE '^[Ss]tate [0-9]+ conflicts: 1 shift/reduce$' syntax.output)" -ne 3 ]; then
        echo "FAIL  3 个冲突 state 并非每个都是「1 shift/reduce」"
        grep -nE '^[Ss]tate [0-9]+ conflicts:' syntax.output | sed 's/^/      /'
        fail=$((fail + 1))
    fi
else
    echo "FAIL  未找到 syntax.output —— 冲突数断言无法执行"
    echo "      （它由 bison -v 生成。脚本开头已确认 ./parser 存在，而 test 目标依赖"
    echo "        parser，故该文件本应存在；缺失说明构建链被绕过）"
    fail=$((fail + 1))
fi

# ── 文档里的 `X()` 必须真实存在 ────────────────────────────────
# 抽成独立脚本（它不需要 ./parser，也不该与用例计数混在一起），由这里调用 ——
# 于是"改完文档顺手跑一下自检"就能发现悬空名字，不必记得另跑一条命令。
# 背景：交付的 PDF 里曾把决策 5、6 的开关写成 `useStructTag()` / `checkSubscript()`，
# 而这两个名字在 src/ 里**不存在**（真实位置见 docs/实验二报告.md 亮点 3）。
# 那类错**可以自动抓**，抓不到就会一路进 PDF。
if ! bash scripts/check_docs.sh; then
    fail=$((fail + 1))
fi

echo "----------------------------------------"
echo "通过 $pass 个，失败 $fail 个"
[ "$fail" -eq 0 ]
