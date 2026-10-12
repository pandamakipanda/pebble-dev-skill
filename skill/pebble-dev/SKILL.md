---
name: pebble-dev
description: 要给 Pebble 智能手表开发应用或表盘、构建/排查 .pbw、用 Rebble 或 Core Devices 的 SDK 与模拟器，或需要 Pebble C API（窗口/图层/AppMessage/健康/按钮）的确切语义与踩坑时使用。含 arm64 环境搭建、无显示器跑模拟器截图、以及"让 AI 写的 C 代码不出 bug"的验证方法论。
---

# Pebble 应用开发

面向"用 AI 编码智能体开发 Pebble 手表应用（产出 `.pbw`）"的实操手册。

**文中的数据分两档，别混用：**
- **【实测】** = 在 aarch64 Ubuntu 容器（Android 内）上真跑出来、可复现的数字
- **【报告】** = 只有单一来源（含子智能体报告），未独立复现

---

## 一、动手前先确认三件事

1. **目标硬件是哪个平台代号**（决定 `targetPlatforms`）：

| 代号 | 机型 | 屏幕 | 颜色 | 备注 |
|---|---|---|---|---|
| aplite | Pebble / Pebble Steel | 144×168 | 黑白 | 代码+堆仅 24 KB，最紧张 |
| basalt | Pebble Time / Time Steel | 144×168 | 64 色 | |
| chalk | Pebble Time Round | 180×180 | 64 色 | 圆屏 |
| diorite | Pebble 2 | 144×168 | 黑白 | 无罗盘 |
| **emery** | **Pebble Time 2**（新） | 200×228 | 64 色 | 代码+堆 128 KB |
| **flint** | **Pebble 2 Duo**（新） | 144×168 | 黑白 | 代码+堆 64 KB |
| **gabbro** | **Pebble Round 2**（新） | 260×260 | 64 色 | 圆屏 |

`pebble new-project` 默认给**全部 7 个平台**——务必先裁掉不需要的，这是 `.pbw` 体积的唯一大头（见第五节）。

2. **表盘还是应用**：`package.json` 的 `pebble.watchapp.watchface` 决定（`true`=表盘）。
   **表盘收不到方向键**，按键被系统接管；要做交互（菜单、按钮、列表）必须是 watchapp。
3. **要不要联网**：需要联网/定位/配置页就加 `src/pkjs/index.js`（PebbleKit JS，跑在手机端 App 的沙箱里）。

---

## 二、环境搭建（含 4 个真实坑）

```bash
# 官方推荐（注意 Python 版本上限）
uv tool install pebble-tool --python 3.13     # 5.0.18+ 尚不支持 Python 3.14
pebble sdk install latest                     # 或指定版本，如 4.33.1
pebble new-project --simple myapp             # --javascript / --worker / --rocky
cd myapp && pebble build                      # → build/myapp.pbw
```

Ubuntu 依赖：`python3-pip python3-venv nodejs npm libsdl1.2debian libfdt1`。

| 坑 | 现象 | 解法 |
|---|---|---|
| **arm64 装不上** | `pip install pebble-tool` 因 `pypkjs`→`stpyv8` 失败。【实测】`stpyv8` 在 PyPI 上只有 x86_64/macOS/Windows 轮子，**无 aarch64** | `pip install --no-deps pebble-tool` 再逐个装依赖。**构建 `.pbw` 不需要 pypkjs**（只有模拟器跑 PebbleKit JS 才要） |
| **SDK 路径与文档不符** | 文档说 `~/.pebble-sdk` | 【实测】真实路径是 `~/.local/share/pebble-sdk/SDKs/`，SDK 二进制在 `<sdk>/sdk-core/pebble/<platform>/`，工具链在 `<sdk>/toolchain/arm-none-eabi/bin/` |
| `analyze-size` 没用 | 输出全是 0 | 【报告】本 SDK 4.33.1 + pebble-tool 5.0.40 下不可靠。改用 `arm-none-eabi-size -A build/<plat>/pebble-app.elf` 和 `nm --print-size --size-sort` |
| **模拟器起不来** | `Emulator launch timed out` | **真因往往是 `/etc/hosts` 为空导致 `localhost` 解析失败**，不是"没显卡"。【报告】补 `127.0.0.1 localhost` 后官方路线直接通。详见第六节 |

**好消息【实测】**：官方下载服务器提供 `toolchain-linux-aarch64.tar.gz`，**arm64 上能完整构建**（`arm-none-eabi-gcc 14.2.1`）。手机里开发 Pebble 是可行的。

