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
trap 'rm -f "$tmp"' EXIT

for cmm in Test/sample/*.cmm Test/err/*.cmm; do
    [ -e "$cmm" ] || continue
    exp="${cmm%.cmm}.exp"
    if [ ! -f "$exp" ]; then
        echo "FAIL  $cmm  （缺同名 .exp）"
        fail=$((fail + 1))
        continue
    fi

    ./parser "$cmm" > "$tmp" 2>&1
    status=$?
    # parser 恒返回 0（要求书未规定退出码），非 0 一定是异常。
    # 段错误会留下空输出，不查状态码就会与"正确地什么都不打印"混淆。
    if [ "$status" -ne 0 ]; then
        echo "FAIL  $cmm  （parser 退出码 $status）"
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
# 唯一允许的冲突是悬空 else（设计文档 §6.2.1 / §6.2.2）。
# 这条断言不可省：实测有无 %left LB DOT 的 LR 动作表**完全相同**，
# 所有 .exp 比对都察觉不到它被误删，只有这个计数能发现。
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
    if [ "$n" -ne 1 ]; then
        echo "FAIL  期望恰好 1 个冲突 state（悬空 else），实测 $n 个"
        grep -nE '^[Ss]tate [0-9]+ conflicts:' syntax.output | sed 's/^/      /'
        fail=$((fail + 1))
    elif ! grep -qE '^[Ss]tate [0-9]+ conflicts: 1 shift/reduce$' syntax.output; then
        echo "FAIL  唯一的冲突 state 不是「1 shift/reduce」"
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
