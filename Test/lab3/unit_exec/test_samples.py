"""要求书官方样例对拍（Task 4）。

本文件里的两段 IR **逐字抄自**《实验三——中间代码生成.pdf》4.1.6：
- `sample2.ir`      = 样例 2 的 28 行输出（`fact` + `main`，含函数间那个空行）
- `sample_opt1.ir`  = 样例 1 的**优化后**版本（17 行）

与 Task 3 自造夹具不同，这两段是课程方给出的**官方文本**，能独立检验参考机
（`scripts/irsim_ref.py`）的解析与执行 —— 这是参考机可信度的第一份外部证据。

抄录已做机械核对：把 PDF 对应段按『丢页码行 / 剥行号前缀 / 去尾空白』规范化后，
本目录下两份文件与 PDF **逐行全等**（17/17、28/28）。

语义（由官方输入段确定）：
- 样例 2：读入 m 输出 m!（`fact` 以 `n == 1` 为基，`main` 里 `m <= 1` 直接取 1）。
- 样例 1：读入 n 输出 sgn(n)。
"""
import contextlib
import io
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "scripts"))
from irsim_ref import run

HERE = os.path.dirname(os.path.abspath(__file__))


def _run(name, inputs=()):
    return run(os.path.join(HERE, name), inputs=list(inputs))


# ------------------------------------------------------------------ 样例 2：阶乘

def test_sample2_factorial():
    """读入 m=5 ⇒ 输出 5! = 120（走 `m > 1` 分支，递归 `fact(5)`）。"""
    r = _run("sample2.ir", inputs=[5])
    assert r.outputs == [120] and r.status == "ok"


def test_sample2_factorial_zero():
    """读入 m=1 ⇒ 输出 1（`main` 里 `IF v2 > #1` 不成立，直接 `v3 := #1`）。"""
    r = _run("sample2.ir", inputs=[1])
    assert r.outputs == [1] and r.status == "ok"


# --------------------------------------------------------------- 样例 1：符号函数

def test_sample_opt1_sign():
    """优化版 sign：三条路径都要走到（`>` / `<` / fall-through 输出 0）。"""
    for n, want in [(-7, -1), (0, 0), (9, 1)]:
        r = _run("sample_opt1.ir", inputs=[n])
        assert r.outputs == [want], "n=%d" % n
        assert r.status == "ok", "n=%d" % n


# ------------------------------------------------------------------ 优化前后对拍

def test_opt_version_uses_at_most_as_many_instructions():
    """要求书自己给的两版：优化版的 instrCnt 必须 <= 原始版（同一输入 n=-7）。

    实测：原始版 sign.ir = 18 条，优化版 sample_opt1.ir = 11 条（见下一条的踪迹核对）。
    """
    a = _run("sign.ir", inputs=[-7])
    b = _run("sample_opt1.ir", inputs=[-7])
    assert b.instr_cnt <= a.instr_cnt, (b.instr_cnt, a.instr_cnt)


# ------------------------------------------------- 计数口径的独立核对（防『凑巧过』）

def _trace(path, inputs):
    """跑一遍并把执行踪迹（run(trace=True) 打到 stderr 的每行）抓回来。"""
    buf = io.StringIO()
    with contextlib.redirect_stderr(buf):
        r = run(path, inputs=list(inputs), trace=True)
    lines = [ln for ln in buf.getvalue().splitlines() if ln.strip()]
    return r, lines


def test_instr_cnt_matches_execution_trace():
    """`instr_cnt` 必须等于**实际执行路径**的踪迹行数，两版各核一遍。

    为什么单独钉这条：上面两条断言只比"优化版 <= 原始版"，一个**恒定**的计数偏移
    （例如计数从 1 改成 0 起步、或把某个指令漏计）在两版上会同时出现而互相抵消。
    这里用 `run(..., trace=True)` 的逐指令输出把口径钉死 —— 踪迹行与 `instr_cnt`
    由同一处 `+1` 驱动，二者相等才算口径正确；再把两个**绝对值**（18 / 11）钉住，
    使"整体偏移"这种错法也照不出来。

    n=-7 时两条都是**走 `n < 0` 那一支**的动态路径（`sign.ir` 18 条 vs `sign.ir`
    的三条路径分别是 18/17/12；`sample_opt1.ir` 三条路径分别是 11/10/11）。
    """
    a, ta = _trace(os.path.join(HERE, "sign.ir"), [-7])
    b, tb = _trace(os.path.join(HERE, "sample_opt1.ir"), [-7])

    assert a.outputs == [-1] and b.outputs == [-1]
    assert len(ta) == a.instr_cnt == 18, (len(ta), a.instr_cnt)
    assert len(tb) == b.instr_cnt == 11, (len(tb), b.instr_cnt)
    # 踪迹确实走的是 n<0 分支（只有该分支会写回 t6），不是别的路径
    assert any("'t6'" in ln for ln in tb), tb
    assert any("'t6'" in ln for ln in ta), ta
