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
trap 'rm -f "$tmp" "$err"' EXIT

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
    else
        fail=$((fail + 1))
        echo "FAIL  $cmm"
        diff -u "$exp" "$tmp" | sed 's/^/      /' | head -30
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
        echo "FAIL  期望恰好 3 个冲突 state（State 27、32 来自 Def : error，State 114 是悬空 else），实测 $n 个"
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

echo "----------------------------------------"
echo "通过 $pass 个，失败 $fail 个"
[ "$fail" -eq 0 ]
