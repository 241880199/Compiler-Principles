# Lab3 arrnd（高维数组）instrCnt 证据

- 日期：2026-10-17
- 分支：`lab3`；生成时基线 commit：`bfaf47a`（Task 13 改动的工作树，本文件随 Task 13 提交）
- 生成方式：在 WSL Ubuntu-22.04 中执行 `bash scripts/run_lab3.sh Test/lab3/arrnd`，
  把脚本的**原始输出**原样附在下面。`instrCnt` 是 `scripts/irsim_ref.py` 在参考机上
  执行后输出的第一行、由 `run_lab3.sh` 打印；本文件中的数字**不是手抄**的。
- 覆盖：`mat2.cmm`（`int a[2][3]` 读写）、`mat3.cmm`（`int c[2][2][2]` 三维）、
  `matinit.cmm`（混合下标表达式 `a[i+1][j*2]`，含常量下标折叠）、
  `nested.cmm`（一维数组在 `while` 中，`DEC` 只发一次）。
- 说明：本文件是**生成的证据日志**，不是判据本体；判据是各用例的 `.exp`
  （由 `scripts/c_oracle.py` 独立生成）与 `run_lab3.sh` 的逐用例比对。

## 原始输出

```text
ok   Test/lab3/arrnd/mat2  (instrCnt=228)
ok   Test/lab3/arrnd/mat3  (instrCnt=440)
ok   Test/lab3/arrnd/matinit  (instrCnt=456)
ok   Test/lab3/arrnd/nested  (instrCnt=194)
----
pass=4 fail=0
```
