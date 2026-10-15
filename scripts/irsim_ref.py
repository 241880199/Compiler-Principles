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
