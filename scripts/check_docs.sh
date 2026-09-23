#!/usr/bin/env bash
# 文档 ↔ 代码 名字检查：文档里出现的每一个 `X()` 形式，都必须在 src/ 或 scripts/
# 里找到同名标识符。
#
# **为什么要这条**：交付的 PDF 里曾经写着决策 5、6 的开关是 `useStructTag()`（5）与
# `checkSubscript()`（6）—— 而这两个名字在 src/ 里**根本不存在**（真实位置是
# `analyzeStructSpecifier` 里 `nchild == 2` 的分支、以及 `analyzeSubscript`）。
# 助教按名字去翻代码会翻不到，而这句恰恰是报告「亮点 3」的核心论据。
# 名字对不对**可以自动抓**，就不该靠人眼 —— 与 run_tests.sh 里那些"防线会不会
# 静默消失"的断言防的是同一类：一个只在文档里存在的名字，没有任何信号。
#
# **豁免**：单字母名（文档里的示意名，如 `f()`/`g()`/`x()`），以及 C 标准库/工具入口
# （见 EXEMPT）。豁免名单要**短** —— 每加一个名字就少一分判别力。
set -u
cd "$(dirname "$0")/.."

DOCS="README.md docs/实验二报告.md docs/实验二报告.tex \
      docs/superpowers/specs/2026-09-22-cmm-lab2-design.md \
      docs/superpowers/plans/2026-09-22-cmm-lab2.md"

# C 标准库 / 语言与工具入口：不是本项目定义的函数，文档里出现属正常。
EXEMPT=" main printf fprintf sprintf snprintf malloc calloc realloc free exit abort \
         strcmp strcpy strlen yyparse yylex yywrap func "

# ── 语料：src/ 与 scripts/ 的**代码行**（丢掉整行注释）────────────────────
# ⚠ **必须去注释**：一条**否定式**的注释（"这里以前叫 `useStructTag()`"）会让那个名字
# 在代码里"存在"，于是检查器**恰好**看不见它被写出来要防的那种名字 —— 这不是假想，
# 本脚本第一版就踩了两次：它自己的说明段、以及 run_tests.sh 里调用它的那段注释，
# 两处都把两个反例名字变成了"证据"（实测：把 `useStructTag()` 放回设计文档，
# 检查照样全绿）。**检查器不能是自己的证据来源。**
# 安全性：实测本项目**没有**任何"整行注释之后还跟着代码"的行
# （`^\s*/\*.*\*/\s*[A-Za-z_]` 零命中），故按行首标记丢弃整行是安全的。
# 残留局限：**行尾**注释里的名字仍算"存在"（未处理；本项目暂无此情形）。
corpus=$(mktemp)
trap 'rm -f "$corpus"' EXIT
for f in src/*.c src/*.h scripts/*.sh; do
    grep -vE '^[[:space:]]*([*]|/\*|#)' "$f" >> "$corpus"
done

fail=0
total=0
for d in $DOCS; do
    # 文档缺失 ⇒ 这条防线自己的前提没了。响亮退出，不静默跳过 ——
    # 否则删掉设计文档会让它的名字从此不再被检查，而脚本照样全绿。
    if [ ! -f "$d" ]; then
        echo "FAIL  名字检查：文档不存在 —— $d（该文档里的名字已不再被检查）"
        fail=$((fail + 1))
        continue
    fi
    # 抽 `标识符()`：标识符只含 [A-Za-z0-9_]，不以数字开头。
    for name in $(grep -oE '[A-Za-z_][A-Za-z0-9_]*\(\)' "$d" | sed 's/()$//' | sort -u); do
        [ "${#name}" -le 1 ] && continue                 # 单字母：示意名
        case "$EXEMPT" in *" $name "*) continue ;; esac  # 标准库/工具入口
        total=$((total + 1))
        # 词边界用 [^A-Za-z0-9_] 手工锚定。
        if ! grep -qE "(^|[^A-Za-z0-9_])${name}([^A-Za-z0-9_]|$)" "$corpus"; then
            echo "FAIL  $d 里的 \`${name}()\` 在 src/ 与 scripts/ 里都找不到同名标识符"
            fail=$((fail + 1))
        fi
    done
done

# **自身哨兵**：一个名字都没抽出来（或语料是空的），说明上面的正则、文件清单或
# 语料构造坏了 —— 而"零个名字、零个失败"会伪装成全绿，正是本脚本要防的那种失效。
if [ "$total" -eq 0 ]; then
    echo "FAIL  名字检查一个标识符都没抽到 —— 检查本身已失效（正则或文档清单坏了）"
    fail=$((fail + 1))
fi
if [ ! -s "$corpus" ]; then
    echo "FAIL  名字检查的语料是空的（src/ 与 scripts/ 都没读到内容）—— 检查本身已失效"
    fail=$((fail + 1))
fi

if [ "$fail" -eq 0 ]; then
    echo "文档↔代码名字检查：抽到 $total 个 \`X()\`，全部在 src/ 或 scripts/ 中找到"
fi
[ "$fail" -eq 0 ]
