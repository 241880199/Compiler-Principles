"""执行引擎用例（Task 3）。

真机口径（irsim 2.0.2，设计文档 §3）：
- `instrCnt` 在 `execute_code` 的第一件事就 +1 ⇒ **FUNCTION / LABEL / GOTO / DEC 全计数**
  （§3.6：效率 = 动态执行的指令行数，不是静态行数）。
- `CALL` 为被调函数**重新分配**局部变量地址、`RETURN` 时恢复（§3.4）；
  递归靠这一点才能正确。
- `ARG` 压栈 / `PARAM` 弹栈 ⇒ 实参书写顺序与形参声明顺序**相反**（§3.4）。
- `/` 在装载期被改写成 `//` ⇒ 向下取整，`-7 / 2 == -4`（§3.7-1）。
- 运行期越界 → `IndexError` → `status == "memory"`（§3.5）。

⚠️ `status == "memory"` 只许由**内存越界**产生：见 `test_only_memory_oob_maps_to_memory`，
它钉住 `except` 不许被放宽成吞掉别的异常。
"""
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


def test_sign_function_read_exhausted_defaults_to_zero():
    """`READ` 的输入耗尽时取 0（无输入 ⇒ n=0 ⇒ 走 else 分支，输出 0）。"""
    assert _run("sign.ir", inputs=[]).outputs == [0]


# --------------------------------------------------------------------- 指令计数口径（§3.6）

def test_instruction_count_includes_labels():
    """LABEL / FUNCTION / GOTO / RETURN 行都被计数（spec §3.6）。

    counted.ir 恰好走 4 条：FUNCTION main(→codes[0]) / GOTO / LABEL label1 / RETURN #0。
    """
    r = _run("counted.ir")
    assert r.status == "ok"
    assert r.instr_cnt == 4


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
    """写到 1 MiB（地址 1048576 = 第 262145 个字）⇒ IndexError ⇒ status 'memory'。"""
    r = _run("oob.ir")
    assert r.status == "memory"


def test_only_memory_oob_maps_to_memory():
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
