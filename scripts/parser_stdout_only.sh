#!/usr/bin/env bash
# 实验三的 parser 单参数会把中间代码打到 stdout；而 Test/lab2 的语料期望的是
# "无错时 stdout 为空"（实验二的口径）。补一个丢弃用的输出文件即可让行为回到实验二。
# 错误路径不受影响：那条分支在翻译之前就返回了。
#
# CMM_SEMANTIC_ONLY 另外关掉翻译：lab2_wideargs_ok 的调用有 60 个实参，超过
# 翻译层 MAX_CALL_ARGS=16 的**故意**响亮失败上限（Task 11 的定长缓冲取舍），
# 而实验二只考察语义分析。该开关只影响本脚本，不改变 ./parser 的对外契约。
export CMM_SEMANTIC_ONLY=1
exec ./parser "$1" /dev/null
