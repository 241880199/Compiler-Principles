# 实验四（目标代码生成）设计文档

- 日期：2026-11-05
- 状态：设计已确认；实现计划见 `docs/superpowers/plans/2026-11-05-cmm-lab4.md`
- 代码仓库：`D:\Projects\编译原理\作业一\Compiler-Principles`，分支 `lab4`（从 `lab3` 切）
- 交付包：`D:\Projects\编译原理\作业四\proj4_submission.zip`
- 范围：**只做必做内容**，不做任何选做（实验四本身也没有选做）

---

## 1. 实验内容与目标

把实验三产出的三地址中间代码（`InterCodeList`）翻译为 MIPS32 汇编（允许伪指令），
使输出能在 SPIM Simulator 上正确运行。判据只有一个：**SPIM 能执行且结果正确**；
寄存器的使用、栈管理方式都可以不遵循 MIPS 约定（要求书 5.1.1 明示）。

### 1.1 要求书对输入的假设（5.1.1）

1. 输入无词法/语法/语义错误，函数必有 `return`
2. 无注释、八进制/十六进制常数、浮点常数或变量
3. 整型常数都在 16 位有符号范围内
4. 无结构体、无高于一维的数组（实验三的 IR 已能处理高维数组，代码生成沿用即可）
5. 无全局变量，所有变量不重名，变量存储放在所属函数的活动记录里
6. 函数参数只能是简单变量（数组、结构体不作参数）
7. 函数不返回结构体或数组
8. 函数只定义一次（没有声明）

**推论**：不需要处理全局数据段中的变量；所有局部变量、临时变量、`DEC` 数组都在栈帧内。
实验三的 IR 可能包含数组形参与高维数组（选做 3.2），代码生成照常支持，不增加额外设计。

### 1.2 非目标

- 不做全局图染色、不做活跃变量分析、不追求指令数最优
- 不修改实验三的翻译层与优化层
- 不引入第三方库；只使用 SPIM 自带的伪指令与 `syscall`

---

## 2. 总体架构

```
C-- 源码
  │ lex / bison / semantic（实验一、二，不改）
  ▼
AST ──translate.c──► InterCodeList ──optimize.c──► InterCodeList
                                                       │
                                                       ├─ printIR()  → .ir（实验三口径）
                                                       └─ generateMIPS() → .s（实验四）
```

新增模块：

| 文件 | 职责 |
|---|---|
| `src/target.h` / `src/target.c` | IR → MIPS32 汇编的唯一实现；只读 IR 链表，不改动 IR |
| `scripts/run_lab4.sh` | 端到端测试台：编译 → SPIM 执行 → 与 `.exp` 逐字节比对 |

修改：

| 文件 | 改动 |
|---|---|
| `Makefile` | `SRCS` 增加 `src/target.c`；`%.o` 依赖表增加 `src/target.h` |
| `src/main.c` | 输出模式分派；默认 `.ir` 输出 IR，其余输出 MIPS；支持 `--ir` / `--asm` 覆盖 |
| `README.md` | 增加实验四章节（功能、编译、运行、测试） |

### 2.1 输出模式分派

- `--ir`：强制输出中间代码（实验三回归用）
- `--asm`：强制输出 MIPS32
- 缺省：输出文件后缀为 `.ir` → IR；其余（含 `.s`/`.asm`/无后缀/stdout）→ MIPS

这样 `./cc in.cmm out.ir` 保持实验三行为，`./cc in.cmm out.s` 满足实验四判分习惯。

---

## 3. 栈帧与调用约定

不遵循 MIPS ABI，采用对编译器实现最简单、对递归天然安全的一套约定。

### 3.1 活动记录布局

```
高地址
        ┌──────────────────────────────┐
        │ 调用者压入的实参              │  0($fp) = 第 1 个实参
        │  0($fp), 4($fp), …           │
  $fp ──┼──────────────────────────────┤
        │ -4($fp)  旧 $ra               │
        │ -8($fp)  旧 $fp               │
        │ -12($fp) 起：局部变量/临时变量 │
        │           与 DEC 数组          │
        └──────────────────────────────┘
  $sp 指向当前帧最低处
低地址
```

