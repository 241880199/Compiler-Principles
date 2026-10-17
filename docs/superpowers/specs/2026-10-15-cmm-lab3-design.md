# 实验三（中间代码生成）设计文档

- 日期：2026-10-15
- 状态：设计已评审通过；实现计划见 `docs/superpowers/plans/2026-10-15-cmm-lab3.md`，实现进行中
- 代码仓库：`D:\Software\编译原理\作业一\Compiler-Principles`，新分支 `lab3`（从 `main` 切）
- 交付包：`D:\Software\编译原理\作业三\proj3_submission.zip`
- 本文档位置：仓库内（随 `lab3` 分支版本化）。`作业三/` 目录只留课程原始材料（两份 PDF + irsim 的四个 .pyc）与交付包 `proj3_submission.zip`

---

## 1. 实验内容与范围

把符合要求的 C-- 源代码翻译为三地址中间代码（线形 IR），按指定文本格式输出，使其能被课程虚拟机
`irsim` 正确执行。在正确的前提下，**中间代码的执行效率计入额外奖励**（比 50% / 80% 的同学高）。

### 1.1 要求书对输入的假设（实验三 4.1.1）

1. 假设1：不出现注释、八进制/十六进制整型常数、浮点型常数或变量
2. 假设2：不出现结构体类型或**高维**（高于 1 维）数组类型的变量
3. 假设3：任何函数参数只能是简单变量（结构体、数组都不作参数）
4. 假设4：没有全局变量，**且所有变量均不重名**
5. 假设5：函数不返回结构体或数组类型的值
6. 假设6：函数只定义一次（没有函数声明）
7. 假设7：输入不含任何词法/语法/语义错误（函数必有 return）

**推论（直接决定设计）**：

- 假设4 ⇒ 中间代码里**不需要作用域**，符号表可以是一张平表，名字全局唯一
- 假设1 ⇒ 只需处理 `int` 与 `int` 字面量，无需浮点
- 假设2+3 ⇒ **一维数组的局部变量属于必做**（`int vals[6]; vals[i] = read();`），
  只有"高维数组"与"数组作参数"才属于选做 3.2

### 1.2 本次交付的配置

**必做 + 选做 3.2（数组）**。不实现选做 3.1（结构体）；遇到结构体类型时按 4.1.7 样例 1 的规定输出

```
Cannot translate: Code contains variables or parameters of structure type.
```

且**不产出任何中间代码**。

- 编译期开关 `LAB3_ENABLE_32`（默认 `1`）用于切出"仅必做"的构建，以便一旦判分口径需要翻转时
  只改一个点（见 §11）。
- 选做 3.2 的两个子项都要做：
  - **(c) 一维数组类型变量可作函数参数**（不作返回值）
  - **(d) 可出现高维数组类型变量**（不作参数、不作返回值）

### 1.3 非目标

- 不做选做 3.1（结构体变量、结构体作参数）
- 不做浮点、八进制/十六进制、注释
- 不做全局变量、函数声明、跨作用域重名
- 不改动实验二 `semantic.c` 已有的检查逻辑（只在遇到结构体时给出 §1.2 的提示并终止翻译）

---

## 2. 交付物与验收

### 2.1 交付包形制

**照抄实验二实测通过（96/100）的形态**，不引入其它模板中的 `Test/`、`parser`、`README`：

```
proj3_submission.zip
├── Code/
│   ├── Makefile      # 由官方 Makefile 派生（见 §2.2）
│   ├── lexical.l
│   ├── lexer.c       # ★ 现场用 flex 从 lexical.l 生成，见 §2.2
│   ├── syntax.y
│   ├── main.c
│   ├── tree.c  tree.h
│   ├── symtab.c  symtab.h
│   ├── semantic.c  semantic.h
│   ├── type.c  type.h
│   ├── util.c  util.h
│   ├── ir.c  ir.h
│   ├── translate.c  translate.h
│   └── optimize.c  optimize.h
└── report.pdf        # ≤3 页
```

约束：`Code/` 下**不得建子目录**（官方 README 明确要求）；报告**最小字号五号/英文 11 号**。

### 2.2 判分机的构建路径（实验一/二栽过的坑，必须复现）

判分机用**课程给的参考 Makefile**（本机在 `作业一/Makefile_ref`）覆盖后 `make`，其关键两行是：

```make
CFILES = $(shell find ./ -name "*.c")          # 注意：会把现场生成的 lex.yy.c 也算进去
parser: syntax $(filter-out $(LFO),$(OBJS))    # ★ lex.yy.o 被主动滤掉
	$(CC) -o parser $(filter-out $(LFO),$(OBJS)) -lfl -ly
lexical: $(LFILE)                              # 只跑 flex，从不编译产物
```

⇒ **扫描器必须以一个「不叫 `lex.yy.c` 的 `.c` 文件」随包提交**（`Code/lexer.c`）。

时序上两种情形都安全，但都必须靠 `lexer.c` 提供 `yylex`：

- **全新构建**：`CFILES = $(shell find …)` 在 make **解析期**求值，此时 `lex.yy.c` 尚未由
  `lexical` 目标生成 ⇒ `OBJS` 里根本没有 `lex.yy.o`，`filter-out` 是空操作。
  **若包里只有 `lexical.l` 而没有 `lexer.c`，链接就会 `undefined reference to 'yylex'`
  —— 实验一/二正是栽在这一处。**
