"""用**原版 irsim**（课程发的 `作业三/irsim.pyc`）无界面跑一份 `.ir`，输出机器可读结果。

**本脚本必须跑在 Python 3.8 + PyQt5 上**（原版字节码是 CPython 3.8 编的），例如：

    build/py38/python.exe scripts/irsim_orig_probe.py --ir Test/lab3/unit_exec/sign.ir --inputs -7

它的用途是与无头参考机 `scripts/irsim_ref.py` **逐条对拍**（见 `scripts/compare_refsim.py`）：
参考机的每一条执行语义都只有字节码依据，原版是唯一能把它验实/证伪的外部判据。

原版被绕开的三处「会挂住」的东西（原版是 GUI 程序，直接 import 会自己 `app.exec_()`）：

1. **模块顶层无 `if __name__ == '__main__'` 保护** —— `irsim.pyc` 一被装载就
   `QApplication(sys.argv)` → `IRSim()` → `show()` → `exec_()`。故装载前先把
   `QApplication.exec_` 打成空操作。
2. **模态对话框** —— `QMessageBox.information/critical/warning/question/about` 全部换成
   不阻塞的桩，记录被调用的 (kind, title, text)；`QInputDialog.getInt` 换成从脚本化的
   输入序列取下一个整数（模拟用户敲 `READ` 的值），序列耗尽时返回 `(0, False)`
   —— 即**用户取消**，此时原版 `READ` **不写内存**（见 `execute_code` 的 `if ok:` 分支）。
3. **Qt 平台插件路径** —— 本脚本跑在 **embeddable** 版 3.8 上，`PyQt5` 包自己找不到
   `PyQt5/Qt5/plugins`（`QApplication(...)` 直接 `qFatal`/abort，退出码 `0xC0000409`，
   Qt 的报错走 `OutputDebugString` 故 stderr 上一片空白）。**显式设**
   `QT_PLUGIN_PATH` / `QT_QPA_PLATFORM_PLUGIN_PATH` 后正常。平台插件用 `minimal`
   （`offscreen` 在本机同样 abort）。

输出（stdout，单行 JSON）：

    {"ok": true, "status": "ok|memory|pc|error", "outputs": [...], "instr_cnt": N,
     "messages": [...], "exception": null | {"type": ..., "args": [...]}}

`status` 的口径直接取自原版 `run()` 里 `error_code` 的三分支（1/2/3）与我们的
`'ok'/'memory'/'pc'` 一一对应；装载期异常（`IRSyntaxError` 等）记 `"error"`。
"""

