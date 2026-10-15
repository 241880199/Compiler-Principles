# 参考机 `scripts/irsim_ref.py` 的出处与残余风险

本文记录无头参考机（`scripts/irsim_ref.py`）**每一条语义**的来源：它对应设计文档
`docs/superpowers/specs/2026-10-15-cmm-lab3-design.md` 的哪一节、有没有**独立于我们自己推演**的
证据背书。

> 证据分级：
> - **【官方样例】** —— 由 4.1.6 两段官方 IR（`Test/lab3/unit_exec/sample2.ir`、
>   `sample_opt1.ir`，逐字抄录并已机械核对 17/17、28/28 全等）背书。若空行占一个 `codes`
>   下标，`FUNCTION main` 的入口与所有标号都会错位 ⇒ 跑不出 120。
> - **【原版对拍】** —— 由**课程下发的原版 irsim**（`作业三/irsim.pyc` v2.0.2，CPython 3.8 +
>   PyQt5，真跑）背书。驱动器 `scripts/irsim_orig_probe.py`、比对器 `scripts/compare_refsim.py`。
>   这是本实验里**最强**的一类证据：它就是判分机本身。
> - **【字节码】** —— 由对 `作业三/irsim.pyc` 的字节码反解背书（设计文档 §3 的来源）。
> - **【无独立证据】** —— 只有字节码/推演，没有任何外部判据 ⇒ **残余风险**。

---

## 0. 对拍是怎么做的（先说方法，否则下面的结论无从检验）

原版是 GUI 程序：一被装载就 `QApplication(sys.argv)` → `IRSim()` → `show()` → `exec_()`，
`RUN` 一路靠 `QMessageBox` / `QInputDialog` 说话。驱动器按三处绕开（**原版字节码一行没改**）：

1. **顶层没有 `if __name__ == '__main__'` 保护** ⇒ 装载前把 `QApplication.exec_` 打成空操作。
2. **模态框** ⇒ `QMessageBox.{information,critical,warning,question,about}` 换成记录文本的桩；
   `QInputDialog.getInt` 换成从脚本化序列取下一个整数，序列耗尽返回 `(0, False)`（= 用户取消）。
3. **Qt 平台插件** ⇒ embeddable 版 3.8 下 `PyQt5` 找不到自己的 `PyQt5/Qt5/plugins`，
   `QApplication(...)` 会直接 abort（`qFatal` 走 `OutputDebugString`，stderr 一片空白，
   退出码 `0xC0000409`）。显式设 `QT_PLUGIN_PATH` / `QT_QPA_PLATFORM_PLUGIN_PATH` +
   `QT_QPA_PLATFORM=minimal` 后正常。

跑法：`python scripts/compare_refsim.py`（3.11 侧跑参考机、子进程跑 3.8 侧跑原版），
逐条比 `codes` / `symTable` / `functionDict` / `labelTable` / `entranceIP` /
`outputs` / **`instr_cnt`** / 结束状态 / 异常。

**实测总账（48 条比对）：一致 48，不一致 0。** 首轮对拍曾得 **一致 29 / 不一致 19**：
19 条里 **17 条是同一个根因**（`instrCnt` 计数口径，§2-6）、1 条是 `READ` 取消（§2-10）、
1 条是 `t3.ir` 的异常类型（§2-9）。**三类都已按真机语义修正**（见 §2-6、§2-9、§2-10），
现 **48 条全部一致**。t3.ir 那条（1-token 行 `a:=b`）原先参考机抛干净的 `IRSyntaxError`、
真机抛 `IndexError` —— 那是参考机**比真机宽容**，已按真机**同序取值**修正（§2-9、§2-11）。
**没有任何一条不一致出现在"输出值"或"结束状态"上。**
原始输出见 `build/compare_out.txt`（`python scripts/compare_refsim.py` 可复现）。

---

## 1. 逐条对照

