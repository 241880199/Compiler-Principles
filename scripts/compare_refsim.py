"""把无头参考机 `scripts/irsim_ref.py` 与**原版 irsim** 逐条对拍。

用法（跑在**本机 Python 3.11** 上；原版那侧是子进程，跑在 `build/py38/python.exe` 上）：

    python scripts/compare_refsim.py                 # 全部夹具
    python scripts/compare_refsim.py sign.ir rec.ir  # 只跑指定名字的夹具

对拍维度（每一维都单独比，**不做"都对就算过"的合并**）：

| 维度 | 参考机侧 | 原版侧 |
|---|---|---|
| 装载状态 | `load_ir` 接受 / 抛异常 | `loadFile` 后 `entranceIP` 是否 -1 / 抛异常 |
| `codes` | `Program.codes` | `form.codes` |
| `symtab` / `funcs` / `label_table` / `entry` | `Program` 的对应字段 | `form.symTable` / `functionDict` / `labelTable` / `entranceIP` |
| 输出 | `Result.outputs` | `console.toPlainText()` 逐行 |
| **指令数** | `Result.instr_cnt` | `form.instrCnt` |
| 结束状态 | `Result.status` | `run()` 里 `error_code` 的 1/2/3 分支 |
| 异常 | 原样漏出的异常 | 原样漏出的异常 |

**编码归一**（否则全是假阳性）：原版把 `DEC` 存成**裸字符串** `'DEC'`
（`sanity_check` 里其它分支都 `BUILD_TUPLE`，只有 DEC 直接 `codes.append('DEC')`），
参考机存成 `("DEC",)`。执行期两者等价（`'DEC'[0] == 'D'` 谁也匹配不上，落到兜底
`return 0`），故比较前统一成 `["DEC"]`。

**为什么原版那侧必须单开进程**：原版字节码是 CPython 3.8 编的，本机 3.11 连
`marshal` 都读不进（`ValueError: bad marshal data`）；且原版一被装载就建 QApplication。

**定向夹具**（`TARGETED`）是照着已发现的语义缺口现造的，写在 `build/probe_ir/`（gitignore），
**不碰 `Test/lab3/**`**。
"""

import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import irsim_ref  # noqa: E402

PY38 = os.path.join(ROOT, "build", "py38", "python.exe")
PROBE = os.path.join(HERE, "irsim_orig_probe.py")
EXEC_DIR = os.path.join(ROOT, "Test", "lab3", "unit_exec")
PARSE_DIR = os.path.join(ROOT, "Test", "lab3", "unit_parse")
EXTRA_DIR = os.path.join(ROOT, "build", "probe_ir")

# ---------------------------------------------------------------- 夹具

CASES = [
    (EXEC_DIR, "sign.ir", [-7]),
    (EXEC_DIR, "sign.ir", [0]),
    (EXEC_DIR, "sign.ir", [9]),
    (EXEC_DIR, "sign.ir", []),
    (EXEC_DIR, "sample2.ir", [5]),        # 要求书 4.1.6 样例：fact(5)
    (EXEC_DIR, "sample2.ir", [1]),
    (EXEC_DIR, "sample2.ir", [0]),
    (EXEC_DIR, "sample_opt1.ir", [-7]),   # 要求书 4.1.6 优化样例
    (EXEC_DIR, "sample_opt1.ir", [0]),
    (EXEC_DIR, "sample_opt1.ir", [9]),
    (EXEC_DIR, "counted.ir", []),
    (EXEC_DIR, "counted_dec.ir", []),
    (EXEC_DIR, "rec.ir", []),
    (EXEC_DIR, "argorder.ir", []),
    (EXEC_DIR, "divneg.ir", []),
    (EXEC_DIR, "divzero.ir", []),
    (EXEC_DIR, "oob.ir", []),
    (EXEC_DIR, "deref_arith.ir", []),
    (EXEC_DIR, "deref_call.ir", []),
    (EXEC_DIR, "deref_mov.ir", []),
]