- 帧大小 = 8（`$ra`、旧 `$fp`）+ 局部区大小，按 8 字节对齐。
- 局部区按“每个变量 4 字节；`DEC x n` 的数组占 n 字节”顺序分配，偏移在函数入口一次算定。
- `$fp` 在函数执行期间不变，因此调用前压栈不会影响局部变量寻址。

### 3.2 Prologue / Epilogue

```asm
# prologue（frameSize 为常量）
addiu $sp, $sp, -frameSize
sw    $ra, frameSize-4($sp)      # 等价于 -4($fp)
sw    $fp, frameSize-8($sp)      # 等价于 -8($fp)
addiu $fp, $sp, frameSize
# 形参：第 i 个 PARAM 从 4*i($fp) 拷进它自己的局部槽
lw    $t0, 0($fp)
sw    $t0, paramSlot($fp)
...
```

```asm
# epilogue（函数末尾统一一处）
lw    $ra, -4($fp)
move  $sp, $fp
lw    $fp, -8($sp)
jr    $ra
```

`IR_RETURN x` 先 `lw/li $v0, x`，再 `j .L_epilogue_<函数名>`。函数末尾的 epilogue
标号只出现一次，多个 `return` 共用。

### 3.3 调用序列

- 调用者按 IR 的 `ARG` 顺序（**源码逆序**，实验三已保证）逐个 `addiu $sp,$sp,-4; sw`；
- `jal f`；
- 返回后 `addiu $sp,$sp,4*n` 清理实参；
- 被调函数返回值在 `$v0`，调用者把它存进 `IR_CALL` 的目标槽。
- 因为所有实参都走栈、所有局部变量都在帧内，`$t0–$t9`/`$a0–$a3` 无需保存/恢复。

### 3.4 `read` / `write`

直接内联系统调用，不生成 `read:`/`write:` 子程序：

```asm
# READ x
li   $v0, 5
syscall
sw   $v0, slot(x)

# WRITE x
lw   $a0, slot(x)      # 或 li $a0, k
li   $v0, 1
syscall
li   $v0, 4
la   $a0, _ret         # "\n"
syscall
```

`main` 由 SPIM 启动代码 `jal main` 进入，`main` 的 `jr $ra` 返回后 SPIM 正常结束。

---

## 4. 指令选择与寄存器分配

### 4.1 朴素寄存器分配

按指导书 5.2.4：**所有变量与临时变量都放在栈帧里**。每条 IR 指令：

1. 把用到的操作数 `lw` 到 `$t0/$t1`（常量直接 `li`）；
2. 执行运算，结果放 `$t2`；
3. 把结果 `sw` 回目标槽。

这样寄存器只作为指令间的临时周转，不需要寄存器描述符、不需要溢出决策，
正确性最容易保证。代价是访存多；实验四不检查效率。

### 4.2 逐类指令的翻译

| IR | MIPS32 |
|---|---|
| `LABEL L :` | `L:` |
| `FUNCTION f :` | `.globl main`（仅 main）/ `f:` |
| `x := #k` | `li $t0, k` + `sw` |
| `x := y` | `lw` + `sw`（`move` 等价） |
| `x := y + #k` | `lw` + `addi` + `sw` |
| `x := y + z` | 两条 `lw` + `add` + `sw` |
| `x := y - z` / `*` / `/` | `sub`；`mult`+`mflo`；`div`+`mflo` |
| `x := &y` | `addiu $t0,$fp,offset(y)` + `sw` |
| `x := *y` | `lw $t0,slot(y)` + `lw $t0,0($t0)` + `sw` |
| `*x := y` | 地址入 `$t0`、值入 `$t1` + `sw $t1,0($t0)` |
| `GOTO L` | `j L` |
| `IF x relop y GOTO L` | 两条 `lw`/`li` + `beq/bne/bgt/blt/bge/ble` |
| `RETURN x` | `lw/li $v0` + `j epilogue` |
| `DEC x n` | 无指令（预扫描已分配 n 字节） |
| `ARG x` | 载入 + 压栈 |
| `x := CALL f` | `jal f` + 清理实参 + `sw $v0,slot(x)` |
| `PARAM x` | prologue 中从 `4*i($fp)` 拷入 `slot(x)` |
| `READ x` / `WRITE x` | 见 §3.4 |