- **增量构建**（上一次留下的 `lex.yy.c` 还在）：`lex.yy.o` 进入 `OBJS` 后被 `filter-out`
  滤出链接行，`lexer.o` 保留 ⇒ 不会重复定义 `yylex`。

打包脚本必须**现场**由 `src/lexical.l` 生成 `Code/lexer.c`，**不存副本**（副本会静默过期）。

### 2.3 验收标准

| 编号 | 判据 | 判定方式 |
|---|---|---|
| A1 | 干净目录 `make` 零 gcc 警告 | 两条构建路径各跑一遍 |
| A2 | 两套 Makefile（自建 / 参考）从零构建后产物逐字节对拍一致 | 脚本对拍 |
| A3 | 全部用例"执行结果等价"通过（不是文本等价） | 无头参考机，见 §9 |
| A4 | 效率：优化前/后 `instrCnt` 差值可量化并记录 | 参考机输出 |
| A5 | 包内 `report.pdf` 与仓库那份逐字节相同 | 脚本比对 |
| A6 | 冲突数哨兵（沿用实验二）仍恰好为 3 | `run_tests.sh` |

---

## 3. 目标语言（虚拟机接受的 IR 文本，逐条实测反解）

以下全部来自对本机 `作业三\irsim.pyc` 的字节码反解，**不是文档转述**。

> ⚠️ **一处例外（2026-10-15 修正）**：**「被引用的标号必须有定义」这条检查不是反解出来的。**
> 真机确有 `labelCheck` 与 `UndefinedLabelError`，但其实现未能反解确证 —— 其 code 对象的
> 常量表里没有 `'GOTO'`/`'IF'`/`'CALL'` 这类关键字，与常见的按关键字比较写法形状不符。
> 参考实现里保留的是一层**我们自己的防御性检查**，并已在代码里逐字声明。
> 对本实验**无实害**：我们生成的 IR 里每个被引用的标号都有定义，该检查永不触发。
> 其余各条（指令形状、操作数取值、调用约定、内存模型、计数口径）均有字节码或官方样例背书。

### 3.1 词法

`strs = line.split()` —— 空白分词，**运算符两侧必须有空格**。`loadFile` 逐行读，
`line.isspace()` 的行被**整行跳过**（不占 `codes` 下标）。每个非空行恰好一条指令。

### 3.2 接受的指令

| 形式 | 约束 | 运行语义 |
|---|---|---|
| `LABEL x :` | `len==3` 且 `strs[2]==":"`；`x=="main"` 报错 | 无操作 |
| `FUNCTION f :` | 同上；`f=="main"` 时 `entranceIP` = **该行在 `codes` 里的下标**（真机 `lineno` 只对非空白行自增，故它**就是** codes 下标，不是物理行号 —— 空白行会让两者错开） |
| `GOTO x` | `len==2` | `ip = labelTable[x]` |
| `RETURN x` | `len==2` | 见 §3.4 |
| `READ x` | `len==2` 且 `x[0].isalpha()` | `mem[symTable[x][0]//4] = 读入值` |
| `WRITE x` | `len==2`（**无 isalpha 限制**，故 `WRITE #5` 合法） | 输出 `getValue(x)` |
| `ARG x` | `len==2` | `argumentStack.append(getValue(x))` |
| `PARAM x` | `len==2` 且 `x[0].isalpha()` | `mem[symTable[x][0]//4] = argumentStack.pop()` |
| `DEC x size` | `len==3` 且 `int(size) % 4 == 0` | 无操作（计数 1） |
| `IF x op y GOTO z` | `len==6`，`strs[4]=="GOTO"`，`op ∈ {> < >= <= == !=}` | `if eval(str(getValue(x)) + op + str(getValue(y))): ip = labelTable[z]` |
| `x := y` | `len==3`，`strs[1]==":="`；`x[0] ∉ {'&','#'}` | `MOV`，见 §3.3 |
| `x := CALL f` | `len==4`，`strs[2]=="CALL"` | 见 §3.4 |
| `x := a op b` | `len==5`，`op ∈ {+ - * /}` | `mem[symTable[x][0]//4] = eval(str(getValue(a)) + op + str(getValue(b)))`，其中 `/` 在装载期被**改写为 `//`** |

### 3.3 操作数取值 `getValue`

| 写法 | 含义 |
|---|---|
| `#n` | 立即数 `n`（`int(n)`，可带负号，如 `#-1`） |
| `var` | `mem[symTable[var][0] // 4]` |
| `&var` | `symTable[var][0]`（**字节地址**） |
| `*var` | `mem[ mem[symTable[var][0]//4] // 4 ]`（先取 var 的值当地址，再取值） |

`MOV`（`x := y`）：`value = getValue(y)`；若 `x` 以 `*` 开头则
`mem[ mem[symTable[x[1:]][0]//4] // 4 ] = value`，否则 `mem[symTable[x][0]//4] = value`。

### 3.4 调用约定（★ 顺序容易写反）

`CALL f`：
1. `oldAddrs = {}`；`oldOffset = offset`
2. 对 `functionDict[f]` 里每个变量 `k`：记下旧地址，并令
   `symTable[k] = (getNewAddr(symTable[k][1]), 旧size, 旧array)`
   （`getNewAddr(size)` = 返回当前 `offset` 并 `offset += size`）
