#!/usr/bin/env bash
# 实验三自检运行台：对每个用例跑 parser -> 用无头参考机执行 -> 与 .exp 比对。
#
#   bash scripts/run_lab3.sh [<用例目录或单个 .cmm>]     # 默认 Test/lab3
#
# parser 必须把中间代码写到第二个参数指定的文件；本运行台**把两类失败分开报**，
# 否则满屏同一种 mismatch，分不清是环境没齐还是判据本身不对：
#   (a) "parser 未产出 IR"          —— 第二个参数指定的文件缺失或为空（驱动没接上）
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

# ── --report：优化前后指令数对照（Task 20）──────────────────────────────
# 两个**独立二进制**各跑一遍：
#   ./parser            优化开（默认 LAB3_32=1 构建）
#   build/parser-noopt  优化关（-DLAB3_OPT=0，Task 18 的 Makefile 目标）
# 对每个用例，两份 WRITE 输出都必须与 .exp 逐字节相同（cmp，不是 diff），
# 才允许计一行；任一失败整体退出 1，**不打印**一张看起来完整的表。
# 临时件全部落 build/lab3-report/（已 gitignore），不落 /tmp。
report_mode() {
    local dir="${1:-}"
    local out="$REPO/build/lab3-report"
    mkdir -p "$out"
    local mklog="$out/make.log"

    # 不用 make clean（会清掉其他产物）；两个独立二进制已经消除了“切换
    # LAB3_OPT 却复用旧 .o”的静默失效。
    if ! make -C "$REPO" parser build/parser-noopt >"$mklog" 2>&1; then
        echo "FAIL: 构建 parser / build/parser-noopt 失败（日志：$mklog）"
        tail -20 "$mklog" | sed 's/^/       /'
        return 1
    fi
    local on="$REPO/parser"
    local off="$REPO/build/parser-noopt"
    if [ ! -x "$on" ] || [ ! -x "$off" ]; then
        echo "FAIL: 缺少可执行文件：$on 或 $off"
        return 1
    fi

    # 无目录 = 五个**真实用例组**；**不扫** Test/lab3/selftest（负例夹具，必红）。
    local cmms=()
    if [ -z "$dir" ]; then
        local g d f
        for g in basic arr1d arrnd func req; do
            d="$REPO/Test/lab3/$g"
            if [ ! -d "$d" ]; then
                echo "FAIL: 找不到真实用例组目录 $d"
                return 1
            fi
            while IFS= read -r f; do
                cmms+=("$f")
            done < <(find "$d" -name '*.cmm' | sort)
        done
    elif [ -d "$dir" ]; then
        while IFS= read -r f; do
            cmms+=("$f")
        done < <(find "$dir" -name '*.cmm' | sort)
    elif [ -f "$dir" ]; then
        cmms=("$dir")
    else
        echo "FAIL: 找不到用例路径：$dir"
        return 1
    fi
    if [ "${#cmms[@]}" -eq 0 ]; then
        echo "FAIL: 没有找到任何 .cmm 用例"
        return 1
    fi

    local names=() befores=() afters=() drops=()
    local total_before=0 total_after=0
    local cmm base exp in rel tag on_ir off_ir on_got off_got rc cnt
    local before after drop

    for cmm in "${cmms[@]}"; do
        base="${cmm%.cmm}"
        exp="$base.exp"
        in="$base.in"
        rel="${base#"$REPO"/}"
        tag="$(printf '%s' "$rel" | tr '/' '_')"
        on_ir="$out/$tag.on.ir"
        off_ir="$out/$tag.off.ir"
        on_got="$out/$tag.on.got"
        off_got="$out/$tag.off.got"

        if [ ! -f "$exp" ]; then
            echo "FAIL $rel: 缺 .exp（缺期望值必须 FAIL，不得出表）"
            return 1
        fi
        if [ ! -f "$in" ]; then
            echo "FAIL $rel: 缺 .in（缺输入必须 FAIL，不得出表）"
            return 1
        fi

        rm -f "$on_ir" "$off_ir" "$on_got" "$off_got"

        # (1) 优化开 parser -> IR
        "$on" "$cmm" "$on_ir" >"$out/$tag.on.parser.out" 2>"$out/$tag.on.parser.err"
        rc=$?
        if [ "$rc" -ne 0 ]; then
            echo "FAIL $rel: 优化开 parser 退出码 $rc"
            head -5 "$out/$tag.on.parser.err" | sed 's/^/       /'
            return 1
        fi
        if [ ! -s "$on_ir" ]; then
            echo "FAIL $rel: 优化开 parser 未产出 IR"
            return 1
        fi

        # (2) 优化关 parser -> IR
        "$off" "$cmm" "$off_ir" >"$out/$tag.off.parser.out" 2>"$out/$tag.off.parser.err"
        rc=$?
        if [ "$rc" -ne 0 ]; then
            echo "FAIL $rel: 优化关 parser 退出码 $rc"
            head -5 "$out/$tag.off.parser.err" | sed 's/^/       /'
            return 1
        fi
        if [ ! -s "$off_ir" ]; then
            echo "FAIL $rel: 优化关 parser 未产出 IR"
            return 1
        fi

        # (3) 优化开：参考机执行，WRITE 输出与 .exp 逐字节比对
        if ! "$PY" -I "$HERE/irsim_ref.py" --run "$on_ir" --in "$in" \
                >"$on_got" 2>"$out/$tag.on.sim.err"; then
            echo "FAIL $rel: 优化开 IR 参考机执行失败（日志：$out/$tag.on.sim.err）"
            head -5 "$out/$tag.on.sim.err" | sed 's/^/       /'
            return 1
        fi
        tail -n +2 "$on_got" >"$out/$tag.on.vals"
        if ! cmp -s "$exp" "$out/$tag.on.vals"; then
            echo "FAIL $rel: 优化开 WRITE 输出与 .exp 不符"
            diff -u "$exp" "$out/$tag.on.vals" | head -20 | sed 's/^/       /'
            return 1
        fi
        after="$(head -n 1 "$on_got")"
        if ! printf '%s' "$after" | grep -qE '^[0-9]+$'; then
            echo "FAIL $rel: 优化开 instrCnt 非整数：$after"
            return 1
        fi

        # (4) 优化关：同一套门槛
        if ! "$PY" -I "$HERE/irsim_ref.py" --run "$off_ir" --in "$in" \
                >"$off_got" 2>"$out/$tag.off.sim.err"; then
            echo "FAIL $rel: 优化关 IR 参考机执行失败（日志：$out/$tag.off.sim.err）"
            head -5 "$out/$tag.off.sim.err" | sed 's/^/       /'
            return 1
        fi
        tail -n +2 "$off_got" >"$out/$tag.off.vals"
        if ! cmp -s "$exp" "$out/$tag.off.vals"; then
            echo "FAIL $rel: 优化关 WRITE 输出与 .exp 不符"
            diff -u "$exp" "$out/$tag.off.vals" | head -20 | sed 's/^/       /'
            return 1
        fi
        before="$(head -n 1 "$off_got")"
        if ! printf '%s' "$before" | grep -qE '^[0-9]+$'; then
            echo "FAIL $rel: 优化关 instrCnt 非整数：$before"
            return 1
        fi

        drop="$(awk -v b="$before" -v a="$after" \
                'BEGIN { if (b == 0) printf "0.0"; else printf "%.1f", (b-a)/b*100 }')"
        names+=("$rel")
        befores+=("$before")
        afters+=("$after")
        drops+=("$drop")
        total_before=$((total_before + before))
        total_after=$((total_after + after))
    done

    # 全部用例都过了双门槛才出表 —— 否则上面已经 FAIL 退出。
    # 表头按**显示宽度**手工对齐（printf 的宽度按字节算，CJK 会短一截）。
    printf '%s%39s%s   %s    %s\n' '用例' '' '优化前' '优化后' '降幅'
    local i
    for i in "${!names[@]}"; do
        printf '%-40s %8d %8d %7s%%\n' \
            "${names[$i]}" "${befores[$i]}" "${afters[$i]}" "${drops[$i]}"
    done
    local total_drop
    total_drop="$(awk -v b="$total_before" -v a="$total_after" \
                  'BEGIN { if (b == 0) printf "0.0"; else printf "%.1f", (b-a)/b*100 }')"
    printf '%-40s %8d %8d %7s%%\n' '合计' "$total_before" "$total_after" "$total_drop"

    # 负降幅照实留在表里，并在 stderr 点名，提醒报告里调查（不隐藏）。
    for i in "${!names[@]}"; do
        if [ "${afters[$i]}" -gt "${befores[$i]}" ]; then
            echo "WARN ${names[$i]}: 优化后 ${afters[$i]} > 优化前 ${befores[$i]}（负降幅，需调查）" >&2
        fi
    done

    # ── 要求书 4.1.6 样例 1：优化后动态条数 vs 官方 sample_opt1.ir ──────
    # 仅默认模式（五个真实组）输出，定长目录模式只出该目录的表。
    if [ -z "$dir" ]; then
        local s1="$REPO/Test/lab3/req/sample1.cmm"
        local s1_ir="$out/sample1.on.ir"
        "$on" "$s1" "$s1_ir" >"$out/sample1.on.parser.out" 2>"$out/sample1.on.parser.err"
        rc=$?
        if [ "$rc" -ne 0 ] || [ ! -s "$s1_ir" ]; then
            echo "FAIL: sample1 优化开 parser 失败（退出码 $rc）"
            head -5 "$out/sample1.on.parser.err" | sed 's/^/       /'
            return 1
        fi
        echo ""
        echo "样例 1（Test/lab3/req/sample1.cmm 优化后 vs Test/lab3/unit_exec/sample_opt1.ir）"
        # 同样按显示宽度对齐。
        printf '%s%6s %2s%s %4s%s\n' '输入' '' '' '我们的优化后' '' '官方优化版'
        local x safe o_got f_got o_cnt f_cnt worse=0
        for x in -7 0 9; do
            safe="$(printf '%s' "$x" | tr '-' 'm')"
            printf '%s\n' "$x" >"$out/sample1_$safe.in"
            o_got="$out/sample1_$safe.on.got"
            f_got="$out/sample1_$safe.off.got"
            if ! "$PY" -I "$HERE/irsim_ref.py" --run "$s1_ir" \
                    --in "$out/sample1_$safe.in" >"$o_got" 2>"$out/sample1_$safe.on.err"; then
                echo "FAIL: sample1 优化后 IR 在输入 $x 下执行失败"
                head -5 "$out/sample1_$safe.on.err" | sed 's/^/       /'
                return 1
            fi
            if ! "$PY" -I "$HERE/irsim_ref.py" \
                    --run "$REPO/Test/lab3/unit_exec/sample_opt1.ir" \
                    --in "$out/sample1_$safe.in" >"$f_got" 2>"$out/sample1_$safe.off.err"; then
                echo "FAIL: 官方 sample_opt1.ir 在输入 $x 下执行失败"
                head -5 "$out/sample1_$safe.off.err" | sed 's/^/       /'
                return 1
            fi
            tail -n +2 "$o_got" >"$out/sample1_$safe.on.vals"
            tail -n +2 "$f_got" >"$out/sample1_$safe.off.vals"
            if ! cmp -s "$out/sample1_$safe.on.vals" "$out/sample1_$safe.off.vals"; then
                echo "FAIL: sample1 输入 $x 时我们的 WRITE 输出与官方优化版不一致"
                diff -u "$out/sample1_$safe.off.vals" "$out/sample1_$safe.on.vals" \
                    | head -10 | sed 's/^/       /'
                return 1
            fi
            if [ "$x" = "-7" ] && ! cmp -s "$REPO/Test/lab3/req/sample1.exp" \
                    "$out/sample1_$safe.on.vals"; then
                echo "FAIL: sample1 输入 -7 时我们的 WRITE 输出与 .exp 不符"
                return 1
            fi
            o_cnt="$(head -n 1 "$o_got")"
            f_cnt="$(head -n 1 "$f_got")"
            printf '%-10s %14d %14d\n' "$x" "$o_cnt" "$f_cnt"
            if [ "$o_cnt" -gt "$f_cnt" ]; then worse=1; fi
        done
        if [ "$worse" -eq 0 ]; then
            echo "结论：我们的动态条数 <= 官方优化版（三种输入均不更差）"
        else
            echo "警告：有输入我们的动态条数高于官方优化版，见上表（需在报告中调查）"
        fi
    fi

    return 0
}

if [ "${1:-}" = "--report" ]; then
    shift
    report_mode "${1:-}"
    exit $?
fi

# parser 路径可被 LAB3_PARSER 覆盖。两处用途：
#   1) 判别力自证：用一个会写出固定 IR 的桩 parser，验证"(b) IR 结果与 .exp 不符"
#      这条分支真的会红；
#   2) `make opt-off-test`：LAB3_OPT 是**编译期宏**，环境变量改不了已编译的 parser，
#      故该目标先用 -DLAB3_OPT=0 构建 build/parser-noopt，再经本变量指向它，跑同一批
#      .cmm 并以 .exp 为裁判 —— “优化未改行为”因此由参考机独立证明。
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

    # 缺 .in：FAIL，不得 SKIP —— 与"缺 .exp"并列的显式守卫。
    # 若放任它过去，参考机的 _read_inputs 会抛 FileNotFoundError，最终只报一句
    # 含糊的"参考机执行失败"加一段回溯，看不出真正缺的是 .in。
    if [ ! -f "$in" ]; then
        echo "FAIL $rel: 缺 .in（缺输入必须 FAIL，不得 SKIP）"
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
        echo "FAIL $rel: parser 未产出 IR（第二个参数指定的文件缺失或为空）"
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
