# Pebble 模拟器无头运行 + 截图工具包

在**没有物理显示器、没有 X server、没有 SDL 窗口**的 Android 容器（Ubuntu 24.04 aarch64）里，
把 Pebble QEMU 模拟器跑起来、装上 `.pbw`、并截到真实渲染画面。

## 结论（一句话）

**不需要 websockify，不需要 VNC，不需要 Xvfb，也不需要 SDL 窗口。**
有两条都能走通的路：

* **路线 1（官方命令，最省心）**：把 `/etc/hosts` 修好后，
  `pebble install --emulator <plat>` 和 `pebble screenshot --emulator <plat>` 直接可用。
* **路线 2（绕开 pypkjs，最稳）**：用 `libpebble2` 的 `QemuTransport` 直连 QEMU 的
  Pebble QEMU Protocol 串口，截图用 `libpebble2.services.screenshot.Screenshot`
  （由**固件自己渲染**并回传，所以不依赖任何显示后端）。

实测两条都成功了：

```
$ pebble install --emulator emery /tmp/qw/ai-watchface.pbw
Installing app...
App install succeeded.
$ pebble screenshot --emulator emery --no-open /root/shot-official-emery.png
Saved screenshot to /root/shot-official-emery.png      # 200x228, 显示 15:49 / Mon 12 / 100%
```

> 走官方命令时**务必重定向到文件，不要用管道**（`> log 2>&1`）。
> `pebble install` 跑完不会自己退出，而它拉起的 QEMU/pypkjs 会继承 stdout 把管道一直占住，
> 于是 `... | tail` 永远不返回。用 `timeout -k 5 150 pebble ... > log 2>&1` 更保险。

## 三个关键坑（之前卡住的原因）

1. **SPI flash 尺寸错了。** emery / flint / gabbro 的 `qemu_spi_flash.bin` 是 **32 MiB (33554432 B)**；
   手工用 `truncate(16*1024*1024)` 造的空文件是错的，固件起不来。
   basalt 才是 16 MiB。
   正确来源（pebble-tool 已经解压好，也可自己从 `.bz2` 解）：
   - `/root/.local/share/pebble-sdk/4.33.1/<plat>/qemu_spi_flash.bin` （pebble-tool 解压的持久副本）
   - `/root/.local/share/pebble-sdk/SDKs/4.33.1/sdk-core/pebble/<plat>/qemu/qemu_spi_flash.bin.bz2` （原始压缩包）

2. **`pebble install --emulator` 的 "Emulator launch timed out" 真因是 DNS，不是 SDL。**
   `_wait_for_qemu()` 用 `socket.create_connection(('localhost', port))`，
   容器里 `/etc/hosts` 原本为空 → `getaddrinfo('localhost')` 直接 `EAI_NONAME`
   → 60 次重试全失败 → 报超时。**补上 `127.0.0.1 localhost` 后这条路就通了**
   （`/tmp/pb-emulator.json` 会被正常写出，pypkjs 也能起来）。

3. **`screendump`（QEMU monitor）也能出图，但不能信。**
   它能给出正确尺寸（200x228）且确有内容，但 `pebble-display` 是自定义设备，
   QEMU 转换出来的颜色不对（黑屏显示成灰 0x42）。**要真实像素请用 Pebble 协议截图。**

## 用法

脚本已做成**可移植**：不硬编码任何本机路径，会自动发现 SDK、qemu-pebble 与 SPI flash
（从 SDK 的 `qemu_spi_flash.bin.bz2` 解压），并**校验 flash 尺寸**。

```bash
pip install libpebble2 Pillow          # 依赖（若用 pebble-tool 的 venv 会自动找到）

# 把 .pbw 装进模拟器并截图 → <prefix>-before.png / -after.png / -after2.png
python3 emu_run.py emery /path/to/app.pbw /tmp/out

# 只截启动画面（不装东西）
python3 emu_run.py emery - /tmp/boot
```

选项（一般不用给）：

| 选项 | 说明 |
|---|---|
| `--proto/--cons/--gdb/--mon PORT` | 四个串口/监视口，默认 12344/12345/12346/12347 |
| `--sdk-root DIR` | Pebble SDK 根目录（含 `toolchain/` 与 `sdk-core/`） |
| `--qemu PATH` | `qemu-pebble` 可执行文件 |
| `--flash-dir DIR` | SPI flash 存放目录，默认 `$TMPDIR/pebble-emu` |

