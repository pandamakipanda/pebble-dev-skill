# Pebble 手表应用开发指南（AI 编码智能体协作版）

面向读者：想让 AI（编码智能体 / CLI Agent）帮自己开发 Pebble 手表应用的开发者。全文偏实操，能给命令就给可复制的命令，能给数字就给数字，给不出的地方如实写「未验证」。不写营销话术。

**数据可信度约定**——本文每句话按这三类标注，请按标注决定信多少：

| 标注 | 含义 | 你该怎么用 |
| --- | --- | --- |
| **实测** | 我们在一台真实机器上跑出来的现象与数字 | 可直接当基准，换环境可能要复测 |
| **官方** | 来自 Pebble / Rebble 官方文档或官方仓库 | 可引用，注意文档可能滞后于工具 |
| **未验证** | 我们知道有这回事，但没有亲自跑通 | 不要当结论，自己去官方渠道确认 |

**实测环境**：aarch64（arm64）Ubuntu 24.04，跑在 Android 手机容器里；`pebble-tool 5.0.40`；Pebble SDK `4.33.1`；SDK 自带工具链 `arm-none-eabi-gcc 14.2.1` 与 `qemu-pebble`。下文所有「实测」数字都出自这套环境。换到 x86_64 PC 上多数结论只会更顺（例如第 2 节的依赖坑不会遇到），但我们没有在 x86_64 上逐条复测。

**本文明确未验证的事项**：

| 未验证项 | 说明 |
| --- | --- |
| x86_64 上的逐条复测 | 本文实测全部在 aarch64 Ubuntu 24.04 上完成 |
| arm64 上跑 PebbleKit JS 模拟器 | `stpyv8` 无 aarch64 轮子；本文只绕过了安装，没跑通模拟器里的 JS |
| `gabbro` 的具体分辨率 | 本文未记录，请查官方硬件页 |
| `emery` 的资源上限 | 本文未记录 |
| Core Devices 新硬件 2026 年的分发入口 | 见第 8 节，请查官方最新说明 |
| `analyze-size` 在其它版本下的表现 | 只测了 SDK 4.33.1 + pebble-tool 5.0.40 |

「未验证」不等于「不行」，「实测」也只代表我们这一套环境。
## 1. 先厘清概念
### 1.1 最终产物是 `.pbw`
Pebble 应用最终交付的是一个 **`.pbw` 包**，本质上就是一个 **zip**。手表实际执行的是包里的 `pebble-app.bin`（详见 7.1）。
### 1.2 三层代码，别混在一起谈
一个项目里最多可以有三层代码，跑在**三台不同设备**上，能力完全不同：

| 层 | 目录 | 跑在哪 | 语言 | 典型用途 |
| --- | --- | --- | --- | --- |
| 手表端 | `src/c` | 手表（MCU） | C | UI、传感器、存储、绘制 |
| 手机端 JS | `src/pkjs` | 手机 Pebble App 内（PebbleKit JS） | JavaScript | 联网、调手机能力、配置页 |
| 手机原生伴侣 | 独立工程 | 手机原生 | Java/Kotlin、Obj-C/Swift | PebbleKit Android / iOS，深度集成 |

> 给 AI 下指令时先说明改哪一层。「帮我加个联网天气」在三层里对应三种完全不同的实现。
### 1.3 三种工程形态

| 形态 | 说明 | 如何指定 |
| --- | --- | --- |
| watchface（表盘） | 常驻表盘 | `package.json` 的 `pebble.watchapp.watchface = true` |
| watchapp（应用） | 从菜单启动的应用 | 同字段 `false` / 不设 |
| Rocky.js | 用 **JS** 写的表盘 | `pebble new-project --rocky` |

`pebble.watchapp.watchface` 决定前两者的身份，写错表现完全不同（例如系统不把它当表盘挂载）。
### 1.4 官方资料在哪

| 站点 | 说明 |
| --- | --- |
| <https://developer.rebble.io/> | **旧站镜像，含全部 API 文档**——查 API 优先来这里 |
| <https://developer.rebble.com/> | 新站 |
| <https://github.com/coredevices/PebbleOS> | 开源固件 PebbleOS（含面向 AI 的 `AGENTS.md`，见 5.1） |
| <https://pebbleos-core.readthedocs.io> | PebbleOS 文档 |

## 2. 环境搭建（含实测踩坑）
### 2.1 官方推荐装法

```bash
uv tool install pebble-tool --python 3.13
```

> **官方注意**：`pebble-tool` **5.0.18+ 尚不支持 Python 3.14**，所以上面显式指定 `--python 3.13`。
### 2.2 Ubuntu 系统依赖

```bash
sudo apt install python3-pip python3-venv nodejs npm libsdl1.2debian libfdt1
```

### 2.3 实测坑 1：arm64 上 `pip install pebble-tool` 一定失败
这是本文最该先看的一条，它会直接卡死 arm64 用户的第一次安装。依赖链：`pebble-tool` → `pypkjs` → `stpyv8`。

