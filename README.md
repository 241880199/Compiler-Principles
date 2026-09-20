# Compiler-Principles

南京大学《编译原理》课程实验代码。

## 实验一：C-- 词法分析器与语法分析器

用 GNU Flex + GNU Bison + C 实现 C-- 语言的词法分析与语法分析，检测并报告词法
错误（类型 A）与语法错误（类型 B），无错时按先序打印语法树。

**已实现的功能**

- 错误类型 A：词法未定义字符、不符合词法单元定义的字符
- 错误类型 B：语法错误，支持在单个文件内报告**多处**错误
- 选做 1.1：识别八进制 / 十六进制数，对 `09`、`0x3G` 报类型 A
- 选做 1.2：识别指数形式浮点数，对 `1.05e` 报类型 A
- 选做 1.3：滤除 `//` 与 `/*…*/` 注释，对未闭合注释报类型 A
- 语法树先序打印，含行号、词素、十进制数值

**设计文档**：[`docs/superpowers/specs/2026-09-20-cmm-lab1-design.md`](docs/superpowers/specs/2026-09-20-cmm-lab1-design.md)

### 环境要求

GNU Flex、GNU Bison、GCC、GNU Make。评分环境为 Ubuntu 20.04 / GCC 7.5.0 /
Flex 2.6.4 / Bison 3.0.4；开发环境为 WSL2 Ubuntu 22.04 / GCC 11.4.0 /
Flex 2.6.4 / Bison 3.8.2。代码只使用两个 Bison 版本均支持的写法。

### 编译

```bash
make            # 生成 ./parser（同时生成 ./cc 软链接）
```

### 运行

```bash
./parser Test/sample/01.cmm     # 或 ./cc Test/sample/01.cmm
```

### 测试

```bash
make test       # 跑 Test/ 下全部用例并比对期望输出
```

### 目录结构

```
src/            Flex 词法规则、Bison 文法、AST 定义与打印、入口
Test/sample/    要求书样例：NN.cmm 输入 + NN.exp 期望输出
Test/err/       自建边界用例
scripts/        环境安装、环境探测、测试执行脚本
docs/           设计文档
```