| 参考机语义（设计文档小节） | 证据 | 说明 |
|---|---|---|
| 逐行读、空白行**整行跳过**、不占 `codes` 下标（§3.1） | **【官方样例】**+**【原版对拍】** | 原版 `codes` 与参考机逐项相同（含空行两侧的下标）。 |
| `FUNCTION main` 的入口 = 该行在 `codes` 里的**下标**（§3.2） | **【官方样例】** | `sample2.ir` 空行正卡在 `main` 之前。 |
| `LABEL x :` / `FUNCTION f :` 的形状与登记（§3.2） | **【官方样例】**+**【原版对拍】** | 两段样例都用；原版 `labelTable`（LABEL 与 FUNCTION 都登记）与参考机逐项相同。 |
| `GOTO x` = `ip = labelTable[x]`（§3.2） | **【官方样例】** | 跳转**目标**一致；下游的**计数**口径见 §2-6（已按真机修正：跳转命中的标号不计）。 |
| `IF x op y GOTO z` 的关系运算 `>`、`<`、`==`（§3.2） | **【官方样例】** | 同上。 |
| `READ x` / `WRITE x` 的形状与语义（§3.2、§3.3） | **【官方样例】** | 输入**取消时不写内存**已按真机修正 —— 见 §2-10。 |
| `ARG x` 压栈 / `PARAM x` 弹栈，**实参书写顺序与形参声明顺序相反**（§3.4） | **【原版对拍】** | `argorder.ir`：原版 `outputs=[12]`，参考机 `[12]`（形参/实参写反会得 21）。 |
| `CALL f` 为被调函数**重新分配**局部变量地址、`RETURN` 时**恢复**（§3.4） | **【原版对拍】** | `rec.ir`（含 `sample2.ir` 的递归 `fact`）：原版与参考机都得 `120`。 |
| `RETURN x`：把返回值写进**调用点那个变量**（§3.4） | **【原版对拍】** | 同上；原版 `callStack` 存 `(CALL 的 ip, 目标变量, 旧地址表, 旧 offset)`，与参考机同构。 |
| `RETURN x` 在调用栈空时 = 程序正常结束（§3.4） | **【原版对拍】** | 原版此处 `return 1`（`error_code==1` → "Finish" 对话框），参考机 `status=='ok'`。 |
| `RETURN x` 的 `x` 可以不是立即数（§3.3/§3.4） | **【官方样例】** | `RETURN v1`/`RETURN t3`/`RETURN t2`。 |
| `x := y`（`MOV`）双向语义（§3.3） | **【官方样例】**+**【原版对拍】** | 含 `*x` 的两个方向，见 §2-2。 |
| `#n` 立即数取值，**可带负号** `#-1`（§3.3） | **【官方样例】** | `sample_opt1.ir` 的 `t6 := #-1`。 |
| `x := a op b` 的 `-` 与 `*`（§3.2） | **【官方样例】**+**【原版对拍】** | `sample2.ir` 得 120 双方一致。 |
| `x := CALL f` 的记录形式 `('CALL', x, f)`（§3.4） | **【原版对拍】** | `codes` 逐项相同。 |
| `DEC x size` 进 `codes` 且是空操作（§3.2） | **【原版对拍】** | 见 §2-1。 |
| `WRITE` / `ARG` 无 `isalpha` 限制（§3.2） | **【原版对拍】** | 装载：`write_imm.ir`、`arg_imm.ir` 双方都接受；执行：定向 `write_imm_exec.ir`（`WRITE #5`）双方都输出 `[5]`。 |
| `READ` / `PARAM` 有 `isalpha` 限制（§3.2） | **【原版对拍】** | `read_imm.ir`、`param_imm.ir` 双方都拒绝。 |
| 被引用的标号必须有定义（§3，`labelCheck`） | **【原版对拍】** | **本文件旧版说它"未被反解确证"，现已确证并验证一致** —— 见 §2-4。 |

---

## 2. 曾经"没有任何独立证据"的语义 —— 现在的逐条判词

> 本节即本任务的全部产出。**逐条给出：已由原版验证一致 / 与原版不一致 / 原版本机无法验证。**
> 「一致」一律附上两边的实测值；「不一致」一律附上根因与各自的原始输出。

### 2.1 `DEC x size` 指令 —— **已由原版验证一致**

`counted_dec.ir`（`FUNCTION main` / `DEC a 4` / `RETURN #0`，无跳转）：

| | outputs | instr_cnt | status |
|---|---|---|---|
| 参考机 | `[]` | **3** | `ok` |
| 原版 | `[]` | **3** | `ok` |

⇒ `DEC` 确为**空操作但计数 1**，参考机口径正确。

