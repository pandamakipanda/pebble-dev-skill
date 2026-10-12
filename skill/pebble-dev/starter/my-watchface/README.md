# ai-watchface —— 用 AI 编码智能体开发的 Pebble 表盘

这是一个**真实可构建**的 Pebble 表盘工程：AI 写代码 → 真实构建 → 模拟器/真机安装。
它同时是一份「怎么让 AI 写的 Pebble 代码不出 bug」的示范工程。

构建产物：`build/ai-watchface.pbw`（约 27 KB，含 4 个平台）

---

## 1. 一分钟上手

```bash
# 依赖：python3-venv + node + uv（Ubuntu 上还要 libsdl1.2debian libfdt1，模拟器用）
uv tool install pebble-tool --python 3.13
pebble sdk install latest          # 或指定版本，如 4.33.1

cd ai-watchface
pebble build                       # -> build/ai-watchface.pbw
pebble install --emulator emery    # 需要图形界面
pebble install --phone <手机IP>     # 需要手机 App 里打开 Developer Connection
```

## 2. 工程结构

```
ai-watchface/
├── package.json          # 应用元数据：uuid / targetPlatforms / 资源清单
├── wscript               # waf 构建脚本（默认即可，一般不用改）
├── src/c/
│   ├── ai-watchface.c    # 表盘主体：窗口、三个文本框、订阅
│   └── fmt.c / fmt.h     # 纯 C 格式化层（不含 pebble.h → 可跑 PC 单元测试）
├── tests/test_fmt.c      # 单元测试 + 与 snprintf/strftime 的差分对拍
├── bench/
│   ├── bench_fmt.c       # CPU 成本基准
│   ├── main_naive.c      # 对照组：库函数写法（用于 A/B 量体积）
│   └── package.json      # 对照组用的元数据
├── measure.sh            # 可复现的体积/AB 测量脚本
└── Makefile              # make test / bench / size / ab
```

## 3. 测试与测量（本工程的重点）

```bash
make test     # 格式化 18,286 检查点 + 布局 2,554,681 检查点（穷举）
make mutate   # 变异测试：注入 20 个已知 bug，验证测试套件真的抓得到
make bench    # CPU 成本基准（本机 ns/次）
make size     # 真实构建 + 体积分解
make ab       # 朴素写法 vs 本实现：完整链接后的 .text/.pbw 对比
```

`make test` 的验证手段：

| 手段 | 覆盖 |
|---|---|
| 期望值断言 | 午夜/正午/12 小时制去零/日期/电量/夹取等边界 |
| **差分对拍** | 手写实现 vs `snprintf`+`strftime`，穷举 1440 组时分 × 2 制式 + 日期 + 电量 |
| **出货函数等价性** | 直接测 main.c 真正调用的 `fmt_time_ship`，同时对比库函数参考与 `fmt_time` |
| **布局不变量穷举** | `fmt_layout_stack` 是纯函数且被出货代码调用，故可穷举 24 万+ 行高组合与 6600 种屏幕尺寸，验证"在屏幕内 / 非零高 / 尽量不重叠 / **分布均衡**" |
| **变异测试** | 注入 20 个已知 bug（含 off-by-one、恒真比较、越界写、缓冲区改小），确认每个都能让测试变红 |

### 变异测试结果（`make mutate`）

```
结果：抓 20 / 逃逸 0 / 过期 0
所有注入的 bug 都被测试套件捕获，且变异体全部有效 ✓
```

这套变异测试**曾经真的抓到过盲区**：第一轮跑出 3 个逃逸（`fmt_changed` 恒真、
`fmt_time_ship` 不夹取、小缓冲越界写都没被覆盖），补完用例后才全绿。它还抓到过
1 个"过期变异体"（替换字符串不再匹配源码 → 变异根本没生效却显示逃逸），
所以脚本现在会强制校验每个变异都真的改动了源码。
| 金丝雀字节 | `out_len = 1..8` 全扫，确认不越界写、始终留结尾 0 |
| UBSan | 未定义行为检查（ASan 在 Android 内核上无法映射 shadow 内存，故只用 UBSan） |

