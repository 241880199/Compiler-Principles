"""执行引擎用例（Task 3）。

真机口径（irsim 2.0.2，设计文档 §3）：
- `instrCnt` 在 `execute_code` 的第一件事就 +1，主循环再**无条件** `ip += 1` ⇒
  **FUNCTION / GOTO / DEC / RETURN 全计数**；`LABEL` 行**只在被"顺序落到"或作为入口时才计数**，
  **被跳转（GOTO/IF/CALL）命中的那条 `LABEL` 行会被那个无条件 `+1` 整条跨过 —— 不执行、不计数**
  （§3.6：效率 = 动态执行的指令行数，不是静态行数；此口径已由原版对拍证伪并修正，见
  `docs/lab3/refsim-provenance.md` §2.6）。
- `CALL` 为被调函数**重新分配**局部变量地址、`RETURN` 时恢复（§3.4）；
  递归靠这一点才能正确。
- `ARG` 压栈 / `PARAM` 弹栈 ⇒ 实参书写顺序与形参声明顺序**相反**（§3.4）。
- `/` 在装载期被改写成 `//` ⇒ 向下取整，`-7 / 2 == -4`（§3.7-1）。
- 运行期越界 → `IndexError` → `status == "memory"`（§3.5）。
- **写回目标为 `*x` 的指令崩**（`x := ...` 之外的写回走裸下标 `symTable[x][0]`，而 `*x`
  不在表里 ⇒ `KeyError`）（§3.7-2）：`ARITH` 的 `*x := a op b` 与 `RETURN` 的
  `*x := CALL f` 都属此类 —— 参考机的用途是预测判分机，这种崩溃要一起复现，
  不许"更宽容"地静默成功。

⚠️ `status == "memory"` 只许由**内存越界**产生：见 `test_non_indexerror_is_not_swallowed`，
它钉住 `except` 不许被放宽成吞掉别的异常。
"""
import contextlib
import io
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "scripts"))
from irsim_ref import run

HERE = os.path.dirname(os.path.abspath(__file__))


def _run(name, inputs=()):
    return run(os.path.join(HERE, name), inputs=list(inputs))


# --------------------------------------------------------- 要求书 4.1.6 样例 1（原始 26 行）

def test_sign_function_from_requirement_sample():
    """要求书 4.1.6 样例 1 的原始中间代码。"""
    r = _run("sign.ir", inputs=[-7])
    assert r.outputs == [-1] and r.status == "ok"


def test_sign_function_covers_all_three_branches():
    """sgn(n) 的真值表：三条路径都要走到（样例 1 是 if / else-if / else）。"""
    assert _run("sign.ir", inputs=[-7]).outputs == [-1]
    assert _run("sign.ir", inputs=[0]).outputs == [0]
    assert _run("sign.ir", inputs=[9]).outputs == [1]


def test_read_exhausted_does_not_write_memory():
    """`READ` 输入耗尽 = 真机弹窗被**取消** ⇒ **不写内存**，变量保留旧值（§3.3）。

    真机 `READ` 是 `result, ok = QInputDialog.getInt(...)`，只有 `ok` 为真才
    `mem[...] = result`；`ok` 为假（取消）时**什么都不写**。我们的输入序列耗尽即"取消"。

    两半都要钉：
    - `sign.ir` 无输入：被读的 `n` 是**全新变量**（本来 0）⇒ 不写也还是 0，输出 0
      （这条**分不出**"写 0"与"不写"，只保证行为不变）；
    - `read_cancel.ir`：`t := #5` 后 `READ t` 且不给输入 ⇒ 若"写 0"会得 `[0]`，
      真机口径（不写）得 `[5]` —— **这一条才有判别力**。
    """
    assert _run("sign.ir", inputs=[]).outputs == [0]
    assert _run("read_cancel.ir", inputs=[]).outputs == [5]


# --------------------------------------------------------------------- 指令计数口径（§3.6）