**附带发现（表示层，无行为差异）**：原版 `codes` 里 `DEC` 是**裸字符串** `'DEC'`
（`sanity_check` 其它分支都 `BUILD_TUPLE`，只有 DEC 直接 `codes.append('DEC')`），
参考机存成 `("DEC",)`。两者在 `execute_code` 里等价：`'DEC'[0] == 'D'` 谁也匹配不上，
落到兜底 `return 0`（已计数）。比较器里做了归一。

### 2.2 解引用 `*var` / `&var` 的全部形态 + 两条 `KeyError` 陷阱 —— **已由原版验证一致**

| 夹具 | 参考机 | 原版 | 判词 |
|---|---|---|---|
| `deref_mov.ir`（`r := *p` 读、`*p := #99` 写） | `outputs=[8, 99]`, cnt 9, `ok` | `outputs=[8, 99]`, cnt 9, `ok` | **一致** |
| `deref_arith.ir`（`*p := q + #1`） | `KeyError('*p')`, cnt 4 | `KeyError('*p')`, cnt 4 | **一致** |
| `deref_call.ir`（`*p := CALL f`） | `KeyError('*p')`, cnt **5** | `KeyError('*p')`, cnt **5** | **一致** |
| `oob.ir`（`*p := #1` 解引用写越界） | `status='memory'`, cnt 3 | `status='memory'`, cnt 3 | **一致** |
| 定向 `deref_read.ir`（`WRITE &q` / `WRITE *p`） | `outputs=[0, 8]`, cnt 6 | `outputs=[0, 8]`, cnt 6 | **一致** |

⇒ §3.7-2 的两条"陷阱"（`ARITH` 与 `RETURN` 的写回目标走**裸下标**、不做 `*` 解引用 ⇒
`KeyError`）**在原版上原样复现**；`MOV` 两个方向都支持 `*` 也复现。参考机没有"更宽容"，
也没有"更严格"。

### 2.3 `/` 的整除（向下取整）语义 —— **已由原版验证一致**

| 夹具 | 参考机 | 原版 |
|---|---|---|
| `divneg.ir`（`(-7) / 2`） | `outputs=[-4]`, cnt 6, `ok` | `outputs=[-4]`, cnt 6, `ok` |
| `divzero.ir`（`1 / 0`） | 抛 `ZeroDivisionError('integer division or modulo by zero')`, cnt 4 | 同类型同消息，cnt 4 |

⇒ 装载期把 `/` 改写成 `//`、运行期用 Python 整除（**向下取整**，`-7/2 == -4` 而非 C 的 `-3`）
得到确证；除零**原样漏出 `ZeroDivisionError`**、不被吞成 `memory`，也确证。

### 2.4 「被引用的标号必须有定义」（`labelCheck`）—— **已由原版验证一致，且原实现已被确证**

本文件旧版（与本任务前的设计文档）声明：真机确有 `labelCheck`，**但其实现未被反解确证**，
参考机里保留的是一层"我们自己的防御性检查"。**这条现已推翻**：

- **字节码**：`labelCheck` 遍历 `codeList`（= 逐行装载成功的那些行）的**文本**，
  `strs[0]=='GOTO'` → 查 `strs[1]`；`strs[0]=='IF'` → 查 `strs[5]`；
  `len(strs) > 2 and strs[2] == 'CALL'` → 查 `strs[3]`。**与参考机 `label_check` 逐字同构**。
- **行为**：`t5.ir`（`GOTO nowhere`）—— 原版弹出
  `"Undefined label at line 2:\n\nGOTO nowhere"` 并把状态置为 `Loading failed.`
  （`initialize()` 把 `entranceIP` 打回 -1）；参考机抛 `UndefinedLabelError('nowhere')`。

⇒ 结论一致（都拒绝），**参考机里那层检查不是"我们加的"，就是真机的语义**。

### 2.5 内存模型细节 —— **已由原版验证一致**