**失败原因（实测）**：`stpyv8` 在 PyPI 上**只有 x86_64 Linux / macOS / Windows 轮子，没有 aarch64 轮子**，所以在 arm64 上 `pip install pebble-tool` 会因拉不到 `stpyv8` 而失败。

**绕过办法**：

```bash
pip install --no-deps pebble-tool   # 先装本体，不带依赖
# 再按报错缺什么补什么，逐个安装其余依赖
```

**关键认知（实测）**：

| 组件 | 什么时候才需要 |
| --- | --- |
| `pypkjs` | **只在模拟器里跑 PebbleKit JS 时才需要** |
| `stpyv8` | 同上（`pypkjs` 的依赖） |
| 构建 `.pbw` | **不需要** `pypkjs`，也不需要 `stpyv8` |

也就是说：arm64 上完全可以只做「写 C → 构建 `.pbw` → 装真机」，跳过模拟器里的 JS 执行。等确实要在模拟器调 `src/pkjs` 时再回头解决 `stpyv8`（本文未验证 arm64 上跑通模拟器 JS 的办法）。
### 2.4 实测坑 2：SDK 的真实安装路径
官方文档常写的 `~/.pebble-sdk` **不是**本文实测环境的真实路径。**实测真实路径**：

```
~/.local/share/pebble-sdk/SDKs/
```

```bash
ls ~/.local/share/pebble-sdk/SDKs/    # 能看到 4.33.1 之类的 SDK 版本目录
```

查头文件、查工具链、写脚本都以这个路径为准（这也是 5.2 节的前提）。
### 2.5 实测成功案例：Pebble 开发可以在 arm64 手机上完成
我们在一台 **aarch64 Ubuntu 24.04**（Android 手机容器）上完整跑通了从装 SDK 到构建出 `.pbw` 的全过程：

| 步骤 | 实测结果 |
| --- | --- |
| `pebble sdk install 4.33.1` | 下载 `sdk-core.tar.gz`（约 **48 MB**） |
| 同上 | 下载 `toolchain-linux-aarch64.tar.gz`（约 **189 MB**） |
| 工具链内容 | 含 `arm-none-eabi-gcc 14.2.1` 与 `qemu-pebble` |
| 构建 | 成功，产物正常可用 |

> **结论（实测）**：**Pebble 开发可以在 arm64 Linux / 手机上完成。** 官方提供 aarch64 工具链。
### 2.6 最短可用命令序列

```bash
pebble sdk install latest      # 装 SDK
pebble new-project NAME        # 新建工程
cd NAME
pebble build                   # 构建；产物：build/NAME.pbw
```

### 2.7 `pebble new-project` 的可选参数

| 参数 | 作用 |
| --- | --- |
| `--simple` | 最小 C 模板（3.1 节实测用的就是它） |
| `--javascript` | 带 `src/pkjs`（PebbleKit JS） |
| `--worker` | 带后台 worker |
| `--rocky` | Rocky.js 表盘（JS 写表盘） |

## 3. 工程结构（实测生成结果）
### 3.1 `--simple` 模板生成的文件
`pebble new-project --simple X` 实测生成：

| 路径 | 说明 |
| --- | --- |
| `X/README.md` | 项目说明 |
| `X/package.json` | 应用元数据（见 3.5） |
| `X/src/c/X.c` | 手表端 C 源码 |
| `X/wscript` | waf 构建脚本 |

注意：**没有** `resources/` 目录（因为 `resources.media` 为空）；也没有 `src/pkjs/`（那是 `--javascript` 才有的）。
### 3.2 默认目标平台：全部 7 个
`--simple` 模板生成的 `targetPlatforms` 默认是全部 7 个平台：

```
aplite, basalt, chalk, diorite, emery, flint, gabbro
```

**这直接决定 `.pbw` 体积**——每个平台都会在包里占一份。7 平台 → 4 平台，`.pbw` 少约 11 KB（7.3 节）。
### 3.3 平台与机型对照（来源：官方 Hardware Information）

| 平台 | 机型 | 屏幕 | 色彩 | 备注 |
| --- | --- | --- | --- | --- |
| `aplite` | Pebble / Pebble Steel | 144×168 | 黑白 | |
| `basalt` | Pebble Time / Time Steel | 144×168 | 64 色 | |
| `chalk` | Pebble Time Round | 180×180（圆） | 64 色 | 圆屏 |
| `diorite` | Pebble 2 | 144×168 | 黑白 | |
| `emery` | **Pebble Time 2** | 200×228 | 64 色 | **新硬件** |
| `flint` | **Pebble 2 Duo** | 144×168 | 黑白 | **新硬件** |
| `gabbro` | **Pebble Round 2** | 圆屏 | 未在本文记录 | **新硬件** |