PARSE_CASES = [
    "arg_imm.ir", "arith_badop.ir", "call_extra.ir", "dec_bad.ir", "dec_nonnum.ir",
    "dec_ok.ir", "if_bad_goto.ir", "label_main.ir", "lhs_amp.ir", "lhs_hash.ir",
    "param_imm.ir", "param_var.ir", "read_imm.ir", "read_var.ir",
    "t1.ir", "t2.ir", "t3.ir", "t4.ir", "t5.ir", "write_imm.ir",
]

# 名字 -> (源码, 输入)。这些是照着**已发现的语义缺口**现造的判别性夹具：
#   * read_cancel   —— READ 弹窗被取消时到底写不写内存
#   * goto_last     —— 跳到**最后一行**的标号（跳转后 ip+=1 直接越界）
#   * param_empty   —— PARAM 遇空 argumentStack
#   * relops        —— >= 与 !=
#   * bigdec        —— offset > 1 MiB 的装载期拒绝
#   * deref_read    —— &x / *x 的读形态
TARGETED = {
    "read_cancel": ("""FUNCTION main :
t := #5
READ t
WRITE t
RETURN #0
""", []),
    "goto_last": ("""FUNCTION main :
GOTO end
RETURN #0
LABEL end :
""", []),
    "param_empty": ("""FUNCTION main :
r := CALL f
WRITE r
RETURN #0
FUNCTION f :
PARAM x
RETURN x
""", []),
    "relops": ("""FUNCTION main :
a := #3
b := #3
IF a >= b GOTO one
GOTO two
LABEL one :
WRITE #11
GOTO done
LABEL two :
WRITE #22
LABEL done :
IF a != b GOTO x
WRITE #33
GOTO fin
LABEL x :
WRITE #44
LABEL fin :
RETURN #0
""", []),
    "bigdec": ("""FUNCTION main :
DEC a 1048580
RETURN #0
""", []),
    "deref_read": ("""FUNCTION main :
q := #8
p := &q
WRITE &q
WRITE *p
RETURN #0
""", []),
    "write_imm_exec": ("""FUNCTION main :
WRITE #5
ARG #7
RETURN #0
""", []),
    "badrelop": ("""FUNCTION main :
a := #1
b := #2
IF a <> b GOTO l
RETURN #0
LABEL l :
RETURN #0
""", []),
}


def write_targeted():
    os.makedirs(EXTRA_DIR, exist_ok=True)
    for name, (src, _) in TARGETED.items():
        with open(os.path.join(EXTRA_DIR, name + ".ir"), "w", encoding="utf-8",
                  newline="\n") as fp:
            fp.write(src)


def norm_codes(codes):
    """把两侧的 codes 归一成 list[list[str]]（见模块 docstring 的「编码归一」）。"""
    out = []
    for c in codes:
        if isinstance(c, str):          # 原版的 DEC
            out.append([c])
        else:
            out.append([str(tok) for tok in c])
    return out


def ref_exec(path, inputs):
    """参考机侧：装载 + 执行。异常原样带出，交给调用方归类。"""
    try:
        p = irsim_ref.load_ir(path)
    except BaseException as e:                      # noqa: BLE001
        return {"phase": "load",
                "exception": {"type": type(e).__name__, "args": list(e.args)}}
    info = {"phase": "run",
            "codes": norm_codes(p.codes),
            "symtab": {k: list(v) for k, v in p.symtab.items()},
            "funcs": {k: list(v) for k, v in p.funcs.items()},
            "label_table": dict(p.labels),
            "entry": p.entry}
    st = irsim_ref._State(p, list(inputs))
    try:
        irsim_ref._execute(st, False)
    except BaseException as e:                      # noqa: BLE001
        info["exception"] = {"type": type(e).__name__, "args": list(e.args)}
    else:
        info["exception"] = None
        info["status"] = st.result.status
    info["instr_cnt"] = st.instr_cnt
    info["outputs"] = list(st.result.outputs)
    return info


