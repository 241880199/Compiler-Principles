#!/usr/bin/env bash
# 交付包复核：把 zip 解到临时目录，从零构建，再拿**解包产物**与**仓库构建**逐字节对拍。
#
#   用法： bash scripts/verify_package.sh <zip>
#
# 为什么是"对拍"而不是"再比对一遍 .exp"：
#   仓库自检 scripts/run_tests.sh 已经把仓库的 ./parser 钉在全部 .exp 上（75 个全过）。
#   本脚本只要证明**包里的构建与仓库的构建逐字节同行为**，结论就传递过去了 ——
#   于是这里不存在第二份期望值，也就不会出现"两份期望各自漂移"的问题。
#
# 本脚本的六条断言，每条都对应一种曾经真实发生过的失效：
#   ① 条目清单 —— 交付包里混进过无关的生成物（实验一：Code/lexer.c，57KB 的 flex 产物，
#      Makefile 从未引用）。不是"多一个文件"，而是"包里有与构建无关的东西"。
#   ② 包内 report.pdf 与仓库那份逐字节相同 —— 报告是现场排版的，很容易"改了 .tex、
#      忘了重编、把旧 PDF 打进包"。写上名字却没有产物与它相连，正是本项目反复出现的一类。
#   ③ 干净目录 make 通过 + **零 gcc 警告** —— 仓库自检跑的构建用的是仓库 Makefile；
#      包里的 Makefile 是派生的，仓库自检**看不到**它。这一条是它唯一的把关者。
#   ④ 两个运行入口 ./parser 与 ./cc 都在且可执行。
#   ⑤ 冲突数恰好 3、且每个都是 1 shift/reduce（锚定匹配，非子串）—— 文法退化的唯一哨兵。
#      子串匹配会放过 "1 shift/reduce, 1 reduce/reduce"。
#   ⑥ 行为对拍覆盖仓库的全部用例目录，且数目不为 0（语料被清空时"0 个不一致"也报成功）。
set -euo pipefail

cd "$(dirname "$0")/.."
REPO=$PWD
ZIP=$(realpath -m "${1:?用法: bash scripts/verify_package.sh <zip>}")

if [ ! -f "$ZIP" ]; then
    echo "错误：找不到 $ZIP" >&2
    exit 1
fi

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

echo "── 交付包复核：$(basename "$ZIP") ──"

# ── ① 条目清单：包内文件必须恰好是「预期的那一份」────────────────
# 预期 = report.pdf + Code/Makefile + HEAD 里 src/ 下的全部跟踪文件（平铺）。
{
    echo "report.pdf"
    echo "Code/Makefile"
    git ls-tree -r --name-only HEAD src | sed 's|^src/|Code/|'
} | sort > "$WORK/expected"
unzip -Z1 "$ZIP" | grep -v '/$' | sort > "$WORK/actual"

if ! diff -q "$WORK/expected" "$WORK/actual" > /dev/null; then
    echo "FAIL  包内条目与预期不符（左=预期，右=包内）："
    diff -u "$WORK/expected" "$WORK/actual" | sed 's/^/      /'
    exit 1
fi
echo "  ① 条目：$(wc -l < "$WORK/actual") 个，与预期一致"

# ── ② 包内 report.pdf 必须是仓库当前那份 ───────────────────────
REPORT_PDF=docs/实验二报告.pdf
if [ ! -s "$REPORT_PDF" ]; then
    echo "FAIL  找不到 $REPORT_PDF —— 无法核对包内报告" >&2
    echo "      报告需现场排版：xelatex 编**两遍**（只编一遍页数不作数）。" >&2
    exit 1
fi
if ! cmp -s <(unzip -p "$ZIP" report.pdf) "$REPORT_PDF"; then
    echo "FAIL  包内 report.pdf 与 $REPORT_PDF 不一致" >&2
    echo "      多半是改了 .tex 却没重编，或包是上一版打的 —— 重新打包即可。" >&2
    exit 1
fi
echo "  ② report.pdf 与 $REPORT_PDF 逐字节一致（$(pdfinfo "$REPORT_PDF" | awk '/^Pages/{print $2}') 页）"

# ── ③ 干净目录从零构建，且零警告 ───────────────────────────────
# `make` 会顺带跑 bison -v，于是 ⑤ 要的 syntax.output 也在这一步产生。
unzip -q "$ZIP" -d "$WORK/unpacked"
cd "$WORK/unpacked/Code"

if ! make clean > /dev/null 2>&1; then
    echo "FAIL  make clean 失败（包内 Makefile 的 clean 规则有问题）" >&2
    exit 1
fi
if ! make > "$WORK/build.log" 2>&1; then
    echo "FAIL  干净目录下 make 失败："
    sed 's/^/      /' "$WORK/build.log" | tail -30
    exit 1
