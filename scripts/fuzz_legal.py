#!/usr/bin/env python3
"""批量生成**按文档构造即合法**的 C-- 程序，用 ./parser 跑，凡有输出即报。

用途：把"对合法代码误报"（要求书 3.1.3 明令扣分的那一类）系统挖干 —— 手写几十个程序
只能抽样，这个能跑上千个。

**本工具的可信度靠"自证"**：生成器每一步都**同时**算出 (文本, 类型)，并在每个运算符处
断言类型符合要求书假设（1/2）与文法；一旦不符**当场抛异常**，不生成那个程序。于是
"报红"只可能来自解析器 —— 否则就是生成器自己产出的非法程序（那是噪声，见下）。

刻意排除的歧义构造（其合法性本身有争议，混进来只会产生假警报）：
裸 `struct X;`、块内定义的结构体标签跨块使用、匿名结构体之间的等价、**数组整体赋值**、
两个 float 的关系运算、多行表达式、结构等价赋值、**调用后面才定义的函数**（前向引用）。
要试它们，用专门的实验分支，不要用本工具。

用法：
    python3 scripts/fuzz_legal.py [轮数] [--seed N] [--keep]
自证判别力：
    FUZZ_PARSER=<一个总是多报一条错的假解析器> python3 scripts/fuzz_legal.py 20
    —— 每一轮都必须被报出来；若为 0，说明判据没判别力。
"""
import os
import random
import subprocess
import sys
import tempfile

BASIC = ("int", "float")


class Arr:
    def __init__(self, elem, ndims):
        self.elem = elem
        self.ndims = ndims


class Struct:
    def __init__(self, name, fields):
        self.name = name
        self.fields = fields                    # [(fname, type)]


def is_arr(t):
    return isinstance(t, Arr)


def is_struct(t):
    return isinstance(t, Struct)


def base_of(t):
    return base_of(t.elem) if is_arr(t) else t


def base_str(t):
    return "struct " + t.name if is_struct(t) else t


def suffix_of(t):
    return "[3]" * t.ndims if is_arr(t) else ""


def decl(t, name):
    return "%s %s%s" % (base_str(base_of(t)), name, suffix_of(t))


def same_type(a, b):
    if is_arr(a) and is_arr(b):
        return a.ndims == b.ndims and same_type(a.elem, b.elem)
    if is_struct(a) and is_struct(b):
        return a.name == b.name
    return a == b


def key(t):
    if is_arr(t):
        return ("arr", key(t.elem), t.ndims)
    if is_struct(t):
        return ("struct", t.name)
    return t


class Scope:
    def __init__(self, parent=None):
        self.vars = {}
        self.parent = parent

    def visible(self):
        out, s = {}, self
        while s:
            for k, v in s.vars.items():
                out.setdefault(k, v)
            s = s.parent
        return out


class Bug(Exception):
    """生成器自身的不变式被破坏 —— 属于本工具的 bug，不是解析器的问题"""


