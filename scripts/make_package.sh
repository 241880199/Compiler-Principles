#!/usr/bin/env bash
# 打交付包：与实验一**同构** —— 根下 report.pdf，源码平铺在 Code/。
#
#   用法： bash scripts/make_package.sh [输出文件]
#          缺省输出 ../../作业三/proj3_submission.zip（见下方 OUT 处的说明）
#          ALLOW_DIRTY=1  工作区有未提交改动时仍继续（会打印警告）
#          SKIP_VERIFY=1  不调 verify_package.sh（**不建议**，见下）
#
# 形制来自实验一已交付的 作业一/proj1_submission.zip：根 report.pdf + Code/ 平铺源码，
# 不带 Test/、不带 scripts/、不带报告 .tex。README「目录结构」一节写的"外加
# Test/sample、Test/err 作样例"是设计期的口径，实验一实际交的包并不含 Test/，
# 本脚本以**已交付事实**为准。
#
# 与实验一交付包的**唯一**差别：Code/Makefile 是机械派生的，不是另手写一份。
# 见下方「扁平 Makefile」一节 —— 手抄的第二份 Makefile 一旦与仓库那份分叉，
# 交付包会编不过，而仓库自检完全看不到（本项目反复出现过的一类失效）。
set -euo pipefail

cd "$(dirname "$0")/.."
REPO=$PWD
# 缺省输出到 **作业三/** —— 交付物按作业归档，且**不放仓库里**（仓库是公开的，交付包
# 不该混进去；`*.zip` 也未被 .gitignore 覆盖，落进来就是脏工作区）。
# 注意本仓库的物理位置有点绕：它在 `作业一/Compiler-Principles`，而实验三的交付物属于
# **兄弟**目录 `作业三/` —— 所以是 `../../作业三/`，不是 `../`（写错过一次：包落进了
# 作业一/，与实验一的包混在一起，而脚本照旧打印成功）。
OUT=$(realpath -m "${1:-$REPO/../../作业三/proj3_submission.zip}")
REPORT_PDF=docs/实验三报告.pdf

