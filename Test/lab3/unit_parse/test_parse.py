import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "scripts"))
from irsim_ref import load_ir, IRSyntaxError, UndefinedLabelError

HERE = os.path.dirname(os.path.abspath(__file__))


def _ir(name):
    return load_ir(os.path.join(HERE, name))


def _must_reject(name):
    """断言夹具被拒；返回捕获到的 IRSyntaxError（便于再断言其内容）。"""
    try:
        _ir(name)
    except IRSyntaxError as e:
        return e
    raise AssertionError("%s 应当被拒绝，但 load_ir 接受了它" % name)


def _must_leak_valueerror(name):
    """夹具在装载期抛**裸 ValueError** 而不是 IRSyntaxError（契约见模块 docstring）。"""
    try:
        _ir(name)
    except IRSyntaxError as e:
        raise AssertionError(
            "%s 应当漏出 ValueError（真机契约），却抛了 IRSyntaxError: %r" % (name, e))
    except ValueError:
        return
    raise AssertionError("%s 应当抛 ValueError，但没有抛任何异常" % name)


# ---------------------------------------------------------------- 基本形状

def test_parses_all_forms():
    p = load_ir(os.path.join(HERE, "t1.ir"))
    assert p.entry == 0
    assert p.labels["label1"] == 8   # LABEL label1 : 是第 9 行 ⇒ 下标 8（同用例已断言 codes[8]）
    assert p.codes[0] == ("LABEL", "main")
    assert p.codes[1] == ("DEC",)
    assert p.codes[2] == ("MOV", "t1", "#5")
    assert p.codes[3] == ("ARITH", "t2", "&a", "+", "#4")
    assert p.codes[4] == ("MOV", "*t2", "t1")
    assert p.codes[5] == ("CALL", "x", "f")
    assert p.codes[6] == ("IF", "x", ">", "#0", "label1")
    assert p.codes[7] == ("GOTO", "label2")
    assert p.codes[8] == ("LABEL", "label1")
    assert p.codes[9] == ("RETURN", "#0")

def test_funcs_records_variables():
    """`funcs` 是 Task 3 执行引擎消费的接口（CALL 时靠它重分配地址）⇒ 必须被钉住。

    顺序 = 函数体内**变量首次出现**的顺序；`#5`/`&a` 这类操作数不登记（`&a` 里的 `a`
    早已登记，`#5` 是立即数）。`f` 体为空 ⇒ 空列表。
    """
    p = load_ir(os.path.join(HERE, "t1.ir"))
    assert p.funcs == {"main": ["a", "t1", "t2", "x"], "f": []}

def test_div_becomes_floordiv():
    p = load_ir(os.path.join(HERE, "t2.ir"))   # 内容：FUNCTION main : / t := a / b
    assert p.codes[1] == ("ARITH", "t", "a", "//", "b")

def test_rejects_single_token_line():
    """`a:=b` 分词后只有 **1 个** token ⇒ 走的是 sanity_check 末尾的兜底 else。

    ⚠️ 它**不**覆盖 `strs[1] == ":="` 那个分支 —— 把整个 `:=` 分支删掉这个用例照样通过。
    真正走 `:=` 分支并在其中失败的用例见 test_rejects_arith_with_bad_operator /
    test_rejects_hash_on_assignment_lhs / test_rejects_call_with_extra_operand。
    """
    _must_reject("t3.ir")   # 内容：FUNCTION main : / a:=b

def test_blank_lines_are_skipped_without_index():
    p = load_ir(os.path.join(HERE, "t4.ir"))   # 第 2 行是空行
    assert p.codes[1] == ("GOTO", "label1")

def test_rejects_undefined_label():
    """钉住 label_check 这层**我们自己加的**防御性检查（非反解语义，见其 docstring）。

    t1.ir 里 label2 与 f 之所以必须有定义，正是因为这条检查会拒绝无定义的引用。
    """
    try:
        load_ir(os.path.join(HERE, "t5.ir"))   # 内容：FUNCTION main : / GOTO nowhere
    except UndefinedLabelError as e:
        assert str(e) == "nowhere"
        return
    raise AssertionError("引用了未定义标号 nowhere，应当被拒绝")


# ------------------------------------------------- spec §3.2 的形状约束
# 正例与反例成对：规则被写反时，成对的另一方必须变红。

def test_write_accepts_immediate():
    """`WRITE x` **没有** `x[0].isalpha()` 限制（spec §3.2）—— 最容易写反的一处不对称。"""
    p = _ir("write_imm.ir")
    assert p.codes[1] == ("WRITE", "#5")

def test_arg_accepts_immediate():
    """`ARG x` 同样没有 isalpha 限制。"""
    p = _ir("arg_imm.ir")
    assert p.codes[1] == ("ARG", "#5")

def test_read_accepts_variable():
    """正例：`READ x` 满足 isalpha 约束。"""
    p = _ir("read_var.ir")
    assert p.codes[1] == ("READ", "x")

def test_read_rejects_immediate():
    """反例：`READ #5` 违反 isalpha 约束（与 test_write_accepts_immediate 成对）。"""
    _must_reject("read_imm.ir")

def test_param_accepts_variable():
    """正例：`PARAM x` 满足 isalpha 约束。"""
    p = _ir("param_var.ir")
    assert p.codes[1] == ("PARAM", "x")

def test_param_rejects_immediate():
    """反例：`PARAM #5` 违反 isalpha 约束（与 test_arg_accepts_immediate 成对）。"""
    _must_reject("param_imm.ir")

def test_rejects_label_main():
    """`main` 只能是 FUNCTION，LABEL 不行（spec §3.2 第一行）。"""
    _must_reject("label_main.ir")

def test_dec_accepts_size_multiple_of_four():
    """正例：`DEC a 8`；顺带钉住数组的大小与 is_array 标记确实进了符号表。"""
    p = _ir("dec_ok.ir")
    assert p.codes[1] == ("DEC",)
    assert p.symtab["a"] == [0, 8, True]

def test_rejects_dec_size_not_multiple_of_four():
    """反例：`DEC a 5`（size 必须是 4 的倍数）。"""
    _must_reject("dec_bad.ir")

def test_rejects_if_without_goto_at_position_four():
    """反例：`IF x > y z GOTO` 是 6 个 token，但 strs[4] != "GOTO"。"""
    _must_reject("if_bad_goto.ir")

def test_rejects_call_with_extra_operand():
    """反例：`x := CALL f g` —— `x := CALL f` 必须恰好 len==4。"""
    _must_reject("call_extra.ir")

def test_rejects_arith_with_bad_operator():
    """反例：`x := a b c` 是 len==5，但 strs[3] 不是 + - * /（走 `:=` 分支的 else）。"""
    _must_reject("arith_badop.ir")

def test_rejects_hash_on_assignment_lhs():
    """反例：`#x := y` —— `:=` 左边的首字符不许是 `#`（spec §3.7 第 3 条）。"""
    _must_reject("lhs_hash.ir")

def test_rejects_ampersand_on_assignment_lhs():
    """反例：`&x := y` —— `:=` 左边的首字符不许是 `&`。"""
    _must_reject("lhs_amp.ir")

def test_int_parse_failure_leaks_valueerror():
    """`DEC a abc` 抛**裸 ValueError**，不是 IRSyntaxError —— 与真机一致（见模块 docstring）。

    这条断言**故意**钉住"漏 ValueError"这个行为：将来若有人顺手把它改成 IRSyntaxError，
    这里会变红。真机 `sanity_check` 捕的正是 `(IRSyntaxError, ValueError)`，故漏出是忠实的。
    """
    _must_leak_valueerror("dec_nonnum.ir")
