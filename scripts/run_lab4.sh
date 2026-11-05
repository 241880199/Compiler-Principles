#!/usr/bin/env bash
# 实验四自检运行台：把 .cmm 编译成 MIPS32 汇编，交给 SPIM 执行，与 .exp 逐字节比对。
#
#   用法： bash scripts/run_lab4.sh [<用例目录或单个 .cmm>]
#          无参数 = Test/lab3 的五个真实用例组 + Test/lab4
#
# 必须在 WSL 内跑（仓库构建出的 ./cc 是 Linux ELF，SPIM 也装在 WSL 里）：
#   wsl.exe -d Ubuntu-22.04 -- bash -lc 'cd <repo> && bash scripts/run_lab4.sh'
#
# 三条纪律（与实验三运行台一致）：
#   * 缺 .exp 必须 FAIL，不得 SKIP；
#   * 一个用例都没跑（pass=0）必须退出 1；
#   * 临时件只落仓内 build/lab4/（已 gitignore）。
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
cd "$REPO"

OUT=build/lab4
mkdir -p "$OUT"

if [ ! -x ./cc ]; then
    echo "错误：找不到可执行的 ./cc（先在仓库根 make）" >&2
    exit 1
fi
if ! command -v spim >/dev/null 2>&1; then
    echo "错误：找不到 spim（Ubuntu: sudo apt-get install -y spim）" >&2
    exit 1
fi

# 收集用例：显式参数优先；否则跑实验三五个真实用例组 + 实验四用例。
collect() {
    if [ "$#" -gt 0 ]; then
        for x in "$@"; do
            if [ -d "$x" ]; then
                find "$x" -name '*.cmm' | sort
            elif [ -f "$x" ]; then
                printf '%s\n' "$x"
            else
                echo "错误：找不到用例 $x" >&2
                exit 1
            fi
        done
    else
        for d in Test/lab3/basic Test/lab3/arr1d Test/lab3/arrnd \
                 Test/lab3/func Test/lab3/req Test/lab4; do
            [ -d "$d" ] && find "$d" -name '*.cmm' | sort
        done
    fi
}

# SPIM 8 的 -quiet 仍把版本横幅打到 stdout（5 行，最后一行以 "Loaded:" 开头）。
# 只剥掉**开头**的已知横幅行；程序自己输出同样文字不会被误删。
strip_banner() {
    awk '
        !started {
            if ($0 ~ /^Loaded: /) { started = 1; next }
            if ($0 ~ /^SPIM Version / || $0 ~ /^Copyright / ||
                $0 ~ /^All Rights Reserved\.$/ || $0 ~ /^See the file README/) next
            started = 1
        }
        { print }
    '
}

pass=0
fail=0

while IFS= read -r cmm; do
    [ -n "$cmm" ] || continue
    base=${cmm%.cmm}
    in="$base.in"
    exp="$base.exp"
    tag=$(printf '%s' "${base#Test/}" | tr '/' '_')
    s="$OUT/$tag.s"
    raw="$OUT/$tag.raw"
    got="$OUT/$tag.out"

    if [ ! -f "$exp" ]; then
        echo "FAIL  $cmm  （缺同名 .exp）"
        fail=$((fail + 1))
        continue
    fi
    [ -f "$in" ] || in=/dev/null

    if ! ./cc "$cmm" "$s" >"$OUT/cc.stdout" 2>"$OUT/cc.stderr"; then
        echo "FAIL  $cmm  （cc 退出码非 0）"
        sed 's/^/      /' "$OUT/cc.stderr" | head -5
        fail=$((fail + 1))
        continue
    fi
    if [ -s "$OUT/cc.stderr" ]; then
        echo "FAIL  $cmm  （cc 的 stderr 非空）"
        sed 's/^/      /' "$OUT/cc.stderr" | head -5
        fail=$((fail + 1))
        continue
    fi

    # 实验三的 .in 用逗号分隔整数；SPIM 的 read_int 一次读一行，故先换行。
    tr ',' '\n' < "$in" > "$OUT/$tag.in"
    spim -quiet -file "$s" < "$OUT/$tag.in" > "$raw" 2>"$OUT/$tag.spim.err"
    rc=$?
    strip_banner < "$raw" > "$got"

    if [ "$rc" -ne 0 ]; then
        echo "FAIL  $cmm  （SPIM 退出码 $rc）"
        sed 's/^/      /' "$OUT/$tag.spim.err" | head -5
        fail=$((fail + 1))
        continue
    fi
    if diff -q "$exp" "$got" >/dev/null; then
        pass=$((pass + 1))
    else
        echo "FAIL  $cmm  （输出与 .exp 不符）"
        diff -u "$exp" "$got" | sed 's/^/      /' | head -12
        fail=$((fail + 1))
    fi
done < <(collect "$@")

echo "lab4: 通过 $pass 个，失败 $fail 个"
if [ "$pass" -eq 0 ]; then
    echo "错误：一个用例都没跑 —— 判据失效" >&2
    exit 1
fi
[ "$fail" -eq 0 ]
