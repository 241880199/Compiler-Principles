# Compiler-Principles

南京大学《编译原理》课程实验代码。

## 实验一：C-- 词法分析器与语法分析器

用 GNU Flex + GNU Bison + C 实现 C-- 语言的词法分析与语法分析，检测并报告词法
错误（类型 A）与语法错误（类型 B），无错时按先序打印语法树。

**已实现的功能**

- 错误类型 A：词法未定义字符、不符合词法单元定义的字符
- 错误类型 B：语法错误，支持在单个文件内报告**多处**错误
- 选做 1.1：识别八进制 / 十六进制数，对 `09`、`0x3G` 报类型 A
- 选做 1.2：识别指数形式浮点数，对 `1.05e` 报类型 A
- 选做 1.3：滤除 `//` 与 `/*…*/` 注释，对未闭合注释报类型 A
- 语法树先序打印，含行号、词素、十进制数值

**设计文档**：[`docs/superpowers/specs/2026-09-20-cmm-lab1-design.md`](docs/superpowers/specs/2026-09-20-cmm-lab1-design.md)

---

## 环境要求与工具版本

需要 **flex、bison、gcc、make** 四件工具。Ubuntu / WSL 下一行装齐：

```bash
sudo apt-get update && sudo apt-get install -y flex bison gcc make
```

本程序实测通过的工具版本（要求书要求注明所用工具版本）：

| 工具 | 本机实测版本 | 评分环境 | 说明 |
|---|---|---|---|
| gcc | **11.4.0**（Ubuntu 11.4.0-1ubuntu1~22.04.3） | 7.5.0 | 代码只用 C99 子集，两版均可编译 |
| flex | **2.6.4** | 2.6.4 | 与评分环境完全一致 |
| bison | **3.8.2** | 3.0.4 | 只使用两版通用的写法（见下） |
| make | **4.3** | — | — |

查看本机版本：

```bash
gcc --version | head -1; flex --version; bison --version | head -1; make --version | head -1
```

**Bison 版本兼容**：为兼容评分环境的 Bison 3.0.4，文法中**不使用** `%define parse.error
detailed`（3.0.4 无此值）等 3.1+ 新增写法；语义值用 `%define api.value.type {Node *}`
而非 `%union`。本程序自行定义 `main` 与 `yyerror`，因此链接时**不需要 `-ly`**
（评分环境亦无 `liby`）；同时在 `lexical.l` 里声明 `%option noyywrap`。

## 编译

在项目根目录执行：

```bash
make            # 生成 ./parser（并复制一份 ./cc）
```

从零开始（助教拿到的是纯源码，建议先清理再构建，这已在干净目录下验证一次通过）：

```bash
make clean && make
```

预期：**无编译错误、无 gcc 警告**；Bison 只报 1 处 shift/reduce 冲突，即悬空 else，
属预期（见下）。若终端里没有 `syntax.output`，`make test` 的冲突数断言无法执行。

## 运行

```bash
./parser Test/sample/semantic.cmm     # 输出语法树
./cc     Test/sample/semantic.cmm     # 等价，./cc 是同一份程序的副本
```

- 无错误时按先序打印语法树，子结点每层缩进 2 空格，ε 结点不打印。
- 有错误时**只**打印错误信息，**不**打印语法树。格式：
  `Error type [A|B] at Line [n]: [说明].`
- 错误信息与语法树都输出到 **stdout**。
- 退出码：正常分析完毕返回 `0`（无论有无词法/语法错误）；仅当缺少文件名参数或
  文件打不开时返回 `1`。

## 测试

```bash
make test       # 跑 Test/sample 与 Test/err 下全部用例，与同名 .exp 逐字节比对
```

预期输出以 `通过 27 个，失败 0 个` 结尾。

`make test` 除比对用例之外还做三条断言，三条都不可省：

1. **冲突数恰好 1**（`syntax.output` 中恰有 1 个 `State N conflicts: 1 shift/reduce`）。
   这是悬空 else，Bison 默认移进即 C 语义，保持默认。此断言是防文法退化的**哨兵**：
   实测把 `%left LB DOT` 删掉后，LR 动作表 `yypact/yydefact/yytable/yycheck/...`
   **逐字节完全不变**，全部用例照过，**只有冲突计数**（1 → 11 个 state、
   21 处 shift/reduce）能发现它。
2. **`Test/err/crlf.cmm` 必须真含 CR**。该用例的全部价值在 `\r` 上（验证空白规则
   `[ \t\r]+` 能吃掉 CRLF）。`.gitattributes` 用 `-text` 让它在索引里保留 CRLF，
   但那只保护"入索引的那一刻"；若有人就地把它改写成 LF，全部用例照过、防线静默
   消失，故在运行期再断言一次。
3. **parser 的 stderr 必须为空**。脚本把 stdout 与 stderr **分别**重定向：stdout 用于
   比对 `.exp`，stderr 非空即判 FAIL。此前两者合并（`> "$tmp" 2>&1`），把错误信息
   改成写 stderr 仍然全绿，而要求书 2.1.3 要求输出到**标准输出**（判分脚本读 stdout）
   —— 合并流会让这条规范悄悄失效。

另有两条单测目标（不在 `make test` 范围内）：

```bash
make unit-test      # 语法树构造与打印（Test/unit/test_tree）
make lexer-test     # 词法分析器单独测试（Test/unit/test_lexer）
```

## 目录结构

```
Makefile        make → ./parser 与 ./cc
src/            Flex 词法规则、Bison 文法、AST 定义与打印、入口、错误上报
Test/sample/    要求书样例：NN.cmm 输入 + NN.exp 期望输出
Test/err/       自建边界用例
Test/unit/      单测（语法树 / 词法分析器）
scripts/        环境安装、环境探测、测试执行脚本
docs/           设计文档、实验报告大纲
```

仓库中**不含**任何构建产物（`*.o`、`lex.yy.c`、`syntax.tab.*`、`parser`、`cc`）
与 PDF，均由 `.gitignore` 排除。