| 断言 | 参考机 | 原版 |
|---|---|---|
| `mem = [0]*262144`（1 MiB 个字），地址按字节存、用时 `//4` | §3.5 | `stop()` 与 `loadFile()` 里都是 `[0] * 262144`；`getValue`/`execute_code` 全部 `… // 4` |
| 运行期越界 → `IndexError` → `status=='memory'` | `oob.ir` → `memory`, cnt 3 | `oob.ir` → `memory`, cnt 3 |
| `PARAM` 遇空 `argumentStack` 也报成内存错 | 定向 `param_empty.ir` → `memory`, cnt **3** | 同 → `memory`, cnt **3** |
| `offset > 1 MiB` 判装载失败 | 定向 `bigdec.ir`（`DEC a 1048580`）→ 抛 `IRSyntaxError('offset exceeds 1MiB')` | 同 → `Loading failed.`、`entranceIP=-1`（**双方都拒绝**） |

### 2.6 `instrCnt` 的计数口径 —— ★ **已修正：与原版逐条一致**

**（首轮对拍时本条被证伪，随后按真机语义修正了参考机；下面先记根因，再记修正与复核。）**

本文件旧版写的是：*"`execute_code` 第一件事就 +1，故 `LABEL`/`FUNCTION`/`DEC` 都计数"*。
**那个规则不完整、会数多。** 空操作指令（`DEC`）与**入口/顺序落到**的 `LABEL` 确实计数，
但真机**不会执行、也不会计数"被跳转直接落到的那一条 `LABEL` 行"**。

**根因：主循环那个无条件 `ip += 1` 与跳转目标下标错位**（`run()` 的字节码，
`作业三/irsim.pyc` 第 138–148 行）：

```
141  ip < 0 / ip >= len(codes) → error_code = 3
144  code = codes[ip]
145  execute_code(code) → error_code
146  if error_code > 0: break
148  self.ip += 1          ← ★ 无条件执行；而 GOTO/IF(成立)/CALL/RETURN 已在
                            ★ execute_code 里把 self.ip 写成了「目标标号自身的下标」
```

`execute_code` 的四个跳转分支写的都是**目标标号自身的下标**（`GOTO x` → `ip = labelTable[x]`；
`IF … GOTO z` → `ip = labelTable[z]`；`CALL f` → `ip = labelTable[f]`；
`RETURN` → `ip = 压栈的调用点下标`）。而 `labelTable[L]` 登记的是 **`LABEL`/`FUNCTION`
那一行自己的下标**（`sanity_check` 里 `labelTable[strs[1]] = lineno`）。
⇒ 跳转落到 `labelTable[L]` 后又被那个无条件 `+1` 跨过，
**`codes[labelTable[L]]`（那条 `LABEL`）既不执行、也不计数**。反之，靠**顺序落到**或作为
**入口**（`run()` 先 `self.ip = self.entranceIP` 再进循环，循环体里才 `+=1`）的 `LABEL`
仍然会被执行并计数。

> **一句话**：真机的"下一站" = `跳转目标的自身下标 + 1`；**标号行本身被跨过**。

**修正（`scripts/irsim_ref.py::_step`）**：`GOTO` / `IF`（成立）/ `CALL` 三个分支返回
`p.labels[...] + 1`；`RETURN` 压栈的本来就是 `调用点 ip + 1`，**不加 `+1`**（这一处本来就对）；
非跳转指令仍返回 `ip + 1`。（**没有**改成"在 `_execute` 里加 `+1`" —— `_step` 返回
"最终下一个 ip"这个形状没变，只改了三个跳转分支。）

**修正后复核（`python scripts/compare_refsim.py`，48 条）**：所有 `instr_cnt` 与原版逐条一致，
例：`sign.ir`(`-7`) **15=15**、`sample2.ir`(5) **45=45**、`rec.ir` **41=41**、
`counted.ir` **3=3**、`goto_last.ir` **2=2**、`deref_call.ir` **5=5**。首轮那条关系式
*（原版 = 参考机 − 落在标号下标上的控制转移条数）*已由"修正后恒等"取代。

**修正前后对照**（旧版参考机 vs 真机，三点实测，旧版全部满足上面那条关系式）：

| `sign.ir` 输入 | 旧参考机 | 原版（真机） | 差 | trace 里被跳转跨过的 `LABEL` |
|---|---|---|---|---|
| `-7` | 18 | 15 | 3 | ip=10 `label2`、ip=14 `label4`、ip=22 `label6` |
| `0` | 17 | 15 | 2 | ip=10 `label2`、ip=19 `label5` |
| `9` | 12 | 10 | 2 | ip=6 `label1`、ip=23 `label3` |