import argparse
import json
import os
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ir", required=True, help="要执行的 .ir 文件")
    ap.add_argument("--inputs", default="", help="逗号分隔的整数输入序列（模拟 READ 的输入）")
    ap.add_argument("--in", dest="infile", default=None,
                    help="输入文件，每行一个整数（与 --inputs 二选一）")
    ap.add_argument("--irsimc", default=None, help="irsim.pyc 路径（默认自动找 作业三/）")
    ap.add_argument("--dump", action="store_true", help="额外把 codes/symTable 打到 stderr")
    ap.add_argument("--meta", action="store_true",
                    help="JSON 里附上装载后的 codes/symtab/funcs/label_table（供对拍）")
    args = ap.parse_args()

    inputs = []
    if args.infile:
        with open(args.infile, "r", encoding="utf-8") as fp:
            inputs = [int(t) for t in fp.read().split()]
    elif args.inputs.strip():
        inputs = [int(t) for t in args.inputs.split(",")]

    # ---- 1. 定 irsim.pyc 与它依赖的两个模块所在目录 ----
    if args.irsimc:
        irsim_pyc = os.path.abspath(args.irsimc)
    else:
        here = os.path.dirname(os.path.abspath(__file__))
        cand = os.path.join(here, "..", "..", "..", "作业三", "irsim.pyc")
        cand = os.path.normpath(cand)
        if not os.path.exists(cand):
            raise SystemExit("找不到 irsim.pyc，请用 --irsimc 指定")
        irsim_pyc = cand
    dist_dir = os.path.dirname(irsim_pyc)
    for dep in ("ui_mainwindow.pyc", "resources_rc.pyc"):
        if not os.path.exists(os.path.join(dist_dir, dep)):
            raise SystemExit("原版发行目录缺少 %s：%s" % (dep, dist_dir))

    # ---- 2. Qt 无头 ----
    # embeddable 版 Python 下 PyQt5 找不到 `PyQt5/Qt5/plugins`，QApplication 会直接 abort。
    # 从 PyQt5 包的位置推出 plugins 目录并显式指给 Qt。
    _sp = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                       "build", "py38", "Lib", "site-packages")
    if os.path.isdir(_sp) and _sp not in sys.path:
        sys.path.append(_sp)
    import PyQt5                                            # noqa: E402
    _plugins = os.path.join(os.path.dirname(os.path.abspath(PyQt5.__file__)),
                            "Qt5", "plugins")
    if os.path.isdir(_plugins):
        os.environ["QT_PLUGIN_PATH"] = _plugins
        os.environ["QT_QPA_PLATFORM_PLUGIN_PATH"] = os.path.join(_plugins, "platforms")
    os.environ["QT_QPA_PLATFORM"] = os.environ.get("QT_QPA_PLATFORM", "minimal")

    sys.path.insert(0, dist_dir)
    # Qt 会去解析 sys.argv，把我们的 CLI 参数塞给它会让它报错；先藏起来。
    saved_argv = sys.argv
    sys.argv = ["irsim"]

    from PyQt5 import QtWidgets  # noqa: E402

    messages = []          # 被拦下的 QMessageBox 调用
    input_trace = []       # READ 实际取到的 (value, ok)

    # 顶层 exec_() 会阻塞 ⇒ 打成空操作（必须在装载 irsim 之前）
    QtWidgets.QApplication.exec_ = lambda self: 0
    QtWidgets.QApplication.exec = lambda self: 0

    def _mb(kind):
        def stub(parent, title, text, *a, **kw):
            messages.append({"kind": kind, "title": title, "text": text})
            if kind == "question":
                return QtWidgets.QMessageBox.No
            return QtWidgets.QMessageBox.Ok
        return staticmethod(stub)

    for _k in ("information", "critical", "warning", "question", "about"):
        setattr(QtWidgets.QMessageBox, _k, _mb(_k))

    def _get_int(parent, title, label, value=0, minValue=-2147483647,
                 maxValue=2147483647, step=1, flags=0):
        if inputs:
            v = inputs.pop(0)
            input_trace.append([v, True])
            return v, True
        # 输入耗尽 = 用户取消 ⇒ 原版 `if ok:` 为假 ⇒ **不写内存**
        input_trace.append([0, False])
        return 0, False

    QtWidgets.QInputDialog.getInt = staticmethod(_get_int)
    QtWidgets.QFileDialog.getOpenFileName = staticmethod(
        lambda *a, **kw: ("", ""))

    # ---- 3. 装载原版（SourcelessFileLoader：它只有 .pyc） ----
    import importlib.machinery
    import importlib.util
    loader = importlib.machinery.SourcelessFileLoader("irsim_orig", irsim_pyc)
    spec = importlib.util.spec_from_loader("irsim_orig", loader)
    mod = importlib.util.module_from_spec(spec)
    sys.modules["irsim_orig"] = mod
    loader.exec_module(mod)          # ← 顶层 GUI 代码在这里跑；exec_ 已打成空操作
    form = mod.form                  # 顶层建好的那个 IRSim 实例

    if args.dump:
        form.loadFile(args.ir)
        sys.stderr.write("codes=%r\n" % (form.codes,))
        sys.stderr.write("symTable=%r\n" % (form.symTable,))
        sys.stderr.write("labelTable=%r\n" % (form.labelTable,))
        sys.stderr.write("functionDict=%r\n" % (form.functionDict,))
        sys.stderr.write("entranceIP=%r offset=%r\n" % (form.entranceIP, form.offset))
        return

    # ---- 4. 装载并运行 ----
    out = {"ok": True, "status": None, "outputs": [], "instr_cnt": None,
           "messages": messages, "exception": None, "input_trace": input_trace}

    try:
        form.loadFile(args.ir)
    except BaseException as e:                      # noqa: BLE001
        # 真机 loadFile 只捕 (IRSyntaxError, ValueError, DuplicatedLabelError,
        # DuplicatedVariableError, CurrentFunctionNoneError)；别的异常（例如单 token 行
        # 触发的 IndexError）会**原样穿出 loadFile**。如实记录。
        out["ok"] = False
        out["status"] = "load-exception"
        out["exception"] = {"type": type(e).__name__, "args": list(e.args)}
        out["messages"] = messages
        print(json.dumps(out, ensure_ascii=False))
        return

    # loadFile 在入口缺失/校验失败时会弹 critical 并 initialize()（entranceIP 回 -1）。
    if form.entranceIP == -1:
        out["ok"] = False
        out["status"] = "error"
        out["loaded"] = False
        out["exception"] = {"type": "LoadError",
                            "args": [m["text"] for m in messages]}
        print(json.dumps(out, ensure_ascii=False))
        return

    out["loaded"] = True
    if args.meta:
        # 供对拍用：原版装载后的 codes / symTable / funcs / labelTable 全貌
        # 注意：真机把 `DEC` 存成**裸字符串** `'DEC'`（其余分支才 BUILD_TUPLE）。
        # 直接 `list(c)` 会把它拆成 ['D','E','C']，故原样保留字符串。
        out["codes"] = [c if isinstance(c, str) else list(c) for c in form.codes]
        out["symtab"] = {k: list(v) for k, v in form.symTable.items()}
        out["label_table"] = dict(form.labelTable)
        out["funcs"] = {k: list(v) for k, v in form.functionDict.items()}
        out["entry"] = form.entranceIP

    try:
        form.run()
    except BaseException as e:                      # noqa: BLE001 —— 原样记录
        out["ok"] = False
        out["status"] = "exception"
        out["exception"] = {"type": type(e).__name__, "args": list(e.args)}

    # ---- 5. 取结果 ----
    out["instr_cnt"] = form.instrCnt
    text = form.console.toPlainText()
    out["outputs"] = [int(t) for t in text.split("\n") if t.strip() != ""]

    if out["status"] is None:
        kinds = [(m["kind"], m["text"]) for m in messages]
        for kind, txt in kinds:
            if kind == "information" and "exited gracefully" in txt:
                out["status"] = "ok"
                break
            if kind == "critical" and "Illegal memory access" in txt:
                out["status"] = "memory"
                break
            if kind == "warning" and "Program Counter goes out of bound" in txt:
                out["status"] = "pc"
                break
        else:
            # 没有任何收尾对话框 —— 原版三条出口都会弹一个，故这是异常路径
            out["status"] = "unknown"

    sys.argv = saved_argv
    print(json.dumps(out, ensure_ascii=False))


if __name__ == "__main__":
    main()
