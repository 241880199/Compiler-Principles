#!/usr/bin/env bash
# 关键变异的**判别力自证**（Task 10 审查修正时入库；Task 11 追加两条；Task 11b 再加一条）。
#
#   bash Test/lab3/selftest/run_mutation_checks.sh     # 退出码 0 = 全部变异检查过
#
# 为什么入库：这些变异
#   ① 去掉 newRelop 里填 opText 那步（src/tree.c）
#   ② 去掉 ST_WHILE 的回跳 GOTO（src/translate.c）
#   ③ Task 11：translateArgs 逆序改正序（应**恰好**打红 call-test）
#   ④ Task 11：去掉常量下标折叠（应**恰好**打红 arr-test）
#   ⑤ Task 11b：把裸 `read();` 又丢掉（应打红 call-test 与 translate-test）
# ①② 原先只在 gitignore 的 build/ 下跑过（build/t10-mutate.sh、build/t10c_keymut*），
# 不可复现 —— 等于"判据对改坏有判别力"这件事跟着报告走、不跟着判据走。这与
# run_selftest.sh 入库的理由是同一个（那一次审查也发现上一轮的判别力证据不可复现）。
# ③④ 是 Task 11 两处最易写反处（ARG 顺序、常量下标折叠）的守护；它们顺带证明
# call-test / arr-test **分开**是必要的（否则两类变异无法各自"恰好"打红一条）。
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

# ══════════════════════════════════════════════════════════════════════════
# 变异 3（Task 11）：src/translate.c —— translateArgs 把实参逆序改回**正序**
#   即：把"递归右侧之后再追加本项"改回"先追加本项再递归"。
#   预期：**只有 call-test 变红**（ARG 顺序变了 ⇒ 逐字节 IR 变、参考机把 f(1,2,3)
#   算成 321 而不是 123）。arr-test 不受影响 —— 两条夹具分开正是为这条"恰好"。
#   这条是 Task 11 最易写反处（ARG 压栈 / PARAM 弹栈）的守护。
# ══════════════════════════════════════════════════════════════════════════
echo "== 变异 3：translateArgs 逆序改正序（src/translate.c）=="
begin_mutation src/translate.c
python3 - "$MUT_FILE" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
old = r'''    if (a->nchild == 3)
        tail = translateArgs(tail, a->child[2], argList, n);    /* 更靠后的实参先入列 */
    if (*n >= MAX_CALL_ARGS) {
        fprintf(stderr, "internal error: 实参个数超过 %d（translateArgs 定长缓冲）\n",
                MAX_CALL_ARGS);
        abort();
    }
    argList[(*n)++] = t;                                        /* ★ 本项排在它们之后 */
'''
new = r'''    if (*n >= MAX_CALL_ARGS) {
        fprintf(stderr, "internal error: 实参个数超过 %d（translateArgs 定长缓冲）\n",
                MAX_CALL_ARGS);
        abort();
    }
    argList[(*n)++] = t;   /* MUTATION(selftest): 本项先入列 = 正序 —— call-test 应变红 */
    if (a->nchild == 3)
        tail = translateArgs(tail, a->child[2], argList, n);
'''
assert s.count(old) == 1, "期望恰好出现 1 次，实测 %d 次" % s.count(old)
open(p, "w", encoding="utf-8", newline="").write(s.replace(old, new))
PY
assert_target "变异3：变异后仍能编过（call 二进制）" Test/unit/test_translate_prog zero
assert_target "变异3：call-test 变红（ARG 顺序）" call-test nonzero
assert_target "变异3：arr-test 不受影响（仍绿）" arr-test zero
if end_mutation; then record 0 "变异3：src/translate.c 逐字节复原（sha256 一致）"
else record 1 "变异3：src/translate.c 逐字节复原" "sha256 不一致"; fi
assert_target "变异3：复原后 call-test 复绿" call-test zero

