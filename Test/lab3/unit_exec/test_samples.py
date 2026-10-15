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

    实测（真机口径，跳转命中标号不计）：原始版 sign.ir = 15 条，优化版 sample_opt1.ir = 10 条
    （见下一条的独立核对）。
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


def _count_dispatch(path, inputs):
    """跑一遍，并把 `_step` 的**调用次数**数出来（独立于 `Result.instr_cnt`）。

    `_execute` 主循环每轮派发一次 `_step`；这就是"应被计数"的**独立度量**。
    这里用 monkeypatch 包一层计数，与 `_execute` 自己 `st.instr_cnt += 1` 的那一处
    **机制上分离**：若有人把计数从主循环挪进 `_step` 的某个分支（例如"跳转不算一条"），
    派发次数与 `instr_cnt` 就会分叉。
    """
    import irsim_ref
    n = [0]
    real = irsim_ref._step

    def counting(st, code, ip):
        n[0] += 1
        return real(st, code, ip)

    irsim_ref._step = counting
    try:
        r = irsim_ref.run(path, inputs=list(inputs))
    finally:
        irsim_ref._step = real
    return r, n[0]


def test_instr_cnt_matches_dispatch_count_and_absolute_value():
    """`instr_cnt` 必须等于主循环的**派发次数**（独立度量），且**绝对值**钉死。

    为什么重写这条（Task 4 审查的 Important）：旧版断言的是"踪迹行数 == `instr_cnt`"，
    而 `irsim_ref._execute` 里 `st.instr_cnt += 1` 与 `print(...st.instr_cnt...)`
    在**同一个块**里、每轮恰一行 ⇒ 那个等式**恒成立**，无论计数规则怎么错都没有判别力
    （是一条**同义反复**的假绿）。旧 docstring 说的"二者由同一处 `+1` 驱动，故相等即口径正确"
    是**错的**，已删。

    现在两件事一起做：
    1. **独立**数 `_step` 的调用次数，断言 == `instr_cnt`（抓"计数被挪到某个分支"这类错）；
    2. 钉住**绝对值** 15 / 10 —— 这一条才真正咬住真机口径"**跳转命中的 LABEL 不计**"
       （旧口径下 `sign.ir` 是 18、`sample_opt1.ir` 是 11）。

    n=-7 时两条都走 `n < 0` 那一支（`sign.ir` 三条路径 15/15/10；`sample_opt1.ir` 10/9/9）。
    """
    a, da = _count_dispatch(os.path.join(HERE, "sign.ir"), [-7])
    b, db = _count_dispatch(os.path.join(HERE, "sample_opt1.ir"), [-7])

    assert a.outputs == [-1] and b.outputs == [-1]
    assert da == a.instr_cnt == 15, (da, a.instr_cnt)
    assert db == b.instr_cnt == 10, (db, b.instr_cnt)
    # 踪迹确实走的是 n<0 分支（只有该分支会写回 t6），不是别的路径
    _, ta = _trace(os.path.join(HERE, "sign.ir"), [-7])
    _, tb = _trace(os.path.join(HERE, "sample_opt1.ir"), [-7])
    assert any("'t6'" in ln for ln in tb), tb
    assert any("'t6'" in ln for ln in ta), ta