---

## 三、工程结构

```
myapp/
├── package.json      # uuid / targetPlatforms / watchface / messageKeys / resources
├── wscript           # waf 构建脚本（一般不用改）
├── src/c/*.c         # 手表端 C
├── src/pkjs/index.js # 可选：手机端 JS（联网/定位/配置页）
├── worker_src/c/     # 可选：后台 worker
└── resources/        # 图片/字体等（能不用就不用）
```

`package.json` 要点：

| 字段 | 说明 |
|---|---|
| `pebble.uuid` | 必须唯一 UUID v4，由 `new-project` 生成，别手改 |
| `pebble.targetPlatforms` | **只留要支持的** |
| `pebble.watchapp.watchface` | `true`=表盘 |
| `pebble.messageKeys` | 必须是**合法 C 标识符**，会生成 `MESSAGE_KEY_xxx`；只与 PebbleKit JS 通信时才需要 |
| `pebble.capabilities` | `location` / `configurable` / `health` |

编译参数**由 waf 提供**（不用自己写）：`-std=c99 -mcpu=cortex-m3 -Os -ffunction-sections -fdata-sections -Wall -Wextra -Werror -Wl,--gc-sections`。
`pebble compile-commands` 可导出 `compile_commands.json`（对 AI 和编辑器都有用）。

平台宏：`PBL_COLOR` / `PBL_BW` / `PBL_ROUND` / `PBL_RECT` / `PBL_DISPLAY_WIDTH` / `PBL_DISPLAY_HEIGHT` / `PBL_PLATFORM_*`。
更推荐用 `layer_get_bounds()` 拿尺寸做自适应，而不是堆平台宏。

---

## 四、API 语义速查（【实测】反汇编核实，写代码前可先 grep 本地头文件求证）

**纪律：先 grep `<sdk>/sdk-core/pebble/<platform>/include/pebble.h`，别凭记忆写 2016 年的博客代码。** 头文件带完整文档注释，是第一手依据。

| API | 确切语义（已核实） |
|---|---|
| `text_layer_set_text` | 固件**无条件** `layer_mark_dirty`（不做内容比较）→ 你跳过调用不会导致不刷新。**不复制字符串**，只存指针 ⇒ 必须传生命周期足够长的缓冲（用 `static`） |
| `text_layer_create` / `window_create` | 文档与实现都保证**可能返回 NULL** ⇒ 必须判空。传 NULL layer 给 `set_text` 会 `passert` → **致命 fault** |
| `fonts_get_system_font` | 未知 key **不返回 NULL**，回退到默认字体。但 `text_layer_set_font(NULL)` 是**延迟硬崩**（测量时解引用地址 0x2）⇒ 仍应判空 |
| `graphics_text_layout_get_content_size` | 可在 `window_load` 里调，**不需要 GContext、不需要"在绘制之后"**。空串/退化 box 返回 `{0,0}` ⇒ 用它算层高时要兜底，否则得到 0 高图层（存在但看不见） |
| `battery_state_service_peek()` | 纯取值，**不会回调**你注册的 handler |
| 未订阅就 `unsubscribe` | 实现幂等安全（有 `list_contains` 前置检查），但文档未定义 ⇒ 自己用 bool 记账更稳 |
| `tick_timer_service_subscribe(MINUTE_UNIT, h)` | 每分钟边界回调；**跨小时/午夜一定回调**且 `tm` 是新的；订阅后约 1 秒内有次 `units_changed==0` 的回调；应用恢复后 ≤1 秒即刷新（**不会陈旧一整分钟**） |
| `window_load` 的时机 | **异步**——在 `window_stack_push` 之后的动画/合成路径里才跑，**晚于 `init()`**。所以 `init()` 里订阅的回调理论上可能先于它触发 ⇒ 回调里必须对"layer 还没建"做防御 |
| `layer_set_frame` / `get_frame`（未挂父层） | 安全。`set_frame` 末尾的 `mark_dirty` 对没 window 的层直接返回 |

### 一个必须知道的字形坑

`FONT_KEY_BITHAM_*_NUMBERS` 系列是**数字子集字体**（官方样张只展示数字与连字符），**冒号等标点不保证有字形**，会渲染成豆腐块。
⇒ 显示 `HH:MM` 这类含标点的文本，用全字符集字体（如 `FONT_KEY_BITHAM_42_BOLD`）。这条我们靠"看样张 + 换字体后截图正常"确认。

---

