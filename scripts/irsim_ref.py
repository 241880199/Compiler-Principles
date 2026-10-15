"""irsim 的无头参考实现。

语义逐条来自对 作业三/irsim.pyc（版本 2.0.2，编译于 2023-02-02）的字节码反解，
与该设计文档 §3 一一对应。本文件**不依赖 PyQt、不依赖 Python 3.8**。

**异常契约（调用方必读）**：语法错误抛 `IRSyntaxError`；但**操作数里的整数解析失败
会原样漏出 `ValueError`**（例：`DEC a abc`、`#abc`）。
这不是疏漏，而是**忠实**：真机 `sanity_check` 捕的正是 `(IRSyntaxError, ValueError)`。
⇒ 调用方若只 `except IRSyntaxError` 会漏掉这一类；**不要"顺手修"成 `IRSyntaxError`**，
测试 `test_int_parse_failure_leaks_valueerror` 专门钉住这条行为。
"""

import sys

RELOPS = (">", "<", ">=", "<=", "==", "!=")
ARITHOPS = ("+", "-", "*", "/")


class IRSyntaxError(Exception):
    pass


class DuplicatedLabelError(Exception):
    pass


class DuplicatedVariableError(Exception):
    pass


class UndefinedLabelError(Exception):
    pass


class Program:
    def __init__(self):
        self.codes = []          # list[tuple]
        self.labels = {}         # name -> 在当前 codes 中的下标（LABEL 与 FUNCTION 都登记）
        self.funcs = {}          # 函数名 -> 函数体变量名列表（按出现顺序）
        self.entry = -1
        self.symtab = {}         # 名 -> [地址, 大小, 是否数组]
        self.offset = 0
        self.current_function = None


def load_ir(path):
    """解析 .ir 文件。与真实 loadFile 一致：逐行、空白行整行跳过、每行一条指令。"""
    p = Program()
    with open(path, "r") as fp:
        for raw in fp:
            if raw.isspace():
                continue
            sanity_check(p, raw, len(p.codes))
    if p.entry == -1:
        raise IRSyntaxError("Cannot find program entrance (no FUNCTION main : line)")
    label_check(p)
    if p.offset > 1048576:
        raise IRSyntaxError("offset exceeds 1MiB")
    return p


def _table_insert(p, var, size=4, is_array=False):
    if var.isdigit():
        raise IRSyntaxError("bare number used as operand: %r" % var)
    if var[0] in "&*":
        var = var[1:]
    elif var[0] == "#":
        int(var[1:])            # 与真实实现一致：非数字会抛 ValueError
        return
    if var in p.symtab:
        return
    if p.current_function is None:
        raise IRSyntaxError("line does not belong to any function")
    p.funcs.setdefault(p.current_function, []).append(var)
    if p.current_function == "main":
        p.symtab[var] = [p.offset, size, is_array]
        p.offset += size
    else:
        p.symtab[var] = [-1, size, is_array]


