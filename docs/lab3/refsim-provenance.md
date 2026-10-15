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

**实测总账（48 条比对）：一致 29，不一致 19。** 19 条不一致里 **17 条是同一个根因**
（§2 第 6 条的 `instrCnt` 口径），1 条是 `READ` 取消（§2 第 10 条），1 条是 `t3.ir` 的
异常类型（§2 第 9 条）。**没有任何一条是"输出值"不一致。** 原始输出见
`build/compare_out.txt`（`python scripts/compare_refsim.py` 可复现）。

---

## 1. 逐条对照

| 参考机语义（设计文档小节） | 证据 | 说明 |
|---|---|---|
| 逐行读、空白行**整行跳过**、不占 `codes` 下标（§3.1） | **【官方样例】**+**【原版对拍】** | 原版 `codes` 与参考机逐项相同（含空行两侧的下标）。 |
| `FUNCTION main` 的入口 = 该行在 `codes` 里的**下标**（§3.2） | **【官方样例】** | `sample2.ir` 空行正卡在 `main` 之前。 |
| `LABEL x :` / `FUNCTION f :` 的形状与登记（§3.2） | **【官方样例】**+**【原版对拍】** | 两段样例都用；原版 `labelTable`（LABEL 与 FUNCTION 都登记）与参考机逐项相同。 |
| `GOTO x` = `ip = labelTable[x]`（§3.2） | **【官方样例】** | ⚠️ 下游的**计数**受 §2-6 影响，但跳转**目标**一致。 |
| `IF x op y GOTO z` 的关系运算 `>`、`<`、`==`（§3.2） | **【官方样例】** | 同上。 |
| `READ x` / `WRITE x` 的形状与语义（§3.2、§3.3） | **【官方样例】** | ⚠️ 输入**取消**这一支与参考机不同 —— 见 §2-10。 |
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
| `deref_call.ir`（`*p := CALL f`） | `KeyError('*p')`, cnt 6 | `KeyError('*p')`, cnt 5 | 异常**一致**；cnt 差 1 是 §2-6 的连带 |
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
| `PARAM` 遇空 `argumentStack` 也报成内存错 | 定向 `param_empty.ir` → `memory`, cnt 4 | 同 → `memory`, cnt 3（差 1 源自 §2-6） |
| `offset > 1 MiB` 判装载失败 | 定向 `bigdec.ir`（`DEC a 1048580`）→ 抛 `IRSyntaxError('offset exceeds 1MiB')` | 同 → `Loading failed.`、`entranceIP=-1`（**双方都拒绝**） |

### 2.6 `instrCnt` 的计数口径 —— ★ **已由原版证伪（本任务最重要的一条）**

本文件旧版写的是：*"`execute_code` 第一件事就 +1，故 `LABEL`/`FUNCTION`/`DEC` 都计数"*。
**那个规则是错的。** 原版**不会执行、也不会计数"被跳转直接落到的那一条 `LABEL` 行"**。

**根因（`run()` 的字节码，`作业三/irsim.pyc` 第 138–148 行）**：

```
145  execute_code(code) → error_code
146  if error_code > 0: break
148  self.ip += 1          ← ★ 无条件执行；GOTO/IF/CALL/RETURN 已在 execute_code 里
                            ★ 把 self.ip 写成了「目标下标」，这里又被 +1
```

而 `labelTable[L]` 登记的是 **`LABEL` 那一行自己的下标**（`sanity_check` 里
`labelTable[strs[1]] = lineno`）。⇒ 跳转落到 `labelTable[L]` 后又被 +1，
**`codes[labelTable[L]]`（那条 `LABEL`）被整条跨过**。反之，靠**顺序落到**或作为
**入口**（`run()` 先 `self.ip = self.entranceIP` 再进循环，循环体里才 `+=1`）的 `LABEL`
仍然会被执行并计数。

**关系式：原版 instr_cnt = 参考机 instr_cnt − （落在标号下标上的控制转移条数）。**