`-7` 的真机原始 trace（`build/trace_orig.py Test/lab3/unit_exec/sign.ir -7`）：

```
cnt=1   ip=0   ('LABEL', 'main')     ← 入口那条 LABEL 照样计数
cnt=2   ip=1   ('READ', 't1')
...
cnt=6   ip=5   ('GOTO', 'label2')
cnt=7   ip=11  ('MOV', 't4', '#0')   ← ★ ip 从 5 直接到 11：ip=10 的 LABEL label2 被跨过
cnt=8   ip=12  ('IF', 'v1', '<', 't4', 'label4')
cnt=9   ip=15  ('MOV', 't5', '#1')   ← ★ ip=14 的 LABEL label4 被跨过
...
cnt=12  ip=18  ('GOTO', 'label6')
cnt=13  ip=23  ('LABEL', 'label3')   ← 这条是顺序落到的，计数
cnt=15  ip=25  ('RETURN', 't8')
FINAL instrCnt=15
```

**连带更新**：`Test/lab3/unit_exec/test_exec.py` 里的计数用例已按新口径改
（`counted.ir` 由 4 改 **3**，并加一条逐指令踪迹用例证明**被跨过的正是那条 `LABEL`**）；
`test_samples.py` 里"踪迹行数 == `instr_cnt`"那条**同义反复**的假绿断言已换成"独立数
`_step` 派发次数 == `instr_cnt` + 钉绝对值 15 / 10"。

**对优化判据的意义**：现在参考机的绝对 `instr_cnt` **就是**判分机的绝对指令数，
不再需要换算。要求书的定性主张（"优化减少执行指令数"）依旧成立：n=-7 时
`sign.ir` **15** vs `sample_opt1.ir` **10**（真机同值）。

### 2.7 `status == "pc"`（IP 越过 `codes` 末尾）—— **已由原版验证一致**

定向 `goto_last.ir`（`FUNCTION main` / `GOTO end` / `RETURN #0` / `LABEL end :` —— 跳到**最后一行**）：

| | 结果 |
|---|---|
| 参考机 | `status='pc'`，cnt **2** |
| 原版 | 弹 **Warning** `"Program Counter goes out of bound. The running program will be terminated instantly."`（= `error_code 3`），cnt **2** |

⇒ 三档出口（1 = 正常结束 / 2 = 内存越界 / 3 = PC 越界）与参考机的
`ok` / `memory` / `pc` **一一对应**，cnt 亦一致。它**同时**确认了"跳到末尾标号 ⇒
目标自身下标 +1 直接越界"这个组合行为（正是 §2-6 修正后的口径）。

### 2.8 关系运算 `>=`、`<=`、`!=` —— **已由原版验证一致**

- 定向 `relops.ir`（`>=` 与 `!=`）：参考机 `outputs=[11, 33]`, cnt **10**；原版 `outputs=[11, 33]`, cnt **10**
  （首轮差 3 全来自 §2-6，修正后一致）。
- `rec.ir` 用 `<=`：两边都得 120。
- 原版实现是 `eval(str(v1) + code[2] + str(v2))`，参考机是显式 `_eval_relop` —— **对整数语义无差**。

### 2.9 `GOTO`/`IF`/`CALL` 之外形态的装载期校验 —— **已由原版验证一致**

对拍里 `unit_parse/` 的 20 个正/反例（= `compare_refsim.py` 的 `PARSE_CASES`）**全部接受/拒绝一致**：

| 夹具 | 参考机 | 原版 | 判词 |
|---|---|---|---|
| `read_imm.ir`（`READ #5`）/ `param_imm.ir`（`PARAM #5`） | 拒绝 | 拒绝 | 一致 |
| `lhs_amp.ir`（`&x := y`）/ `lhs_hash.ir`（`#x := y`） | 拒绝 | 拒绝 | 一致 |
| `dec_bad.ir`（`DEC a 5`，size 非 4 的倍数） | 拒绝 | 拒绝 | 一致 |
| `if_bad_goto.ir`（`IF x > y z GOTO`） | 拒绝 | 拒绝 | 一致 |
| `call_extra.ir`（`x := CALL f g`） | 拒绝 | 拒绝 | 一致 |
| `arith_badop.ir`（`x := a b c`） | 拒绝 | 拒绝 | 一致 |
| `label_main.ir`（`LABEL main :`） | 拒绝 | 拒绝 | 一致 |
| 定向 `badrelop.ir`（`IF a <> b GOTO l`，RELOPS 白名单） | 拒绝 | 拒绝 | 一致 |
| `dec_nonnum.ir`（`DEC a abc`） | 拒绝（漏 `ValueError`） | 拒绝（弹 "Syntax error at line 2"） | 一致 |
| `arg_imm.ir`/`write_imm.ir`/`read_var.ir`/`param_var.ir`/`dec_ok.ir`/`t1`/`t2`/`t4.ir` | 接受 | 接受 | 一致 |