def test_jumped_over_label_is_not_executed_nor_counted():
    """★ 真机计数口径（spec §3.6）：**跳转命中的那条 `LABEL` 行不执行、不计数**。

    `counted.ir`：FUNCTION main / GOTO label1 / LABEL label1 / RETURN #0。

    `GOTO label1` 把控制交给 `label1`（在 `codes` 里的下标 2），而真机主循环随后**无条件**
    `ip += 1` ⇒ 下一站直接是 `RETURN #0`（下标 3），**`LABEL label1` 被整条跨过**。
    ⇒ 恰好 **3** 条：FUNCTION main（入口，计数）、GOTO、RETURN。

    旧口径（跳转目标不加 +1）会执行并计数那条被跳过的 `LABEL` ⇒ 得 4 —— 本用例即为此而设。
    """
    r = _run("counted.ir")
    assert r.status == "ok"
    assert r.instr_cnt == 3


def test_counted_trace_proves_the_skipped_line_is_a_label():
    """上一条的对偶：用逐指令踪迹证明**被跨过的正是那条 `LABEL`**，不是别处漏计。

    只断言 `instr_cnt == 3` 对"任意漏计一条"都成立；这里把踪迹内容钉住：
    踪迹是 `('LABEL', 'main')` / `('GOTO', 'label1')` / `('RETURN', '#0')`，
    **不含**被跳转命中的 `('LABEL', 'label1')`。
    """
    buf = io.StringIO()
    with contextlib.redirect_stderr(buf):
        r = run(os.path.join(HERE, "counted.ir"), trace=True)
    lines = [ln for ln in buf.getvalue().splitlines() if ln.strip()]
    assert r.instr_cnt == len(lines) == 3, lines
    assert any("('LABEL', 'main')" in ln for ln in lines), lines
    assert any("'GOTO'" in ln for ln in lines), lines
    assert any("'RETURN'" in ln for ln in lines), lines
    assert not any("('LABEL', 'label1')" in ln for ln in lines), lines


def test_instruction_count_includes_dec():
    """`DEC` 是空操作但**照样计数**（真机 execute_code 第一件事就 +1）。

    counted_dec.ir：FUNCTION main / DEC a 4 / RETURN #0 ⇒ 3。
    若不计数 DEC 会得 2 —— 这条用例专门把 DEC 的口径钉住。
    """
    r = _run("counted_dec.ir")
    assert r.status == "ok"
    assert r.instr_cnt == 3


# ----------------------------------------------------------- 调用约定：地址保存/恢复（§3.4）

def test_call_restores_addresses():
    """递归 fact(5) == 120 —— 靠 CALL 重新分配地址、RETURN 恢复（含递归深度 5）。"""
    r = _run("rec.ir", inputs=[])
    assert r.outputs == [120]
    assert r.status == "ok"


def test_arg_param_order():
    """ARG 压栈 / PARAM 弹栈：实参书写顺序与形参声明顺序相反。

    f(p, q) = p*10 + q；实参 a=1,b=2 按 `ARG #2` / `ARG #1` 压栈 ⇒ 得 12。
    **形参或实参顺序写反会得 21** —— 这条用例对顺序有判别力。
    """
    r = _run("argorder.ir")
    assert r.outputs == [12]
    assert r.status == "ok"


# --------------------------------------------------------------------- 整除语义（§3.7-1）

def test_floor_division_semantics():
    """spec §3.7-1：'/' 被改写为向下取整，-7/2 == -4，而不是 C 的 -3。"""
    r = _run("divneg.ir")
    assert r.outputs == [-4]
    assert r.status == "ok"


# --------------------------------------------------------------------- 越界与异常边界（§3.5）

def test_memory_out_of_bound():
    """写到 1 MiB（地址 1048576 = 第 262145 个字）⇒ IndexError ⇒ status 'memory'。

    ⚠️ `oob.ir` 用 `*p := #1`（**MOV 的解引用写**）触发越界，故这条"内存"用例
    **同时依赖 MOV 的解引用可写**。这不是缺陷（该夹具难绕开解引用路径），但下游若单独
    收紧 `MOV`，它会以一个**与内存无关的理由**变红 —— 那时别把它当成回归。
    """
    r = _run("oob.ir")
    assert r.status == "memory"