def sanity_check(p, line, lineno):
    strs = line.split()
    if strs[0] in ("LABEL", "FUNCTION"):
        if len(strs) != 3 or strs[2] != ":":
            raise IRSyntaxError("bad label line: %r" % line)
        if strs[1] in p.labels:
            raise DuplicatedLabelError(strs[1])
        p.labels[strs[1]] = lineno
        if strs[1] == "main":
            if strs[0] == "LABEL":
                raise IRSyntaxError("LABEL main is not allowed")
            p.entry = lineno
        if strs[0] == "FUNCTION":
            p.current_function = strs[1]
            p.funcs[strs[1]] = []
        p.codes.append(("LABEL", strs[1]))
        return

    if p.current_function is None:
        raise IRSyntaxError("line %d does not belong to any function" % lineno)

    if strs[0] == "GOTO":
        if len(strs) != 2:
            raise IRSyntaxError(line)
        p.codes.append(("GOTO", strs[1]))
    elif strs[0] in ("RETURN", "READ", "WRITE", "ARG", "PARAM"):
        if len(strs) != 2:
            raise IRSyntaxError(line)
        if strs[0] in ("READ", "PARAM") and not strs[1][0].isalpha():
            raise IRSyntaxError(line)
        _table_insert(p, strs[1])
        p.codes.append((strs[0], strs[1]))
    elif strs[0] == "DEC":
        if len(strs) != 3 or int(strs[2]) % 4 != 0:
            raise IRSyntaxError(line)
        if strs[1] in p.symtab:
            raise DuplicatedVariableError(strs[1])
        _table_insert(p, strs[1], int(strs[2]), True)
        p.codes.append(("DEC",))
    elif strs[0] == "IF":
        if len(strs) != 6 or strs[4] != "GOTO" or strs[2] not in RELOPS:
            raise IRSyntaxError(line)
        _table_insert(p, strs[1])
        _table_insert(p, strs[3])
        p.codes.append(("IF", strs[1], strs[2], strs[3], strs[5]))
    elif len(strs) >= 3 and strs[1] == ":=":
        if strs[0][0] in "&#":
            raise IRSyntaxError(line)
        _table_insert(p, strs[0])
        if strs[2] == "CALL":
            if len(strs) != 4:
                raise IRSyntaxError(line)
            p.codes.append(("CALL", strs[0], strs[3]))
        elif len(strs) == 3:
            _table_insert(p, strs[2])
            p.codes.append(("MOV", strs[0], strs[2]))
        elif len(strs) == 5 and strs[3] in ARITHOPS:
            _table_insert(p, strs[2])
            _table_insert(p, strs[4])
            op = "//" if strs[3] == "/" else strs[3]
            p.codes.append(("ARITH", strs[0], strs[2], op, strs[4]))
        else:
            raise IRSyntaxError(line)
    else:
        raise IRSyntaxError(line)


def label_check(p):
    """所有被引用的标号都必须有定义。

    ⚠️ **保真度声明：本函数不是从 irsim.pyc 反解出来的。** 真机确有 `labelCheck` 与
    `UndefinedLabelError`，但它的实现**未被反解确证**（其 code 对象的常量表里没有
    `'GOTO'`/`'IF'`/`'CALL'` 这类关键字，与下面按关键字比较的写法形状不同）。
    这里保留的是一层**我们自己的防御性检查**，目的是保证生成的 IR 自洽。
    对本实验**无实害**：我们生成的 IR 里每个被引用的标号都有定义，故本检查永不触发。
    真机行为若与此不同，也不会影响任何判据。
    """
    for c in p.codes:
        if c[0] == "GOTO" and c[1] not in p.labels:
            raise UndefinedLabelError(c[1])
        if c[0] == "IF" and c[4] not in p.labels:
            raise UndefinedLabelError(c[4])
        if c[0] == "CALL" and c[2] not in p.labels:
            raise UndefinedLabelError(c[2])


# ============================================================================
#  执行引擎（设计文档 §3）
# ============================================================================

class Result:
    def __init__(self):
        self.outputs = []
        self.instr_cnt = 0
        self.status = "ok"       # 'ok' | 'memory' | 'pc'


MEM_WORDS = 262144               # 262144 个字 = 1 MiB，地址按字节计（§3.5）