**拒绝的「机制」天然不同、但不构成语义不一致**：参考机是库，把异常抛给调用方；
原版是 GUI，在 `loadFile` 边界上弹 `QMessageBox.critical` 再 `initialize()`。
两侧对"这份 IR 不被接受"的**结论**一致。

**`t3.ir`（单 token 行 `a:=b`）—— ★ 已对齐**：

| | 结果 |
|---|---|
| 参考机（改前） | `IRSyntaxError('a:=b\n')` —— **比真机宽容** |
| 参考机（改后） | **`IndexError('list index out of range')`** |
| 原版（真机） | **`IndexError('list index out of range')`** |

根因与修正：真机 `sanity_check` 的赋值分支**先取 `strs[1]`**（反汇编行 294、偏移 716–742：
`LOAD strs; LOAD 1; BINARY_SUBSCR` 紧接 `len(strs) < 3`），1 元素列表取下标 1 ⇒ 当场
`IndexError`；它**不在** `loadFile` 捕的 `(IRSyntaxError, ValueError, DuplicatedLabelError,
DuplicatedVariableError, CurrentFunctionNoneError)` 名单里 ⇒ **原样穿出 `loadFile`**（连对话框
都不弹）。改前参考机写成 `len(strs) >= 3 and strs[1] == ":="`，**短路**掉了这次越界访问，
报成干净的 `IRSyntaxError` ⇒ 比真机宽容。现按真机**同序取值**重写（详见 §2.11）。
反例 `not_assign.ir`（`a b c`，token 数够但 `strs[1] != ':='`）仍抛 `IRSyntaxError` ——
证明未"一刀切把所有语法错误变成 IndexError"。

### 2.10 `READ` 弹窗被取消时**不写内存** —— ★ **已修正：与原版一致**

原版 `execute_code` 的 `READ` 分支是：

```
425  result, ok = QInputDialog.getInt(self, 'IR Simulator - Read',
                                      'Please enter an integral value for %s' % code[1], 0)
426  if ok:  mem[symTable[code[1]][0] // 4] = result      ← ★ ok 为假时**什么都不写**
```

即 **`ok` 为假（用户取消）时不写内存，变量保留旧值**。驱动器把 `QInputDialog.getInt`
换成脚本化序列，**序列耗尽返回 `(0, False)`** —— 正是"取消"。

**首轮参考机写的是** `v = st.inputs.pop(0) if st.inputs else 0`（输入耗尽**写 0**），
与原版不一致；**现已改成"没有可用输入就不写"**：

```python
if st.inputs:                                # ★ 输入耗尽 = 取消 ⇒ 不写，保留旧值
    st.store_var(code[1], st.inputs.pop(0))
```

定向 `read_cancel.ir`（`t := #5` / `READ t` / `WRITE t` / `RETURN #0`，**不给任何输入**）：

| | outputs |
|---|---|
| 参考机（修正后） | **`[5]`**（不写 ⇒ `t` 保留旧值 5） |
| 原版 | **`[5]`**（取消 ⇒ 不写） |
| （旧参考机，已废弃） | `[0]`（把 0 写进 `t`） |

**判别力（如实说）**：`sign.ir` / `sample2.ir` / `sample_opt1.ir` 里被 `READ` 的变量都是
**全新的**（旧值本来就是 0），两种语义**同值** —— 所以这条一直没被发现，
**只有**"`READ` 前该变量已被赋过值"（如 `read_cancel.ir` 的 `t := #5`）才分得开。
`Test/lab3/unit_exec/read_cancel.ir` 已入库，由 `test_read_exhausted_does_not_write_memory`
钉住（把它改回"写 0"该用例即变红，已实测）。