> **为什么能这么测**：`fmt.c` 不含 `pebble.h`，被测代码与出货代码是**同一份源文件**
> （测试时用 `-DFMT_TEST_HOOKS` 打开导出；出货构建里这些函数是 `static`，会被
> `--gc-sections` 丢弃）。这是让 AI 生成代码可自动验证的关键手法。

## 4. 优化清单（都有实测或官方文档依据）

| 编号 | 做法 | 依据 / 实测 |
|---|---|---|
| OPT1 | `tick_timer_service_subscribe(MINUTE_UNIT, …)` | 减少的是**用户回调次数**（86400 → 1440 次/天）。⚠️ 不是"唤醒次数"：反汇编 `tick_timer_add_subscriber()` 可见，**任何** tick 订阅都让 app 挂在同一个 1 Hz 内核事件流上，派发器每秒都在跑。收益是"不做无谓的 UI 更新" |
| OPT2 | 文本内容不变就不调 `text_layer_set_text` | 少一次脏矩形；本机 `fmt_changed` ≈ 4 ns/次 |
| OPT3 | 电池 `peek()` 取初值 + `subscribe` 变化 | 不轮询 |
| OPT4 | 全部用系统字体，不打包资源 | app 资源 = 0 字节 |
| OPT5 | `targetPlatforms` 只留要支持的平台 | 7 平台 vs 4 平台，`.pbw` 少约 11 KB |
| OPT6 | 时间串交给系统 `strftime` | **实测：Pebble 上 `strftime`/`snprintf` 是 libpebble.a 里 12 字节的 syscall 桩，边际体积≈0**；手写替换反而更大 |
| OPT7 | 层高用 `graphics_text_layout_get_content_size` 实测 | 避免 42pt 字体在 144×168 屏上被裁切 |
| OPT8 | 不做配置 → 不引入持久化/配置页 | 零额外代码 |

### 本轮优化实测（逐步累积）

| 手段 | `.text` | `.data` | `.pbw` | 备注 |
|---|---|---|---|---|
| 基线 | 2077 | 28 | 29,181 | |
| 去重复字体查找 + 去空转 frame 改写 | 2069 | 28 | 29,149 | 只省 8 字节——`-Os` 早把字体指针常量折叠了 |
| `DAY_NAMES` 指针数组 → 扁平字符串 | 2051 | **0** | 28,844 | **RAM 省 28 字节**（指针数组必须待在 `.data`） |
| **合计** | **-26** | **-28** | **-337** | |

**⚠️ 试过但会做废包的：LTO。** 在 `wscript` 里给 `CFLAGS`/`LINKFLAGS` 加 `-flto`，`.text` 能降到 1813（-12.4%）、构建也报成功——**但产物是坏的**：`pebble-app.bin` 开头没有 `PBLAPP` 元数据头（对比 hex 可见直接是 `.text`）。原因是 `inject_metadata.py` 是**原地改写** objcopy 出来的 bin，而 LTO 下目标文件是 GIMPLE 字节码、objcopy 取不到内容，注入器写了个寂寞且**不报错**。
⇒ 已撤销，并加了 `make verify`（`tests/verify_pbw.py`）专门拦这个：检查 `PBLAPP` 魔数、元数据字段可解析、与 manifest 交叉核对。

### 平台数是 `.pbw` 体积的唯一大杠杆（逐个实测）

| targetPlatforms | `.pbw` |
|---|---|
| emery + flint + gabbro + basalt | 29,148 |
| emery + flint + gabbro | 22,017 |
| emery + flint | 14,888 |
| emery | 7,764 |

**每减少一个平台省约 7,130 字节**（= 4092 字节空资源包 + 约 2.2 KB 代码 + 元数据）。
注意：**这纯属宿主侧的下载体积**，不影响手表上的代码大小、RAM 或耗电。

### 体积实测（emery 平台，`make ab` 可复现）