class _State:
    def __init__(self, p, inputs):
        self.p = p
        self.inputs = inputs
        self.mem = [0] * MEM_WORDS
        self.symtab = {k: list(v) for k, v in p.symtab.items()}
        self.call_stack = []          # (ip, funcname, old_addrs, old_offset)
        self.arg_stack = []
        self.offset = p.offset
        self.instr_cnt = 0
        self.result = Result()

    # ---- 取值（spec §3.3） ----
    def get_value(self, var):
        if var[0] == "#":
            return int(var[1:])
        if var[0] == "&":
            return self.symtab[var[1:]][0]
        if var[0] == "*":
            return self.mem[self.mem[self.symtab[var[1:]][0] // 4] // 4]
        return self.mem[self.symtab[var][0] // 4]

    def store_var(self, var, value):
        if var[0] == "*":
            self.mem[self.mem[self.symtab[var[1:]][0] // 4] // 4] = value
        else:
            self.mem[self.symtab[var][0] // 4] = value

    def new_addr(self, size):
        a = self.offset
        self.offset += size
        return a


def _execute(st, trace):
    p = st.p
    ip = p.entry
    while ip >= 0:
        if ip >= len(p.codes):
            st.result.status = "pc"
            return
        code = p.codes[ip]
        st.instr_cnt += 1
        if trace:
            print("%4d  %-40s  ip=%d" % (st.instr_cnt, code, ip), file=sys.stderr)
        try:
            nxt = _step(st, code, ip)
        except IndexError:
            st.result.status = "memory"
            return
        if nxt is None:          # RETURN 且调用栈为空 => 正常结束
            return
        ip = nxt


def _step(st, code, ip):
    p = st.p
    op = code[0]

    if op == "LABEL":
        return ip + 1
    if op == "READ":
        v = st.inputs.pop(0) if st.inputs else 0
        st.store_var(code[1], v)
        return ip + 1
    if op == "WRITE":
        st.result.outputs.append(st.get_value(code[1]))
        return ip + 1
    if op == "GOTO":
        return p.labels[code[1]]
    if op == "IF":
        x, relop, y, z = code[1], code[2], code[3], code[4]
        v1, v2 = st.get_value(x), st.get_value(y)
        if _eval_relop(v1, relop, v2):
            return p.labels[z]
        return ip + 1
    if op == "MOV":
        st.store_var(code[1], st.get_value(code[2]))
        return ip + 1
    if op == "ARITH":
        v1, v2 = st.get_value(code[2]), st.get_value(code[4])
        # ★ 目标**不走 `store_var`**：真机此处就是 `mem[symTable[code[1]][0] // 4] = ...`，
        #   不做 `*` 解引用（`tableInsert` 已把 `*` 剥掉，表里只有 `x`）。
        #   ⇒ `*x := a op b` 在此抛 **KeyError**；真机不捕它，我们也不捕（§3.7-2）。
        #   注意顺序与真机同构：Python 先算 RHS 再解析赋值目标。
        st.mem[st.symtab[code[1]][0] // 4] = _eval_arith(v1, code[3], v2)
        return ip + 1
    if op == "DEC":
        return ip + 1
    if op == "ARG":
        st.arg_stack.append(st.get_value(code[1]))
        return ip + 1
    if op == "PARAM":
        st.store_var(code[1], st.arg_stack.pop())
        return ip + 1
    if op == "CALL":
        dst, fname = code[1], code[2]
        old_addrs = {}
        old_offset = st.offset
        for k in p.funcs.get(fname, []):
            old_addrs[k] = list(st.symtab[k])
            st.symtab[k] = [st.new_addr(st.symtab[k][1]), st.symtab[k][1], st.symtab[k][2]]
        st.call_stack.append((ip + 1, dst, old_addrs, old_offset))
        return p.labels[fname]
    if op == "RETURN":
        if not st.call_stack:
            return None
        rv = st.get_value(code[1])
        ret_ip, dst, old_addrs, old_offset = st.call_stack.pop()
        for k, v in old_addrs.items():
            st.symtab[k] = v
        st.offset = old_offset
        st.store_var(dst, rv)
        return ret_ip
    raise IRSyntaxError("unhandled %r" % (code,))


def _eval_relop(a, relop, b):
    if relop == ">":  return a > b
    if relop == "<":  return a < b
    if relop == ">=": return a >= b
    if relop == "<=": return a <= b
    if relop == "==": return a == b
    if relop == "!=": return a != b
    raise IRSyntaxError(relop)


def _eval_arith(a, op, b):
    # 与真实实现一致：装载期已把 '/' 改写为 '//'，故这里 Integer 除法向下取整
    if op == "+":  return a + b
    if op == "-":  return a - b
    if op == "*":  return a * b
    if op == "//": return a // b
    raise IRSyntaxError(op)


def run(path, inputs=(), trace=False):
    """执行一份 .ir。

    返回 `Result`（`.outputs` / `.instr_cnt` / `.status`）。
    `status` 为 `'ok'`（RETURN 时调用栈已空 = 正常结束）、`'memory'`（越界）或
    `'pc'`（ip 越过 codes 末尾 —— 说明 IR 没有正常的 RETURN 出口）。
    """
    p = load_ir(path)
    st = _State(p, list(inputs))
    _execute(st, trace)
    st.result.instr_cnt = st.instr_cnt
    return st.result
