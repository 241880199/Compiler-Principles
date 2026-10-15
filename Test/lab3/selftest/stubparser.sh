#!/usr/bin/env bash
# 自证用的**桩 parser**：读入 .cmm（$1），把一份**固定 IR** 写到第二个参数（$2）。
# 调用形状与真 parser 完全一致（run_lab3.sh 调 `"$PARSER" <cmm> <ir>`），故可经
# LAB3_PARSER 覆盖真 parser —— 真 parser 要到 Task 12 才会往第二份文件写中间代码，
# 在那之前，用这个桩把运行台的"IR 结果与 .exp 不符"上色分支测通。
set -eu
cmm="${1:?stubparser: 缺少 <cmm> 参数}"
ir="${2:?stubparser: 缺少 <ir> 输出路径}"
[ -r "$cmm" ] || { echo "stubparser: 读不到用例 $cmm" >&2; exit 2; }
# 固定 IR：只写一个 42。onlyok 的 .exp 就是 42（绿）；onlybad 的 .exp 是 41（红）。
cat > "$ir" <<'IR'
FUNCTION main :
t1 := #42
WRITE t1
RETURN #0
IR
