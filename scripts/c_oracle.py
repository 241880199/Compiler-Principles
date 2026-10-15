#!/usr/bin/env python3
"""独立的期望输出 oracle：把 .cmm 程序当 C 编译运行，得到"真值"参考输出。

    python -I scripts/c_oracle.py <x.cmm> <x.in>      # stdout：每行一个整数（WRITE 的值）

为什么需要它（spec §9.2）：`.cmm` 基本是 C 的子集 —— 把 `read`/`write` 换成两个垫片、
把 `/` 换成向下取整的 `__div`，就能用 gcc 编译**同一份程序**、跑出**完全不经过我们编译器**
的真实输出。这条链是实验一/二"绿色掩盖"的根治点：期望值不再由被测对象自己产生。

两条语义必须与虚拟机对齐（缺一不可）：

1. `/` → `__div(a, b)`：真机在装载期把 `/` 改写成 Python 的 `//`（**向下取整**，spec §3.7-1、
   暂定取舍 P1），而 C 的 `/` 是**向零截断**，两者在负数上不同（`-7/2` 真机是 `-4`、C 是 `-3`）。
   只有包成函数、写对向下取整的算式，期望值才与真机一致。
2. `read()` → `__read()`（依次取 .in 里的整数）、`write(e);` → `printf("%d\\n", (int)(e));`。

实现用**词法 + 递归下降**而不是纯正则替换，原因是 `/` 的改写需要正确的**操作数边界**：
`a * b / c` 的正确改写是 `__div(a * b, c)`（`/` 与 `*` 同级、左结合），
纯文本替换会得到 `a * __div(b, c)` —— 值不同。表达式之外的部分（声明、函数、数组、
结构体等 spec 替换表里标"原样"的行）逐 token 原样透传。语法依据仓库 `src/syntax.y`（C-- 文法）。

运行环境：Windows 侧没有 gcc，本脚本自动经 WSL（默认 Ubuntu-22.04，可用 `CMM_WSL_DIST` 覆盖）
调用 gcc；若本机（WSL 内）直接有 gcc 则本地编译。临时文件只落在 `build/oracle/`（已 gitignore）。
"""

import os
import re
import shutil
import subprocess
import sys

WSL_DIST = os.environ.get("CMM_WSL_DIST", "Ubuntu-22.04")
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD_DIR = os.path.join(REPO, "build", "oracle")


# ---------------------------------------------------------------------------
# C 垫片：与虚拟机语义对齐的 read / 向下取整除法
# ---------------------------------------------------------------------------
SHIM = r'''#include <stdio.h>
#include <stdlib.h>
static int __in[4096];
static int __nin = 0, __ip = 0, __inited = 0;
static void __load(void) {
    const char *path;
    FILE *f;
    if (__inited) return;
    __inited = 1;
    path = getenv("CMM_IN");
    if (!path) return;
    f = fopen(path, "r");
    if (!f) return;
    while (__nin < 4096 && fscanf(f, "%d,", &__in[__nin]) == 1) __nin++;
    fclose(f);
}
static int __read(void) { __load(); return __ip < __nin ? __in[__ip++] : 0; }
/* 与虚拟机一致：'/' 被改写为向下取整（spec 3.7-1、暂定取舍 P1）。 */
static int __div(int a, int b) { return (a / b) - ((a % b != 0) && ((a < 0) != (b < 0))); }
'''


# ---------------------------------------------------------------------------
# 词法
# ---------------------------------------------------------------------------
_TOKEN_RE = re.compile(r"""
    (?P<ws>\s+)
  | (?P<num>\d+\.\d+|\.\d+|\d+)
  | (?P<id>[A-Za-z_]\w*)
  | (?P<relop><=|>=|==|!=|<|>)
  | (?P<logop>&&|\|\|)
  | (?P<op>[-+*/()\[\]{};=,!&|.])
  | (?P<other>.)
""", re.VERBOSE)


class ParseError(Exception):
    pass


def tokenize(src):
    toks = []
    for m in _TOKEN_RE.finditer(src):
        if m.lastgroup == "ws":
            continue
        toks.append((m.lastgroup, m.group()))
    toks.append(("eof", ""))
    return toks


