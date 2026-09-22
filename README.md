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

**与实验二的关系**：实验二的 `./parser` **不再打印语法树**（实验一指导书第 25 页明说
那段打印代码"后面的实验不会再用"）。实验一那 30 个 `.exp` 装的仍是语法树，因此它们
改由回归驱动 `Test/regress/driver` 运行 —— 见下面「测试」一节。

---

## 实验二：C-- 语义分析与类型检查

在实验一建好的语法树上**手写**语义分析与类型检查（语义部分不借助任何工具，
前端仍是实验一的 Flex + Bison）。实现了**必做 17 种语义错误 + 三项选做**，
合计 **19 个错误码**：

| 类别 | 内容 | 错误码 |
|---|---|---|
| 必做 | 未定义变量、未定义函数、变量重复定义、函数重复定义、赋值类型不匹配、左值、操作数类型、`return` 类型、实参不匹配、对非数组用 `[]`、对普通变量用 `()`、下标非整数、对非结构体用 `.`、访问未定义域、域名重复/域初始化、结构体名冲突、直接用未定义结构体 | 1–17 |
| 选做 2.1 | 函数**声明**（原型）：定义不可重复，相互一致的声明可重复。为此给文法加了一条产生式 `ExtDef : Specifier FunDec SEMI` | 18、19 |
| 选做 2.2 | **嵌套作用域**：内层可重复定义外层变量并**遮蔽**它，离开内层即消亡，不同函数体的局部变量可重名 | 无新码，改变 1–17 的判定 |
| 选做 2.3 | **结构等价**（忽略域名、**不展开数组**）；数组等价忽略长度，基类型 + 维数相同即同类型 | 无新码，改变 5/7 的判定 |

**⚠️ 与实验一的行为差异（最容易踩的一条）**：

- 词法/语法有错 → 仍打印 `Error type [A|B] at Line [n]: …`（实验一行为不变）
- **语义**有错 → 打印 `Error type [错误码] at Line [n]: …`
- **完全无错时不输出任何内容**（实验一那时是打印语法树）—— 与要求书一致
- 退出码不变：正常分析完毕返回 `0`（无论有无错误）；缺文件名参数或文件打不开返回 `1`
- 每个输入文件**每行最多只有一条错误**（要求书规定）

**设计文档**：[`docs/superpowers/specs/2026-09-22-cmm-lab2-design.md`](docs/superpowers/specs/2026-09-22-cmm-lab2-design.md)
**实验报告**：`docs/实验二报告.md`（`docs/实验二报告.pdf` 为同一内容的排版稿）

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
| bison | **3.8.2** | **3.5.1**（实验二要求书）/ 3.0.4（实验一要求书） | 只使用三版通用的写法（见下）；**3.5.1 与 3.0.4 均已实机复跑整套用例** |
| make | **4.3** | 4.2（Ubuntu 20.04） | 见下方"不要用 `make -j`" |

查看本机版本：

```bash
gcc --version | head -1; flex --version; bison --version | head -1; make --version | head -1
```

**关于评分环境的 Bison：已实机验证，不是"无法实机验证"。** 从 Ubuntu 归档取出
`bison_3.5.1+dfsg-1_amd64.deb`（focal —— **实验二要求书写的判分版本**）与
`bison_3.0.4.dfsg-1build1_amd64.deb`（bionic —— 实验一要求书写的版本），用
`dpkg-deb -x` 解包（配合环境变量 `BISON_PKGDATADIR` 指向解包出的 `usr/share/bison`）
后，在仓库副本里各自重新生成 `syntax.tab.c` 并跑完整套件，实测：

| 检查项 | bison 3.8.2（本机） | bison 3.5.1（focal） | bison 3.0.4（bionic） |
|---|---|---|---|
| 编译警告 | 3 处 shift/reduce 冲突 | 3 处 shift/reduce 冲突 | 3 处 shift/reduce 冲突 |
| 冲突 state | `27` / `32` / `114`，各 `1 shift/reduce` | 同左 | 同左 |
| 总 state 数 | 118 | 118 | 118 |
| `bash scripts/run_tests.sh` | **通过 73 个，失败 0 个** | **通过 73 个，失败 0 个** | **通过 73 个，失败 0 个** |

