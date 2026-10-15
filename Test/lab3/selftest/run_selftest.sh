#!/usr/bin/env bash
# run_lab3.sh（运行台）的**判别力自证** —— committed，可一键复跑。
#
#   bash Test/lab3/selftest/run_selftest.sh        # 退出码 0 = 5 条自证全过
#
# 为什么入库（Task 5 审查发现）：上一轮那四条判别力实测的证据全在 gitignore 的 build/ 下，
# 不可复现 —— 等于"运行台正确"这件事没有任何 committed 的守护。本脚本把它固化下来：
# 逐条断言运行台的**实际输出**与**退出码**，任一条不符即退出 1，末尾打印 `selftest pass=N fail=M`。
#
# 设计要点：
#   * 一律用**桩 parser**（LAB3_PARSER 覆盖，见同目录 stubparser.sh），不依赖真 parser
#     （它要到 Task 12 才会往第二份文件写中间代码）；
#   * 断言不看"人眼观察"，只看可复算的输出串与 $?；
#   * 自证用例（各自一个子目录）：
#       ① onlyok/  ：桩输出 42 = okcase.exp     ⇒ 绿（退 0）
#       ② onlybad/ ：桩输出 42 ≠ badcase.exp 41 ⇒ 红（退 1，报『IR 结果与 .exp 不符』）
#       ③ noexp/   ：有 .cmm/.in、缺 .exp       ⇒ FAIL『缺 .exp』且**非 SKIP**
#       ④ empty/   ：无任何 .cmm                ⇒ 退出码 1
#       ⑤ noin/    ：有 .cmm/.exp、缺 .in       ⇒ FAIL『缺 .in』（不是含糊的『参考机执行失败』）
#
# 环境：运行台需要 bash + python3（跑 irsim_ref.py）。项目在 WSL 里跑：
#   wsl.exe -d Ubuntu-22.04 -- bash -c 'cd <repo> && bash Test/lab3/selftest/run_selftest.sh'
set -u

SELF="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$SELF/../../.." && pwd)"   # selftest -> lab3 -> Test -> 仓库根
RUN="$REPO/scripts/run_lab3.sh"
STUB="$SELF/stubparser.sh"

PASS=0
FAIL=0

record() {  # record <0=好 1=坏> <名称> [细节]
    if [ "$1" -eq 0 ]; then
        echo "ok   自证：$2"
        PASS=$((PASS + 1))
    else
        echo "FAIL 自证：$2${3:+ —— $3}"
        FAIL=$((FAIL + 1))
    fi
}

# 跑一次运行台（用桩 parser）；输出与退出码分别落到全局 $out / $rc。
run_runner() {  # run_runner <用例目录>
    out="$(LAB3_PARSER="$STUB" bash "$RUN" "$1" 2>&1)"
    rc=$?
}

show() { printf '%s\n' "$out" | sed 's/^/       /' | head -14; }

# ---------- ① 桩输出与 .exp 相符 ⇒ 绿（退 0） ----------
run_runner "$SELF/onlyok"
if [ "$rc" -eq 0 ] && printf '%s' "$out" | grep -qE 'pass=1 fail=0'; then
    record 0 "① 桩输出与 .exp 相符 ⇒ 绿（退 0）"
else
    record 1 "① 桩输出与 .exp 相符 ⇒ 绿（退 0）" "rc=$rc"; show
fi

# ---------- ② 故意不符 ⇒ 变红（退 1） ----------
run_runner "$SELF/onlybad"
if [ "$rc" -eq 1 ] && printf '%s' "$out" | grep -qF 'IR 结果与 .exp 不符'; then
    record 0 "② 故意不符 ⇒ 变红（退 1，报『IR 结果与 .exp 不符』）"
else
    record 1 "② 故意不符 ⇒ 变红（退 1）" "rc=$rc"; show
fi

# ---------- ③ 缺 .exp ⇒ FAIL（『缺 .exp』且非 SKIP） ----------
# 注意：运行台那句 FAIL 的正文里本身就带"不得 SKIP"字样，故"非 SKIP"必须按**行首**判定
# （欲判 SKIP 行为的变异会打印一行 `SKIP ...`，而 FAIL 行以 `FAIL ` 开头）。
run_runner "$SELF/noexp"
if [ "$rc" -eq 1 ] && printf '%s' "$out" | grep -qE '^FAIL .*缺 \.exp' \
        && ! printf '%s' "$out" | grep -qE '^SKIP'; then
    record 0 "③ 缺 .exp ⇒ FAIL（报『缺 .exp』且非 SKIP）"
else
    record 1 "③ 缺 .exp ⇒ FAIL（报『缺 .exp』且非 SKIP）" "rc=$rc"; show
fi

# ---------- ④ 空目录 ⇒ 退出码 1 ----------
run_runner "$SELF/empty"
if [ "$rc" -eq 1 ] && printf '%s' "$out" | grep -qE 'pass=0 fail=0'; then
    record 0 "④ 空目录 ⇒ 退出码 1"
else
    record 1 "④ 空目录 ⇒ 退出码 1" "rc=$rc"; show
fi

# ---------- ⑤ 缺 .in ⇒ FAIL（新加的守卫；而非含糊的『参考机执行失败』） ----------
run_runner "$SELF/noin"
if [ "$rc" -eq 1 ] && printf '%s' "$out" | grep -qF '缺 .in' \
        && ! printf '%s' "$out" | grep -qF '参考机执行失败'; then
    record 0 "⑤ 缺 .in ⇒ FAIL（报『缺 .in』，不是含糊的『参考机执行失败』）"
else
    record 1 "⑤ 缺 .in ⇒ FAIL（报『缺 .in』）" "rc=$rc"; show
fi

echo "----"
echo "selftest pass=$PASS fail=$FAIL"
# 有失败、或一个都没跑成（防"零用例静默绿"），一律退出 1。
if [ "$FAIL" -ne 0 ] || [ "$PASS" -eq 0 ]; then
    exit 1
fi
exit 0