### 3.4 体积与内存上限（官方）

| 平台 | 资源上限 | 代码 + 堆上限 |
| --- | --- | --- |
| Aplite | 96 k | 24 k |
| Basalt | 256 k | 64 k |
| Emery | 未在本文记录 | 128 k |

另外：**资源数量最多 256 个。** SDK 上限按平台分档，做多平台应用时永远按最紧的那档（aplite：24 k 代码+堆）设计——否则在你自己的 emery 模拟器上跑得好好的，装到 aplite 上就爆了。
### 3.5 `package.json` 关键字段

| 字段 | 作用 | 注意事项 |
| --- | --- | --- |
| `pebble.uuid` | 应用唯一标识 | **必须是唯一的 UUID v4**，重复会安装冲突 |
| `pebble.displayName` | 手表上显示的名字 | |
| `pebble.targetPlatforms` | 目标平台数组 | 写错平台名会**构建失败**；也是体积第一杠杆 |
| `pebble.watchapp.watchface` | true=表盘，false=应用 | 决定工程形态（见 1.3） |
| `pebble.messageKeys` | 手表 C 与 PebbleKit JS 通信的键 | **必须是合法 C 标识符**；构建时生成 `MESSAGE_KEY_xxx` |
| `pebble.resources.media` | 打包进应用的图片/字体等资源 | 留空则应用资源 0 字节（体积手段，见 7.3） |
| `pebble.capabilities` | 声明需要的能力 | 可取 `location` / `configurable` / `health` |

**`messageKeys` 的坑**：键名会变成 C 宏 `MESSAGE_KEY_<key>`，所以 `"my-key"` 这种带连字符的名字会直接让编译失败——必须是 `my_key` 这样的合法 C 标识符。
### 3.6 编译宏

| 宏 | 含义 |
| --- | --- |
| `PBL_COLOR` | 当前平台是彩色屏 |
| `PBL_ROUND` | 当前平台是圆屏 |
| `PBL_DISPLAY_WIDTH` / `PBL_DISPLAY_HEIGHT` | 当前平台屏幕尺寸 |
| `PBL_PLATFORM_APLITE` / `_BASALT` / `_CHALK` / `_DIORITE` / `_EMERY` / `_FLINT` / `_GABBRO` | 当前平台是哪一个（对应 3.3 的 7 个平台名） |

AI 写多平台代码时最容易犯的错就是**忘了 `PBL_ROUND`**：方屏上排得整整齐齐的布局，到 chalk / gabbro 上会被圆形裁掉四角。
### 3.7 编译参数与编译数据库（实测）
编译参数由 waf 提供。**实测**最小模板的默认参数：

```
-std=c99 -mcpu=cortex-m3 -mthumb -Os \
-ffunction-sections -fdata-sections \
-Wall -Wextra -Werror -Wl,--gc-sections
```

其中两件事对 AI 协作特别重要：

| 参数 | 对 AI 的意义 |
| --- | --- |
| `-Werror` | **警告即失败**。AI 写的代码「能过编译」= 真的干净，没有藏着的警告 |
| `-Os` + `--gc-sections` | 未引用的代码会被丢弃，所以「少写代码」不一定「少占体积」（见 7.2） |

导出编译数据库（让 AI / 编辑器获得准确的补全与跳转）：

```bash
pebble compile-commands     # 导出 compile_commands.json
```

这一步成本极低、收益极高：有了 `compile_commands.json`，AI 和编辑器都能直接看到 `pebble.h` 里真实的函数签名，而不是靠 2016 年博客里的记忆。
## 4. 构建与安装
### 4.1 常用命令

| 命令 | 作用 |
| --- | --- |
| `pebble build` | 构建，产物 `build/<name>.pbw` |
| `pebble clean` | 清理构建产物 |
| `pebble install --emulator emery` | 装到模拟器（平台可换 `basalt`/`chalk`/`diorite`/`flint`/`gabbro`） |
| `pebble install --phone <手机IP>` | 装到真机，**需先在手机 App 里打开 Developer Connection** |
| `pebble install <file.pbw> --qemu` | 把指定 pbw 装到 QEMU |
| `pebble compile-commands` | 导出 `compile_commands.json` |
| `pebble logs` | 看 `APP_LOG()` 输出（见第 6 节） |
| `pebble screenshot` | 抓模拟器画面（见 5.3） |

### 4.2 实测构建耗时

| 场景 | 耗时（实测） |
| --- | --- |
| 7 平台，最小模板 | **21 秒** |
| 4 平台，真实表盘 | **13 秒** |