class Gen:
    def __init__(self, rng):
        self.rng = rng
        self.structs = []
        self.n = 0

    def fresh(self, p):
        self.n += 1
        return "%s%d" % (p, self.n)

    # ── 顶层 ────────────────────────────────────────────────────
    def make_structs(self):
        for _ in range(self.rng.randint(0, 2)):
            st = Struct(self.fresh("S"), [])
            self.structs.append(st)
            for _ in range(self.rng.randint(1, 2)):
                t = self.rng.choice(list(BASIC)) if self.rng.random() < 0.75 \
                    else Arr(self.rng.choice(list(BASIC)), self.rng.randint(1, 2))
                st.fields.append((self.fresh("f"), t))

    def struct_decl(self, st):
        return "struct %s\n{\n%s};\n" % (
            st.name, "".join("  %s;\n" % decl(t, fn) for fn, t in st.fields))

    def seed_globals(self, scope, types):
        out = ""
        for t in types:                          # 每种形状一个 ⇒ 候选集永不为空
            name = self.fresh("g")
            scope.vars[name] = t
            out += "%s;\n" % decl(t, name)
        return out

    # ── 表达式：(文本, 类型) ────────────────────────────────────
    def exp(self, want, scope, depth, funcs):
        c = self._cands(want, scope, depth, funcs)
        if not c:
            raise Bug("无候选：%s" % (key(want),))
        text, t = self.rng.choice(c)()
        if not same_type(t, want):
            raise Bug("候选返回 %s，要的是 %s：%s" % (key(t), key(want), text))
        return text

    def _cands(self, want, scope, depth, funcs):
        c = []
        vis = scope.visible()

        if want in BASIC:
            lits = ["0", "1", "2", "7"] if want == "int" else ["0.5", "1.5", "2.25"]
            c.append(lambda w=want, L=lits: (self.rng.choice(L), w))
            for name, t in vis.items():
                if same_type(t, want):
                    c.append(lambda n=name, t=t: (n, t))
            if depth > 0:                                # 调用也要受深度约束，否则 F(F(F(…
                for fname, fret, params in funcs:        # 只含已出现过的（含自身）
                    if same_type(fret, want):
                        c.append(lambda f=fname, ps=params, r=fret:
                                 ("%s(%s)" % (f, ", ".join(
                                     self.args(ps, scope, depth - 1, funcs))), r))
            if depth > 0:
                for w in (want,):
                    c.append(lambda w=w: (self.binop(w, scope, depth, funcs), w))
                    c.append(lambda w=w: ("(%s)" % self.exp(w, scope, depth - 1, funcs), w))
                    c.append(lambda w=w: ("-(%s)" % self.exp(w, scope, depth - 1, funcs), w))
            if want == "int":
                for op in ("<", ">", "==", "!="):
                    for bt in BASIC:                     # 两侧**同型**（假设 1）
                        c.append(lambda o=op, b=bt: (self.relop(o, b, scope, depth, funcs), "int"))
                if depth > 0:
                    c.append(lambda: ("(%s) %s (%s)" % (
                        self.exp("int", scope, depth - 1, funcs), self.rng.choice(["&&", "||"]),
                        self.exp("int", scope, depth - 1, funcs)), "int"))
                    c.append(lambda: ("!(%s)" % self.exp("int", scope, depth - 1, funcs), "int"))

        if is_arr(want):
            for name, t in vis.items():
                if is_arr(t) and t.ndims >= want.ndims and same_type(t.elem, want.elem):
                    if t.ndims == want.ndims:
                        c.append(lambda n=name, t=t: (n, t))
                    else:
                        c.append(lambda n=name, d=t.ndims - want.ndims, e=want.elem:
                                 (n + "[0]" * d, Arr(e, want.ndims)))
            if depth > 0:
                bigger = Arr(want.elem, want.ndims + 1)
                c.append(lambda: ("%s[%s]" % (self.exp(bigger, scope, depth - 1, funcs),
                                              self.exp("int", scope, depth - 1, funcs)),
                                  want))

        if is_struct(want):
            for name, t in vis.items():
                if same_type(t, want):
                    c.append(lambda n=name, t=t: (n, t))
                elif is_struct(t):
                    src = next((s for s in self.structs if s.name == t.name), None)
                    if src:
                        for fn, ft in src.fields:
                            if same_type(ft, want):
                                c.append(lambda n=name, f=fn, t=ft: ("%s.%s" % (n, f), t))
            if depth > 0:
                for fname, fret, params in funcs:
                    if same_type(fret, want):
                        c.append(lambda f=fname, ps=params, r=fret:
                                 ("%s(%s)" % (f, ", ".join(
                                     self.args(ps, scope, depth - 1, funcs))), r))
        return c

    def binop(self, w, scope, depth, funcs):
        """同型算术（假设 1：int 与 float 不能相互运算）
        ⚠️ 子表达式**一律加括号**：生成器构造的是**树**，而序列化后重新解析是**左结合**的
        —— 不括号保形，`A op (B op' C)` 会被读成 `(A op B) op' C`，类型断言就拦不住混合
        运算了（曾因此产出 `int < float` 的非法程序，还被当成"解析器误报"）。"""
        return "(%s) %s (%s)" % (self.exp(w, scope, depth - 1, funcs),
                                 self.rng.choice("+-*/"),
                                 self.exp(w, scope, depth - 1, funcs))

    def relop(self, op, bt, scope, depth, funcs):
        """关系运算：两侧同型（int/int 或 float/float），结果 int（括号保形同上）"""
        return "(%s) %s (%s)" % (self.exp(bt, scope, depth - 1, funcs), op,
                                 self.exp(bt, scope, depth - 1, funcs))

    def args(self, params, scope, depth, funcs):
        return [self.exp(pt, scope, max(depth - 1, 0), funcs) for _, pt in params]

    # ── 左值：**只取标量**（数组整体赋值是有争议的构造，刻意排除）──
    def lvalue(self, scope):
        opts = []
        for name, t in scope.visible().items():
            if not is_arr(t):
                opts.append((name, t))
            if is_arr(t) and t.ndims == 1:
                # ⚠️ 只有**一维**数组取一层下标才是标量。多维数组 `x[0]` 仍是数组，
                # 那会变成"数组整体赋值"（本工具刻意排除的歧义构念）—— 曾经因为判据
                # 写成 `ndims >= 1` 而把 `g5[0] = <int>` 当成合法，产出假警报。
                opts.append(("%s[0]" % name, t.elem))
            if is_struct(t):
                src = next((s for s in self.structs if s.name == t.name), None)
                if src:
                    for fn, ft in src.fields:
                        if not is_arr(ft):
                            opts.append(("%s.%s" % (name, fn), ft))
        return self.rng.choice(opts) if opts else None

    def stmt(self, scope, ret_type, depth, funcs):
        r = self.rng.random()
        if r < 0.34 or depth <= 0:
            lv = self.lvalue(scope)
            if lv is None:
                return ";\n"
            name, t = lv
            return "%s = %s;\n" % (name, self.exp(t, scope, max(depth - 1, 0), funcs))
        if r < 0.48:
            return "if (%s)\n  %s\n" % (self.exp("int", scope, depth - 1, funcs),
                                        self.stmt(scope, ret_type, depth - 1, funcs))
        if r < 0.58:
            return "if (%s)\n  %s\nelse\n  %s\n" % (
                self.exp("int", scope, depth - 1, funcs),
                self.stmt(scope, ret_type, depth - 1, funcs),
                self.stmt(scope, ret_type, depth - 1, funcs))
        if r < 0.68:
            return "while (%s)\n  %s\n" % (self.exp("int", scope, depth - 1, funcs),
                                           self.stmt(scope, ret_type, depth - 1, funcs))
        if r < 0.80:
            return "{\n%s}\n" % self.block_body(Scope(scope), ret_type, depth - 1, funcs)
        if r < 0.90:
            return "%s;\n" % self.exp(self.rng.choice(list(BASIC)), scope,
                                      max(depth - 1, 0), funcs)
        return "return %s;\n" % self.exp(ret_type, scope, max(depth - 1, 0), funcs)

    def block_body(self, scope, ret_type, depth, funcs):
        """块：局部定义必须在语句之前（CompSt : LC DefList StmtList RC）"""
        out = ""
        for _ in range(self.rng.randint(0, 2)):
            t = self.rng.choice(list(BASIC))
            name = self.fresh("v")
            init = " = " + self.exp(t, scope, 1, funcs) if self.rng.random() < 0.6 else ""
            out += "  %s%s;\n" % (decl(t, name), init)
            scope.vars[name] = t
        for _ in range(self.rng.randint(1, 2)):
            out += "  " + self.stmt(scope, ret_type, depth, funcs).replace("\n", "\n  ").rstrip() + "\n"
        return out

    # ── 组装 ────────────────────────────────────────────────────
    def program(self):
        gscope = Scope()
        self.gscope = gscope          # 供 fuzz_inject.py 取"哪些全局名是 int 标量"
        self.make_structs()
        types = ["int", "float", Arr("int", 1), Arr("float", 1), Arr("int", 2)]
        if self.structs:
            types.append(self.structs[0])
        text = "".join(self.struct_decl(s) for s in self.structs)
        text += self.seed_globals(gscope, types)

        sigs = []
        for _ in range(self.rng.randint(1, 2)):
            ret = self.rng.choice(list(BASIC))
            params = [(self.fresh("p"), self.rng.choice(types))
                      for _ in range(self.rng.randint(0, 2))]
            sigs.append((self.fresh("F"), ret, params))
        sigs.append(("main", "int", []))

        seen = []
        for fname, ret, params in sigs:
            body = Scope(gscope)
            for pn, pt in params:
                body.vars[pn] = pt
            calls = list(seen) + [(fname, ret, params)]      # 只含已出现过的 + 自身
            ps = ", ".join(decl(t, pn) for pn, t in params)
            text += "\n%s %s(%s)\n{\n%s}\n" % (ret, fname, ps,
                                               self.block_body(body, ret, 1, calls))
            seen.append((fname, ret, params))
        return text