### 4.3 预扫描

对每个函数（`IR_FUNCTION` 到下一个 `IR_FUNCTION` 或表尾）：

1. 收集所有 `IR_DEC`：数组名 → 字节数；
2. 收集所有作为操作数出现的 `OP_VAR`（目标、源、`PARAM` 名）：
   去重后每个分配 4 字节（若已在 DEC 中则用数组大小）；
3. 统计 `IR_PARAM` 个数与顺序；
4. 得到 `frameSize`，然后在函数入口输出 prologue。

未知名字（理论上不会出现）必须**响亮失败**：`fprintf(stderr, ...) + abort()`，
绝不静默分配一个 0 偏移槽。

---

## 5. 测试与验收

### 5.1 单元/端到端测试台

`scripts/run_lab4.sh`：

```
对每个 .cmm：
  ./cc in.cmm build/lab4/<tag>.s         # 生成汇编
  spim -quiet -file <tag>.s < in.in       # WSL 内执行
  与同名 .exp 逐字节比对
```

判据：

- 每个用例必须有 `.in` 与 `.exp`，缺失即 FAIL（不得 SKIP）；
- `cc` 退出码非 0 或 stderr 非空即 FAIL；
- SPIM 退出码非 0 或输出不符即 FAIL；
- 一个用例都没跑必须退出 1。

### 5.2 用例覆盖

- 实验三全部 24 个真实用例（`Test/lab3/{basic,arr1d,arrnd,func,req}`）；
- 新增实验四用例：
  - 递归（阶乘、Fibonacci）
  - 多实参与实参顺序（3 个、5 个实参）
  - 嵌套调用（调用表达式作为实参）
  - 数组形参、高维数组
  - 多函数程序与 `main` 调用
  - 边界：除零以外的负数除法（MIPS `div` 语义与 C 一致：向零截断）

### 5.3 回归

- `bash scripts/run_tests.sh`：实验一/二 73 个用例全绿
- `bash scripts/run_lab3.sh`：实验三 24 个用例全绿（`.ir` 后缀仍走 IR）
- 所有构建必须零 gcc 警告（`scripts/check_warnings.sh`）

---

## 6. 交付物

| 交付物 | 位置 |
|---|---|
| 源代码（lab4 分支） | 仓库 `src/`、`Makefile`、`README.md` |
| 实验报告 | `docs/实验四报告.tex` / `.pdf`，≤3 页，五号字以上 |
| 测试台 | `scripts/run_lab4.sh` + `Test/lab4/` |
| 交付包 | `作业四/proj4_submission.zip`：根 `report.pdf` + `Code/` 平铺源码 |

报告结构（3 页以内）：

1. 实现了哪些功能、如何实现（IR → MIPS、栈帧、调用约定、数组/参数/递归）
2. 如何编译（`make`、`./cc in.cmm out.s`、SPIM 运行方法）
3. 亮点：自洽的栈帧与全栈传参约定（为何不需要保存寄存器、递归为何天然正确）、
   代码生成前的 IR 优化复用、SPIM 端到端差分测试

---

## 7. 风险与处置

| 风险 | 处置 |
|---|---|
| SPIM 8.0 与课程 9.1.9 伪指令差异 | 只用两版都支持的 `li/la/move/addi/add/sub/mult/div/mflo/jal/jr/lw/sw/bgt/blt/beq/bne` 等 |
| 多实参/嵌套调用的栈偏移错 | 所有局部访问一律经 `$fp`，永不使用 `$sp` 相对偏移 |
| 数组地址与 `&` 语义混淆 | `OP_ADDR` 只在“取数组基址”出现；数组形参是 `OP_VAR`（值即地址），代码生成按操作数种类分派 |
| 实验一/二/三回归被输出模式分派破坏 | `.ir` 后缀保留 IR；回归脚本原样跑 |
| WSL 与 Windows 路径混用 | 构建/执行一律在 WSL 内；仓库路径用 `/mnt/d/...` |