环境：aarch64（Android 手机容器）。**增量构建更快**——所以「改一点 → 立刻 build 验证」这个循环在手机上也可行。反馈回路越快，AI 自我纠错的轮数越少。
### 4.3 构建会打印内存报告
每次构建都会给出**每个平台**的内存报告，含资源大小、RAM 占用、剩余堆。示例（真实表盘实测）：`RAM 占用：2069 字节 / 128 KB（1.6%）`。这就是第 7 节里「体积/内存是可量化目标」的来源。
## 5. 用 AI 开发的正确姿势（核心章节）
决定「AI 能不能帮你把 Pebble 应用写对」的是四件事：① 官方已经在为 AI 准备上下文；② 本地头文件是第一手 API 依据；③ 确定性反馈回路；④ 把易错逻辑抽成可在 PC 上测的纯 C 模块。
### 5.1 官方已经在为 AI 智能体准备上下文
`coredevices/PebbleOS` 仓库根目录里**已经放好了面向编码智能体的上下文文件**：

| 路径 | 实测内容 |
| --- | --- |
| `AGENTS.md` | 仓库根目录，智能体上下文主文件 |
| `.agents/skills/` | 技能目录，含一个 `working-with-qemu/SKILL.md` |
| `.claude/CLAUDE.md` | 内容只有一行 `../AGENTS.md`（即软链到同一份） |
| `.claude/skills` | 软链到 `.agents/skills` |

也就是说：**同一份上下文同时喂给通用 AGENTS 系工具和 Claude 系工具**，不用两边维护。`AGENTS.md` 里实测写了这些内容：

| 主题 | 内容 |
| --- | --- |
| 工程组织 | 仓库如何分层 |
| 代码风格 | C 用 `clang-format`，Python 用 `ruff` |
| 日志 | 日志级别的使用规范 |
| 固件构建 | `pbl configure --board=...` / `pbl build` / `pbl test` |
| 子模块 | 浅克隆策略 |
| 加 SDK 函数 | 「给 app 暴露新 SDK 函数需要**三处协同改动**」 |

`CONTRIBUTING.md` 的要求：提交需要 **DCO 签名**；提交信息格式为 `area: description`。

> 要改 PebbleOS 固件本身，**先把 `AGENTS.md` 喂给你的 AI**——这就是官方替你写好的提示词。
### 5.2 本地头文件是第一手 API 依据
SDK 装好后，真正的 API 声明在这里（**注意 2.4 节的真实路径**）：

```
<SDK>/sdk-core/pebble/<platform>/include/pebble.h
<SDK>/sdk-core/pebble/<platform>/include/pebble_fonts.h
```

这两个头文件**带完整的文档注释**。所以正确姿势是：**让 AI 先 grep 头文件，再写代码。**

| 做法 | 结果 |
| --- | --- |
| 让 AI 凭记忆写（训练数据里多是 2016 年前后的博客/示例） | 调用早已废弃的 API，或参数顺序错 |
| 让 AI 先 grep `pebble.h` 再写 | 签名、枚举、常量都以本地 SDK 为准 |

示例指令：「先 grep 本机 SDK 的 `pebble.h`，确认 xxx 函数的真实签名与参数含义，再写调用它的代码；不确定的字段不要猜。」
### 5.3 确定性反馈回路（AI 最需要的东西）
AI 写代码最大的问题是「看起来对」。Pebble 的好处是**这个平台给了一整套可判定的信号**：

| 手段 | 命令 | 给 AI 的信号 |
| --- | --- | --- |
| 编译 | `pebble build` | 精确的编译错误；`-Werror` 让警告也变成失败 |
| 内存报告 | （构建时自动打印） | 体积/内存是**可量化**的目标 |
| 编译数据库 | `pebble compile-commands` | 导出 `compile_commands.json` 供 AI/编辑器精确索引 |
| 日志 | `pebble logs` | 看 `APP_LOG()` 输出 |
| 断点调试 | `pebble gdb` | 模拟器里断点、看调用栈与局部变量 |
| 事件注入 | `pebble emu-*` 系列 | 可复现的回归测试 |
| 视觉验证 | `pebble screenshot` | 抓模拟器画面做视觉回归 |
| 体积分析 | `pebble analyze-size` | **实测不可靠，见下** |

`pebble emu-*` 事件注入（做回归特别有用）：

| 命令 | 注入什么 |
| --- | --- |
| `pebble emu-battery` | 电量变化 |
| `pebble emu-time-format` | 12/24 小时制切换 |
| `pebble emu-tap` | 敲击事件 |
| `pebble emu-button` | 按键事件 |
| `pebble emu-accel` | 加速度计数据 |
| `pebble emu-compass` | 罗盘朝向 |
| `pebble emu-bt-connection` | 蓝牙连接/断开 |

> 把「12/24 小时制切换」当必测项——5.4 节实测抓到的那个 bug 就出在这里。

**`analyze-size` 的实测警告**：在 SDK 4.33.1 + pebble-tool 5.0.40 下，`pebble analyze-size` 对本项目的输出**全是 0，不可靠**。改用工具链自带命令：

```bash
arm-none-eabi-size -A <elf>
arm-none-eabi-nm --print-size --size-sort <elf>
```

