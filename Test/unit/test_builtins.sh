#!/usr/bin/env bash
# ── read/write 内建登记 + 「翻译以语义错误为闸」的端到端判据 ──────────────────
# 用**真 parser**（不是手工造 AST）钉住四类行为：
#   1. 合法 lab3 程序不再打印假的 `Error type 2`，且照常产出 IR；
#   2. 用户重定义 read/write ⇒ 错误 4（行号取用户定义处），且**不翻译**；
#   3. read/write 的实参个数不对 ⇒ 错误 9 且进程**不得 abort**，且不翻译；
#   4. 既有语义错误用例 lab2_multi（**不走** CMM_SEMANTIC_ONLY）⇒ stdout 与
#      .exp 逐字节相同、不翻译 —— 证明语义错误输入不会再进翻译层。
#
# 每个用例都**分别**捕获 stdout / stderr / 退出码，并断 IR 文件 —— 四样缺一不可。
# 只捕获 stdout 会让"翻译层 abort 却留下正确诊断"蒙混过关（第 3 类正是如此）。
#
# 通过打印 `PASS builtin-test`；任一失败打印 `FAIL <case>: <原因>` 并退出 1。
set -u
cd "$(dirname "$0")/../.."

if [ ! -x ./parser ]; then
    echo "错误：未找到 ./parser，请先运行 make" >&2
    exit 1
fi

# 本脚本第 4 条**故意**不走语义专用入口：CMM_SEMANTIC_ONLY 若从外部环境漏进来，
# 这条就不再有判别力（它要证明的正是 `semErrs` 闸本身）。
unset CMM_SEMANTIC_ONLY 2>/dev/null || true

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

fail=0
fail_case() {
    echo "FAIL $1: $2"
    fail=1
}

# run_case <name> <input>：把三样分别落到 $tmp/<name>.{out,err,ir}，rc 存入全局 rc。
run_case() {
    name=$1
    input=$2
    ./parser "$input" "$tmp/$name.ir" > "$tmp/$name.out" 2> "$tmp/$name.err"
    rc=$?
}

# 退出码与 stderr：四条用例的共同断言。
check_common() {
    name=$1
    if [ "$rc" -ne 0 ]; then
        fail_case "$name" "退出码 $rc（期望 0；非 0 可能是 yyparse 失败或翻译层 abort）"
    fi
    if [ -s "$tmp/$name.err" ]; then
        fail_case "$name" "stderr 非空（正常流程不得写 stderr）"
        sed 's/^/      /' "$tmp/$name.err" | head -10
    fi
}

# ── 1. 合法程序：假错误 2 必须消失，IR 必须非空 ─────────────────────────────
run_case ok Test/unit/builtin_ok.cmm
check_common ok
if [ -s "$tmp/ok.out" ]; then
    fail_case ok "stdout 非空（read/write 未登记为内建时会打印假的 Error type 2）"
    sed 's/^/      /' "$tmp/ok.out" | head -10
fi
if [ ! -s "$tmp/ok.ir" ]; then
    fail_case ok "IR 输出为空（合法程序应当被翻译）"
fi

# ── 2. 用户重定义 read/write：错误 4，行号取用户定义处，且不翻译 ──────────────
run_case redef Test/unit/builtin_redef.cmm
check_common redef
printf '%s\n' \
    'Error type 4 at Line 1: Redefined function "read".' \
    'Error type 4 at Line 5: Redefined function "write".' > "$tmp/redef.exp"
if ! diff -u "$tmp/redef.exp" "$tmp/redef.out"; then
    fail_case redef "stdout 与期望逐字节不符"
fi
if [ -s "$tmp/redef.ir" ]; then
    fail_case redef "语义错误时仍产出了 IR（翻译闸失效）"
fi

# ── 3. 畸形内建调用：错误 9、不得 abort，且不翻译 ────────────────────────────
run_case badcall Test/unit/builtin_badcall.cmm
check_common badcall
printf '%s\n' \
    'Error type 9 at Line 3: Function "read()" is not applicable for arguments "(int)".' \
    'Error type 9 at Line 4: Function "write(int)" is not applicable for arguments "()".' \
    > "$tmp/badcall.exp"
if ! diff -u "$tmp/badcall.exp" "$tmp/badcall.out"; then
    fail_case badcall "stdout 与期望逐字节不符"
fi
if [ -s "$tmp/badcall.ir" ]; then
    fail_case badcall "语义错误时仍产出了 IR（翻译闸失效）"
fi

# ── 4. 既有语义错误用例：stdout 与 .exp 相同，且不进翻译层 ───────────────────
run_case multi Test/lab2/lab2_multi.cmm
check_common multi
if ! diff -u Test/lab2/lab2_multi.exp "$tmp/multi.out"; then
    fail_case multi "stdout 与 Test/lab2/lab2_multi.exp 逐字节不符"
fi
if [ -s "$tmp/multi.ir" ]; then
    fail_case multi "语义错误输入仍产出了 IR（翻译闸失效）"
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "PASS builtin-test"