**对本实验的实际影响：低。** 我们生成的 IR 里每个被 `READ` 的变量只读一次、且读前未赋值；
判分机在批处理下也不会"取消"。但作为一条真机行为，现已如实对齐。

### 2.11 装载器异常对齐：`sanity_check` / `tableInsert` / `loadFile` 的取值顺序审计 —— ★ `t3.ir` 已对齐

**本节的增量**：把真机 `sanity_check` / `tableInsert` / `loadFile` 的**取值顺序**与参考机
`irsim_ref.py` 里对应函数**逐条对照**，专找"我们加了长度/类型防护、而真机没有 ⇒ 真机会抛、
我们不会"这一类（即"参考机比真机宽容"）。依据 = `build/irsim_dis.txt`（原版 `irsim.pyc`
的反汇编全文，本族的"真机到底怎么走"的权威）。

**结论：同一族的可观察偏离恰好 1 处，已改齐。**

| # | 位置 | 真机（反汇编依据） | 参考机（改前） | 判定 |
|---|---|---|---|---|
| 1 | `sanity_check` 赋值分支的条件顺序 | 行 294、偏移 716–742：`elif strs[1] != ":=" or len(strs) < 3:`，字节码**先 `LOAD strs; LOAD 1; BINARY_SUBSCR`（取 `strs[1]`）再**比 `len(strs) < 3` | `elif len(strs) >= 3 and strs[1] == ":=":`，**先比长度**，把这次 `strs[1]` 访问**短路**掉 | **改**（参考机更宽容） |

**改动**（`scripts/irsim_ref.py::sanity_check`）：把该分支**按真机的取值顺序**重写为
`elif strs[1] != ":=" or len(strs) < 3: raise IRSyntaxError`，再接
`elif strs[0][0] in "&#": raise` 与 `else:`（`:=` 分支主体，起手 `tableInsert(strs[0])`）。
这**不是**"特判后手动 `raise IndexError`"，而是让 `strs[1]` 的越界访问**自然发生** —— 与真机同构。

**效果**：1-token 行 `a:=b`（`t3.ir`）现在**两侧都漏 `IndexError('list index out of range')`**
（真机 `sanity_check` 只捕 `(IRSyntaxError, ValueError, DuplicatedLabelError,
DuplicatedVariableError, CurrentFunctionNoneError)`，IndexError 原样穿出 `loadFile`）。
反例 `not_assign.ir`（`a b c`，token 数够但 `strs[1] != ':='`）仍抛干净的 `IRSyntaxError`。
（原版实测：`not_assign.ir` → 弹 `"Syntax error at line 2:\n\na b c\n"`，`Loading failed.`。）

> **比对器配套修正**：`compare_refsim.py::compare_parse` 原先把"原版拒绝不是 `loadFile` 内的
> 对话框（即原样漏出别的异常）"一律记为**不一致** —— 那是在参考机**比真机宽容**时写下的规则。
> 按"参考机不得比真机宽容"，正确判据应是"两侧漏出**同一类**异常即一致"。已把它改成
> `if r_exc != o_exc:`（仅当参考机没漏、或漏的类型/参数与真机不同才判不一致）。

**同族审计的其余逐条结论（均为"报告不改"）**：

- **`tableInsert`（行 351–368）vs 参考机 `_table_insert`**：
  - 参考机多一句 `if p.current_function is None: raise IRSyntaxError(...)`；真机**没有**这句
    （它在 `sanity_check` 行 267 先 `raise CurrentFunctionNoneError`，故进 `tableInsert` 时
    `currentFunction` 必非 None）。⇒ 该守卫**不可达**，无行为差异。
  - 参考机用 `funcs.setdefault(current_function, []).append(var)`；真机用
    `self.functionDict[self.currentFunction].append(var)`（**直接下标** ⇒ 若 name 不在
    `functionDict` 会 `KeyError`）。真机里 `currentFunction` 与 `functionDict` 在 `sanity_check`
    行 263–264 **同步**设置，故该 `KeyError` **不可达**。⇒ 无行为差异。
  - 这两处**形状上**确属"真机会抛、我们不会"，但都**不可达**（`sanity_check` 已先拦）。
    故**不改**：改了也测不到，且超出"同一族可观察偏离"的范围。