def orig_exec(path, inputs):
    cmd = [PY38, PROBE, "--ir", path, "--meta",
           "--inputs", ",".join(str(i) for i in inputs)]
    cp = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                        timeout=180)
    if cp.returncode != 0:
        return {"phase": "probe",
                "exception": {"type": "ProbeExit",
                              "args": [cp.returncode,
                                       cp.stderr.decode("utf-8", "replace")[-800:]]}}
    out = json.loads(cp.stdout.decode("utf-8"))
    if "codes" in out:
        out["codes"] = norm_codes(out["codes"])
    out["phase"] = "load" if out.get("loaded") is False else "run"
    return out


def compare_exec(dirname, name, inputs):
    path = os.path.join(dirname, name)
    r = ref_exec(path, inputs)
    o = orig_exec(path, inputs)
    diffs = []

    # ---- 装载 ----
    r_load_fail = r["phase"] == "load"
    o_load_fail = o["phase"] == "load"
    if r_load_fail != o_load_fail:
        diffs.append("装载成败不同：参考机=%r 原版=%r"
                     % (r.get("exception"), o.get("exception")))
        return diffs, r, o
    if r_load_fail:
        # 两侧都拒 ⇒ **对外可观察的结论一致**（"这份 IR 不被接受"）。两侧抛/弹的东西
        # 天然不同：参考机是库、直接把异常抛给调用方；原版是 GUI、在 loadFile 边界上
        # 弹对话框再 initialize()。机制写入 r/o 供报告引用，但不计为不一致。
        # 唯一算不一致的是原版**连拦都没拦住**（异常穿出 loadFile）—— 见下面 o_exc 分支。
        if o.get("exception") and o["exception"]["type"] not in ("LoadError",):
            diffs.append("两侧都拒绝，但原版没拦在 loadFile 里：参考机=%s 原版=%s"
                         % (r["exception"], o["exception"]))
        return diffs, r, o

    # ---- 装载产物 ----
    for key in ("codes", "symtab", "funcs", "label_table", "entry"):
        if r.get(key) != o.get(key):
            diffs.append("%s 不同：\n        参考机=%r\n        原版  =%r"
                         % (key, r.get(key), o.get(key)))

    # ---- 运行结果 ----
    if r.get("outputs") != o.get("outputs"):
        diffs.append("outputs 不同：参考机=%r 原版=%r (%r)"
                     % (r.get("outputs"), o.get("outputs"), inputs))
    if r.get("instr_cnt") != o.get("instr_cnt"):
        diffs.append("instr_cnt 不同：参考机=%r 原版=%r"
                     % (r.get("instr_cnt"), o.get("instr_cnt")))

    rex, oex = r.get("exception"), o.get("exception")
    if (rex is None) != (oex is None):
        diffs.append("异常有无不同：参考机=%r 原版=%r" % (rex, oex))
    elif rex is not None and (rex["type"] != oex["type"] or rex["args"] != oex["args"]):
        diffs.append("异常内容不同：参考机=%r 原版=%r" % (rex, oex))
    elif rex is None and r.get("status") != o.get("status"):
        diffs.append("结束状态不同：参考机=%r 原版=%r"
                     % (r.get("status"), o.get("status")))

    return diffs, r, o