3. `callStack.append((当前ip, f, oldAddrs, oldOffset))`；`ip = labelTable[f]`
   —— ⚠️ **注意**：`run()` 主循环在 `execute_code` 返回后**无条件** `self.ip += 1`，
   故**实际下一站是 `labelTable[f] + 1`**（`FUNCTION f :` 那一行被整条跨过，
   **不执行、不计数**）。`GOTO` / `IF`（成立）到 `LABEL` 同理。详见 §3.6。

`x := CALL f` 的记录形式为 `('CALL', x, f)`。

`RETURN x`：
1. 若 `callStack` 为空 ⇒ **程序正常结束**，返回码 1，报 `Total instructions = instrCnt`
2. `returnValue = getValue(x)`
3. 弹出栈顶：`ip = 栈顶ip`；把 `oldAddrs` 逐项写回 `symTable`；`offset = oldOffset`
   —— `RETURN` 压/写的是**调用点自身**的 `ip`，主循环再 `+1` 即落到**调用点的下一条**
   （`CALL` 处压栈的是"调用点 ip"，故 `RETURN` 本身**不额外 `+1`**）
4. `mem[symTable[栈顶函数名][0] // 4] = returnValue` —— 即**返回值写进调用点那个变量**（`x`）

⇒ **ARG 压栈 / PARAM 弹栈**，所以 **`ARG` 的书写顺序必须与 `PARAM` 声明顺序相反**。
要求书 4.1.1 第 10 条与样例 2 都明确了这一点。

### 3.5 内存模型

- `mem = [0] * 262144`（262144 个字 = 1 MiB），地址按**字节**计，访问时 `//4`
- 装载期检查 `offset > 1048576` 即判失败
- `main` 的变量在装载期按出现顺序分配 `offset`；**非 main 函数**的变量装载期地址为 `-1`，
  每次 `CALL` 时重新分配、`RETURN` 时恢复（§3.4）
- 运行期越界 → `IndexError` → 报 "Illegal memory access"
- ⚠️ **`PARAM` 遇到空的 `argumentStack` 也报"内存错"**：Python 的 `list.pop()` 在空表上抛
  **`IndexError`**，而真机整个 `execute_code` 包在 `except IndexError` 里 ⇒ 一条**实参下溢**
  会被报成 "Illegal memory access"。这是真机的**真实行为**，不是我们的 bug，**别去"修"它**。
  ⇒ 生成器必须保证 `ARG` 与 `PARAM` 配对（§3.4）；参考机同构地把 `arg_stack.pop()` 放在
  被 `except IndexError` 覆盖的 `_step` 里。
- ⚠️ Python 的负下标会静默回绕（`mem[-1]` 合法），**不得依赖**，我们的生成器保证地址非负

### 3.6 效率指标

`execute_code` 的**第一件事**就是 `self.instrCnt += 1`，所以**每一轮主循环派发 = 计数 1**。但
`run()` 主循环在 `execute_code` 之后还有一个**无条件**的 `self.ip += 1`，而 `GOTO` / `IF`（成立）/
`CALL` / `RETURN` 已把 `self.ip` 写成了**目标标号自身的下标**（`labelTable[...]`），
两者叠加 ⇒ **跳转命中的那一行（必然是 `LABEL` / `FUNCTION` 行）被那个 `+1` 跨过**。

> **效率 = 动态执行的指令行数。**
> `DEC`（空操作）会被计数；作为**入口**或**顺序落到**的 `LABEL` / `FUNCTION` 行会被计数；
> **被跳转（`GOTO`/`IF`/`CALL`）命中的那条 `LABEL` / `FUNCTION` 行被跨过 —— 既不执行、也不计数。**

（此口径由**原版 irsim 对拍**确证并修正，见 `docs/lab3/refsim-provenance.md` §2-6。
`scripts/irsim_ref.py::_step` 里 `GOTO`/`IF`(成立)/`CALL` 三个分支返回 `labelTable[...] + 1`，
`RETURN` 返回压栈的 `调用点 ip + 1`，非跳转返回 `ip + 1`。）

这直接给出优化方向：**减少被执行的指令条数**（而不是减少静态行数）。

### 3.7 已知语义陷阱（必须记录并回避）

1. **`/` 是整除（向下取整）**：装载期把 `/` 改写成 Python 的 `//`。对负数与 C 的
   "向零截断"**不一致**（`-7/2` 在 C 里是 `-3`，在此虚拟机上被 `--` 语义算出 `-4`）。
   ⇒ 期望输出的生成器必须模仿 `//`（见 §9.2），且这条要作为一个**暂定取舍**记录下来（§11）。