## 五、优化：先量，别信直觉

### ⚠️ 一个会把包做废的"优化"：不要开 LTO

给 `wscript` 的 `CFLAGS`/`LINKFLAGS` 加 `-flto`：`.text` 真的从 2069 降到 1813（-12.4%）、
`.pbw` 从 29149 降到 27597、**构建还报 success** —— 但**产物是坏的**：

`pebble-app.bin` 开头没有 `PBLAPP` 元数据头（hex 第一行直接是 `.text` 机器码，
而正常包是 `50424c4150500000`）。根因：`common/tools/inject_metadata.py` 是
**原地改写** objcopy 出来的 bin（往偏移 0x0/0x8/…/0x80 写 PebbleProcessInfo），
而 LTO 下目标文件是 GIMPLE 字节码，`objcopy -O binary` 取不到内容 → 注入器写了个寂寞，
**且不报错**。装到手表直接失败。

**所以：每次构建后都要验产物，不能只看 build 成功。** 本 skill 的 `starter/tests/verify_pbw.py`
就是干这个的（检查 `PBLAPP` 魔数、元数据字段可解析、bin 大小与 manifest 一致），已接进 `make verify`。

### 平台数是 `.pbw` 体积的唯一大杠杆【实测】

| targetPlatforms | `.pbw` |
|---|---|
| emery + flint + gabbro + basalt | 29,148 |
| emery + flint + gabbro | 22,017 |
| emery + flint | 14,888 |
| emery | 7,764 |

**每减一个平台省约 7,130 字节**（= 4092 字节空资源包 + 约 2.2 KB 代码 + 元数据）。
但要说清楚：**这只影响宿主侧下载体积**，不影响手表上的代码大小/RAM/耗电。

### 体积【实测】

| 成分 | 占 `.pbw` | 说明 |
|---|---|---|
| **各平台空资源包（SDK 固定）** | **约 57–60%** | **每个平台固定带一份 4092 字节**，即使 `resources.media` 为空也存在（实测） |
| 各平台应用代码 | 约 27% | 写进手表 flash 的只有 `pebble-app.bin`（约 2 KB/平台） |
| 元数据 json | 约 7% | |
| ZIP 结构 | 约 5% | `.pbw` 内条目全是 `Stored`（0% 压缩） |

**结论：`.pbw` 体积的大头是"平台数 × 固定开销"，唯一有效手段是减少 `targetPlatforms`。**
DWARF 调试信息只进 ELF、**不进固件**，不用为它焦虑。

代码级优化我们实测只挤出这些（都很小，别期望靠重构代码大幅瘦身）：

| 手段 | 收益 |
|---|---|
| 去掉重复的 `fonts_get_system_font` 查表 + 空转的 `set_frame` | `.text` **-8 字节**（`-Os` 早把字体指针常量折叠了，收益几乎为零） |
| `char *const DAY_NAMES[7]` → 扁平字符串 `"SunMon..."` | `.data` **-28 字节**（指针数组必须待在 `.data`；扁平串进 flash），`.text` 再 -18 |
| 删掉不用的 `messageKeys` | 每平台少一个编译单元 |
| 合计 | `.text` 2077→2051，`.data` 28→**0**，`.pbw` 29181→28844 |

### ⚠️ 一条反直觉结论（我们的假设被自己的实测推翻了）

**"手写格式化替代 `snprintf`/`strftime` 以省体积"在 Pebble 上是错的。**
`strftime`/`snprintf`/`memmove`/`strcmp` 在 `libpebble.a` 里是 **12 字节的 syscall 桩**（【实测】667 个符号平均 12 字节），未被引用就被 `--gc-sections` 丢掉 ⇒ 用它**边际成本≈0**，手写实现反而净增代码。

手写的真实收益是**可测试性**和**速度**（本机 `fmt_time` 5.6 ns vs `strftime` 37.8 ns，约 6.8 倍），不是体积。**要省体积请去减平台、别去重写 libc 调用。**

### 功耗

`tick_timer_service_subscribe(MINUTE_UNIT, ...)` 而不是 `SECOND_UNIT`。
**但别把收益说错**【实测反汇编】：`tick_timer_add_subscriber()` 只在**第一个**订阅者出现时挂 `regular_timer_add_seconds_callback`（1 Hz）——**任何** tick 订阅都让 app 挂在同一个 1 Hz 事件流上，派发器每秒仍跑（localtime + 逐字段比较）。

