#!/usr/bin/env bash
# 遍历 Test/ 下全部 .cmm，与同名 .exp 逐字节比对
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
        echo "SKIP  $cmm  （无同名 .exp）"
        continue
    fi
    ./parser "$cmm" > "$tmp" 2>&1
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
if [ -f syntax.output ]; then
    n=$(grep -c "conflicts:" syntax.output)
    if [ "$n" -ne 1 ]; then
        echo "FAIL  期望恰好 1 个冲突 state（悬空 else），实测 $n 个"
        grep -n "conflicts:" syntax.output | sed 's/^/      /'
        fail=$((fail + 1))
    elif ! grep -q "conflicts: 1 shift/reduce" syntax.output; then
        echo "FAIL  唯一的冲突 state 不是「1 shift/reduce」"
        grep -n "conflicts:" syntax.output | sed 's/^/      /'
        fail=$((fail + 1))
    fi
else
    echo "SKIP  冲突数断言（未找到 syntax.output；需先 make，它由 bison -v 生成）"
fi

echo "----------------------------------------"
echo "通过 $pass 个，失败 $fail 个"
[ "$fail" -eq 0 ]
