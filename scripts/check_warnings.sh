#!/usr/bin/env bash
# 数 gcc 警告。**必须排除 bison 的**
#   src/syntax.y: warning: 3 shift/reduce conflicts
# —— 它在任何正确构建里都会出现，裸 `grep -c warning` 会恒为 1，是一条永远亮的假红灯。
# 判据：gcc 警告形如 "文件:行:列: warning:"（有 行:列），bison 那条没有。
# 用法： bash scripts/check_warnings.sh <build.log>     输出：gcc 警告条数
set -u
log="${1:?用法: bash scripts/check_warnings.sh <build.log>}"
grep -cE ':[0-9]+:[0-9]+: warning:' "$log" || true