def test_arith_target_deref_reproduces_real_crash():
    """spec §3.7-2：`*x := a op b` 在真机上抛 **KeyError**（`ARITH` 只做 `symTable[x][0]`）。

    参考机的用途是**预测判分机**，故这种崩溃必须一起复现，不许"更宽容"地静默成功 ——
    否则生成器真吐出这种 IR 时我们会得到假绿。

    `deref_arith.ir` 的 `p` 指向一片**合法**内存（`&q`），宽容版会安静写进去；
    故这条能真正区分"复现崩溃"与"静默成功"。

    断言钉住**是哪一个** `KeyError`（`e.args == ("*p",)`）：只断言"抛了 KeyError"的话，
    将来别处冒出一个无关的 `KeyError` 它照样绿。
    """
    try:
        _run("deref_arith.ir")
    except KeyError as e:
        assert e.args == ("*p",), "KeyError 应指向被误用的 `*p`，实际 %r" % (e.args,)
        return
    raise AssertionError("`*x := a op b` 应复现真机的 KeyError，而不是静默成功")


def test_call_target_deref_reproduces_real_crash():
    """§3.7-2（同源缺口）：`*p := CALL f` 在真机上同样抛 **KeyError**。

    真机 `RETURN` 写回返回值那一步是 `mem[symTable[stackItem[1]][0] // 4] = returnValue`
    —— 与 `ARITH` 的写法**同构**的裸下标，**完全没有** `*` 处理。`tableInsert` 会把 `*`
    剥掉再登记，所以符号表里只有 `p`、没有 `*p`。

    `deref_call.ir` 的 `p` 指向**合法**内存（`p := &q`）⇒ 宽容版（`store_var`）会安静
    写进 `q`，只有裸下标版才会崩 —— 故这条能真正区分"复现崩溃"与"静默成功"。
    该形态**能通过装载**（`sanity_check` 只拦 `&`/`#` 开头的左值），不是不可达的病态输入。

    反例（普通 `x := CALL f` 必须照常工作）由既有的 `test_call_restores_addresses`
    （`rec.ir`：`result := CALL fact`）与 `test_arg_param_order`（`argorder.ir`：`r := CALL f`）
    覆盖 —— 这半条不能省：否则这条收紧就无从与"把所有 CALL 都弄坏了"区分开。
    """
    try:
        _run("deref_call.ir")
    except KeyError as e:
        assert e.args == ("*p",), "KeyError 应指向被误用的 `*p`，实际 %r" % (e.args,)
        return
    raise AssertionError("`*x := CALL f` 应复现真机的 KeyError，而不是静默成功")


def test_mov_deref_still_works():
    """**反例**：`MOV` 的两个方向都支持 `*`，别把收紧做过头。

    `r := *p`（解引用读）与 `*p := #99`（解引用写）都必须正常工作 ——
    后者正是 §3.7-2 要求的绕行写法（先生成 `t := a op b`，再 `*x := t`）。
    ⇒ 证明我们只收紧了 `ARITH` 与 `RETURN` 的**写回目标**，没有把所有 `*` 都封了。
    """
    r = _run("deref_mov.ir")
    assert r.outputs == [8, 99]
    assert r.status == "ok"


def test_non_indexerror_is_not_swallowed():
    """只有内存越界才走 'memory' 这支；别的异常必须原样漏出。

    `divzero.ir` 做 `1 / 0`：真机用 Python 的 `//`，会抛 **ZeroDivisionError** ——
    它不是 IndexError，主循环不该把它算作 'memory'。
    ⇒ 若有人把 `except IndexError` 放宽成 `except Exception`，这条会变红。
    """
    try:
        _run("divzero.ir")
    except ZeroDivisionError:
        return
    raise AssertionError("除零应原样漏出 ZeroDivisionError，而不是被吞成 status=='memory'")