# ---------------------------------------------------------------------------
# 递归下降（C-- 文法；表达式产出 C 代码串，其余部分原样透传 token）
# ---------------------------------------------------------------------------
class _Parser:
    def __init__(self, toks):
        self.toks = toks
        self.i = 0
        self.out = []           # 输出片段：token 文本 或 表达式代码串

    # -- 低层辅助 --
    def kind(self, k=0):
        return self.toks[min(self.i + k, len(self.toks) - 1)][0]

    def text(self, k=0):
        return self.toks[min(self.i + k, len(self.toks) - 1)][1]

    def at(self, s):
        return self.toks[self.i][1] == s

    def next(self):
        t = self.toks[self.i]
        self.i += 1
        return t

    def take(self, s=None):
        t = self.next()
        if s is not None and t[1] != s:
            raise ParseError("期待 %r，实得 %r" % (s, t[1]))
        return t

    def emit(self, s):
        self.out.append(s)

    # ---------- 顶层：ExtDefList ----------
    def parse_program(self):
        while self.kind() != "eof":
            self.parse_extdef()

    def parse_extdef(self):
        self.parse_specifier()
        if self.at(";"):
            self.emit(self.take(";")[1])
            return
        if self.kind() == "id" and self.text(1) == "(":
            self.parse_fundec()
            if self.at("{"):
                self.parse_compst()
            elif self.at(";"):
                self.emit(self.take(";")[1])        # 函数声明（本版本不支持，透传）
            else:
                raise ParseError("ExtDef：FunDec 之后期待 '{' 或 ';'，实得 %r" % self.text())
        else:
            self.parse_extdeclist()
            self.emit(self.take(";")[1])

    def parse_specifier(self):
        if self.at("struct"):
            self.parse_struct_specifier()
        elif self.kind() == "id" and self.text() in ("int", "float"):
            self.emit(self.next()[1])
        else:
            raise ParseError("Specifier：实得 %r" % self.text())

    def parse_struct_specifier(self):
        self.emit(self.take("struct")[1])
        if self.kind() == "id":                     # OptTag / Tag
            self.emit(self.next()[1])
        if self.at("{"):
            self.emit(self.take("{")[1])
            self.parse_deflist()
            self.emit(self.take("}")[1])

    def parse_vardec(self):
        if self.kind() != "id":
            raise ParseError("VarDec：期待标识符，实得 %r" % self.text())
        self.emit(self.next()[1])
        while self.at("["):
            self.emit(self.take("[")[1])
            if self.kind() != "num":
                raise ParseError("数组维度应为整型常数，实得 %r" % self.text())
            self.emit(self.next()[1])
            self.emit(self.take("]")[1])

    def parse_extdeclist(self):
        self.parse_vardec()
        while self.at(","):
            self.emit(self.take(",")[1])
            self.parse_vardec()

    def parse_fundec(self):
        self.emit(self.next()[1])                   # ID
        self.emit(self.take("(")[1])
        if not self.at(")"):
            self.parse_varlist()
        self.emit(self.take(")")[1])

    def parse_varlist(self):
        self.parse_paramdec()
        while self.at(","):
            self.emit(self.take(",")[1])
            self.parse_paramdec()

    def parse_paramdec(self):
        self.parse_specifier()
        self.parse_vardec()

    def parse_deflist(self):
        while self.text() in ("int", "float", "struct"):
            self.parse_def()

    def parse_def(self):
        self.parse_specifier()
        self.parse_declist()
        self.emit(self.take(";")[1])

    def parse_declist(self):
        self.parse_dec()
        while self.at(","):
            self.emit(self.take(",")[1])
            self.parse_dec()

    def parse_dec(self):
        self.parse_vardec()
        if self.at("="):
            self.emit(self.take("=")[1])
            self.emit(self.parse_exp())

    def parse_compst(self):
        self.emit(self.take("{")[1])
        self.parse_deflist()
        while not self.at("}"):
            self.parse_stmt()
        self.emit(self.take("}")[1])

    def parse_stmt(self):
        t = self.text()
        if t == "{":
            self.parse_compst()
        elif t == "return":
            self.emit(self.take("return")[1])
            self.emit(self.parse_exp())
            self.emit(self.take(";")[1])
        elif t == "if":
            self.emit(self.take("if")[1])
            self.emit(self.take("(")[1])
            self.emit(self.parse_exp())
            self.emit(self.take(")")[1])
            self.parse_stmt()
            if self.at("else"):
                self.emit(self.take("else")[1])
                self.parse_stmt()
        elif t == "while":
            self.emit(self.take("while")[1])
            self.emit(self.take("(")[1])
            self.emit(self.parse_exp())
            self.emit(self.take(")")[1])
            self.parse_stmt()
        elif t == ";":
            self.emit(self.take(";")[1])            # 空语句：文法里没有，容错透传
        else:
            self.emit(self.parse_exp())
            self.emit(self.take(";")[1])

    # ---------- 表达式：返回 C 代码串 ----------
    def parse_exp(self):
        return self.parse_assign()

    def parse_assign(self):
        left = self.parse_or()
        if self.at("="):
            self.take("=")
            return "(%s = %s)" % (left, self.parse_assign())
        return left

    def parse_or(self):
        left = self.parse_and()
        while self.at("||"):
            self.take("||")
            left = "(%s || %s)" % (left, self.parse_and())
        return left

    def parse_and(self):
        left = self.parse_rel()
        while self.at("&&"):
            self.take("&&")
            left = "(%s && %s)" % (left, self.parse_rel())
        return left

    def parse_rel(self):
        left = self.parse_add()
        while self.kind() == "relop":
            op = self.next()[1]
            left = "(%s %s %s)" % (left, op, self.parse_add())
        return left

    def parse_add(self):
        left = self.parse_mul()
        while self.at("+") or self.at("-"):
            op = self.next()[1]
            left = "(%s %s %s)" % (left, op, self.parse_mul())
        return left

    def parse_mul(self):
        left = self.parse_unary()
        while self.at("*") or self.at("/"):
            if self.at("*"):
                self.take("*")
                left = "(%s * %s)" % (left, self.parse_unary())
            else:
                self.take("/")
                # 真机的 '/' 是向下取整（Python //），包成 __div 才有正确边界与语义
                left = "__div(%s, %s)" % (left, self.parse_unary())
        return left

    def parse_unary(self):
        if self.at("-"):
            self.take("-")
            return "(-%s)" % self.parse_unary()
        if self.at("!"):
            self.take("!")
            return "(!%s)" % self.parse_unary()
        return self.parse_postfix()

    def parse_postfix(self):
        s = self.parse_primary()
        while True:
            if self.at("["):
                self.take("[")
                idx = self.parse_exp()
                self.take("]")
                s = "%s[%s]" % (s, idx)
            elif self.at("."):
                self.take(".")
                s = "%s.%s" % (s, self.next()[1])
            else:
                break
        return s

    def parse_primary(self):
        k = self.kind()
        if k == "num":
            return self.next()[1]
        if k == "id":
            name = self.next()[1]
            if self.at("("):
                self.take("(")
                args = []
                if not self.at(")"):
                    args.append(self.parse_exp())
                    while self.at(","):
                        self.take(",")
                        args.append(self.parse_exp())
                self.take(")")
                if name == "read":
                    if args:
                        raise ParseError("read() 不应带实参")
                    return "__read()"
                if name == "write":
                    if len(args) != 1:
                        raise ParseError("write 需要恰好 1 个实参，实得 %d 个" % len(args))
                    return 'printf("%%d\\n", (int)(%s))' % args[0]
                return "%s(%s)" % (name, ", ".join(args))
            return name
        if self.at("("):
            self.take("(")
            e = self.parse_exp()
            self.take(")")
            return "(%s)" % e
        raise ParseError("Primary：意外 token %r" % self.text())