| 版本 | `.text`(emery) | `.pbw` |
|---|---|---|
| 朴素写法（`snprintf`/`strftime`，无防护） | **1383 字节** | **26,420 字节** |
| 本实现（可测的出货格式化 + 空指针/越界防护 + 布局兜底 + 文本测量） | 1917 字节 | 28,540 字节 |

**结论要说清楚**：本实现比朴素写法**大 534 字节代码 / 2,120 字节 `.pbw`**，换来的是
（a）防护（空指针、越界、非法入参全部有处理）、（b）可验证性（12,708 个检查点，其中
2,880 条直接对出货函数做等价性验证）、（c）格式化快约 6 倍（本机 5.6 ns vs 37.8 ns/次）。
在 64 KB（flint/basalt）/128 KB（emery/gabbro）的预算下，534 字节 ≈ 1.1%，
这个取舍划算 —— 但**不要**把"手写格式化"说成省体积的手段，在这个 SDK 上是错的
（`strftime`/`snprintf` 是 `libpebble.a` 里 12 字节的 syscall 桩，用完即被 `--gc-sections` 丢掉）。

### `.pbw` 体积构成（27 KB 里真正属于你的部分）

| 成分 | 字节 | 占比 |
|---|---|---|
| 各平台应用代码 | 7,668 | 26.9% |
| **各平台空资源包（SDK 固定）** | 16,368 | **57.4%** |
| 元数据 json | 1,997 | 7.0% |
| ZIP 结构开销 | 1,566 | 5.5% |

* 每个平台固定带一份 4,092 字节空资源包，即使 `resources.media` 为空也存在（已实测）。
* 写进手表 flash 的只有 `pebble-app.bin`（约 1.8 KB/平台）；DWARF 调试信息不进固件。
* `.pbw` 内条目全部 `Stored`（0% 压缩）。

### 内存占用（构建时自动打印）

```
EMERY APP MEMORY USAGE
Total size of resources:        4092 bytes / 256.0KB
Total footprint in RAM:         2161 bytes / 128.0KB     ← 1.6%
Free RAM available (heap):      129003 bytes
```

静态占用（`.data` + `.bss`）四个平台都是 **68 字节**，其余是栈/堆预留。

## 5. 调试手段

| 手段 | 用法 |
|---|---|
| 应用日志 | `APP_LOG(APP_LOG_LEVEL_DEBUG, "…")` + `pebble logs`（按级别着色） |
| GDB | `pebble gdb`（模拟器内），断点/backtrace/`info locals`；`<app_crashed>` 断点由工具自动插入 |
| 模拟器事件注入 | `pebble emu-battery --percent 20 --charging`、`emu-time-format --format 24h`、`emu-tap`、`emu-button` |
| 编译期体检 | SDK 默认 `-Wall -Wextra -Werror -Os --gc-sections`，警告即失败 |
| 体积体检 | `make size`、`arm-none-eabi-nm --print-size --size-sort` |

本机（无图形界面）限制：`pebble install --emulator` 需要 SDL 窗口，无 `DISPLAY` 时会
`Emulator launch timed out`。要在无头环境验证渲染需 Xvfb。

## 5.5 本轮深度 debug 发现并修复的问题

这些是**真 bug**，不是理论风险。每条都留下了防回归的测试。