- **`sanity_check` 的"行不属于任何函数"**：真机 `raise CurrentFunctionNoneError`（行 268），
  被 `sanity_check` 自己的 `except` 捕住 → 弹框、返回 `False`；参考机 `raise IRSyntaxError`。
  两侧**结论一致**（都拒绝），仅异常类型不同。**不改**（非同一族：两侧都抛，不涉及宽容度）。
  定向 `no_func.ir`（`RETURN #0` 出现在任何 `FUNCTION` 前）实测：真机弹
  `"Line 1 does not belong to any function"` 并 `Loading failed.`；参考机抛
  `IRSyntaxError('line 0 does not belong to any function')`。
- **`loadFile`（行 108–135）**：真机 `lineno` **只在 `sanity_check` 返回真值时 +1**（行 114–118），
  参考机用 `len(p.codes)`——每个成功的 `sanity_check` 恰好追加一条 code ⇒ 两者**恒等**；
  `line.isspace()` 整行跳过、`open(fname, 'r')`、结尾 `entranceIP`/`labelCheck`/`offset>1MiB`
  的判定次序，与参考机 `load_ir` 的 `entry / label_check / offset` 检查**逐条对应**。**无偏离**。

**仍未证（本节的诚实边界）**：
- 只证了**走过的那些形状**：`t3.ir`（1-token 行）+ `not_assign.ir`（3-token 非赋值）。
  `strs` 长度为 **0**、含**制表符/多空白**、或 `#`/`&` 落在赋值 LHS 之外的组合，
  **未逐一在原版上跑过** —— 它们走同一段字节码，理论上同构，但**没有独立实测**。
- `tableInsert` 那两处"不可达偏离"是按**控制流论证**不可达，**非实测**（没有构造出能让真机
  在此 `KeyError` 的 IR；若哪天真构造出来，就说明该论证有误）。

---

## 3. 结论

1. **原版对拍做成了**：`scripts/irsim_orig_probe.py`（跑在仓内 `build/py38/` 的 CPython 3.8 +
   PyQt5 上，无界面、屏蔽全部模态框）+ `scripts/compare_refsim.py`（逐条比对）。
   首轮 48 条：**一致 29、不一致 19**；**按真机语义修正参考机后，48 条：一致 48、不一致 0**。
   原始输出 `build/compare_out.txt`，一条命令可复现。
2. **旧版 §2 的九类"无独立证据"语义，八类被原版验证一致**（`DEC` 计数、解引用全部形态与
   两条 `KeyError` 陷阱、整除与除零、`labelCheck`、内存模型与三档越界、`status=='pc'`、
   `>=/<=/!=`、装载期全部形状校验）。
3. **一类被证伪、并已修正：`instrCnt` 的计数口径。** 真机**不执行、也不计数被跳转直接命中的
   那条 `LABEL` 行**（主循环无条件 `ip += 1` 与"目标标号自身的下标"错位，§2-6）。
   参考机原实现多计；现已把 `GOTO` / `IF`（成立）/ `CALL` 的下一站改成 `labelTable[...] + 1`
   （`RETURN` 本就不加、非跳转不变），**修正后与原版逐条一致**。
4. **另一条真不一致也已修正**：`READ` 弹窗被取消（输入耗尽）时**不写内存**（§2-10）。
5. **第三处也已对齐（本节新增）：`t3.ir` 的装载期异常类型** —— 参考机原比真机宽容（把真机
   会当场漏出的 `IndexError` 报成干净的 `IRSyntaxError`）。已按真机**同序取值**改齐（§2-9、§2-11），
   并做了 `sanity_check`/`tableInsert`/`loadFile` 三函数的**同族审计**（§2-11）：同族可观察偏离
   **仅此 1 处**，另有 3 处"真机会抛、我们不会"的**不可达**形状与 1 处异常类型差异，均**报告不改**。
6. **没有任何一条不一致出现在"输出值"或"结束状态"上**。**执行内核的可观察行为与原版一致**，
   装载期异常行为现也与真机**同构**（含"该漏 `IndexError` 就漏 `IndexError`"）。
7. 因此：参考机作为**正确性判据**（哪些 IR 跑出什么输出、抛不抛异常、判不判内存越界）
   可以放心用；作为**效率判据**时，其绝对 `instr_cnt` **现在就是**判分机的绝对指令数，
   不必再换算。
