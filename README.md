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
| bison | **3.8.2** | 3.0.4 | 只使用两版通用的写法（见下）；**并已在 3.0.4 实机复跑整套用例** |
| make | **4.3** | 4.2（Ubuntu 20.04） | 见下方"不要用 `make -j`" |

查看本机版本：

```bash
gcc --version | head -1; flex --version; bison --version | head -1; make --version | head -1
```

**关于评分环境的 bison 3.0.4：已实机验证，不是"无法实机验证"。** 从 Ubuntu bionic
的归档取出 `bison_3.0.4.dfsg-1build1_amd64.deb`、用 `dpkg-deb -x` 解包（配合环境变量
`BISON_PKGDATADIR` 指向解包出的 `usr/share/bison`）后，在仓库副本里重新生成
`syntax.tab.c` 并跑完整套件，实测：

| 检查项 | bison 3.8.2 | bison 3.0.4 |
|---|---|---|
| 编译警告 | 3 处 shift/reduce 冲突 | 3 处 shift/reduce 冲突 |
| 冲突 state | `26` / `31` / `114`，各 `1 shift/reduce` | `26` / `31` / `114`，各 `1 shift/reduce` |
| `bash scripts/run_tests.sh` | **通过 30 个，失败 0 个** | **通过 30 个，失败 0 个** |

（冲突由 1 变 3 是采纳 `Def : error` 的结果：其中 2 处由它引起、1 处仍是悬空 else。
详见下文「测试」一节的冲突断言语与 `src/syntax.y` 的注释。）

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

**不要用 `make -j`。** `syntax.tab.c syntax.tab.h: src/syntax.y` 是一条"一个 recipe、
两个目标"的多目标规则，`make -j` 会把它当成两条独立规则**并发跑两次 bison**、两次写
同一个 `syntax.tab.c`。可移植的分组写法（`&:`）需要 make ≥ 4.3，评分镜像是 Ubuntu
20.04 的 make 4.2，因此不使用 `&:`，而是明确要求串行构建（本项目很小，耗时可忽略）。

预期：**无编译错误、无 gcc 警告**；Bison 报 3 处 shift/reduce 冲突（悬空 else 1 处 +
`Def : error` 引起的 2 处），全部属预期（见下）。若终端里没有 `syntax.output`，
自检脚本的冲突数断言无法执行。

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

## 测试（**可选**的自检；提交内容本身不含测试）

```bash
make                       # 自检脚本需要 ./parser
bash scripts/run_tests.sh  # 跑 Test/sample 与 Test/err 下全部用例，与同名 .exp 逐字节比对
```

脚本是**开发期自检工具，不是提交物的一部分**，也**没有** `make test` 目标
（Makefile 里不保留指向提交中不存在路径的目标）。助教按下面的「运行」一节直接
跑 `./parser <文件>` 即可。

预期输出以 `通过 30 个，失败 0 个` 结尾（在 bison 3.8.2 与 3.0.4 下各复跑一次，
两次都是 30/30）。

自检脚本除比对用例之外还做三条断言，三条都不可省：

1. **冲突数恰好 3**（`syntax.output` 中恰有 3 个 `State N conflicts: 1 shift/reduce`），
   归属为：

   | # | state | 产生式项 | 冲突 |
   |---|---|---|---|
   | ① | 26 | `CompSt: LC • DefList StmtList RC` | 移进 `error` vs 按 rule 24 (`DefList`) 归约 |
   | ② | 31 | `DefList: Def • DefList` | 同上（`DefList` 递归，前瞻集不变） |
   | ③ | 114 | `Stmt: IF LP Exp RP Stmt •` / `\| IF LP Exp RP Stmt • ELSE Stmt` | 悬空 else |

   ①② 都由 `Def : error` 引起（**`DefList` 可空**，故"看 `error` 时移进"与
   "按 `DefList → ε` 归约"不可兼得），且都出现在 `CompSt` 一路 —— `error` 能跟在
   `DefList` 之后是因为 `StmtList` 可以 `error` 开头；`StructSpecifier : STRUCT
   OptTag LC • DefList RC`（State 20）**不冲突**，结构体成员表后面只能跟 `RC`，
   `error` 不进它的前瞻集。③ 是悬空 else，Bison 默认移进即 C 语义。

   此断言是防文法退化的**哨兵**，**语义不随期望值改变**（冲突数一变就报警，不区分
   "变好"还是"变坏"）。实测把 `%left LB DOT` 删掉后，LR 动作表
   `yypact/yydefact/yytable/yycheck/...` **逐字节完全不变**，全部用例照过，
   **只有冲突计数**（3 → 13 个 state、23 处 shift/reduce）能发现它。
2. **`Test/err/crlf.cmm` 必须真含 CR**。该用例的全部价值在 `\r` 上（验证空白规则
   `[ \t\r]+` 能吃掉 CRLF）。`.gitattributes` 用 `-text` 让它在索引里保留 CRLF，
   但那只保护"入索引的那一刻"；若有人就地把它改写成 LF，全部用例照过、防线静默
   消失，故在运行期再断言一次。
3. **parser 的 stderr 必须为空**。脚本把 stdout 与 stderr **分别**重定向：stdout 用于
   比对 `.exp`，stderr 非空即判 FAIL。此前两者合并（`> "$tmp" 2>&1`），把错误信息
   改成写 stderr 仍然全绿，而要求书 2.1.3 要求输出到**标准输出**（判分脚本读 stdout）
   —— 合并流会让这条规范悄悄失效。

另有两条单测目标（**不依赖 `scripts/`，因此与自检脚本的有无无关**；需先 `make`
生成 `lex.yy.c` / `syntax.tab.c`）：

```bash
make unit-test      # 语法树构造与打印（Test/unit/test_tree）
make lexer-test     # 词法分析器单独测试（Test/unit/test_lexer）
```

## 目录结构

```
Makefile        make → ./parser 与 ./cc
src/            Flex 词法规则、Bison 文法、AST 定义与打印、入口、错误上报
Test/sample/    要求书样例（NN.cmm + NN.exp 逐字手抄）；另有 floatbuf.cmm —— 非样例，
                是守护 NUMBUF（大浮点值不被截断）的关键用例
Test/err/       自建边界用例
Test/unit/      单测（语法树 / 词法分析器）
scripts/        开发期自检脚本（run_tests.sh 等）；**不是提交物的一部分**，
                也没有对应的 make 目标
docs/           设计文档、实验报告大纲
```

仓库中**不含**任何构建产物（`*.o`、`lex.yy.c`、`syntax.tab.*`、`parser`、`cc`）
与 PDF，均由 `.gitignore` 排除。