用 `build/trace_orig.py`（把 `form.execute_code` 包一层打印 ip 与控制流）实测三点，**全部吻合**：

| `sign.ir` 输入 | 参考机 | 原版 | 差 | trace 里被跨过的 `LABEL` |
|---|---|---|---|---|
| `-7` | 18 | 15 | 3 | ip=10 `label2`、ip=14 `label4`、ip=22 `label6` |
| `0` | 17 | 15 | 2 | ip=10 `label2`、ip=19 `label5` |
| `9` | 12 | 10 | 2 | ip=6 `label1`、ip=23 `label3` |

`-7` 的原始 trace（`build/trace_orig.py Test/lab3/unit_exec/sign.ir -7`）：

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

**影响（要照实说）**：

- **参考机给出的不是判分机的绝对指令数。** 17 条 `instr_cnt` 不一致**全部**由这一条派生
  （例：`rec.ir` 51 vs 41、`sample2.ir` 输入 5 时 57 vs 45、`counted.ir` 4 vs 3）。
- `Test/lab3/unit_exec/test_exec.py::test_instruction_count_includes_labels` 断言
  `counted.ir` 的 `instr_cnt == 4`；**真机是 3**。（该用例本任务不许改，故仅在此记录。）
  同文件的 `test_instruction_count_includes_dec`（`counted_dec.ir`，无跳转）双方都是 3，**仍然正确**。
- 要求书的定性主张（"优化减少执行指令数"）**未被这条动摇**：n=-7 时
  参考机 18(`sign.ir`) vs 11(`sample_opt1.ir`)、原版 15 vs 10 —— **两把尺子都判优化版更省**。
  但两把尺子给出的差值是 7 与 5，**不等于同一个数**；若将来要拿绝对数做判据，必须按上式换算。

### 2.7 `status == "pc"`（IP 越过 `codes` 末尾）—— **已由原版验证一致**

定向 `goto_last.ir`（`FUNCTION main` / `GOTO end` / `RETURN #0` / `LABEL end :` —— 跳到**最后一行**）：

| | 结果 |
|---|---|
| 参考机 | `status='pc'`，cnt 3 |
| 原版 | 弹 **Warning** `"Program Counter goes out of bound. The running program will be terminated instantly."`（= `error_code 3`），cnt 2 |

⇒ 三档出口（1 = 正常结束 / 2 = 内存越界 / 3 = PC 越界）与参考机的
`ok` / `memory` / `pc` **一一对应**。这里正好又叠上了 §2-6 的跳转跨标号（差 1），
且它**同时**确认了"跳到末尾标号 ⇒ 目标+1 越界"这个组合行为。

### 2.8 关系运算 `>=`、`<=`、`!=` —— **已由原版验证一致**

- 定向 `relops.ir`（`>=` 与 `!=`）：参考机 `outputs=[11, 33]`, cnt 13；原版 `outputs=[11, 33]`, cnt 10
  （差 3 全来自 §2-6）。
- `rec.ir` 用 `<=`：两边都得 120。
- 原版实现是 `eval(str(v1) + code[2] + str(v2))`，参考机是显式 `_eval_relop` —— **对整数语义无差**。

### 2.9 `GOTO`/`IF`/`CALL` 之外形态的装载期校验 —— **已由原版验证一致（一处例外）**

`unit_parse/` 的 20 个正/反例**全部接受/拒绝一致**：

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
| `arg_imm.ir`/`write_imm.ir`/`read_var.ir`/`param_var.ir`/`dec_ok.ir`/`t1`–`t4.ir` | 接受 | 接受 | 一致 |

**拒绝的「机制」天然不同、但不构成语义不一致**：参考机是库，把异常抛给调用方；
原版是 GUI，在 `loadFile` 边界上弹 `QMessageBox.critical` 再 `initialize()`。
两侧对"这份 IR 不被接受"的**结论**一致。

**唯一例外 —— `t3.ir`（单 token 行 `a:=b`）**：

| | 结果 |
|---|---|
| 参考机 | `IRSyntaxError('a:=b\n')` |
| 原版 | **`IndexError('list index out of range')`** |

