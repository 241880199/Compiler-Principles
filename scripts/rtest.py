"""极简测试运行器：收集 test_*.py 里所有 test_* 可调用对象并逐个运行。

为什么不用 pytest：本机三个解释器都没装它，而让自检依赖一个需要先安装的东西，
等于给跑得起来加了一道门槛。本运行器零依赖、可直接在交付环境里跑。

用法： python -I scripts/rtest.py <目录> [<目录> ...]
退出码：全部通过 0；有失败 1；**一个用例都没跑到也判 1**（防止语料被清空后假绿）。
"""
import importlib.util, os, sys, traceback


def _load(path):
    name = os.path.splitext(os.path.basename(path))[0]
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def main(dirs):
    files = []
    for d in dirs:
        for n in sorted(os.listdir(d)):
            if n.startswith("test_") and n.endswith(".py"):
                files.append(os.path.join(d, n))
    passed = failed = 0
    for f in files:
        try:
            mod = _load(f)
        except Exception:
            failed += 1
            print("FAIL %s (导入期异常)" % f)
            traceback.print_exc()
            continue
        for attr in sorted(dir(mod)):
            if attr.startswith("test_") and callable(getattr(mod, attr)):
                try:
                    getattr(mod, attr)()
                    passed += 1
                except Exception:
                    failed += 1
                    print("FAIL %s::%s" % (f, attr))
                    traceback.print_exc()
    print("pass=%d fail=%d" % (passed, failed))
    if passed == 0:
        print("没有任何用例跑到 —— 判为红")
        return 1
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
