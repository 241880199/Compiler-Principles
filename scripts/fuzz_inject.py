#!/usr/bin/env python3
"""把**要求书自己钉住的错误**注入到"构造即合法"的生成程序里，校验实测输出。

为什么需要它：`fuzz_legal.py` 的判据是"合法程序输出必须为空" —— 它**证明不了**
错误程序报出的 (类型, 行号) 对不对。而 (类型, 行号) 恰恰是判分**唯一**的判据，
并且它依赖实验一继承下来的那一层（词法单元的 `curTokenLine`、遍历顺序、同行去重）。

判据**取自要求书的样例**，不掺实现者的判断：
  · 把某行 `<名字> = <int 表达式>;`（名字是 in-scope 的 int 标量）改成 `<名字> = 1.5;`
        ⇒ 要求书样例 5：`Error type 5`，行号 = **那一行**
  · 把任意赋值行改成 `<名字> = zz_undef;`
        ⇒ 要求书样例 1：`Error type 1`，行号 = **那一行**（未定义名是类型无关的，故意选它）
两处注入都必须**只有这一条**错误（原程序是合法的 ⇒ 注入后唯一的错就是注入的那个），
所以这里比对的是**完整输出**的 (类型, 行号) 序列，不是"有没有输出"。

用法：
    python3 scripts/fuzz_inject.py [轮数] [--seed N] [--keep]
自证判别力（判据必须能报红）：
    FUZZ_PARSER=<一个把行号统统 +1 的假解析器> python3 scripts/fuzz_inject.py 20
    —— 每一轮都必须被报出来。
"""
import os
import re
import random
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fuzz_legal as FL                                   # noqa: E402

ASSIGN = re.compile(r"^(\s*)([A-Za-z_]\w*)\s*=\s*(.+);\s*$")


def inject(prog, types, rng):
    """随机挑一个赋值行，注入**要求书样例钉住的**一个错误。

    types: 名字 → 类型（生成器的全局作用域）。B 是块级 int 标量，用于需要"任意标量"的注入。
    返回 (新程序, 期望的 (类型, 行号) 列表)。
    """
    lines = prog.split("\n")
    sites = [(i, m.group(2)) for i, ln in enumerate(lines)
             for m in [ASSIGN.match(ln)] if m]
    if not sites:
        return None
    i, name = rng.choice(sites)
    indent = ASSIGN.match(lines[i]).group(1)
    int_scalars = [n for n, t in types.items() if t == "int"]
    scalars = [n for n, t in types.items() if t in ("int", "float")]
    int_arrays = [n for n, t in types.items()
                  if isinstance(t, FL.Arr) and t.ndims == 1 and t.elem == "int"]
    kinds = ["undef_name"]                                  # 样例 1（类型无关，永远可用）
    if name in int_scalars:
        kinds.append("int_gets_float")                      # 样例 5
        kinds.append("call_a_scalar")                       # 样例 11（`i(10)`）
        kinds.append("subscript_a_scalar")                  # 样例 10（`i[0]`）
        kinds.append("field_of_a_scalar")                   # 样例 13（`i.x`）
        kinds.append("const_lhs")                           # 样例 6（`10 = i`）
    if int_arrays and scalars:
        kinds.append("nonint_subscript")                    # 样例 12（`a[1.5]`）
    if scalars:
        kinds.append("undefined_function")                  # 样例 2（`inc(i)`）
    kind = rng.choice(kinds)
    sc = rng.choice(scalars) if scalars else "zz_undef"
    if kind == "int_gets_float":
        lines[i] = "%s%s = 1.5;" % (indent, name)
        want = [(5, i + 1)]
    elif kind == "undefined_function":
        lines[i] = "%szz_undef_fn(%s);" % (indent, sc)
        want = [(2, i + 1)]
    elif kind == "call_a_scalar":
        lines[i] = "%s%s(10);" % (indent, name)
        want = [(11, i + 1)]
    elif kind == "subscript_a_scalar":
        lines[i] = "%s%s[0];" % (indent, name)
        want = [(10, i + 1)]
    elif kind == "field_of_a_scalar":
        lines[i] = "%s%s.x;" % (indent, name)
        want = [(13, i + 1)]
    elif kind == "const_lhs":
        lines[i] = "%s10 = %s;" % (indent, sc)
        want = [(6, i + 1)]
    elif kind == "nonint_subscript":
        lines[i] = "%s%s[1.5] = 10;" % (indent, rng.choice(int_arrays))
        want = [(12, i + 1)]
    else:
        lines[i] = "%s%s = zz_undef;" % (indent, name)
        want = [(1, i + 1)]
    return "\n".join(lines), want


def pairs(out):
    got = []
    for ln in out.splitlines():
        m = re.match(r"^Error type (\d+) at Line (\d+):", ln)
        if m:
            got.append((int(m.group(1)), int(m.group(2))))
    return got


def main():
    argv = [a for a in sys.argv[1:] if not a.startswith("--")]
    rounds = int(argv[0]) if argv else 200
    seed = int(sys.argv[sys.argv.index("--seed") + 1]) if "--seed" in sys.argv else 0
    keep = "--keep" in sys.argv
    parser = os.environ.get("FUZZ_PARSER") or os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", "parser"))
    if not os.path.exists(parser):
        print("先 make（找不到 %s）" % parser, file=sys.stderr)
        return 1

    bad, bug, no_site = 0, 0, 0
    for i in range(rounds):
        rng = random.Random(seed + i)
        try:
            g = FL.Gen(rng)
            prog = g.program()
            res = inject(prog, getattr(g, "gscope", FL.Scope()).vars, rng)
        except Exception as e:
            bug += 1
            print("生成器/注入器异常（第 %d 轮，seed=%d）：%r" % (i, seed + i, e))
            continue
        if res is None:
            no_site += 1
            continue
        src, want = res
        with tempfile.NamedTemporaryFile("w", suffix=".cmm", delete=not keep,
                                         newline="") as f:
            f.write(src)
            f.flush()
            r = subprocess.run([parser, f.name], capture_output=True, text=True, timeout=10)
        got = pairs(r.stdout)
        if got == want and not r.stderr.strip():
            continue
        bad += 1
        print("⚠ (类型,行号) 与要求书的规则不符（第 %d 轮，seed=%d）\n"
              "   期望 %s   实测 %s%s\n──── 程序 ────\n%s"
              % (i, seed + i, want, got,
                 "（stderr 非空！%r）" % r.stderr if r.stderr.strip() else "", src))
    print("-" * 60)
    print("跑了 %d 轮：不符 %d，无注入点 %d，工具自身问题 %d" % (rounds, bad, no_site, bug))
    return 1 if (bad or bug) else 0


if __name__ == "__main__":
    sys.exit(main())