def transform(src):
    """把 .cmm 源码转换成等价的 C 源码（含垫片）。"""
    p = _Parser(tokenize(src))
    p.parse_program()
    if p.kind() != "eof":
        raise ParseError("解析结束后仍有残留 token：%r" % p.text())
    body = " ".join(p.out)
    return SHIM + "\n" + body + "\n"


# ---------------------------------------------------------------------------
# 编译 + 运行
# ---------------------------------------------------------------------------
def _shq(s):
    return "'" + s.replace("'", "'\\''") + "'"


def _to_wsl_path(win_path):
    p = os.path.abspath(win_path).replace("\\", "/")
    m = re.match(r"^([A-Za-z]):/(.*)$", p)
    if m:
        return "/mnt/%s/%s" % (m.group(1).lower(), m.group(2))
    return p


def _run_native(c_file, in_file):
    exe = c_file[:-2] + ".bin"
    cc = subprocess.run(["gcc", "-w", "-o", exe, c_file],
                        capture_output=True, text=True)
    if cc.returncode != 0:
        raise SystemExit("oracle：gcc 编译失败：\n%s" % cc.stderr)
    env = dict(os.environ, CMM_IN=os.path.abspath(in_file))
    r = subprocess.run([exe], capture_output=True, text=True, env=env)
    if r.returncode != 0:
        raise SystemExit("oracle：程序异常退出 rc=%d：\n%s" % (r.returncode, r.stderr))
    return r.stdout