2. **写回目标为 `*x` 的指令会崩（`*x := a op b` 与 `*x := CALL f` 同源）**：
   `ARITH` 分支只做 `symTable[code[1]][0]`，`RETURN` 写回返回值那一步同样是
   `mem[symTable[stackItem[1]][0] // 4] = returnValue` —— **两处都是裸下标、都没有 `*` 处理**，
   而 `*x` 不在表里（`tableInsert` 会把 `*` 剥掉，表里只有 `x`）⇒ 抛 **`KeyError`**，
   且真机**不捕这个异常**，它会一路抛到调用者。
   ⇒ 生成器**永远**先生成 `t := a op b`，再 `*x := t`；调用结果的接收目标也不写 `*x`。
   **★ 参考机已复现该崩溃**（`scripts/irsim_ref.py` 的 `ARITH` 与 `RETURN` 两个分支
   **都不走 `store_var`**，直接写 `st.mem[st.symtab[...][0] // 4] = ...`，与真机同构）。
   这不是"特判 `*` 然后抛"，而是走**与真机同一条取值路径**。
   **理由**：参考机的用途是**预测判分机**，任何「更宽容」的偏离都只会制造假绿 ——
   若哪天生成器真吐出 `*x := a op b`，宽容版会报本地绿、真机却崩。
   边界用一组用例钉住：`Test/lab3/unit_exec/deref_arith.ir` 与 `deref_call.ir`（各应抛
   `KeyError`，且断言钉住 `e.args == ("*p",)`，免得别处的无关 `KeyError` 顶包），加上
   `deref_mov.ir`（`MOV` 的 `*` 源与 `*` 目标都**必须正常**，证明收紧只落在
   `ARITH`/`RETURN` 的写回、没把所有 `*` 都封了）。
   注意 `MOV`（`x := y`）**支持** `*x`，别一起封了；而 `READ`/`PARAM` 的左值装载期就要求
   首字符是字母（§3.2），根本写不出 `*x`，与本案无关。
3. **`&`/`#` 不能在 `:=` 左边**（装载期报语法错）。
4. **一行一条指令**，空行会被跳过（不占下标），故输出**不写空行**。
5. **`ip` 越过 `codes` 末尾 ⇒ 真机的 `error_code = 3`**：真机主循环里有
   `if self.ip >= len(self.codes): error_code = 3`，收尾报 `"Simulation failed: PC error."`。
   ⇒ 参考机的 `status == "pc"` 这一档**有真机依据**（`scripts/irsim_ref.py` 的 `_execute`
   同构地在 `ip >= len(p.codes)` 时置 `status = "pc"`），不是我们编的。它意味着这份 IR
   **没有正常的 `RETURN` 出口**（`main` 一路走到末尾而没 `RETURN`）——即生成器的缺陷，
   而不是输入的问题。

---

## 4. 架构与模块

**方案：IR 先生成到内存，独立打印函数输出；优化作为独立 pass 插在"生成完、打印前"。**
依据：指导书 4.2.2 明确说"一边遍历一边打印其实并不好，因为代码一打印出来就失去了优化的机会"。

```
lexical.l ─┐
           ├─► syntax.tab.c ─► tree.c(AST) ─► semantic.c(语义检查)
syntax.y ─┘                                        │
                                                   ▼
                              translate.c ──► optimize.c ──► ir.c(打印)
                                   │               │
                                   └──── ir.c(IR 数据结构 + 构造函数)
main.c 驱动：cc <输入文件> [<输出文件>]
```

新增文件（其余沿用实验二 `main` 分支）：

| 文件 | 职责 |
|---|---|
| `src/ir.h` / `ir.c` | Operand / InterCode / 双向循环链表、构造函数族、`printIR()` |
| `src/translate.h` / `translate.c` | `translate_Program / ExtDef / FunDec / CompSt / StmtList / Stmt / Def / Dec / VarDec / Exp / Cond / Args` |
| `src/optimize.h` / `optimize.c` | 优化 pass（§7） |

**模块边界**：`translate.c` 只依赖 AST（`tree.h`）、符号表（`symtab.h`）与 IR 构造函数（`ir.h`），
**不直接 `printf`**；`ir.c` 是唯一知道输出格式的地方。这样"换输出格式"与"加优化"互不影响，
也使得"每个单元可独立测试"成立。

---

## 5. IR 数据结构

按指导书 4.2.2 图 5(c)：**双向循环链表**（插入/删除/换位 O(1)、无行数上限——优化 pass 正需要）。

```c
typedef struct Operand_ *Operand;
struct Operand_ {
    /* OP_ADDR 表示「取地址」形式 `&x`：它既作为 `ARG &arr` 的操作数出现，
       也作为 `x := &y + #8` 这类 ARITH 的操作数出现（虚拟机 getValue 认识 `&` 前缀）。
       `*x` 不单设 kind：它只出现在赋值两侧，由 IR_ASSIGN_DEREF / IR_DEREF_ASSIGN 表达。 */
    enum { OP_VAR, OP_CONST, OP_LABEL, OP_FUNC, OP_ADDR } kind;
    union { int var_no; int value; int label_no; int func_no; } u;   /* OP_ADDR 用 u.var_no */
};

typedef struct InterCode_ {
    enum { IR_LABEL, IR_FUNCTION, IR_ASSIGN, IR_ADD, IR_SUB, IR_MUL, IR_DIV,
           IR_ADDR, IR_DEREF_ASSIGN, IR_ASSIGN_DEREF, IR_GOTO, IR_IF, IR_RETURN,
           IR_DEC, IR_ARG, IR_CALL, IR_PARAM, IR_READ, IR_WRITE } kind;
    union {
        struct { Operand x; } one;                     /* GOTO/RETURN/ARG/PARAM/READ/WRITE/LABEL/FUNCTION */
        struct { Operand x, y; } assign;               /* x := y */
        struct { Operand x, y, z; } binop;             /* x := y op z */
        struct { Operand x, y; } deref;                /* *x := y / x := *y */
        struct { Operand x, y, z; int relop; } iff;    /* IF x relop y GOTO z */
        struct { Operand x; int size; } dec;           /* DEC x size */
        struct { Operand x, f; } call;                 /* x := CALL f */
    } u;
} InterCode;