# ══════════════════════════════════════════════════════════════════════════
# 变异 4（Task 11）：src/translate.c —— 去掉常量下标折叠
#   把 `if (classifyExp(idxExp) == EX_INT)` 改成永假（EX_INT 必 nchild==1），
#   于是常量下标退回"先算 i*4 再相加"的一般路径。
#   预期：**只有 arr-test 变红**（逐字节 IR 变：`&a + #8` 变成 `idx := #8/4 …`）；
#   语义不变（参考机 WRITE 输出仍是 27/30），故这条钉的是**产物形态**而非结果。
# ══════════════════════════════════════════════════════════════════════════
echo "== 变异 4：去掉常量下标折叠（src/translate.c）=="
begin_mutation src/translate.c
python3 - "$MUT_FILE" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
old = "    if (classifyExp(idxExp) == EX_INT) {                       /* ★ 常量下标折叠 */\n"
assert s.count(old) == 1, "期望恰好出现 1 次，实测 %d 次" % s.count(old)
new = "    if (classifyExp(idxExp) == EX_INT && idxExp->nchild != 1) { /* MUTATION: 折叠失效 —— arr-test 应变红 */\n"
open(p, "w", encoding="utf-8", newline="").write(s.replace(old, new))
PY
assert_target "变异4：变异后仍能编过（arr 二进制）" Test/unit/test_translate_prog zero
assert_target "变异4：arr-test 变红（常量折叠）" arr-test nonzero
assert_target "变异4：call-test 不受影响（仍绿）" call-test zero
if end_mutation; then record 0 "变异4：src/translate.c 逐字节复原（sha256 一致）"
else record 1 "变异4：src/translate.c 逐字节复原" "sha256 不一致"; fi
assert_target "变异4：复原后 arr-test 复绿" arr-test zero

# ══════════════════════════════════════════════════════════════════════════
# 变异 5（Task 11b）：src/translate.c —— 把 translateCall0 的 read 分支退回
#   "place 为 NULL 时直接 return tail"（即 Task 11 那版把裸 `read();` 整条丢掉）。
#   预期：**call-test 变红**（夹具里 `read(); n = read(); write(n);` 输入 `5 7`：
#   丢掉那次读 ⇒ n 变成 5、后面 `f(n,n+1,n+2)` 算成 567，而期望是按 C 语义钉的 7/789）；
#   **translate-test 也变红**（单测 ⑧ 逐字节钉住 `read();` 必须产出 `READ t1`）；
#   arr-test 不受影响（那条夹具里没有 read）。
#   这条是第 1 条修正（"READ 的消费一次输入是可观察副作用"）的判别力守护。
# ══════════════════════════════════════════════════════════════════════════
echo "== 变异 5：裸 read() 又丢掉（src/translate.c）=="
begin_mutation src/translate.c
python3 - "$MUT_FILE" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
old = """        Operand dst = place ? place : opVar(newTempName());
        InterCode *c = newCode(IR_READ);
        c->u.one.x = dst;
        return irAppend(tail, c);
"""
assert s.count(old) == 1, "期望恰好出现 1 次，实测 %d 次" % s.count(old)
new = """        /* MUTATION(selftest): 退回"值没人要就不翻译"—— 应让 call-test 与 translate-test 变红 */
        if (place) { InterCode *c = newCode(IR_READ); c->u.one.x = place;
                     return irAppend(tail, c); }
        return tail;
"""
open(p, "w", encoding="utf-8", newline="").write(s.replace(old, new))
PY
assert_target "变异5：变异后仍能编过（call 二进制）" Test/unit/test_translate_prog zero
assert_target "变异5：变异后仍能编过（translate 单测二进制）" Test/unit/test_translate zero
assert_target "变异5：call-test 变红（裸 read() 被丢）" call-test nonzero
assert_target "变异5：translate-test 变红（单测 ⑧）" translate-test nonzero
assert_target "变异5：arr-test 不受影响（仍绿）" arr-test zero
if end_mutation; then record 0 "变异5：src/translate.c 逐字节复原（sha256 一致）"
else record 1 "变异5：src/translate.c 逐字节复原" "sha256 不一致"; fi
assert_target "变异5：复原后 call-test 复绿" call-test zero
assert_target "变异5：复原后 translate-test 复绿" translate-test zero

echo "----"
echo "mutation-checks pass=$PASS fail=$FAIL"
# 有失败、或一个都没跑成（防"零检查静默绿"），一律退出 1。
if [ "$FAIL" -ne 0 ] || [ "$PASS" -eq 0 ]; then
    exit 1
fi
exit 0
