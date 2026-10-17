# Lab3 arrnd（高维数组）instrCnt 证据

- 日期：2026-10-17
- 分支：`lab3`；生成时基线 commit：`fcf3ab8`（Task 14b 改动的工作树，本文件随 Task 14b 提交）
- 生成方式：在 WSL Ubuntu-22.04 中执行 `bash scripts/run_lab3.sh Test/lab3/arrnd`，
  把脚本的**原始输出**原样附在下面。`instrCnt` 是 `scripts/irsim_ref.py` 在参考机上
  执行后输出的第一行、由 `run_lab3.sh` 打印；本文件中的数字**不是手抄**的。
- 覆盖：`arrparam.cmm`（要求书 4.1.7 样例 2：`int temp[2]` 形参 + `int op[2]` /
  `int r[1][2]` 双重 while）、`arrparam_chain.cmm`（数组形参再作实参转传，
  钉住 `ARG v` / `ARG &v` 两个不对称）、`arrparam_write.cmm`（变量下标写入数组形参、
  经形参写回调用方数组，混合数组+标量实参顺序）、`mat2.cmm`（`int a[2][3]` 读写）、
  `mat3.cmm`（`int c[2][2][2]` 三维）、
  `matinit.cmm`（混合下标表达式 `a[i+1][j*2]`，含常量下标折叠）、
  `nested.cmm`（一维数组在 `while` 中，`DEC` 只发一次）。
- 说明：本文件是**生成的证据日志**，不是判据本体；判据是各用例的 `.exp`
  （由 `scripts/c_oracle.py` 独立生成）与 `run_lab3.sh` 的逐用例比对。

## 原始输出

```text
ok   Test/lab3/arrnd/arrparam  (instrCnt=112)
ok   Test/lab3/arrnd/arrparam_chain  (instrCnt=24)
ok   Test/lab3/arrnd/arrparam_write  (instrCnt=37)
ok   Test/lab3/arrnd/mat2  (instrCnt=228)
ok   Test/lab3/arrnd/mat3  (instrCnt=440)
ok   Test/lab3/arrnd/matinit  (instrCnt=456)
ok   Test/lab3/arrnd/nested  (instrCnt=194)
----
pass=7 fail=0
```

## 优化前后指令数对照（Task 20，脚本生成）

- 日期：2026-10-17
- 分支：`lab3`；基线 commit：`efc4f04`
- 生成命令：`bash scripts/run_lab3.sh --report`
- 说明：数字由脚本实测；用例覆盖五个真实组（不含 selftest 负例）。优化开/关是两个独立二进制（`./parser` 与 `build/parser-noopt`），每个用例的两份 WRITE 输出都必须与 `.exp` 逐字节相同才计入表中；任一失败脚本退出 1 且不出表。样例 1 部分用同一输入分别执行我们的优化后 IR 与官方 `Test/lab3/unit_exec/sample_opt1.ir`。

```text
用例                                       优化前   优化后    降幅
Test/lab3/basic/arith                          51       42    17.6%
Test/lab3/basic/chain-assign                   13       12     7.7%
Test/lab3/basic/divneg                         44       38    13.6%
Test/lab3/basic/hello                          15       13    13.3%
Test/lab3/basic/ifelse                         25       20    20.0%
Test/lab3/basic/logic                          63       54    14.3%
Test/lab3/basic/readwrite                      21       20     4.8%
Test/lab3/basic/relop                          32       27    15.6%
Test/lab3/basic/while                          87       71    18.4%
Test/lab3/basic/write-stmt                     10        8    20.0%
Test/lab3/arr1d/arrconst                       16       13    18.8%
Test/lab3/arr1d/bubble                       1126      956    15.1%
Test/lab3/arr1d/sum                           209      181    13.4%
Test/lab3/arrnd/arrparam                      112      102     8.9%
Test/lab3/arrnd/arrparam_chain                 24       21    12.5%
Test/lab3/arrnd/arrparam_write                 37       33    10.8%
Test/lab3/arrnd/mat2                          228      199    12.7%
Test/lab3/arrnd/mat3                          440      381    13.4%
Test/lab3/arrnd/matinit                       456      358    21.5%
Test/lab3/arrnd/nested                        194      173    10.8%
Test/lab3/func/argorder                        21       18    14.3%
Test/lab3/func/fact                            57       48    15.8%
Test/lab3/req/sample1                          12        8    33.3%
Test/lab3/req/sample2                          57       48    15.8%
合计                                       3350     2844    15.1%

样例 1（Test/lab3/req/sample1.cmm 优化后 vs Test/lab3/unit_exec/sample_opt1.ir）
输入         我们的优化后     官方优化版
-7                      8             10
0                       8              9
9                       6              9
结论：我们的动态条数 <= 官方优化版（三种输入均不更差）
```
