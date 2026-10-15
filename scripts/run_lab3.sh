#!/usr/bin/env bash
# 实验三自检运行台：对每个用例跑 parser -> 用无头参考机执行 -> 与 .exp 比对。
#
#   bash scripts/run_lab3.sh [<用例目录或单个 .cmm>]     # 默认 Test/lab3
#
# 本阶段（Task 12 之前）parser 还不会"把中间代码写到第二个参数指定的文件"，
# 故本运行台**必须把两类失败分开报**，否则满屏同一种 mismatch，分不清是环境没齐
# 还是判据本身不对：
#   (a) "parser 未产出 IR"          —— 第二个参数指定的文件缺失或为空（本阶段属预期）
#   (b) "IR 产出但结果与 .exp 不符" —— 真正需要看的 mismatch
# 纪律（实验一/二栽过）：
#   * 缺 .exp 必须 FAIL，**不得 SKIP**；
#   * 一个用例都没跑（pass=0）必须退出 1（实验一"通过 0 个仍退 0"）；
#   * 临时件只落仓内 build/（已 gitignore），不落 %TEMP%、不落 MSYS /tmp。
#
# 本脚本要用仓库构建出的 ./parser（Linux ELF），**必须在 WSL 里跑**（本机 Windows 无 gcc/ELF 环境）：
#   wsl.exe -d Ubuntu-22.04 -- bash -lc 'cd <repo> && bash scripts/run_lab3.sh Test/lab3/basic'
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
TESTS="${1:-$REPO/Test/lab3}"

PY="$(command -v python3 || command -v python || true)"
if [ -z "$PY" ]; then echo "找不到 python3 / python" >&2; exit 2; fi

# parser 路径可被 LAB3_PARSER 覆盖 —— 仅供本运行台的判别力自证使用：
# 用一个会写出固定 IR 的桩 parser，就能在 Task 12（真 parser 还不会写第二份文件）
# 之前就验证"(b) IR 结果与 .exp 不符"这条分支真的会红。
PARSER="${LAB3_PARSER:-$REPO/parser}"
if [ ! -x "$PARSER" ]; then
    echo "找不到可执行的 parser：$PARSER（先在仓库根 make，或用 LAB3_PARSER 指定）" >&2
    exit 2
fi

TMP="$REPO/build/lab3-run"
mkdir -p "$TMP"

# 收集用例（支持传目录或单个文件；用 mapfile 以免文件名含空格被 word-split）
if [ -d "$TESTS" ]; then
    mapfile -t cmms < <(find "$TESTS" -name '*.cmm' | sort)
elif [ -f "$TESTS" ]; then
    cmms=("$TESTS")
else
    echo "找不到用例路径：$TESTS" >&2
    exit 2
fi

if [ "${#cmms[@]}" -eq 0 ]; then
    echo "在 $TESTS 下没找到任何 .cmm 用例"
    echo "----"
    echo "pass=0 fail=0"
    echo "无任何用例通过，判为红"
    exit 1
fi

pass=0
fail=0
failed=()

for cmm in "${cmms[@]}"; do
    base="${cmm%.cmm}"
    exp="$base.exp"
    in="$base.in"
    rel="${base#"$REPO"/}"
    tag="$(printf '%s' "$rel" | tr '/' '_')"
    ir="$TMP/$tag.ir"
    got="$TMP/$tag.got"

    # 缺 .exp：FAIL，不得 SKIP
    if [ ! -f "$exp" ]; then
        echo "FAIL $rel: 缺 .exp（缺期望值必须 FAIL，不得 SKIP）"
        fail=$((fail + 1)); failed+=("$rel"); continue
    fi

    rm -f "$ir"
    "$PARSER" "$cmm" "$ir" >"$TMP/parser.out" 2>"$TMP/parser.err"
    rc=$?

    # (0) parser 自己失败
    if [ $rc -ne 0 ]; then
        echo "FAIL $rel: parser 退出码 $rc"
        sed 's/^/       /' "$TMP/parser.err" | head -5
        fail=$((fail + 1)); failed+=("$rel"); continue
    fi

    # (a) parser 没产出 IR —— 与 (b) 分开报
    if [ ! -s "$ir" ]; then
        echo "FAIL $rel: parser 未产出 IR（第二个参数指定的文件缺失或为空）—— 本阶段属预期，等 Task 12"
        fail=$((fail + 1)); failed+=("$rel"); continue
    fi

    # 参考机执行（第一行是 instrCnt，其后才是 WRITE 值）
    if ! "$PY" -I "$HERE/irsim_ref.py" --run "$ir" --in "$in" >"$got" 2>"$TMP/sim.err"; then
        echo "FAIL $rel: 参考机执行失败（见下）"
        sed 's/^/       /' "$TMP/sim.err" | head -5
        fail=$((fail + 1)); failed+=("$rel"); continue
    fi
    instr_cnt="$(head -1 "$got")"
    tail -n +2 "$got" >"$got.vals"; mv "$got.vals" "$got"

    # (b) 结果对不对
    if diff -u "$exp" "$got" >/dev/null; then
        pass=$((pass + 1))
        echo "ok   $rel  (instrCnt=$instr_cnt)"
    else
        echo "FAIL $rel: IR 结果与 .exp 不符"
        diff -u "$exp" "$got" | head -20 | sed 's/^/       /'
        fail=$((fail + 1)); failed+=("$rel")
    fi
done

echo "----"
echo "pass=$pass fail=$fail"
if [ "$pass" -eq 0 ]; then
    echo "无任何用例通过，判为红"
    exit 1
fi
if [ "$fail" -ne 0 ]; then
    printf 'FAILED: %s\n' "${failed[@]}"
    exit 1
fi
exit 0
