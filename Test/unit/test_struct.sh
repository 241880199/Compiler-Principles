#!/usr/bin/env bash
# ── 结构体输入的"不可翻译"提示（选做 3.1 未实现档）───────────────────────────
# 要求书 4.1.7 样例 1：输入含结构体类型的变量/形参时，只打印
#   Cannot translate: Code contains variables or parameters of structure type.
# 且**不产出任何中间代码**（第二个参数指定的输出文件为空）。
#
# 两个夹具：
#   struct_sample.cmm —— 要求书样例 1 逐字抄录（结构体变量 op + 结构体形参 temp）；
#   struct_array.cmm  —— 结构体数组（检测必须沿数组元素递归，不能只看顶层 kind）。
#
# 每个用例分别捕获 stdout / stderr / 退出码，并断言 IR 文件**存在且 0 字节**：
#   · 只看 stdout 会让"提示对了、但同时产出了 IR"蒙混过关；
#   · stderr 必须为空 —— 正常流程不得写 stderr（全局约束）。
#
# 必须显式 unset CMM_SEMANTIC_ONLY：该变量会让语义专用入口跳过翻译，
# 恰好掩盖本脚本要测的行为（且若从外部环境漏进来，断言的判别力就没了）。
set -u
cd "$(dirname "$0")/../.."

if [ ! -x ./parser ]; then
    echo "错误：未找到 ./parser，请先运行 make" >&2
    exit 1
fi

unset CMM_SEMANTIC_ONLY 2>/dev/null || true

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

printf '%s\n' \
    'Cannot translate: Code contains variables or parameters of structure type.' \
    > "$tmp/expected"

fail=0

# run_case <name> <input>：产物分别落到 $tmp/<name>.{out,err,ir}。
run_case() {
    name=$1
    input=$2

    ./parser "$input" "$tmp/$name.ir" > "$tmp/$name.out" 2> "$tmp/$name.err"
    rc=$?

    if [ "$rc" -ne 0 ]; then
        echo "FAIL $name: 退出码 $rc（期望 0）"
        fail=1
    fi
    if [ -s "$tmp/$name.err" ]; then
        echo "FAIL $name: stderr 非空（正常流程不得写 stderr）"
        sed 's/^/      /' "$tmp/$name.err" | head -10
        fail=1
    fi
    if ! cmp -s "$tmp/expected" "$tmp/$name.out"; then
        echo "FAIL $name: stdout 与期望不逐字节相同"
        diff -u "$tmp/expected" "$tmp/$name.out" | sed 's/^/      /' | head -20
        fail=1
    fi
    if [ ! -f "$tmp/$name.ir" ]; then
        echo "FAIL $name: 输出 IR 文件不存在"
        fail=1
    elif [ -s "$tmp/$name.ir" ]; then
        echo "FAIL $name: 输出 IR 文件非空（结构体输入不得产出中间代码）"
        fail=1
    fi
}

run_case struct_sample Test/unit/struct_sample.cmm
run_case struct_array  Test/unit/struct_array.cmm

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "PASS struct-test"