### 5.4 把易错逻辑抽成纯 C 模块（让 AI 代码可信的关键手法）
**做法**：把容易写错的逻辑（时间格式化、字符串拼接、状态机、数值换算）写成**不包含 `pebble.h` 的纯 C 模块**，它就能**直接在 PC 上用 gcc 编译 + 单元测试 + 差分对拍**。

| 步骤 | 做法 |
| --- | --- |
| 1 | 模块只依赖标准 C（`string.h` 等），**不 include `pebble.h`** |
| 2 | PC 上 `gcc` 直接编译，跑单元测试 |
| 3 | **差分对拍**：和标准库实现（如 `snprintf` / `strftime`）逐字节比对输出 |
| 4 | 通过后再接进手表代码 |

**实测战果**：这套测试在真实项目里**立刻抓到一个真 bug**——24 小时制下个位数小时**漏补零**，输出了 `0:00`，正确应为 `00:00`。这个 bug 在模拟器里肉眼很难发现（要正好在 0 点看表盘），差分对拍一秒就抓出来了。**这就是「让 AI 写的代码可信」的关键：不靠人眼 review，靠能自动判定的对照。**
### 5.5 模拟器：无显示器环境也能跑起来（已实测攻克）

一开始我们在无 `DISPLAY` 的容器里跑 `pebble install --emulator` 得到 `Emulator launch timed out`，
一度以为"模拟器必须有图形界面"。**这个判断是错的**，后来被逐层排查推翻。真因有两条：

| # | 真因 | 表现 | 解决 |
| --- | --- | --- | --- |
| 1 | **SPI flash 尺寸不对** | QEMU 进程能起、端口在听，但固件不响应协议 | emery/flint/gabbro 的 `qemu_spi_flash.bin` 是 **32 MiB**，basalt 是 16 MiB；早期手工 `truncate(16MB)` 造的空文件尺寸错了。pebble-tool 早已解压好：`~/.local/share/pebble-sdk/<版本>/<平台>/qemu_spi_flash.bin` |
| 2 | **`localhost` 解析失败（DNS，不是显示）** | `_wait_for_qemu()` 重试 60 次后报超时 | 容器 `/etc/hosts` 原本是空的 → `getaddrinfo('localhost')` 直接失败。补上 `127.0.0.1 localhost` 后**官方路线直接通** |

补上 `/etc/hosts` 之后：

```bash
pebble install --emulator emery          # → App install succeeded.
pebble screenshot --emulator emery out.png
```

SDL 那条 `-display sdl,show-cursor=on` 其实**不报错也不崩**（只有一条无害的 `XDG_RUNTIME_DIR is invalid` 警告）。

#### 更省事的替代路线（推荐）

`libpebble2` 自带 `QemuTransport`，可以直接连 QEMU 的 Pebble 协议串口，
**完全不需要 pypkjs / websockify / VNC / Xvfb / SDL 窗口**：

```python
from libpebble2.communication import PebbleConnection
from libpebble2.communication.transports.qemu import QemuTransport
from libpebble2.services.install import AppInstaller
from libpebble2.services.screenshot import Screenshot   # 固件自己渲染回传

p = PebbleConnection(QemuTransport('127.0.0.1', <协议口>)); p.connect(); p.run_async()
AppInstaller(p, pbw).install(force_install=True)
rows = Screenshot(p).grab_image()      # RGB8 行 → PIL 存 PNG
```

要点：QEMU 参数里 `-serial null` 是 serial0，**serial1 才是 Pebble 协议口**，serial2 是固件控制台；
安装前给 `send_packet` 包一层 `time.sleep(0.002)` 节流，否则串口缓冲会溢出。

#### 四个会误导验证的坑

1. **`pebble screenshot --all-platforms` 会把时间强行设成 10:10**（`_set_time_1010()`，给营销图用的），
   而且它会**先重新 build**。要验证真实时间请用单平台命令。
2. **QEMU monitor 的 `screendump` 颜色不可信**：`pebble-display` 是自定义设备，黑屏会被 dump 成灰 `0x42`。要真实像素请走 Pebble 协议截图。
3. **flash 副本会记住已安装的 app**：复用同一个 `qemu_spi_flash.bin` 时，"装机前"那张图其实已经是表盘了（before == after）。要干净的对照得重新解一份 pristine flash。
4. **basalt 老机器启动日志会打印** `DEBUG post-reset: ... stopped=1`，看着像 VM 挂了，其实是假警报（`info status` 显示 running），只是固件启动慢。

#### 无图形环境下的验证组合拳

即使现在模拟器能跑了，**"能截图"和"能自动化回归"是两件事**。我们的做法是三层：

