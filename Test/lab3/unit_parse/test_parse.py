import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "scripts"))
from irsim_ref import load_ir, IRSyntaxError, UndefinedLabelError

HERE = os.path.dirname(os.path.abspath(__file__))

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

def test_div_becomes_floordiv():
    p = load_ir(os.path.join(HERE, "t2.ir"))   # 内容：FUNCTION main : / t := a / b
    assert p.codes[1] == ("ARITH", "t", "a", "//", "b")

def test_rejects_missing_spaces():
    try:
        load_ir(os.path.join(HERE, "t3.ir"))   # 内容：FUNCTION main : / a:=b
    except IRSyntaxError:
        return
    raise AssertionError("a:=b 应当被拒绝")

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
