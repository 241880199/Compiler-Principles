#!/usr/bin/env bash
# 在 WSL Ubuntu-22.04 里安装 C-- 实验所需的构建工具链
# gcc / flex / bison / make  + libfl-dev（提供 flex 的 yywrap，对应 Makefile 里的 -lfl）
set -e

export DEBIAN_FRONTEND=noninteractive

echo "=== apt-get update ==="
apt-get update -qq

echo "=== apt-get install ==="
apt-get install -y -qq gcc flex bison make libfl-dev

echo
echo "=== 安装结果 ==="
bash "$(dirname "$0")/probe_env.sh"