| 层次 | 手段 | 覆盖 |
| --- | --- | --- |
| 逻辑正确性 | 纯 C 模块 + 单元测试 + 与标准库差分对拍 | 时间/日期/电量字符串 |
| 几何正确性 | 布局抽成纯函数，穷举 24 万+ 行高组合 × 6600 种屏幕尺寸 | 在屏幕内 / 非零高 / 不重叠 / 分布均衡 |
| 视觉正确性 | 模拟器截图（本节的路线）+ 像素留白统计 | 真实渲染、字体字形、圆屏裁切 |
| 测试有效性 | 变异测试：注入 20 个已知 bug 看能否抓到 | 防止"测试全绿但无效" |

### 5.6 把这些拼成一条 AI 工作流
把前文事实按顺序串起来（**不含任何新数字，纯粹是前文的组合**）：

| 阶段 | 你该让 AI 做什么 | 为什么 |
| --- | --- | --- |
| 建项目 | `pebble new-project`，然后**立刻裁 `targetPlatforms`** | 7 平台 → 4 平台省约 11 KB（7.3） |
| 定 API | 先 grep `<SDK>/.../include/pebble.h` | 头文件是本地一手依据（5.2） |
| 导索引 | `pebble compile-commands` | 让 AI/编辑器看到真实签名（5.3） |
| 写逻辑 | 易错逻辑放纯 C 模块，不 include `pebble.h` | 可在 PC 上对拍测试（5.4） |
| 自测 | `gcc` + 单元测试 + 与标准库差分对拍 | 实测抓到过真 bug（5.4） |
| 校验 | `pebble build` | `-Werror`：警告即失败（5.3） |
| 看体积 | 读构建时的内存报告；细看用 `arm-none-eabi-size -A` | `analyze-size` 实测输出全 0（5.3） |
| 看行为 | `pebble install --phone <IP>` + `pebble logs` | 无 `DISPLAY` 时模拟器起不来（5.5） |
| 做回归 | `pebble emu-*` 注入事件 | 尤其 12/24 小时制切换（5.3） |

## 6. 调试
### 6.1 日志

```c
APP_LOG(APP_LOG_LEVEL_DEBUG, "value=%d", v);
```

```bash
pebble logs
```

| 细节 | 说明 |
| --- | --- |
| 着色 | `pebble logs` 按日志级别着色 |
| 提高工具日志级别 | `-v` ~ `-vvvv` |

### 6.2 `pebble gdb`
在模拟器里断点调试，常用操作：

| 操作 | 命令 |
| --- | --- |
| 下断点 | `b file.c:45` |
| 看调用栈 | `bt` |
| 看局部变量 | `info locals` |
| 求值 | `p expr` |

> **`<app_crashed>` 断点由工具自动插入**——崩溃时会停在那里，不用自己找位置。
### 6.3 官方调试指南
常见运行时错误与语法错误的官方说明：<http://developer.rebble.io/guides/debugging/>
### 6.4 日志级别规范（来自 PebbleOS 的 `AGENTS.md`）

| 级别 | 用法 |
| --- | --- |
| `PBL_LOG_WRN` / `PBL_LOG_ERR` | **按字面用**，别拿 `ERR` 当日志打 |
| `DBG` | 常规生命周期事件 |
| `INFO` | **高频路径上不要打 `INFO`** |

最后一条对功耗有实际影响：手表上日志本身也是开销。
## 7. 体积与性能优化
这一节请**完整读完再动手**，因为其中有一条是反直觉的：在 Pebble SDK 上，「为了省体积而手写格式化函数」是**错的**。
### 7.1 一个真实表盘的实测拆解
样本：一个真实表盘，4 平台（`basalt` / `emery` / `flint` / `gabbro`）。**`.pbw` 总大小：约 27–28 KB（实测）**

| 组成 | 字节 | 占比 |
| --- | --- | --- |
| 每平台固定一份的**空资源包**（4 × 4092） | **16368** | **60.1%** |
| 各平台应用代码合计 | 7296 | 26.8% |
| 元数据 | 1997 | 7.3% |
| ZIP 结构 | 1566 | 5.8% |

其余读数要点（全部实测）：

| 结论 | 说明 |
| --- | --- |
| **空资源包占 60.1%** | 每平台固定带一份 **4092 字节**的空资源包；**即使 `resources.media` 为空也存在** |
| 只有 `pebble-app.bin` 写进手表 flash | 约 **1.8 KB / 平台** |
| DWARF 调试信息**不进固件** | 别把调试信息当体积负担 |
| `.pbw` 内条目**全部 `Stored`** | **0% 压缩**——往里塞多少字节，包就大多少 |
| RAM 占用 | **2069 字节 / 128 KB（1.6%）** |

> 「空资源包 4092 × 4 = 16368，占 60.1%」是本节的钥匙：**体积的大头不是你的代码，而是平台数量本身。**
### 7.2 反直觉结论：手写格式化**不**省体积
**在 Pebble SDK 上，「手写格式化替代 `snprintf` / `strftime` 以省体积」是错的。** 原因（实测）：