def compare_parse(name, dirname):
    path = os.path.join(dirname, name)
    try:
        p = irsim_ref.load_ir(path)
        r_ok, r_codes, r_exc = True, norm_codes(p.codes), None
    except BaseException as e:                      # noqa: BLE001
        r_ok, r_codes, r_exc = False, None, {"type": type(e).__name__,
                                             "args": list(e.args)}
    o = orig_exec(path, [])
    o_ok, o_exc = bool(o.get("loaded")), o.get("exception")
    diffs = []
    if r_ok != o_ok:
        diffs.append("接受/拒绝不同：参考机=%s(%s) 原版=%s(%s)"
                     % ("接受" if r_ok else "拒绝", r_exc,
                        "接受" if o_ok else "拒绝", o_exc))
    elif r_ok:
        if r_codes != o.get("codes"):
            diffs.append("codes 不同：参考机=%r 原版=%r" % (r_codes, o.get("codes")))
    elif o_exc and o_exc["type"] not in ("LoadError",):
        # 原版**没**把异常拦在 loadFile 里（dialog 路径），而是原样穿出。
        # 按"参考机不得比原版宽容"：真机泄漏的异常，参考机也要泄漏**同一类**
        # （如 `a:=b` 这类 1-token 行两边都穿出 IndexError）⇒ 那才是"一致"。
        # 只有当参考机没泄漏、或泄漏的类型/参数与真机不同，才算差异。
        if r_exc != o_exc:
            diffs.append("原版拒绝方式不是 loadFile 内的对话框：原版=%s 参考机=%s"
                         % (o_exc, r_exc))
    return diffs, {"ref_ok": r_ok, "ref_exc": r_exc, "orig_ok": o_ok, "orig_exc": o_exc}


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    if not os.path.exists(PY38):
        raise SystemExit("缺少 %s —— 先按报告里的步骤装 3.8 embeddable" % PY38)
    write_targeted()

    only = [a for a in sys.argv[1:] if not a.startswith("-")]
    rows = []
    same = diff = 0

    def do(label, dirname, name, inputs, kind):
        nonlocal same, diff
        if only and name not in only and name[:-3] not in only:
            return
        if kind == "exec":
            ds, r, o = compare_exec(dirname, name, inputs)
            print("\n%-20s inputs=%-8s  %s"
                  % (name, inputs, "一致" if not ds else "**不一致**"))
            print("    参考机: outputs=%r instr_cnt=%r status=%r exc=%r"
                  % (r.get("outputs"), r.get("instr_cnt"), r.get("status"),
                     r.get("exception")))
            print("    原版  : outputs=%r instr_cnt=%r status=%r exc=%r"
                  % (o.get("outputs"), o.get("instr_cnt"), o.get("status"),
                     o.get("exception")))
            if r.get("phase") == "load" and o.get("phase") == "load":
                print("    装载拒绝机制：参考机抛 %s；原版弹对话框后 entranceIP=-1"
                      % r["exception"]["type"])
        else:
            ds, info = compare_parse(name, dirname)
            print("\n%-20s 参考机:%s%-30s 原版:%s%-30s %s"
                  % (name,
                     "接受" if info["ref_ok"] else "拒绝",
                     "" if info["ref_ok"] else str(info["ref_exc"]),
                     "接受" if info["orig_ok"] else "拒绝",
                     "" if info["orig_ok"] else str(info["orig_exc"]),
                     "一致" if not ds else "**不一致**"))
        for d in ds:
            print("    ! " + d)
        rows.append((name, inputs, ds))
        if ds:
            diff += 1
        else:
            same += 1

    print("=" * 78)
    print("A. 执行对拍（Test/lab3/unit_exec/）")
    print("=" * 78)
    for dirname, name, inputs in CASES:
        do("exec", dirname, name, inputs, "exec")

    print("\n" + "=" * 78)
    print("B. 装载对拍（Test/lab3/unit_parse/）")
    print("=" * 78)
    for name in PARSE_CASES:
        do("parse", PARSE_DIR, name, [], "parse")

    print("\n" + "=" * 78)
    print("C. 定向夹具（build/probe_ir/，照已发现的缺口现造）")
    print("=" * 78)
    for name, (_, inputs) in TARGETED.items():
        do("exec", EXTRA_DIR, name + ".ir", inputs, "exec")

    print("\n" + "=" * 78)
    print("总计：一致 %d 条，不一致 %d 条" % (same, diff))
    print("=" * 78)
    return 0 if diff == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