> **两份要求书的 Bison 版本互相矛盾（如实记录）**：实验一要求书写 **3.0.4**、
> 实验二要求书写 **3.5.1**，而**两处都标注"（同实验一）"**。这是一个客观的文档矛盾。
> **处置：取严者。** 实验一为兼容 3.0.4 而回避的写法（不用 `%define parse.error
> detailed`、不用 `%union`、不用 `&:` 分组目标）在 3.5.1 下同样可用，因此实验二
> **不新增任何版本相关限制**，沿用实验一的写法，三版通吃。
> **编号只取决于文法、与 Bison 版本无关**：三版的 state 编号、状态项、规则号逐字相同，
> 只有状态项里那个点的写法不同（3.5.1 / 3.0.4 印 `.`，3.8.2 印 `•`）。

（冲突由 1 变 3 是采纳 `Def : error` 的结果：其中 2 处由它引起、1 处仍是悬空 else。
详见下文「测试」一节的冲突断言语与 `src/syntax.y` 的注释。）

**Bison 版本兼容**：为兼容评分环境最老的 Bison 3.0.4（与 3.5.1），文法中**不使用**
`%define parse.error detailed`（3.0.4 无此值）等 3.1+ 新增写法；语义值用
`%define api.value.type {Node *}` 而非 `%union`。本程序自行定义 `main` 与 `yyerror`，
因此链接时**不需要 `-ly`**（评分环境亦无 `liby`）；同时在 `lexical.l` 里声明
`%option noyywrap`。

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
./parser <文件>      # 当前行为（实验二）
./cc     <文件>      # 等价，./cc 是同一份程序的副本