# 交付包一律**不落在仓库里**（公开仓库 + *.zip 未被 .gitignore 覆盖）。上面那个
# `../` 写成 `../../作业三/` 的错就是**静默**的：包打错地方，脚本照旧打印成功。
# 故把它变成硬失败。
case "$OUT" in
    "$REPO"/*)
        echo "错误：输出路径落在仓库内（$OUT）—— 交付包不入库。" >&2
        echo "      缺省应落在兄弟目录 作业三/ 下。" >&2
        exit 1
        ;;
esac

SENTINEL=PKG-CUT

# ── 0. 交付的是 HEAD，还是工作区？ ────────────────────────────
# 源码与 Makefile 都取自 **git HEAD**（不是工作区）。若工作区在 src/ 或 Makefile 上
# 有未提交改动，那么"刚才自检通过的东西"与"打进包里发出去的东西"就不是同一份 ——
# 这正是要拦的。ALLOW_DIRTY=1 可越过，但会打印警告。
if [ -n "$(git status --porcelain -- src Makefile)" ]; then
    if [ "${ALLOW_DIRTY:-0}" = "1" ]; then
        echo "警告：src/ 或 Makefile 有未提交改动，包内容仍取 git HEAD ——" >&2
        git status --porcelain -- src Makefile | sed 's/^/      /' >&2
    else
        echo "错误：src/ 或 Makefile 有未提交改动，包会取 HEAD 而非工作区。" >&2
        echo "      请先提交（或设 ALLOW_DIRTY=1 明确越过）：" >&2
        git status --porcelain -- src Makefile | sed 's/^/      /' >&2
        exit 1
    fi
fi

# ── 1. 报告 PDF ───────────────────────────────────────────────
# 报告不入库（.gitignore 排除 *.pdf），故只能从工作区取。缺了就响亮退出 ——
# 实验一那次的 PDF 是"编两遍"才收敛的，不能让一个过期或缺席的 PDF 混进交付包。
if [ ! -s "$REPORT_PDF" ]; then
    echo "错误：找不到 $REPORT_PDF" >&2
    echo "      报告需现场排版（xelatex 编**两遍**，只编一遍页数不作数）：" >&2
    echo "      cd docs && xelatex -interaction=nonstopmode 实验三报告.tex  # ×2" >&2
    exit 1
fi

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
STAGE=$WORK/pkg
mkdir -p "$STAGE/Code"

# ── 2. 源码：只取 git 跟踪的 src/，平铺进 Code/ ───────────────
# 用 git archive 而非 cp：构建产物（lex.yy.c、syntax.tab.*、*.o）一律不受 .gitignore
# 之外的意外影响，且**实验一那次的坑**（交付包里混进一个 57KB 的 flex 生成物
# Code/lexer.c，Makefile 从未引用它）在结构上不可能重演。
git archive HEAD src | tar -x -C "$STAGE/Code" --strip-components=1

# ── 2b. 扫描器必须以 .c 形式随包提交（名字**不能**叫 lex.yy.c）─────────
# 判分机不用我们的 Makefile，它把课程给的参考 Makefile（作业一/Makefile_ref）盖上去。
# 那条 `parser` 规则的链接行是 `$(filter-out $(LFO),$(OBJS))` —— 它把 **lex.yy.o 主动
# 滤掉**，而 `lexical` 目标只跑 flex、**从不编译产物**。于是"由 lexical.l 生成的扫描器"
# 在判分机上既不编译也不链接，链接期报：
#     undefined reference to `yylex'  /  `yyin'
# 2026-09-23 首次提交实测就是这个错。所以扫描器要以一个**不叫 lex.yy.c** 的 .c 文件
# 随包提交 —— 实验一交付包里的 `Code/lexer.c` 正是这个用途（先前被误判成"误收进去的
# 生成物"，那个判断是错的）。叫 lex.yy.c 会被上面那条 filter-out 再次滤掉。
# **现场从 HEAD 的 lexical.l 生成**而不是存一份副本：副本会过期，而"改了 .l 忘了重
# 生成 .c"在全绿的自检下是静默的。
flex -o "$STAGE/Code/lexer.c" src/lexical.l

# ── 3. 扁平 Makefile：从仓库 Makefile 机械派生 ────────────────
# 与源码同源：**一律取 HEAD**（`git show HEAD:Makefile`），不取工作区 ——
# 否则会出现"源码是 HEAD 的、Makefile 是工作区的"这种半新半旧的包。
# 变换只有两条：
#   (a) 取到 PKG-CUT 哨兵行为止 —— 其下全是开发期规则（Test/unit、Test/regress），
#       那些目录不进交付包；
#   (b) 去掉 `src/` 前缀、去掉 `-Isrc`（包里的源码平铺在 Code/ 下）。
# **注释与空行一并丢掉**：仓库 Makefile 的注释大量引用 `src/`、`Test/regress/`
# 这些在包里不存在的路径，原样带过去会得到半句话（"包里只有  + Makefile"）。
# 理由留在仓库那份里（维护者在那边），包内只留一份手写的短表头。
# 于是派生规则可以一眼核完：核心段里每一条**非注释行**，去掉 src/ 前缀。
# 哨兵找不到就退出：宁可打包失败，也不要打出一个"切分点猜错了"的包。
git show HEAD:Makefile > "$WORK/head-Makefile"
if ! grep -q "^# $SENTINEL >>>" "$WORK/head-Makefile"; then
    echo "错误：HEAD 的 Makefile 里找不到哨兵行 '# $SENTINEL >>>' —— 打不出交付包。" >&2
    echo "      该行标出核心构建段的末尾（见 Makefile 内注释）。" >&2
    echo "      若刚在源码树里加过它，记得先提交（包只取 HEAD）。" >&2
    exit 1
fi
CUT=$(grep -n "^# $SENTINEL >>>" "$WORK/head-Makefile" | head -1 | cut -d: -f1)

{
    cat <<'HDR'
# 实验三交付包 Makefile —— **由 scripts/make_package.sh 机械派生，请勿手改**。
#
# 派生自仓库 Makefile 的核心构建段（到 PKG-CUT 哨兵为止）中的非注释行，并去掉
# `src/` 前缀 —— 本包内源码平铺于 Code/ 下。改构建规则请改仓库 Makefile 再重新打包。
#
# 两条硬约束（完整理由在仓库 Makefile 里，这里只留结论）：
#   1. 构建请用 `make`，**不要 `make -j`**：`syntax.tab.c syntax.tab.h` 是"一个
#      recipe、两个目标"，并发下会被当成两条规则同时跑 bison、写同一个文件。
#   2. 链接只需 `-lfl`，**不需要 `-ly`**（本项目自带 main 与 yyerror）。

HDR
    head -n "$((CUT - 1))" "$WORK/head-Makefile" \
        | sed -e '/^[[:space:]]*#/d' -e '/^[[:space:]]*$/d' \
              -e 's|-Isrc||' -e 's|src/||g'
    cat <<'FTR'

# 本包只有平铺源码，故 clean 不涉及 Test/、scripts/。
.PHONY: all clean
clean:
	rm -f parser cc *.o lex.yy.c syntax.tab.c syntax.tab.h syntax.output
FTR
} > "$STAGE/Code/Makefile"

cp "$REPORT_PDF" "$STAGE/report.pdf"

# ── 4. 打包 ───────────────────────────────────────────────────
rm -f "$OUT"
( cd "$STAGE" && zip -q -r -X "$OUT" . )

echo "=================================================="
echo "交付包：$OUT"
echo "  大小：$(stat -c%s "$OUT") 字节"
echo "  sha256：$(sha256sum "$OUT" | cut -d' ' -f1)"
echo "  条目（$(unzip -Z1 "$OUT" | wc -l) 个）："
unzip -Z1 "$OUT" | sed 's/^/      /'
echo "== Code/Makefile（脚本生成，仅供过目）=="
sed 's/^/      /' "$STAGE/Code/Makefile"
echo "=================================================="

# ── 5. 就地复核：解包 → 从零 make → 与仓库构建对拍 ────────────
# 打包与复核绑在一起：默认不可能打出一个没人验过的包。
if [ "${SKIP_VERIFY:-0}" = "1" ]; then
    echo "警告：SKIP_VERIFY=1，未复核该包。" >&2
else
    bash scripts/verify_package.sh "$OUT"
fi
