# 贡献指南

感谢你有兴趣改进这个项目。这是一个**面向 AI 编码智能体的 Pebble 开发 skill**，
所以对"事实准确性"的要求比一般项目高：文档里的每个数字都应该可复现。

## 开发环境

```bash
uv tool install pebble-tool --python 3.13
pebble sdk install latest
cd starter/my-watchface && make test
```

## 提交前必须通过

```bash
cd starter/my-watchface
make test      # 格式化 + 布局，约 257 万检查点
make mutate    # 变异测试，必须 20/20 抓到、0 逃逸
make verify    # 产物健全性（需要先 pebble build）
```

CI 会跑同样的东西（见 `.github/workflows/`）。

## 我们特别欢迎的贡献

1. **新的变异体**（`starter/my-watchface/tests/mutation_test.py` 的 `MUTANTS` 表）。
   变异测试的价值 = 它能抓到多少种真实的坏改动。补充时请确保：
   - 替换字符串**能匹配上当前源码**（脚本会校验，过期会报错）
   - 该变异**确实能让测试变红**（逃逸的变异说明测试有盲区，应该补用例而不是删变异）
2. **修正文档里的事实错误**。如果你发现某个数字或结论不对，请附上复现命令。
   我们自己的 README 就被推翻过两次（手写格式化省体积、tick 减少唤醒次数），
   所以"纠错"是这一类 PR 里最受欢迎的。
3. **新的平台/工具链适配**（尤其是 arm64 与 Windows WSL 上的坑）。

## 文档规范

- 数据分档标注：**【实测】** = 真跑出来、可复现；**【报告】** = 单一来源未独立复现；
  **【未验证】** = 没验证过。
- 不要写"应该能""大概"这类模糊结论，要么给出证据，要么标未验证。
- 中文为主，代码与命令用英文。

## 提交信息

```
area: 简短描述

可选的长说明。涉及事实纠正时，请写清"原来的说法"和"实测结果"。
```

`area` 取值举例：`skill`、`starter`、`tests`、`emulator`、`docs`、`ci`。

## 许可

提交即表示你同意以 [MIT](LICENSE) 许可发布你的贡献。
