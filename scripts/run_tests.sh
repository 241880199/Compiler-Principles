#!/usr/bin/env bash
# 遍历 Test/sample 与 Test/err 下全部 .cmm，与同名 .exp 逐字节比对。
# Test/unit/ 不在范围内（由 unit-test / lexer-test 目标各自负责）。
set -u
cd "$(dirname "$0")/.."

if [ ! -x ./parser ]; then
    echo "错误：未找到 ./parser，请先运行 make" >&2
    exit 1
fi

pass=0
fail=0
tmp=$(mktemp)
err=$(mktemp)
trap 'rm -f "$tmp" "$err"' EXIT

for cmm in Test/sample/*.cmm Test/err/*.cmm; do
    [ -e "$cmm" ] || continue
    exp="${cmm%.cmm}.exp"
    if [ ! -f "$exp" ]; then
        echo "FAIL  $cmm  （缺同名 .exp）"
        fail=$((fail + 1))
        continue
    fi

    # stderr **单独**捕获，不并进比对的 stdout。
    # 若写成 `> "$tmp" 2>&1`，把 reportError 改成 fprintf(stderr, ...) 仍然全绿 ——
    # 而要求书 2.1.3 要求错误提示信息输出到**标准输出**，判分脚本读的是 stdout。
    # 合并流会让这条规范悄悄失效，故必须分开断言。
    ./parser "$cmm" > "$tmp" 2> "$err"
    status=$?
    # parser 恒返回 0（要求书未规定退出码），非 0 一定是异常。
    # 段错误会留下空输出，不查状态码就会与"正确地什么都不打印"混淆。
    if [ "$status" -ne 0 ]; then
        echo "FAIL  $cmm  （parser 退出码 $status）"
        fail=$((fail + 1))
        continue
    fi

    # 非空即判 FAIL：标准错误上不该有任何输出。
    if [ -s "$err" ]; then
        echo "FAIL  $cmm  （stderr 非空 —— 要求书 2.1.3 要求输出到标准输出）"
        sed 's/^/      /' "$err" | head -10
        fail=$((fail + 1))
        continue
    fi

    if diff -q "$exp" "$tmp" > /dev/null; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        echo "FAIL  $cmm"
        diff -u "$exp" "$tmp" | sed 's/^/      /' | head -30
    fi
done

# ── 冲突数断言 ────────────────────────────────────────────────
# **期望恰好 3 个冲突 state，且每个都是「1 shift/reduce」。** 归属（实测，粘自
# syntax.output，bison 3.8.2 与 3.0.4 两版一致）：
#
#   ① State 26  `CompSt: LC • DefList StmtList RC`
#        error  shift, and go to state 28
#        error  [reduce using rule 24 (DefList)]      ← 冲突
#   ② State 31  `DefList: Def • DefList`
#        error  shift, and go to state 28
#        error  [reduce using rule 24 (DefList)]      ← 冲突
#   ③ State 114 `Stmt: IF LP Exp RP Stmt •` / `| IF LP Exp RP Stmt • ELSE Stmt`
#        ← 悬空 else，Bison 默认移进即 C 语义
#
# ①② 都是 `Def : error`（错误恢复同步点之四，见 src/syntax.y）引起的同一件事：
# **`DefList` 可空**，于是"看 `error` 时移进"与"按 `DefList → ε` 归约"不可兼得。
# 它们只出现在 `CompSt` 一路（`error` 能跟在 `DefList` 之后，因为 `StmtList` 可
# 以 `error` 开头）；`StructSpecifier : STRUCT OptTag LC • DefList RC`（State 20）
# **不冲突** —— 结构体成员表后面只能跟 `RC`，`error` 根本不进它的前瞻集，
# 那里 `error` 是个无歧义的移进动作。故"两处"指的是 State 26/31，**不是**
# `StructSpecifier`。带同步符的 `Def : error SEMI` 引入的是**同样这 2 个**冲突
# （冲突由 `error` 符号本身引起，与后面跟不跟同步符无关），所以"不带同步符"是
# 纯收益，未额外付冲突代价。
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

# ── 显式声明未纳入范围的 .cmm ─────────────────────────────────
# Test/unit/ 下的 .cmm 由 unit-test / lexer-test 各自负责。显式列出，
# 避免"漏掉某个目录"变成静默行为。
other=$(find Test -name '*.cmm' -not -path 'Test/sample/*' -not -path 'Test/err/*' 2>/dev/null)
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
        echo "FAIL  期望恰好 3 个冲突 state（悬空 else 1 + Def : error 2），实测 $n 个"
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