- ✅ 正确说法：**用户回调** 86400 → 1440 次/天，省掉每秒一次的文本比较与 UI 更新
- ❌ 错误说法："进程唤醒减少 60 倍"

其他：采样类 API 做批处理（`accel_service_set_sampling_rate` + `accel_data_service_subscribe(n, h)`）、罗盘加 `heading_filter`、动画/振动/听写要克制。官方指南：`developer.rebble.io/guides/best-practices/conserving-battery-life/`。

---

## 六、验证方法论（本节是这套 skill 最值钱的部分）

**核心信念：`make test` 全绿不能证明测试有效，只能证明它没报警。**
唯一能证明"测试有能力抓 bug"的手段是**变异测试**——故意注入已知 bug，看测试是否变红。

我们在一轮深度 debug 里的实测记录：第一轮 20 个注入变异**逃逸 3 个**（`fmt_changed` 恒真、越界输入不夹取、小缓冲越界写全都没被覆盖），补完用例才 20/20。另外还抓到一个"过期变异体"（替换字符串不再匹配源码 → 变异根本没生效却显示逃逸），所以变异脚本必须**强制校验每个变异真的改动了源码**。

### 手段与覆盖面

| 手段 | 抓什么 | 怎么做得动 |
|---|---|---|
| **纯 C 模块抽取** | 让逻辑可脱开 `pebble.h` 编译 | 把字符串/时间/布局数学放进不含 SDK 头的 `.c`，出货代码直接调它（**别写成测试里的"手抄副本"**，会漂移） |
| **差分对拍** | 手写实现 vs `snprintf`/`strftime` 逐字节一致 | 穷举 1440 组时分 × 2 制式 + 日期 + 电量 |
| **金丝雀字节** | 缓冲区越界写 | `out_len = 1..N` 全扫，前后放哨兵字节 |
| **编译期护栏** | 缓冲区被改小 | `typedef char assert_x[(N >= 需要) ? 1 : -1];` |
| **布局穷举** | 越界 / 零高 / 重叠 / **分布失衡** | 把布局做成纯函数，穷举行高组合 × 屏幕尺寸（我们跑了 **255 万检查点**） |
| **变异测试** | 测试本身有没有效 | 见上；这是**前置**步骤，不是补充 |
| **模拟器截图** | 唯一能抓"合法但难看"的手段 | 见下 |

**踩过的坑，直接抄结论：**
- 测试文件里**不要**自己抄一份缓冲区尺寸，要从共同头文件取（否则改了出货宏测试不响 = 假安全）
- "合法但难看"的问题（比如三行字全挤到屏幕底部）**结构断言抓不到**——它不越界、不重叠、非零高，完全合法。必须靠**截图 + 像素分布统计**（我们靠"上留白/下留白"才发现回归）
- 布局要**垂直居中**，不要贴底堆叠

### 无显示器环境跑模拟器截图【报告，已跑通】

**两条真因都不是"没有显卡"：**
1. **SPI flash 尺寸**：emery/flint/gabbro 的是 **32 MiB**，basalt 是 16 MiB。自己 `truncate` 造的空文件尺寸不对 → 固件不响应协议。正确文件 pebble-tool 已解压好：`~/.local/share/pebble-sdk/<版本>/<平台>/qemu_spi_flash.bin`
2. **`localhost` 解析失败**：`_wait_for_qemu()` 用 `socket.create_connection(('localhost', port))`，`/etc/hosts` 为空则 `getaddrinfo` 直接失败 → 60 次重试全挂 → 报"超时"

**最省事的路线（不需要 pypkjs / VNC / Xvfb / SDL 窗口）：**
```python
from libpebble2.communication import PebbleConnection
from libpebble2.communication.transports.qemu import QemuTransport
from libpebble2.services.install import AppInstaller
from libpebble2.services.screenshot import Screenshot   # 固件自己渲染回传

p = PebbleConnection(QemuTransport('127.0.0.1', <协议口>)); p.connect(); p.run_async()
AppInstaller(p, pbw).install(force_install=True)
rows = Screenshot(p).grab_image()      # RGB8 行 → PIL 存 PNG
```
QEMU 参数里 `-serial null` 是 serial0，**serial1 才是 Pebble 协议口**，serial2 是固件控制台。
安装前给 `send_packet` 包一层 `time.sleep(0.002)` 节流，否则串口缓冲溢出。