def _run_via_wsl(c_file, in_file):
    c_wsl = _to_wsl_path(c_file)
    exe_wsl = c_wsl[:-2] + ".bin"
    in_wsl = _to_wsl_path(in_file)
    script = "gcc -w -o %s %s && CMM_IN=%s %s" % (
        _shq(exe_wsl), _shq(c_wsl), _shq(in_wsl), _shq(exe_wsl))
    env = dict(os.environ, MSYS_NO_PATHCONV="1")
    # 必须用**非登录** shell（`bash -c`，不可用 `bash -lc`）：`-l` 会 source
    # ~/.bash_profile / ~/.profile，只要它们有任何 stdout 输出，就会被捕获进 .exp，
    # 得到一份"被污染但看着像正常数字"的期望值 —— 最难发现的一类静默错误。
    r = subprocess.run(["wsl.exe", "-d", WSL_DIST, "--", "bash", "-c", script],
                       capture_output=True, text=True, encoding="utf-8",
                       errors="replace", env=env)
    if r.returncode != 0:
        raise SystemExit("oracle：WSL 里 gcc/运行失败 rc=%d：\n%s\n%s"
                         % (r.returncode, r.stdout, r.stderr))
    return r.stdout


def generate(cmm_path, in_path):
    """返回 .cmm 在给定输入下的期望输出字符串（每行一个整数）。"""
    with open(cmm_path, "r", encoding="utf-8") as f:
        src = f.read()
    csrc = transform(src)
    os.makedirs(BUILD_DIR, exist_ok=True)
    # 生成的 .c/.bin 只按 basename 命名 ⇒ 前提：用例名在 Test/lab3/** 内必须唯一
    # （否则不同目录下的同名 .cmm 会互相覆盖 build/oracle/ 里的中间件）。
    stem = os.path.splitext(os.path.basename(cmm_path))[0]
    c_file = os.path.join(BUILD_DIR, stem + ".c")
    with open(c_file, "w", encoding="utf-8") as f:
        f.write(csrc)
    if os.name != "nt" and shutil.which("gcc"):
        return _run_native(c_file, in_path)
    return _run_via_wsl(c_file, in_path)


def main(argv):
    if len(argv) != 2:
        sys.stderr.write("用法：python -I scripts/c_oracle.py <x.cmm> <x.in>\n")
        return 2
    # Windows 的 Python 会把 stdout 文本模式下的 '\n' 翻成 '\r\n'；.exp 必须是 LF
    # （.gitattributes 全局 eol=lf；run_lab3.sh 在 WSL 里与参考机的 LF 输出逐行 diff）。
    try:
        sys.stdout.reconfigure(newline="\n")
    except Exception:
        pass
    sys.stdout.write(generate(argv[0], argv[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