typedef struct InterCodeList_ {
    InterCode *code;
    struct InterCodeList_ *prev, *next;
} InterCodeList;
```

命名：变量 `v1, v2, …`，临时变量 `t1, t2, …`，标号 `label1, label2, …`
（要求书明说这只是样例风格、非强制；沿用它便于与要求书样例逐行对读）。

打印规则（`printIR` 是唯一知道格式的地方）：
`IR_ASSIGN` → `x := y`；`IR_ADDR` → `x := &y`；`IR_ASSIGN_DEREF` → `x := *y`；
`IR_DEREF_ASSIGN` → `*x := y`；`IR_DEC` → `DEC x size`；`IR_CALL` → `x := CALL f`；
`IR_LABEL`/`IR_FUNCTION` → `LABEL x :` / `FUNCTION f :`；
`IR_IF` → `IF x <relop> y GOTO z`；其余单操作数 → `KIND x`。

---

## 6. 翻译模式

### 6.1 核心思想：`place` 作为**继承属性**

指导书表 2 的朴素写法是"先算进临时变量、再拷进目标"，每条表达式多一条 `MOV`。
本设计让 `place` 一路继承下去，**在目标允许时直接把结果算进目标**：

| 场景 | 朴素（指导书表 2） | 本设计 | 省 |
|---|---|---|---|
| `n = read();` | `READ t1` + `v1 := t1` | `READ v1` | 1 |
| `x = 5;` | `t1 := #5` + `x := t1` | `x := #5` | 1 |
| `result = 1;` | `t := #1` + `result := t` | `result := #1` | 1 |
| `if (n > 0) …` | `t2 := #0` + `IF v1 > t2 GOTO L` | `IF v1 > #0 GOTO L` | 1 |
| `result = fact(m);` | `t5 := CALL fact` + `v3 := t5` | `v3 := CALL fact` | 1 |

依据不只是"更优"：**要求书 4.1.6 样例 1 自己给出的"优化后中间代码"正是这么做的**
（对比样例 1 的原始版与优化版：`t2 := #0` 只留一次、`t6 := #-1` 直接写）。

唯一必须补回拷贝的情形：赋值表达式的值**还要被使用**时（如 `a = b = 1;` 中内层赋值的结果），
此时生成 `place := variable` 一条。

### 6.2 表达式

`translate_Exp(Exp, place)`，`place == NULL` 表示值不需要被使用（表达式语句）：

| 产生式 | 翻译 |
|---|---|
| `INT` | `[place := #值]` |
| `ID` | `[place := 变量]` |
| `Exp1 ASSIGNOP Exp2`（Exp1 为 ID） | 若 `place == NULL`：直接 `translate_Exp(Exp2, 变量)`<br>否则：`translate_Exp(Exp2, 变量)` + `[place := 变量]` |
| `Exp1 PLUS/MINUS/STAR/DIV Exp2` | 两侧各要一个临时变量（除非可直接用立即数/变量），`[place := t1 op t2]` |<br>（**落地位置**：Task 8 先一律经临时变量；"可直接用立即数/变量"的那半落在 **Task 9** 的条件翻译（`IF v1 > #0` 不落临时变量），其余等价形态由 **Task 16 起**的优化 pass 折叠消掉。）
| `MINUS Exp1` | `translate_Exp(Exp1, t1)` + `[place := #0 - t1]` |
| `LP Exp RP` | `translate_Exp(Exp, place)` |
| `Exp1 RELOP/AND/OR Exp2`、`NOT Exp1` | `[place := #0]` + `translate_Cond(...)` + `[LABEL l_true] [place := #1] [LABEL l_false]`（指导书表 2） |
| `ID LP RP`，ID 为 `read` | `[READ place]` |
| `ID LP RP`，ID 为普通函数 | `[place := CALL f]` |
| `ID LP Args RP`，ID 为 `write` | `translate_Args(...)` + `[WRITE arg1]` + `[place := #0]` |
| `ID LP Args RP`，ID 为普通函数 | `translate_Args(...)` + 逆序 `[ARG …]` + `[place := CALL f]` |
| `Exp1 LB Exp2 RB` | 见 §6.4 |
| `Exp1 DOT ID` | 选做 3.1，本次不实现 |

`translate_Args(Args, arg_list)`：按指导书表 6，`arg_list` 逐项**头插**以得到逆序。

### 6.3 语句与条件

`translate_Stmt` / `translate_CompSt` / `translate_StmtList` / `translate_Def` / `translate_Dec` /
`translate_VarDec` 按指导书表 3（`Exp SEMI` / `CompSt` / `RETURN` / `IF` / `IF-ELSE` / `WHILE`）
与指导书给 `translate_Exp` 的结构照搬。

`translate_Cond(Exp, label_true, label_false)` 按指导书表 4 的**短路**形式，
`label_true`/`label_false` 作为**继承属性**（函数参数）处理，**不做回填**：

- `Exp1 RELOP Exp2` → 两侧取值后 `[IF t1 op t2 GOTO label_true]` + `[GOTO label_false]`
  （两侧是立即数或简单变量时直接内联，见 §6.1）