def run_one(src, parser, keep_dir=None):
    with tempfile.NamedTemporaryFile("w", suffix=".cmm", delete=False, dir=keep_dir) as f:
        f.write(src)
        path = f.name
    try:
        p = subprocess.run([parser, path], capture_output=True, text=True, timeout=10)
        return p.stdout, p.stderr, path
    finally:
        if keep_dir is None:
            os.unlink(path)


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
    keep_dir = tempfile.mkdtemp(prefix="fuzz_legal_") if keep else None
    bad, bug = 0, 0
    for i in range(rounds):
        try:
            src = Gen(random.Random(seed + i)).program()
        except Bug as e:
            bug += 1
            print("生成器不变式（第 %d 轮，seed=%d）：%s" % (i, seed + i, e))
            continue
        except Exception as e:
            bug += 1
            print("生成器异常（第 %d 轮，seed=%d）：%r" % (i, seed + i, e))
            continue
        try:
            out, errout, path = run_one(src, parser, keep_dir)
        except subprocess.TimeoutExpired:
            bad += 1
            print("⚠ 超时（第 %d 轮，seed=%d）" % (i, seed + i))
            continue
        if errout.strip() or out.strip():
            bad += 1
            print("⚠ 合法程序被误报（第 %d 轮，seed=%d）：\n%s%s──── 程序 ────\n%s"
                  % (i, seed + i, out, errout, src))
    print("-" * 60)
    print("跑了 %d 轮：疑似误报 %d，生成器自身问题 %d" % (rounds, bad, bug))
    if keep_dir:
        print("程序留在 %s" % keep_dir)
    return 1 if (bad or bug) else 0


if __name__ == "__main__":
    sys.exit(main())
