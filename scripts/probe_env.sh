#!/usr/bin/env bash
# 环境探测脚本：确认 WSL 里的工具链版本
echo "=== OS ==="
cat /etc/os-release | head -3
echo "=== tools ==="
for t in gcc flex bison make; do
  printf '%-6s: ' "$t"
  if command -v "$t" >/dev/null 2>&1; then
    "$t" --version 2>&1 | head -1
  else
    echo "NOT FOUND"
  fi
done
echo "=== libfl (needed for -lfl) ==="
ls /usr/lib/x86_64-linux-gnu/libfl* 2>/dev/null || echo "libfl NOT FOUND"
echo "=== bison liby (needed for -ly) ==="
ls /usr/lib/x86_64-linux-gnu/liby* 2>/dev/null || echo "liby NOT FOUND"