- `NOT Exp1` → `translate_Cond(Exp1, label_false, label_true)`
- `Exp1 AND Exp2` → `translate_Cond(Exp1, l1, label_false)` + `[LABEL l1]` + `translate_Cond(Exp2, label_true, label_false)`
- `Exp1 OR Exp2` → `translate_Cond(Exp1, label_true, l1)` + `[LABEL l1]` + `translate_Cond(Exp2, label_true, label_false)`
- 其它 → 求值到 `t1` 后 `[IF t1 != #0 GOTO label_true]` + `[GOTO label_false]`

**不做回填是刻意的**：把跳转目标做成继承属性后，条件表达式内部要往外跳时目标已经在手上，
无需第二遍扫描。这条要在代码注释里写明理由。

### 6.4 数组地址计算（必做一维 + 选做 3.2）

设 `elem_size = 4`（本实验只有 int）。下标从 0 起。

**(a) 一维数组局部变量 `a[i]`**（必做）：

```
t := i * #4            （i 为常量时折成：直接用 #(i*4)）
t2 := &a + t           （i 为 0 时折成：t2 := &a）
```
右值再 `x := *t2`；左值直接 `*t2 := y`。

**(b) 高维数组变量 `r[i][j]`**（选做 3.2d）：

**一次线性化**成"基地址 + 总偏移"，**不把 `r[i]` 当作中间数组表达式去物化行地址**：

```
t1 := i * #(sizeof(行))      行大小 = 4 * ∏(后续维的长度)
t2 := j * #4
t3 := t1 + t2
t4 := &r + t3
```
对照"链式物化行地址"的写法 `t1 := &r`（或 `&r + #0`）+ `t2 := i * #8` + `t3 := t1 + t2` + `t4 := j * #4`
+ `t5 := t3 + t4` + `t6 := *t5`——**多一条指令，且多一个活着的地址临时变量**。
这正是指导书 4.2.8 末尾点出的优化点（"这多次地址计算合并成一次，效率就会得到一定的提高"）。

**(c) 数组作函数参数 `int temp[2]`**（选做 3.2c）：

- 调用点：`translate_Args` 对数组实参生成 **`ARG &arr`**（传地址 / 引用调用，指导书 4.2.4 明确要求）
- 被调函数：形参 `temp` 收到的**就是基地址**，故 `temp[i]` 的地址 = `temp + i*4`：
  `t := i * #4` + `t2 := temp + t`，**不再有 `&`**
- 区分点收敛在一处：`VarDec` 翻译时给该变量打上"本身就是地址"的标记，`translate_Exp` 取基地址时
  按标记决定发 `&var` 还是直接用 `var`

**(d) 数组作参数时的 `DEC`**：形参不 `DEC`；实参所在函数（拥有者）负责 `DEC`。

### 6.5 声明与函数

- `int a[10];` → `[DEC a 40]`
- 形参 → 函数体开头按声明顺序 `[PARAM v1] [PARAM v2] …`
- 函数定义 → `[FUNCTION f :]` + `[PARAM …]` + body
- `main` 也走 `FUNCTION main :`
- 具名初始化 `int i = 0, j = 0;` → 先 `DEC`（若是数组），再按 `Dec → VarDec ASSIGNOP Exp` 翻译，
  `place` 直接取该变量

### 6.6 输入输出

`read()` → `READ place`；`write(a)` → 求值后 `WRITE x`。
注意指导书表 5 让 `write` 还产生 `place := #0`；**当 `place == NULL` 时这条可省**（省 1 条）。

---

## 7. 优化 pass

对 IR 链表迭代到不动点。每一条规则都必须配"改坏它必须变红"的用例（实验二的纪律）。

| # | 规则 | 说明 |
|---|---|---|
| O1 | 常量折叠 | 两侧都是 `#` 的 `ARITH` 直接算成 `x := #值`；含上述**整除语义**（§3.7-1） |
| O2 | 代数化简 | `x + #0`→`x`、`x - #0`→`x`、`x * #1`→`x`、`x / #1`→`x`、`#0 - #0`→`#0` 等 |
| O3 | 立即数/变量直接进 `ARITH` 与 `IF` | 消掉只为"搬一个常量"而生的临时变量及其 `MOV` |
| O4 | 结果直接写入目标变量 | 消掉 `t := …; x := t` 这种"紧接着的一条纯拷贝" |
| O5 | 冗余拷贝 | `x := x` 删除 |
| O6 | 无引用 `LABEL` 删除 | 需要一遍标号引用计数 |
| O7 | `GOTO` 指向紧邻下一条指令 | 删除（fall-through） |
| O8 | `GOTO → LABEL → GOTO` 链折叠 | 迭代到不动点 |
| O9 | 条件跳转 fall-through 消减 | 如 `IF … GOTO L` / `GOTO L` / `LABEL L` 结构中多余的 `GOTO` |

**正确性论证要点**（写进代码注释与报告）：
- O1–O5 是**基本块内**的局部重写，不跨标号，故不影响控制流；
- O6–O9 只依赖**静态标号引用表**，删除的标号保证无引用；
- **不做**跨基本块的活跃性/数据流分析（超出本实验收益，且是缺陷的温床）。

**度量**：每个用例记录优化前/后的 `instrCnt`（§9.1），差值写进报告。

---

## 8. 命令行接口

要求书 4.1.2：程序接收一个输入文件名与一个输出文件名。