| # | 问题 | 触发条件 | 后果 | 测试护栏 |
|---|---|---|---|---|
| 1 | `window_load` 里创建的 layer 失败早退后，订阅回调仍会用 NULL layer 调 `text_layer_set_text` | 低内存导致 `text_layer_create` 返回 NULL | 空指针解引用（延迟到下一次 tick/电量变化才炸） | `push_if_changed` 首行 NULL 守卫 |
| 2 | 静态文本缓存跨 window 生命周期残留 | 表盘被系统重建 / 用户切走再切回 | `push_if_changed` 比较"新 layer 的空内容 vs 旧缓存"判定为无变化 → **屏幕空白** | `window_load` 里清空三个缓存；源码同步断言 |
| 3 | `window_load` 重复调用会覆盖 layer 指针 | 框架重复 load | 内存泄漏 | `window_load` 开头做幂等回收 |
| 4 | 布局在矮屏上会重叠 | 屏幕矮到装不下三行 | 两行字粘在一起 | `tests/test_layout.c` 穷举（曾用 `h=74` 抓出） |
| 5 | 时间层从未做越界夹取 | 屏高 < `h/8 + 行高` | 时间被画到屏幕外 | 同上 |
| 6 | 层高可能超过屏幕高 | 屏高 < 42（字体行高） | 怎么摆都越界 | `fmt_layout_stack` 内夹取层高 |
| 7 | 三个格式化函数在缓冲区太小时契约不一致 | `out_len` 不足 | 一个写字节、一个不写，行为不可预测 | 统一为"**一个字节都不写**"，金丝雀字节验证 |
| 8 | 测试文件自带一份缓冲区尺寸拷贝 | — | 改了出货宏测试也不响（假安全） | 尺寸提升到 `fmt.h` 单一真相源 + 编译期护栏 |
| 9 | `BATT_BUF_LEN=6` 是零余量 | 加一个字符就溢出 | 静默溢出 | 改为 8，并要求"严格大于最大输出" |
| 10 | 测试与出货代码的布局算法是"手抄副本" | 改了出货公式测试不响 | 假安全 | 布局抽成 `fmt.c` 的纯函数，出货代码直接调用 |

## 5.6 视觉验证（已在模拟器上完成）

模拟器本来在这台无显示器容器里跑不起来，被独立子智能体攻克了。**关键真因有两条，都不是"没有显卡"**：

1. **SPI flash 尺寸**：emery/flint/gabbro 的是 **32 MiB**，basalt 是 16 MiB。早期手工 `truncate(16MB)` 造的空 flash 尺寸不对，固件根本起不来。
2. **`Emulator launch timed out` 的真因是 DNS**：`_wait_for_qemu()` 用 `socket.create_connection(('localhost', port))`，而容器里 `/etc/hosts` 是空的 → `getaddrinfo` 失败 → 60 次重试全挂。补上 `127.0.0.1 localhost` 后官方路线直接通了。

最省事的复现方式（不需要 pypkjs / VNC / Xvfb / SDL 窗口）：用 `libpebble2` 的 `QemuTransport` 直连 QEMU 的 Pebble 协议串口（`-serial null` 是 serial0，**serial1 才是协议口**），截图用 `Screenshot.grab_image()` —— 由固件自己渲染回传，不依赖任何显示后端。

工具在 [`../emulator-toolkit/`](../emulator-toolkit/README.md)。四个平台实测结果：

| 平台 | 分辨率 | 白像素 | 上/下留白 | 左/右留白 |
|---|---|---|---|---|
| emery | 200×228 | 1931 | 85 / 73 | 52 / 51 |
| flint | 144×168 | 1931 | 55 / 43 | 24 / 23 |
| basalt | 144×168 | 1901 | 55 / 43 | 24 / 23 |
| gabbro（圆屏） | 260×260 | 1931 | 101 / 89 | 82 / 81 |

四平台都是"黑底白字、时间/日期/电量三行居中"的正确画面，且**留白基本对称**。
`--all-platforms` 有个坑：它会把时间**强行设成 10:10**（营销图用）并重新构建，所以别拿它验证时间渲染。

## 6. 已知风险与未验证项（如实列出）

1. **冒号字形**：本工程已刻意避开 `FONT_KEY_BITHAM_*_NUMBERS` 系列（数字子集字体，
   冒号等标点不保证有字形），统一用 `FONT_KEY_BITHAM_42_BOLD`。
2. ~~渲染效果未经截图确认~~ → **已在模拟器上完成视觉验证**，见 5.6：四平台都渲染正确、留白对称。
3. ~~gabbro（圆屏）视觉~~ → **已截图确认**：260×260 圆屏上三行完整可见，未被圆边裁切。
4. **`text_layer_set_text` 是否无条件标脏**：SDK 头文件未明文承诺，本工程依赖
   "设置文本后固件会重绘"这一通行行为（官方所有教程都这么写）。