# 想看到实验一那种"无错时打印语法树"的行为，用回归驱动（需先 make Test/regress/driver）：
./Test/regress/driver <文件>
```

- **完全无错时不输出任何内容**（实验二要求书的规定；与实验一不同 —— 实验一那时
  会打印语法树，那段代码现在只留在回归驱动里）。
- 有错误时**只**打印错误信息，**不**打印语法树。格式：
  `Error type [错误类型] at Line [行号]: [说明].`
  词法/语法错报 `A` / `B`，语义错报数字码 `1`–`19`。
- **错误信息输出到 stdout**（要求书明确，判分脚本读 stdout），且 **stderr 保持为空**；
  只有"缺文件名参数"与"文件打不开"这两条提示写 stderr。
- 退出码：正常分析完毕返回 `0`（无论有无错误）；仅当缺少文件名参数或文件打不开时
  返回 `1`。
- 同一个输入文件里，**每行最多一条错误**（要求书规定，语义错误的同行去重收敛在
  `src/report.c` 一处）。

## 测试（**可选**的自检；评分流程不依赖它）

```bash
make                       # 自检脚本需要 ./parser
bash scripts/run_tests.sh  # 跑 Test/sample、Test/err、Test/lab2 下全部用例，与同名 .exp 逐字节比对
```

评分流程是 `make` + `./parser <文件>`，**不需要**测试入口；因此 Makefile 里
**没有** `test` 目标 —— 不让"构建可用性"依赖 `scripts/` 这个开发期目录是否
随提交一起打包（打包时漏掉它，`make test` 就会坏，而 `make` 本身不该受影响）。
自检脚本仍在仓库里（`scripts/run_tests.sh`），要用就按上面的命令直接调。

预期输出以 `通过 73 个，失败 0 个` 结尾（**73 = 实验一的 30 个 + 实验二的 43 个**；
在 bison 3.8.2、3.5.1、3.0.4 三版下各复跑一次，三次都是 73/73）。

- 实验一的 30 个（`Test/sample` 19 + `Test/err` 11）：期望的是**语法树或 A/B 错误**，
  由回归驱动 `Test/regress/driver` 跑（脚本自己会 `make` 出它，构建失败则**响亮退出**
  —— 静默跳过会让这 30 个用例无声消失）。
- 实验二的 43 个（`Test/lab2`）：期望的是**语义错误**，由 `./parser` 跑。
  其中 **23 个官方样例**（要求书必做 17 + 选做 6）、**16 个决策/边界用例**
  （`dec01`–`dec15` 及其变体）、**4 个多错误/宽实参表/作用域用例**。

自检脚本除比对用例之外还做六条断言，**每一条都不可省**（1–3 是实验一时的三条，
4–6 是实验二加的，防的是同一件事：**防线静默消失、而所有用例照绿**）：

1. **冲突数恰好 3**（`syntax.output` 中恰有 3 个 `State N conflicts: 1 shift/reduce`），
   归属为：

   | # | state | 产生式项 | 冲突 |
   |---|---|---|---|
   | ① | 27 | `CompSt: LC • DefList StmtList RC` | 移进 `error` vs 按 rule 25 (`DefList`) 归约 |
   | ② | 32 | `DefList: Def • DefList` | 同上（`DefList` 递归，前瞻集不变） |
   | ③ | 114 | `Stmt: IF LP Exp RP Stmt •` / `\| IF LP Exp RP Stmt • ELSE Stmt` | 悬空 else |

   编号是**当前文法**下的实测值（bison 3.8.2 / 3.5.1 / 3.0.4 三版一致）：实验二
   新增了 `ExtDef : Specifier FunDec SEMI`，它只多出 **1 个** state 且落在构造序的
   第 26 位，于是 **≥26 的编号整体 +1**（26→27、31→32、113→114），总 state 数
   117→118；**冲突个数不变**。**编号只取决于文法，与 Bison 版本无关** —— 在
   `lab1` 分支上复测，实验一文法的三处冲突是 `26 / 31 / 113`、总 state 数 117，
   与这里的 `27 / 32 / 114` 是同一件事在两代文法上的取值（`docs/实验报告.md`
   与 `docs/报告大纲.md` 里另有补注）。

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

实验二另加三条同类断言（防的是同一件事：**防线静默消失、而所有用例照绿**）：

4. **官方样例的 `.pairs` 与 `.cmm` 都必须存在**。`.pairs` 钉的是**判分字段**
   （错误类型 + 行号，要求书 3.1.3 明说这是唯一评分标准），而 `.exp` 钉的是我们的
   完整文本。`check_pairs` 对没有 `.pairs` 的用例直接 `return 0`，所以**删掉某条
   官方样例的 `.pairs` 会静默关掉那一层**；同理，把某条官方样例的 `.cmm` 移走
   会让循环不再迭代它（实测：只留下 `.exp` 与 `.pairs` 时脚本照旧打印
   "通过 72 个，失败 0 个"并退出 0）。故脚本用一份 **23 个官方样例的名单**同时
   断言 `.cmm` 与 `.pairs` 的存在 —— 这份名单也顺便充当"哪 23 个是官方样例"的文档，
   **新增官方样例必须与名单在同一次提交里改**。
5. **`dec01`–`dec15` 等决策/边界用例必须存在**。设计文档 §8 的十条"要求书未规定"
   的取舍各自有专属用例钉住当前行为，缺一个就判 FAIL —— 否则改错一条决策不会
   有任何用例变红。
6. **`Test/lab2` 存在时至少真跑成一个用例**。`pass > 0` 那条守卫只管实验一那半边：
   目录在而一个 `.cmm` 都没跑成时会打印"通过 30 个，失败 0 个"并退出 0，
   整半边哨兵失效。

> **⚠️ `.pairs` 的存亡条件：它必须手抄自要求书，永远不许用脚本从 `./parser`
> 的输出生成。** 这不是风格要求，是上面第 4 条那层设计的**存亡条件**：`.pairs`
> 相对于我们的实现是**独立的第二信源**，它的全部价值就在于"我们自己错的时候它
> 不会跟着错"。一旦用 `sed`/`awk` 从实测输出生成它，两层立刻退化成一层（同一个
> 来源抄两遍），而**那时不会有任何信号**：所有用例照绿、防线静默消失 —— 与实验一
> `Test/err/crlf.cmm` 被 `.gitattributes` 规范化掉是同一类失效。当前没有任何机制
> 能自动发现这一点（`check_pairs` 只比对内容、不问来源），所以只能靠这条文档明令
> 加代码评审。**改动 `Test/lab2/*.pairs` 时请先确认它的来源是要求书，不是 `./parser`。**

另有六个单测目标（**不依赖 `scripts/`，因此与自检脚本的有无无关**；需先 `make`
生成 `lex.yy.c` / `syntax.tab.c`）：

```bash
make unit-test       # 语法树构造与打印（Test/unit/test_tree）
make lexer-test      # 词法分析器单独测试（Test/unit/test_lexer）
make type-test       # 类型表示与等价（Test/unit/test_type）—— 实验二
make nodetype-test   # NodeType 枚举与名字的映射（Test/unit/test_nodetype）—— 实验二
make symtab-test     # 符号表：冲突码规则、保留最早（Test/unit/test_symtab）—— 实验二
make report-test     # 数字错误码 + 同行去重（Test/unit/test_report）—— 实验二
```

其中 `type-test` 不是可选项：数组维度顺序写反（`array(2,array(10,int))` 取代
`array(10,array(2,int))`）**在端到端用例里没有任何症状**（长度不参与判等、维数不变、
错误 12 只看下标类型），只有这个单测能发现它。

## 目录结构

```
Makefile        make → ./parser 与 ./cc
src/            lexical.l 词法规则、syntax.y 文法、tree.c AST、report.c 错误上报、
                main.c 入口；实验二另加三个模块 ——
                  type.c     类型表示与等价（名等价 / 结构等价）。**纯模块**，
                             不依赖符号表与 AST，可独立单测
                  symtab.c   符号表（散列 + 每层作用域链表）；scopeEnter/Exit、
                             symEnter/symLookup
                  semantic.c AST 遍历与 19 种检查。依赖 type 与 symtab。
                依赖方向单向：semantic → symtab → type
Test/sample/    实验一要求书样例（NN.cmm + NN.exp 逐字手抄）；另有 floatbuf.cmm ——
                非样例，是守护 NUMBUF（大浮点值不被截断）的关键用例
Test/err/       实验一自建边界用例（含 crlf.cmm —— 全部价值在 CRLF 上）
Test/lab2/      实验二用例（NN.cmm + NN.exp；官方样例另配 NN.pairs 钉判分字段）
Test/regress/   实验一的回归驱动（driver.c 是实验一 main.c 的原样副本）
Test/unit/      单测（语法树 / 词法分析器 / 类型 / NodeType / 符号表 / 错误上报）
scripts/        开发期自检脚本（run_tests.sh 等）；**没有对应的 make 目标**
                （评分流程不需要，见「测试」一节）
docs/           设计文档、实验报告与大纲（实验一、实验二各一份）
```

仓库中**不含**任何构建产物（`*.o`、`lex.yy.c`、`syntax.tab.*`、`syntax.output`、
`parser`、`cc`、单测可执行文件）与 PDF，均由 `.gitignore` 排除。实验二报告的
`.tex` / `.md` 入库，**`.pdf` 不入库**（与实验一同规格）—— 需要时本地现生成
（在临时目录里编译，免得 `aux`/`log` 落进仓库）：

```bash
mkdir -p /tmp/rpt && cp docs/实验二报告.tex /tmp/rpt/ && cd /tmp/rpt \
  && xelatex -interaction=nonstopmode 实验二报告.tex && xelatex -interaction=nonstopmode 实验二报告.tex \
  && cp 实验二报告.pdf "<仓库>/docs/"
```

实测：xelatex 两遍 → **A4、3 页**，无 overfull 警告（要求书的上限是 3 页、
最小字号不小于五号；正文基准字号 11pt）。