```
cc <输入文件> <输出文件>   # 中间代码写入输出文件
cc <输入文件>              # 中间代码写 stdout（兼容参考 Makefile 的 test 目标：./parser ../Test/test1.cmm）
```

- `Code/Makefile` 产出的二进制名叫 `parser`（官方 Makefile 固定），故驱动的 `argv[0]` 与名字无关；
- 输入含结构体（未实现 3.1）时，向 std**out** 打印一行
  `Cannot translate: Code contains variables or parameters of structure type.`，
  **输出文件为空（不写中间代码）**，进程仍 `return 0`。
  （驱动在解析前就按第二个参数打开输出文件，故文件存在但为 0 字节；这与
  `Test/unit/test_struct.sh` 的断言口径一致。）
- **实验三不再向 stdout 打印语法树**（见 §11 P7）。实验一的语法树输出是为了实验一的评测；
  实验三的输出契约是"中间代码"，两者混在一起会让 `./parser in out` 的产物无法判定。
  实现中不再调用语法树打印，也**没有** `LAB3_PRINT_TREE` 编译期开关。

---

**单参数调用的依据（2026-10-15 定）**：要求书 4.1.2 明说程序接收**两个**参数
（`./cc test1 out1.ir`），4.1.3 明说将运行结果输出到**文件**——故判分机必然按两参数调用。
单参数时的 stdout 输出是**额外的保险**（判分机若用单参数并捕获 stdout 也应付得来），
代价见下。

**这条改动的已知代价**：`scripts/run_tests.sh` 是用 `./parser <文件>` **单参数**跑
`Test/lab2` 那约 50 个用例的（`Test/sample`/`Test/err` 走的是另一个二进制
`Test/regress/driver`，不受影响）。给 lab2 那一圈补一个丢弃用的输出文件即可让
stdout 行为回到实验二 —— **既有期望值一个都不用改**。见实现计划 Task 12 Step 1b。

## 9. 验证体系

### 9.1 无头参考机 `scripts/irsim_ref.py`

严格按 §3 的语义实现（纯 Python、无 PyQt、无需 3.8）：

- 输入：某一 `.ir` 文件 + 预设的 `READ` 输入序列
- 输出：`WRITE` 产生的值序列 + **`instrCnt`** + 结束状态（正常结束 / 内存越界 / PC 越界）
- 必须复现的细节：`#`/`&`/`*` 取值、`ARG` 压栈 `PARAM` 弹栈、`CALL` 重新分配地址、
  `RETURN` 写回调用点变量、`/` 的整除语义、以及**跳转与计数的真实关系**（见 §3.6：跳转命中的标号行被跨过、不计数）
- **参考机不得比真机宽容**：真机因未加防护而崩/抛的地方，参考机也要照样崩
  （已按此收紧的有三处：`*x := a op b` 与 `*x := CALL f` 的 `KeyError`、单 token 行的 `IndexError`）

**残留风险（如实记录）**：参考机是按字节码转写的，转写若与真实 `irsim` 有偏差，
本地仍可能"绿"而线上不一致。缓解：
1. **已用原版 `irsim`（Python 3.8 + PyQt5，装在仓内 `build/py38/`）做过 48 条对拍**，
   并据此**证伪并修正**了原先的计数模型（详见 `docs/lab3/refsim-provenance.md`）；
   此前的缓解措施只是"逐条对照 §3 复核"，达不到这个强度；
2. 交付前用**原版 irsim**（需 Python 3.8 + PyQt5）抽验关键用例——本次未选，交付前再确认一次。

### 9.2 独立的期望输出 oracle ★

**关键一招：`.cmm` 程序基本是 C 的子集**。给 `read`/`write` 套两个垫片，
用 `gcc` 编译同一份程序并运行，得到**真实输出**作为期望值。这条链**完全不经过我们的编译器**，
因此不会自证（实验一/二"绿色掩盖"的根治点）。

垫片要点：
- `read()` → 从预设输入序列依次取值的函数
- `write(v)` → 打印的函数
- **`/` 必须改成"向零截断 vs 向下取整"的抉择**：为对齐虚拟机的 `//`（§3.7-1），
  垫片把 `/` 替换为向下取整的除法助手函数；这条是**暂定取舍**（§11）

### 9.3 用例体系

放在 `Test/lab3/`，每个用例 = `xxx.cmm` + `xxx.in`（READ 输入）+ `xxx.exp`（期望输出）。

| 组 | 覆盖 |
|---|---|
| `req-*` | 要求书 4.1.6 必做样例 1/2、4.1.7 选做样例 1/2（选做样例 1 = 结构体，期望是 §1.2 那行提示） |
| `basic` | 赋值/算术/关系/`&&`/`\|\|`/`!`/取负、`if`/`if-else`/`while`/嵌套、`return` |
| `arr1d` | 一维数组读写、下标为常量/变量/表达式、数组作 `read` 目标 |
| `func` | 多函数、递归、`read`/`write`、形参与实参顺序、返回值 |
| `arrnd` | 高维数组（2 维、3 维）、数组作参数（3.2c） |
| `opt` | 每条优化规则的"改坏必须变红"用例（O1–O9 各至少一个） |

### 9.4 回归入口