fi
# 只数**编译器**警告。判据必须锚在 gcc 的诊断格式 `file.c:行:列: warning: ...` 上：
# bison 的冲突提示形如 `syntax.y: warning: 3 shift/reduce conflicts`，那是**预期之内**
# 的（README 写明 3 处冲突属预期），由 ⑤ 单独断言。用裸的 `warning:` 计数会把它算进来，
# 于是这条断言在**正常构建**上误报 —— 判据与它声称检验的东西不一致。
warn=$(grep -cE '^[^:]+\.(c|h):[0-9]+:[0-9]+: warning:' "$WORK/build.log" || true)
if [ "$warn" -ne 0 ]; then
    echo "FAIL  gcc 警告 $warn 条（要求书与 README 都要求零警告）："
    grep -nE '^[^:]+\.(c|h):[0-9]+:[0-9]+: warning:' "$WORK/build.log" | sed 's/^/      /'
    exit 1
fi
echo "  ③ 从零 make：通过，零 gcc 警告"

# ── ④ 两个运行入口都在 ─────────────────────────────────────────
for bin in parser cc; do
    if [ ! -x "$bin" ]; then
        echo "FAIL  解包产物缺可执行文件 ./$bin" >&2
        exit 1
    fi
done
echo "  ④ ./parser 与 ./cc 均在且可执行"

# ── ⑤ 冲突数：恰好 3 个 state，且每个都是「1 shift/reduce」──────
# 与 scripts/run_tests.sh 的断言同形（锚定，非子串；[Ss] 兼容 bison 3.0.4）。
if [ ! -f syntax.output ]; then
    echo "FAIL  解包产物未生成 syntax.output —— 冲突数断言无法执行" >&2
    exit 1
fi
n=$(grep -cE '^[Ss]tate [0-9]+ conflicts:' syntax.output || true)
if [ "$n" -ne 3 ]; then
    echo "FAIL  期望恰好 3 个冲突 state，实测 $n 个："
    grep -nE '^[Ss]tate [0-9]+ conflicts:' syntax.output | sed 's/^/      /'
    exit 1
fi
one=$(grep -cE '^[Ss]tate [0-9]+ conflicts: 1 shift/reduce$' syntax.output || true)
if [ "$one" -ne 3 ]; then
    echo "FAIL  3 个冲突 state 并非每个都是「1 shift/reduce」："
    grep -nE '^[Ss]tate [0-9]+ conflicts:' syntax.output | sed 's/^/      /'
    exit 1
fi
echo "  ⑤ 冲突数：3 个 state，均为 1 shift/reduce"

# ── ⑥ 与仓库构建逐字节对拍 ─────────────────────────────────────
cd "$REPO"
if ! make parser > /dev/null 2>&1; then
    echo "FAIL  仓库自身构建失败，无法对拍（请先修好仓库）" >&2
    exit 1
fi

pass=0
fail=0
lab2_ran=0
set +e
for cmm in Test/sample/*.cmm Test/err/*.cmm Test/lab2/*.cmm; do
    [ -e "$cmm" ] || continue
    case "$cmm" in Test/lab2/*) lab2_ran=$((lab2_ran + 1));; esac
    abs=$REPO/$cmm

    "$REPO/parser" "$abs" > "$WORK/r.out" 2> "$WORK/r.err"; r_status=$?
    "$WORK/unpacked/Code/parser" "$abs" > "$WORK/p.out" 2> "$WORK/p.err"; p_status=$?

    if [ "$r_status" -ne "$p_status" ]; then
        echo "FAIL  $cmm  （退出码不一致：仓库 $r_status vs 包 $p_status）"
        fail=$((fail + 1))
    elif ! diff -q "$WORK/r.out" "$WORK/p.out" > /dev/null; then
        echo "FAIL  $cmm  （stdout 不一致）"
        diff -u "$WORK/r.out" "$WORK/p.out" | sed 's/^/      /' | head -20
        fail=$((fail + 1))
    elif ! diff -q "$WORK/r.err" "$WORK/p.err" > /dev/null; then
        echo "FAIL  $cmm  （stderr 不一致）"
        diff -u "$WORK/r.err" "$WORK/p.err" | sed 's/^/      /' | head -20
        fail=$((fail + 1))
    else
        pass=$((pass + 1))
    fi
done
set -e

if [ "$pass" -eq 0 ]; then
    echo "FAIL  一个用例都没跑到 —— 检查 Test/ 是否被移动或清空"
    exit 1
fi
if [ "$lab2_ran" -eq 0 ]; then
    echo "FAIL  Test/lab2 一个用例都没跑到 —— 实验二这半边对拍已失效"
    exit 1
fi

other=$(find Test -name '*.cmm' -not -path 'Test/sample/*' -not -path 'Test/err/*' \
        -not -path 'Test/lab2/*' 2>/dev/null)
if [ -n "$other" ]; then
    echo "  NOTE  以下 .cmm 不在对拍范围内（由各自的单测目标负责）："
    echo "$other" | sed 's/^/        /'
fi

echo "  ⑥ 行为对拍：$pass 个用例与仓库构建逐字节一致，不一致 $fail 个"
echo "──────────────────────────────────────────"
if [ "$fail" -ne 0 ]; then
    echo "交付包复核失败：$fail 个用例与仓库构建不一致" >&2
    exit 1
fi
echo "交付包复核通过：$(basename "$ZIP") 可从零构建，且与仓库构建行为一致。"
