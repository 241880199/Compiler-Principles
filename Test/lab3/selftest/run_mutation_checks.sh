#!/usr/bin/env bash
# 关键变异的**判别力自证**（Task 10 审查修正时入库）。
#
#   bash Test/lab3/selftest/run_mutation_checks.sh     # 退出码 0 = 两条变异检查全过
#
# 为什么入库：这两条变异
#   ① 去掉 newRelop 里填 opText 那步（src/tree.c）
#   ② 去掉 ST_WHILE 的回跳 GOTO（src/translate.c）
# 原先只在 gitignore 的 build/ 下跑过（build/t10-mutate.sh、build/t10c_keymut*），
# 不可复现 —— 等于"判据对改坏有判别力"这件事跟着报告走、不跟着判据走。这与
# run_selftest.sh 入库的理由是同一个（那一次审查也发现上一轮的判别力证据不可复现）。
#
# 每条检查的形态（任一环不符即整脚本退出 1）：
#   备份 → 施加变异（python 断言目标串恰好出现一次）→ **先断言仍能编过**
#   （否则证明的会是"编译失败"、而非"行为变红"）→ 跑目标断言**变红** →
#   逐字节复原（sha256 核）→ 复跑断言**复绿**。
# 末尾打印 `mutation-checks pass=N fail=0`；有失败即退出 1。
#
# ★ 这个脚本自己也有判别力：把下面任一 expect 的 zero/nonzero 改坏、或把 sha 复原
#   检查绕过，它就会打印 FAIL 并退出 1（改动前实测过一次，见 task-10d-report.md）。
#
# 环境：bash + make + python3 + sha256sum，在 WSL 里跑：
#   wsl.exe -d Ubuntu-22.04 -- bash -c 'cd <repo> && bash Test/lab3/selftest/run_mutation_checks.sh'
set -u

SELF="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$SELF/../../.." && pwd)"   # selftest -> lab3 -> Test -> 仓库根
cd "$REPO" || exit 99

PASS=0
FAIL=0
TMP="$(mktemp -d)"
MUT_FILE=""
BAK=""
SHA_BEFORE=""

# 无论中途成败都复原源文件，绝不把变异留在树上（防"跑一半崩了、源码留在变异态"）。
cleanup() {
    if [ -n "$MUT_FILE" ] && [ -n "$BAK" ] && [ -f "$BAK" ]; then
        cp -f "$BAK" "$MUT_FILE"
    fi
    rm -rf "$TMP"
}
trap cleanup EXIT

record() {  # record <0=好 1=坏> <名称> [细节]
    if [ "$1" -eq 0 ]; then
        echo "ok   $2"; PASS=$((PASS + 1))
    else
        echo "FAIL $2${3:+ —— $3}"; FAIL=$((FAIL + 1))
    fi
}

# 跑 <target> 并断言退出码：zero=期望 0，nonzero=期望非 0。
assert_target() {  # assert_target <名称> <target> <zero|nonzero>
    # 目标名可能含 `/`（如 Test/unit/test_cond），日志名要摊平，否则重定向失败、
    # 命令根本没执行 —— 那条会以 rc=1 混进结果里，看起来像"变异后编不过"。
    logf="$TMP/make_$(printf '%s' "$2" | tr '/' '_').log"
    make "$2" > "$logf" 2>&1
    rc=$?
    case "$3" in
        zero)
            if [ "$rc" -eq 0 ]; then record 0 "$1"
            else record 1 "$1" "期望 rc=0，实测 $rc"; fi ;;
        nonzero)
            if [ "$rc" -ne 0 ]; then record 0 "$1"
            else record 1 "$1" "期望 rc≠0，实测 $rc"; fi ;;
        *) record 1 "$1" "内部错误：未知期望 $3" ;;
    esac
}

begin_mutation() {  # begin_mutation <file>：备份 + 记原始 sha256
    MUT_FILE="$1"
    BAK="$TMP/$(basename "$1").bak"
    cp -f "$1" "$BAK"
    SHA_BEFORE="$(sha256sum "$1" | cut -d' ' -f1)"
}

end_mutation() {  # end_mutation：复原并核 sha256；一致返回 0
    cp -f "$BAK" "$MUT_FILE"
    sha_after="$(sha256sum "$MUT_FILE" | cut -d' ' -f1)"
    [ "$SHA_BEFORE" = "$sha_after" ]
}

# ══════════════════════════════════════════════════════════════════════════
# 变异 1：src/tree.c —— 去掉 newRelop 里填 opText 那步
#   预期：cond-test 与 prog-test **都**变红（RELOP 文本缺失 ⇒ 翻译层响亮 abort）。
#   这一条正是 Task 10b/10c 那条 Critical 的守卫：两个判据都必须咬得住。
# ══════════════════════════════════════════════════════════════════════════
echo "== 变异 1：去掉 newRelop 里填 opText 那步（src/tree.c）=="
begin_mutation src/tree.c
python3 - "$MUT_FILE" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
old = "    n->opText = xstrdup(opText);\n"
assert s.count(old) == 1, "期望恰好出现 1 次，实测 %d 次" % s.count(old)
new = "    /* MUTATION(selftest): 不填 opText —— 应让 cond-test 与 prog-test 都变红 */ (void)opText;\n"
open(p, "w", encoding="utf-8", newline="").write(s.replace(old, new))
PY
assert_target "变异1：变异后仍能编过（cond 二进制）" Test/unit/test_cond zero
assert_target "变异1：变异后仍能编过（prog 二进制）" Test/unit/test_translate_prog zero
assert_target "变异1：cond-test 变红" cond-test nonzero
assert_target "变异1：prog-test 变红" prog-test nonzero
if end_mutation; then record 0 "变异1：src/tree.c 逐字节复原（sha256 一致）"
else record 1 "变异1：src/tree.c 逐字节复原" "sha256 不一致"; fi
assert_target "变异1：复原后 cond-test 复绿" cond-test zero
assert_target "变异1：复原后 prog-test 复绿" prog-test zero

# ══════════════════════════════════════════════════════════════════════════
# 变异 2：src/translate.c —— 去掉 ST_WHILE 的回跳 GOTO
#   预期：prog-test 变红（while 循环体只跑一遍 ⇒ IR 与期望不符）。
# ══════════════════════════════════════════════════════════════════════════
echo "== 变异 2：去掉 ST_WHILE 的回跳 GOTO（src/translate.c）=="
begin_mutation src/translate.c
python3 - "$MUT_FILE" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
old = "        tail = appendGoto(tail, l1);\n"
assert s.count(old) == 1, "期望恰好出现 1 次，实测 %d 次" % s.count(old)
new = "        /* MUTATION(selftest): WHILE 回跳被删 —— 应让 prog-test 变红 */\n"
open(p, "w", encoding="utf-8", newline="").write(s.replace(old, new))
PY
assert_target "变异2：变异后仍能编过（prog 二进制）" Test/unit/test_translate_prog zero
assert_target "变异2：prog-test 变红" prog-test nonzero
if end_mutation; then record 0 "变异2：src/translate.c 逐字节复原（sha256 一致）"
else record 1 "变异2：src/translate.c 逐字节复原" "sha256 不一致"; fi
assert_target "变异2：复原后 prog-test 复绿" prog-test zero

echo "----"
echo "mutation-checks pass=$PASS fail=$FAIL"
# 有失败、或一个都没跑成（防"零检查静默绿"），一律退出 1。
if [ "$FAIL" -ne 0 ] || [ "$PASS" -eq 0 ]; then
    exit 1
fi
exit 0