| 事实 | 说明 |
| --- | --- |
| `strftime` / `snprintf` / `memmove` / `strcmp` 等在 `libpebble.a` 里是 **12 字节的系统调用桩** | 几乎不占空间 |
| 未被引用时会被 `--gc-sections` 丢弃 | 不用的部分根本不进固件 |
| 手写实现反而是**净增** | 实测在 emery 上 **+442 字节 `.text`** |

为什么手写反而更大？因为手写实现同时加了**边界检查、空指针防护、文本测量**，这些在库函数里是复用已有代码的。**那手写的真实收益是什么？**

| 收益 | 实测数据 |
| --- | --- |
| 可测试性 | 可以做成纯 C 模块做差分对拍（见 5.4） |
| 速度 | `fmt_time` **5.6 ns/次** vs `strftime` **37.8 ns/次**，约 **6.8 倍** |

**所以：为了测试性和速度，手写值得；为了体积，手写是亏的。别搞反。**
### 7.3 真正有效的体积手段（按收益排序）

| 优先级 | 手段 | 实测收益 |
| --- | --- | --- |
| **1** | **减少 `targetPlatforms`** | **7 平台 → 4 平台，`.pbw` 少约 11 KB**。这是唯一的大头 |
| 2 | **不打包资源/字体**，全用系统字体 | app 资源 **0 字节** |
| 3 | 不要把调试信息当负担 | DWARF 不进 `.bin`，对固件体积无影响 |

> 一句话：**先砍平台，再砍资源，别去砍库函数。**
### 7.4 真正有效的性能 / 功耗手段

| 优先级 | 手段 | 效果 |
| --- | --- | --- |
| **1** | `tick_timer_service_subscribe(MINUTE_UNIT, ...)`，而不是 `SECOND_UNIT` | **用户回调**从 **86400 次/天** 降到 **1440 次/天**（**60 倍**）。⚠️ 别写成"唤醒减少 60 倍"——反汇编 `tick_timer_add_subscriber()` 证实：任何 tick 订阅都挂在同一个 1 Hz 内核事件流上，派发器每秒都在跑；真实收益是省掉每秒一次的文本比较与 UI 更新 |
| 2 | 采样类 API 做批处理 | 见下方代码 |
| 3 | 只在必要时重绘 | 改文本前先比较内容是否真的变了 |
| 4 | 动画 / 振动 / 蓝牙 / 麦克风（Dictation）都要克制 | 考虑给用户开关 |

**第 1 条**：官方《Conserving Battery Life》**明确点名秒级 tick 是常见耗电原因**。AI 特别容易写出秒级 tick（它看起来「更实时」），这是最该在 review 时盯住的一行。

**第 2 条（批处理）**：

```c
accel_service_set_sampling_rate(ACCEL_SAMPLING_10HZ);
accel_data_service_subscribe(10, handler);   /* 每秒只醒一次 */
compass_service_set_heading_filter(TRIG_MAX_ANGLE / 36);
```

**第 3 条**：

```c
/* 错的：每 tick 都重绘 */
text_layer_set_text(layer, buf);

/* 对的：内容真变了才重绘 */
static char last[32];
if (strcmp(last, buf) != 0) { strcpy(last, buf); text_layer_set_text(layer, buf); }
```

### 7.5 官方优化指南

| 主题 | 链接 |
| --- | --- |
| 省电 | <http://developer.rebble.io/guides/best-practices/conserving-battery-life/> |
| 多平台适配 | <http://developer.rebble.io/guides/best-practices/building-for-every-pebble/> |
| 模块化架构 | <http://developer.rebble.io/guides/best-practices/modular-app-architecture/> |

## 8. 发布
### 8.1 商店入口

| 用途 | 地址 |
| --- | --- |
| 开发者门户（Rebble） | <https://dev-portal.rebble.io/> |
| 发布流程文档 | <http://developer.rebble.io/guides/appstore-publishing/> |

### 8.2 命令行发布
`pebble publish` 命令**是存在的**（见 `pebble --help`），需要先登录：

```bash
pebble login
pebble publish
```

### 8.3 关于新硬件的分发入口（未验证）
**如实说明**：Core Devices 新硬件（Pebble Time 2 / Pebble 2 Duo / Round 2）在 **2026 年的应用分发入口，我们没有实测验证**。请读者查官方最新说明，不要以本文为准。
## 9. 常见坑清单