**四个会误导验证的坑：**
1. `pebble screenshot --all-platforms` 会把时间**强行设成 10:10**（营销图用）**并且会重新 build** ⇒ 别拿它验证时间渲染
2. QEMU monitor 的 `screendump` **颜色不可信**（黑屏被 dump 成灰 `0x42`），要真实像素走 Pebble 协议
3. flash 副本**会记住已安装的 app** ⇒ "装机前"那张图可能已经是表盘了
4. basalt 老机器日志打印 `stopped=1` 是**假警报**（`info status` 显示 running），只是启动慢

---

## 七、调试

| 手段 | 用法 |
|---|---|
| 日志 | `APP_LOG(APP_LOG_LEVEL_DEBUG, "...")` + `pebble logs`（`-v`~`-vvvv` 提高工具日志级别） |
| GDB | `pebble gdb`（模拟器内）；`b file.c:45` / `bt` / `info locals` / `p expr`；`<app_crashed>` 断点由工具自动插入 |
| 事件注入 | `pebble emu-battery --percent 20 --charging`、`emu-time-format --format 24h`、`emu-tap`、`emu-button`、`emu-accel`、`emu-compass`、`emu-bt-connection` |
| 安装 | `pebble install --emulator <plat>` / `--phone <手机IP>`（手机 App 里开 Developer Connection）/ `--qemu`（连已运行的 QEMU） |
| 日志级别规范 | 来自 PebbleOS 的 `AGENTS.md`：`WRN`/`ERR` 按字面用；常规生命周期用 `DBG`；高频路径**不要**打 `INFO` |

官方调试指南：`developer.rebble.io/guides/debugging/`。

---

## 八、发布

- 商店入口：`dev-portal.rebble.io`（Rebble 开发者门户）；`pebble publish` 需要先 `pebble login`
- 流程文档：`developer.rebble.io/guides/appstore-publishing/`
- 【未验证】Core Devices 新硬件（Time 2 / 2 Duo / Round 2）2026 年的分发入口，请查官方最新说明，别照抄旧文档

---

## 九、官方已经在为 AI 智能体铺路（值得直接读）

`github.com/coredevices/PebbleOS` 仓库根目录就有：`AGENTS.md`、`.agents/skills/`（含一个 `working-with-qemu`）、`.claude/CLAUDE.md`（内容是指向 `../AGENTS.md` 的一行软链）、`.claude/skills` 软链到 `.agents/skills`。
`AGENTS.md` 里写了工程组织、代码风格（C 用 clang-format、Python 用 ruff）、日志级别规范、固件构建（`pbl configure --board=...` / `pbl build` / `pbl test`）、子模块浅克隆策略。**做固件/系统层改动前先读它。**

---

## 十、可复用的起始工程（本 skill 自带）

**`starter/`**（就在本 skill 目录下，165 KB，自包含、复制即用）是一个已用模拟器四平台截图验证过的表盘工程：

```
starter/
├── package.json      # 复制后必改 uuid / name / displayName / targetPlatforms
├── wscript           # 不用改
├── src/c/fmt.c/.h    # 纯 C 格式化 + 三段式布局（**不含 pebble.h** ⇒ 可在 PC 上测）
├── src/c/ai-watchface.c  # 出货侧写法示范：NULL 守卫、逐层降级、清缓存、文本测量、订阅记账
├── tests/test_fmt.c      # 期望值 + 差分对拍 + 金丝雀字节（18,286 检查点）
├── tests/test_layout.c   # 布局穷举 + **源码同步断言**（2,554,681 检查点）
├── tests/mutation_test.py # 变异测试（20 个注入 bug）
├── bench/ + measure.sh   # 体积 A/B 与 CPU 基准
└── Makefile          # make test / mutate / size / ab
```

复制到任意位置后直接可用（路径全部相对定位，无硬编码）：

```bash
cp -a <本skill目录>/starter ~/my-pebble-app && cd ~/my-pebble-app
# 改 package.json 里的 uuid/name/displayName/targetPlatforms
make test && make mutate && pebble build
```

想跑真实渲染截图，再配合无头模拟器工具包（见第六节；工作区里还有一份完整版在
`/sdcard/AGENTWORK/pebble-lab/emulator-toolkit/`）。

**注意**：`make test` 里的 `test_layout.c` 带"源码同步断言"，会去读同工程的
`src/c/ai-watchface.c` 与 `src/c/fmt.c` 校验关键写法（布局居中、缓冲区来自 `fmt.h`、
NULL 守卫、清缓存、幂等回收）。**改了出货代码的这几处写法，断言就会报错**——
这是故意的；要么同步改断言，要么确认你确实要改掉那条约定。
