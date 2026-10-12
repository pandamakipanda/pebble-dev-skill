# 更新日志

本文件记录所有值得注意的变更。格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，
版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

## [1.0.0] - 2026-10-12

首个公开版本。内容来自一次真实的端到端开发：用 AI 智能体从零写出一个 Pebble 表盘，
在 aarch64（Android 容器）上装好工具链、构建、并在四个平台的模拟器上截图验证。

### 新增

- **`skill/pebble-dev/SKILL.md`** —— 面向 AI 智能体的 Pebble 开发 skill，10 节：
  平台代号对照、环境搭建（含 arm64 的 4 个真实坑）、工程结构、**API 语义速查**、
  优化实测、**验证方法论**、调试、发布、官方 agent 约定、自带起始工程。
- **`skill/pebble-dev/starter/my-watchface/`** —— 真实可构建的表盘模板：
  - `src/c/fmt.c` / `fmt.h`：**不含 `pebble.h`** 的纯 C 格式化与三段式布局，可在 PC 上测
  - `src/c/ai-watchface.c`：出货侧写法示范（NULL 守卫、逐层降级、清缓存、文本测量、订阅记账）
  - `tests/`：期望值断言 + 差分对拍 + 金丝雀字节 + 布局穷举 + **变异测试** + 产物健全性检查
  - `bench/` + `measure.sh`：体积 A/B 与 CPU 基准
- **`emulator-toolkit/`** —— 在没有显示器的环境里跑 Pebble QEMU 并截图：
  绕开 pypkjs / VNC / Xvfb / SDL，直接用 `libpebble2` 的 `QemuTransport` 走 Pebble 协议串口。
- **`docs/pebble-ai-dev-guide.md`** —— 591 行的完整学习文档。
- GitHub 功能：CI（主机侧检查 + 真机构建）、Release 自动打包、issue 表单（bug / 事实纠错 / 功能）、
  PR 模板、Dependabot、CODEOWNERS、SECURITY、行为准则。

### 实测结论（本项目最重要的产出）

- **纠正**："手写格式化替代 `snprintf`/`strftime` 能省体积" —— 在 Pebble 上**不成立**。
  那些函数是 `libpebble.a` 里 **12 字节的 syscall 桩**，未被引用即被 `--gc-sections` 丢弃。
  手写的真实收益是可测试性与速度（约 6 倍），不是体积。
- **纠正**：`MINUTE_UNIT` 减少的是**用户回调次数**，不是进程唤醒次数
  （反汇编 `tick_timer_add_subscriber()`：任何 tick 订阅都挂在同一个 1 Hz 事件流上）。
- **发现**：`.pbw` 里 57–60% 的体积是"每平台一份 4092 字节空资源包"，四平台 md5 完全相同。
  唯一有效手段是减平台，**每减一个省约 7,130 字节**。
- **发现**：**LTO 会把包做废**。加 `-flto` 后 `.text` 降 12%、构建报成功，
  但 `pebble-app.bin` 缺 `PBLAPP` 元数据头 ⇒ 装不上手表。已加 `make verify` 自动拦截。
- **发现**：`FONT_KEY_BITHAM_*_NUMBERS` 是数字子集字体，冒号等标点会渲染成豆腐块。

### 由本项目方法论抓出的真 bug（10+）

- 局部创建失败导致**整屏空白**（一个 layer 建失败 ⇒ 三个都不挂载）
- 静态文本缓存跨 window 生命周期残留 ⇒ 重新加载**白屏**
- 布局在矮屏上"顾此失彼"⇒ 重叠；以及反向的"整叠贴底"⇒ 视觉回归
- 层高可能超过屏幕高、时间层从未做越界夹取
- 三个格式化函数在小缓冲下契约不一致；`BATT_BUF_LEN` 零余量
- 测试文件自带一份缓冲区尺寸拷贝 ⇒ 改出货宏测试不响（假安全）

### 变更

- 起始工程的 `uuid` 已替换为随机值；复制后请再换成你自己的。
- `DAY_NAMES` 由指针数组改为扁平字符串（`.data` 28 → 0 字节，省的是 RAM）。

[1.0.0]: https://github.com/pandamakipanda/pebble-dev-skill/releases/tag/v1.0.0