`scripts/run_lab3.sh`：构建 → 跑用例 → 比对 → 报告 `pass/fail` 计数与
优化前/后的 `instrCnt` 汇总。**`pass=0` 必须报红退出**（实验一栽过：
"通过 0 个"仍退出 0）。缺 `.exp` 必须 **FAIL 而非 SKIP**。

---

## 10. 报告

- 位置：仓库 `docs/实验三报告.tex`（沿用实验一/二的 LaTeX 体系），产物复制为 `作业三/report.pdf`，**≤3 页**
- 口吻：**只讲实现与为什么这样实现**，不讲判分/自测/开发过程（沿用实验一基准写法）
- 必须包含：姓名、学号、联系邮箱；如何编译
- **亮点 3 节，选题必须避开已用过的 6 个题**：

  已用：① 行号推导与 ε 判定等价 ② 错误恢复的同步点设计 ③ 为什么必须声明 `%left LB DOT`
  ④ 语义分析做成一遍独立后序遍历 ⑤ 符号表「一张散列表 + 十字链表」 ⑥ 一个在语义层观测不到的
  bug 决定了模块边界

  候选：
  1. **`place` 作为继承属性——把结果直接算进目标变量**（含可量化的指令数收益，§6.1）
  2. **多维下标的地址线性化**（§6.4b，直接回应指导书 4.2.8 的提示）
  3. **独立优化 pass 与"先生成到内存、再统一打印"的架构**（§4、§7）

  每节都按同一根线写：**最自然的实现是什么 → 它在哪个具体输入上多花指令 → 我怎么做 → 赢了什么**。

---

## 11. 暂定取舍（要求书未钉住者）——每条 = 开关 + 专属用例

| # | 情形 | 暂定行为 | 落点 | 用例 |
|---|---|---|---|---|
| P1 | `/` 的语义（虚拟机是 `//` 整除，C 是向零截断） | 以**虚拟机**为准：优化时按向下取整折叠 | `optimize.c` 一处常量折叠分支 + §9.2 垫片一处 | `opt/div-neg` |
| P2 | `write` 是否产生 `place := #0` | `place == NULL` 时不产生 | `translate.c` 的 `write` 分支 | `basic/write-stmt` |
| P3 | 赋值表达式的值是否必须落地 | 仅当 `place != NULL` 时补 `place := variable` | `translate.c` 的 `ASSIGNOP` 分支 | `basic/chain-assign` |
| P4 | 未实现 3.1 时的提示与输出文件 | 打印规定英文提示，输出文件**存在但为空**（0 字节，不写中间代码），退出码 0 | `translate.c` 入口一处 | `req/struct-sample` |
| P5 | 一维数组作参数时形参是否 `DEC` | 不 `DEC`（形参本身就是地址） | `translate.c` 的 `ParamDec` 分支 | `arrnd/arrparam` |
| P6 | 变量/标号命名风格 | `v*/t*/label*`（要求书明说非强制） | `ir.c` 一处 | 全部用例 |
| P7 | 实验三是否仍向 stdout 打印语法树 | **不打印**（要求书 4.1.2/4.1.3 的输出契约只有中间代码）；代码保留但不再调用，无编译期开关 | `main.c` 一处 | `basic/hello` 的 stdout 必须为空 |
| P8 | `place` 与条件里读到的操作数**同名**（`x = !x;`／`x = x > 0;`／`x = x && y;`）时的求值次序 | **先清后读**：照指导书表 2 模板 `code0 = [place := #0]` 排在最前，条件再惰性读操作数，于是读到的是刚清掉的 0（**与 C 语义不同**）；等评测反馈再定，改动前必须先确认判分口径 | `translate.c` 的 `translateCondAsValue` + `translateCond` 的 `EX_ID` 惰性读 | `Test/unit/test_cond.c` 的 `x = !x;` |

每条都必须：**行为收敛在代码里的一个位置**、那个位置**带注释点明"要求书未规定，见 spec §11 第 N 条"**、
**各配一个专属用例**。（这三条来自实验二的教训。）

---

## 12. 风险清单

| 风险 | 影响 | 缓解 |
|---|---|---|
| 参考机与真实 `irsim` 有转写偏差 | 本地绿、线上红（实验一/二的病根） | §9.1 的两条；交付前考虑用原版抽验 |
| 选做押注（3.2 vs 3.1） | 若判分口径相反则失分 | 编译期开关 + 共用地址运算基础设施，补 3.1 的边际成本约为一半 |
| 报告与实验一/二撞题 | 观感与评分 | §10 已列禁用题，候选三题互相错开 |
| 交付包形态与判分机期望不符 | 直接无法评测 | 照抄实验二实测通过的形态（§2.1），并在打包后跑两条构建路径对拍 |
| 优化引入正确性回归 | 效率上去了、结果错了 | 优化 pass 可一键关闭；每条规则配反例用例；A3 每条用例都跑优化开/关两遍 |

---

## 13. 里程碑

1. **M1** 无头参考机 + gcc oracle 垫片 + 用例骨架（先把"反馈回路"建起来，再写编译器）
2. **M2** IR 数据结构 + 打印 + `translate` 必做路径 → 过 `basic/`、`func/`、`arr1d/`
3. **M3** 选做 3.2（数组作参数、高维数组）→ 过 `arrnd/`
4. **M4** 优化 pass O1–O9 → 过 `opt/`，并产出优化前后 `instrCnt` 对照
5. **M5** 打包 + 两条构建路径对拍 + 报告 → 交付