根因：`sanity_check` 兜底分支先取 `strs[1]`（`len(strs)==1` ⇒ 越界），而它**不在**
`loadFile` 捕的 `(IRSyntaxError, ValueError, DuplicatedLabelError, DuplicatedVariableError,
CurrentFunctionNoneError)` 名单里 ⇒ **原样穿出 `loadFile`**（连对话框都不弹）。
⇒ 判据（"拒绝"）一致；**参考机在这一点上比原版干净**，且对我们**无实害**（我们不会
生成这种 IR）。记录在此只是为了"一条都没美化"。

### 2.10 ★ 参考机漏掉的语义：`READ` 弹窗被取消时**不写内存** —— **与原版不一致**

原版 `execute_code` 的 `READ` 分支是：

```
425  result, ok = QInputDialog.getInt(self, 'IR Simulator - Read',
                                      'Please enter an integral value for %s' % code[1], 0)
426  if ok:  mem[symTable[code[1]][0] // 4] = result      ← ★ ok 为假时**什么都不写**
```

参考机 `irsim_ref.py` 写的是：

```python
v = st.inputs.pop(0) if st.inputs else 0     # ← 输入耗尽时**写 0**
st.store_var(code[1], v)
```

定向 `read_cancel.ir`（`t := #5` / `READ t` / `WRITE t` / `RETURN #0`，**不给任何输入**）：

| | outputs |
|---|---|
| 参考机 | `[0]`（把 0 写进 `t`） |
| 原版 | **`[5]`**（取消 ⇒ 不写，`t` 保留旧值 5） |

**现有夹具看不出来**：`sign.ir` / `sample2.ir` / `sample_opt1.ir` 里被 `READ` 的变量都是
**全新的**，旧值本来就是 0，两种语义同值 —— 所以这条一直没被发现。
只有"同一个变量被 `READ` 两次、或 `READ` 前已被赋过值"才分得开。

**对本实验的实际风险：低。** 我们生成的 IR 里每个被 `READ` 的变量只读一次、且读前未赋值；
判分机也不会在用户交互里"取消"（它要么喂输入，要么按无输入处理）。但**这是一条真不一致**，
如实记下；若将来要让它也忠实，把 `else 0` 改成"输入耗尽就不写"即可（本任务不许改
`scripts/irsim_ref.py`，故只记录）。

---

## 3. 结论

1. **原版对拍做成了**：`scripts/irsim_orig_probe.py`（跑在仓内 `build/py38/` 的 CPython 3.8 +
   PyQt5 上，无界面、屏蔽全部模态框）+ `scripts/compare_refsim.py`（逐条比对）。
   48 条比对：**一致 29、不一致 19**。原始输出 `build/compare_out.txt`，一条命令可复现。
2. **旧版 §2 的九类"无独立证据"语义，八类被原版验证一致**（`DEC` 计数、解引用全部形态与
   两条 `KeyError` 陷阱、整除与除零、`labelCheck`、内存模型与三档越界、`status=='pc'`、
   `>=/<=/!=`、装载期全部形状校验）。
3. **一类被证伪：`instrCnt` 的计数口径。** 原版**不计数被跳转直接落到的 `LABEL`**，
   参考机多计；关系式与三点实测见 §2-6。**这是唯一会改变绝对数字的结论**，也是本任务
   最该被下游（IR 优化、效率判据）消费的一条。
4. **新发现一条旧清单没有的不一致**：`READ` 取消时不写内存（§2-10）。
5. **没有任何一条不一致出现在"输出值"或"结束状态"上**（唯一一条 outputs 差异是 §2-10 的
   输入取消，属交互边界而非 IR 语义）。**执行内核的可观察行为与原版一致。**
6. 因此：参考机作为**正确性判据**（哪些 IR 跑出什么输出、抛不抛异常、判不判内存越界）
   可以放心用；作为**效率判据**用时，必须记住它的 `instr_cnt` 系统性偏高，
   且偏高量 = 控制转移落在标号上的次数。