环境变量：`PEBBLE_SDK_ROOT` / `PEBBLE_QEMU` / `PEBBLE_EMU_FLASH_DIR`。

> 旧写法 `<plat> <pbw> <prefix> <proto> <cons> <gdb> <mon>`（7 个位置参数）仍然兼容。

退出码：`0` 成功、`1` 启动或连接失败、`2` 参数或环境问题（缺依赖 / 找不到 SDK / flash 尺寸不对）。

## 实测结果

**注意：表盘源码在 15:49 被改过，两版渲染完全不同（见下节）。**

| 平台 | 分辨率 | 装机前（空 emulator） | 装表盘后 |
|---|---|---|---|
| emery  | 200x228 | 灰底 + 大号 `15:38` + 分隔线 + `Install an app to continue` | 黑底 + 白字 `15:40` / `Mon 12` / `100%` |
| flint  | 144x168 | 同上（缩小） | 黑底 + 白字 `15:41` / `Mon 12` / `100%` |
| basalt | 144x168 | 同上 | 黑底 + 白字 `15:47` / `Mon 12` / `80%` |
| gabbro | 260x260 | 同上 | 黑底 + 白字 `15:48` / `Mon 12` / `100%` |

固件版本：emery/flint/gabbro = v4.33.2，basalt = v4.3.0。

## 截图验证的 4 个坑（都实际踩到了）

1. **`pebble screenshot --all-platforms` 会把表盘时间强行设成 `10:10`**
   （`ScreenshotCommand._set_time_1010()`，钟表广告经典时间，给营销图用的）。
   它和"当前真实时间"无关，**别用它判断时间渲染对不对**。
   单平台的 `pebble screenshot --emulator <plat>` 不会改时间，显示的是真实时间。
2. **`--all-platforms` 会先重新 `pebble build`**，所以它截的是**当前源码**的图，
   而不是你手上那个旧 `.pbw`。验证前请先记录 `md5sum build/ai-watchface.pbw`。
3. **`screendump`（QEMU monitor）颜色不可信**，但 `--gif-all-platforms` 内部就是用它逐帧抓的。
   要精确像素用 Pebble 协议截图（`Screenshot(pebble).grab_image()`）。
4. **flash 副本会"记住"已安装的 app**。`emu_run.py` 复用 `/tmp/qw/<plat>_spi.bin`，
   第二次跑时"装机前"那张图其实已经是表盘了（before == after）。
   要干净的 before，请重新从 `.bz2` 解一份 pristine flash。

## 顺带发现：当前表盘有一处布局回归

用同一套截图流程对比两个 build 的白色像素纵向范围：

| build | pbw md5 | emery 白像素行范围 | flint 白像素行范围 | 观感 |
|---|---|---|---|---|
| 15:40 | `2dd074c3…` | `40..223` | `34..163` | 时间在上、日期居中、电量在下，**铺满全屏** |
| 15:49 | `be089f65…` | `156..225` | `96..165` | 三行全部**挤在屏幕最底部**，上方 2/3 全空 |

`src/c/ai-watchface.c` 里布局是靠 `time_y / date_y / batt_y` 算出来的
（`GRect(0, time_y, w, h_time)` 等），15:49 那版把这三个 y 算到了屏幕底部。
复现：`emu_run.py emery <pbw> /tmp/out <4个端口>` 然后看 `/tmp/out-after.png`。

## 备注

* `-display sdl,show-cursor=on` 在这个容器里**不报错也不会崩**（日志只有一条
  `XDG_RUNTIME_DIR is invalid` 警告），pebble-tool 的默认参数可以直接用。
* `pebble install --emulator <plat>` 现在（`/etc/hosts` 修好后）也能跑通到写
  `/tmp/pb-emulator.json` + 拉起 pypkjs；但 pypkjs 依赖 `STPyV8`，
  aarch64 上只有桩，别指望 JS（PebbleKit JS）真的执行。
  纯 C 表盘走 `QemuTransport` 直连最稳。
* `emu_run.py` 用的是 `/tmp/qw/<plat>_spi.bin` 的**副本**，不会污染 SDK 的持久目录，
  可以放心反复跑。
* basalt 用的老机器 `pebble-snowy-bb` 启动日志会打印
  `DEBUG post-reset: halted=0 stopped=1`，**看着像 VM 挂了其实是假警报**——
  `info status` 显示 running，只是固件 v4.3.0 启动慢，耐心等就能连上。