| # | 坑 | 现象 / 原因 | 处理 |
| --- | --- | --- | --- |
| 1 | arm64 上 `pip install pebble-tool` 失败 | `stpyv8` 只有 x86_64/macOS/Windows 轮子，**没有 aarch64** | `pip install --no-deps pebble-tool`，其余依赖逐个装；构建不需要 `pypkjs` |
| 2 | 找不到 SDK | 文档写 `~/.pebble-sdk`，**实测真实路径是** `~/.local/share/pebble-sdk/SDKs/` | 用实测路径 |
| 3 | `pebble analyze-size` 输出全 0 | 实测 SDK 4.33.1 + pebble-tool 5.0.40 下对本项目不可靠 | 改用 `arm-none-eabi-size -A` 或 `nm --print-size --size-sort` |
| 4 | 模拟器起不来 | 无 `DISPLAY` → `Emulator launch timed out`；非 VNC 模式下 pebble-tool 固定用 `-display sdl` | 用真机 + `pebble logs`，或换有图形环境的机器（详见 5.5） |
| 4b | `--qemu` 连不上自己起的 QEMU | ① 该路径要 `pypkjs`，而 `pypkjs` import 期就要 `STPyV8`（aarch64 无包）；② 容器缺 `/etc/hosts` 导致 `localhost` 解析失败 | 补 `pypkjs` 依赖链（含为 `STPyV8` 写 import 桩）并把 `127.0.0.1 localhost` 写进 `/etc/hosts`；但即使如此模拟器仍不响应协议 |
| 4c | 源码被"备份"覆盖 | 自己写 A/B 测量脚本时，`cp -a src src_bak` 之后再 `cp -a src_bak src`，如果 `src_bak` 是**上一轮**留下的旧快照，就会把新代码悄悄换回旧的（本项目真踩过：构建产物与源码对不上，符号表里出现早该消失的 `strftime`） | 备份放工程外（`/tmp`）、每次强制重建、加 `trap ... EXIT` 恢复，并在脚本开头校验一个"只应存在于新版本"的符号（本项目校验 `fmt_time_ship`） |
| 5 | 冒号显示成方块 | `FONT_KEY_*_NUMBERS` 系列是**数字子集字体**，冒号等标点可能**没有字形** | 用 **`FONT_KEY_BITHAM_42_BOLD`** 这类**全字符集字体** |
| 6 | 构建报 `messageKeys` 相关错误 | 键必须能生成合法 C 标识符（会变成 `MESSAGE_KEY_xxx`） | 键名只用字母数字下划线，别用连字符 |
| 7 | 构建失败，报平台未知 | `targetPlatforms` 写错（拼错平台名） | 只写 7 个合法名：`aplite basalt chalk diorite emery flint gabbro` |
| 8 | 安装冲突 | `uuid` 不是唯一的 UUID v4 | 每个应用用独立 UUID，复制的模板工程记得改 |
| 9 | 圆屏被裁掉四角 | 只判断尺寸，忘了 `PBL_ROUND` | 用 `PBL_ROUND` 分支布局 |
| 10 | 表盘没被当表盘 | `pebble.watchapp.watchface` 设错 | 表盘设 `true`，应用设 `false` |
| 11 | 手写格式化后体积反而变大 | 库函数是 12 字节桩 + `--gc-sections` 会丢弃 | 为体积别手写；为测试性/速度可以手写（7.2） |
| 12 | 耗电异常 | `SECOND_UNIT` 秒级 tick：用户回调 86400 次/天 | 改 `MINUTE_UNIT`：1440 次/天（7.4）。注意这只减少回调，不减少内核 1 Hz 事件 |

## 10. 本文件的来源

| # | 主题 | 链接 |
| --- | --- | --- |
| 1 | SDK 安装 | <https://developer.rebble.io/sdk/> |
| 2 | 指南总目录 | <https://developer.rebble.io/guides/toc/> |
| 3 | 命令行工具 | <https://developer.rebble.io/guides/tools-and-resources/pebble-tool/> |
| 4 | `package.json` | <https://developer.rebble.io/guides/tools-and-resources/app-metadata/> |
| 5 | 硬件平台信息 | <https://developer.rebble.io/guides/tools-and-resources/hardware-information/> |
| 6 | pebble-tool 仓库 | <https://github.com/coredevices/pebble-tool> |
| 7 | PebbleOS 仓库（`AGENTS.md`、`.agents/skills`） | <https://github.com/coredevices/PebbleOS> |
| 8 | PebbleOS 文档 | <https://pebbleos-core.readthedocs.io/> |

正文中出现过的其它链接：

| 主题 | 链接 |
| --- | --- |
| 站点首页（旧站镜像，含全部 API 文档） | <https://developer.rebble.io/> |
| 站点首页（新站） | <https://developer.rebble.com/> |
| 调试指南 | <http://developer.rebble.io/guides/debugging/> |
| 发布流程 | <http://developer.rebble.io/guides/appstore-publishing/> |
| 开发者门户 | <https://dev-portal.rebble.io/> |
| 省电最佳实践 | <http://developer.rebble.io/guides/best-practices/conserving-battery-life/> |
| 多平台适配 | <http://developer.rebble.io/guides/best-practices/building-for-every-pebble/> |
| 模块化架构 | <http://developer.rebble.io/guides/best-practices/modular-app-architecture/> |
